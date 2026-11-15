/* fuzz/fuzz.c -- deterministic random-operation fuzz for amoeba.
 *
 * Usage: fuzz <seed> [steps]      (one process per seed)
 *
 * Each step applies one operation to a model that owns NV variables and NC
 * constraint slots, then checks the at-rest invariants from
 * notes/invariant.md: the infeasible-row work list is empty, no non-external
 * row has a negative constant, and an edit's marker is the variable's own
 * ERROR column.  Every few steps the whole solver is round-tripped through
 * am_dump()/am_load(): the loaded solver must have the same shape and the
 * bindings its tableau implies, a truncated stream must not load, and a
 * re-dump/re-load must be stable.  Every fourth round trip replaces the
 * model's solver with the loaded copy, so later operations run on it.
 *
 * The fuzz reads internal fields on purpose: it is a development tool, not a
 * test suite, and its checks are contract-level invariants rather than helper
 * behaviour.  A crash, an assertion or a sanitizer report is the failure.
 *
 * Exit codes: 0 = ok, 1 = setup failure, 2 = check failure (message on stderr).
 */
#define AM_IMPLEMENTATION
#include "amoeba.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NV   8
#define NC   32
#define NOPS 200
#define DBUF (1 << 20)

typedef struct dbuf {
    am_Dumper base;
    char     *p;
    size_t    len;
} dbuf;

typedef struct lbuf {
    am_Loader      base;
    const char    *p;
    size_t         len;
    am_Num        *values;
    am_Id          lvars[NV];
    am_Constraint *lcons[NC];
    int            bad;
} lbuf;

static am_Num         vals[NV];
static am_Num         lvals[NV];
static am_Num         lvals2[NV];
static am_Id          vars[NV];
static am_Constraint *cons[NC];
static am_Constraint *named[NC];
static int            in_solver[NC];
static char           vnames[NV][4];
static char           cnames[NC][4];
static char           dbytes[DBUF];
static char           dbytes2[DBUF];
static unsigned       rstate = 1;
static unsigned       roundtrips = 0;

static const am_Num strengths[4] = {AM_WEAK, AM_MEDIUM, AM_STRONG, AM_REQUIRED};
static const int    rels[3] = {AM_LESSEQUAL, AM_EQUAL, AM_GREATEREQUAL};

static unsigned rnd(void) {
    rstate = rstate * 1103515245u + 12345u;
    return (rstate >> 16) & 0x7fff;
}

static void fail(const char *what, unsigned seed, int step) {
    fprintf(stderr, "FAIL seed=%u step=%d: %s\n", seed, step, what);
    exit(2);
}

/* ---- model <-> callback mapping ---- */

static unsigned vindex(const am_Num *value) {
    unsigned i;
    for (i = 0; i < NV; ++i)
        if (value == &vals[i] || value == &lvals[i] || value == &lvals2[i])
            return i;
    return NV;
}

static am_Name vname(am_Dumper *d, unsigned i, am_Id var, am_Num *value) {
    unsigned at = vindex(value);
    (void)d, (void)i, (void)var;
    return at < NV ? vnames[at] : NULL;
}

static am_Name cname(am_Dumper *d, unsigned i, am_Constraint *c) {
    unsigned at;
    (void)d, (void)i;
    for (at = 0; at < NC; ++at)
        if (named[at] == c) return cnames[at];
    return NULL;
}

static int writer(am_Dumper *d, const void *buf, size_t len) {
    dbuf *db = (dbuf *)d;
    if (len > DBUF - db->len) return AM_FAILED;
    memcpy(db->p + db->len, buf, len);
    return (db->len += len), AM_OK;
}

static am_Num *load_var(am_Loader *l, am_Name name, unsigned i, am_Id var) {
    lbuf    *lb = (lbuf *)l;
    unsigned at;
    (void)i;
    if (name != NULL)
        for (at = 0; at < NV; ++at)
            if (strcmp(name, vnames[at]) == 0) {
                lb->lvars[at] = var;
                return &lb->values[at];
            }
    lb->bad = 1;
    return NULL;
}

static void load_cons(am_Loader *l, am_Name name, unsigned i,
                      am_Constraint *c) {
    lbuf    *lb = (lbuf *)l;
    unsigned at;
    (void)i;
    if (name != NULL)
        for (at = 0; at < NC; ++at)
            if (strcmp(name, cnames[at]) == 0) {
                lb->lcons[at] = c;
                return;
            }
    lb->bad = 1;
}

static const char *reader(am_Loader *l, size_t *plen) {
    lbuf       *lb = (lbuf *)l;
    const char *p = lb->p;
    *plen = lb->len;
    lb->p = NULL, lb->len = 0;
    return p;
}

/* ---- invariant checks ---- */

static void check_rest(am_Solver *S, unsigned seed, int step) {
    am_Iterator it;
    if (S->infeasible_rows.u.id != 0)
        fail("the infeasible-row work list is not empty", seed, step);
    it = amH_iter(&S->rows);
    while (amH_next(&it)) {
        am_Row *r = amH_val(am_Row, it);
        if (!amV_isexternal(it.key) && amN_negative(r->constant))
            fail("a non-external row has a negative constant", seed, step);
    }
    it = amH_iter(&S->suggests);
    while (amH_next(&it)) {
        am_Suggest **sp = amH_val(am_Suggest *, it);
        am_Suggest  *s = *sp;
        if (!amV_isexternal(it.key) || s->constraint.marker.u.id != it.key.u.id
            || !amV_iserror(s->constraint.marker)
            || !amV_iserror(s->constraint.other)
            || s->constraint.other.u.id == s->constraint.marker.u.id)
            fail("an edit's marker is not the variable's own ERROR column",
                 seed, step);
    }
}

static void verify_bindings(am_Solver *S, const am_Id *ids,
                            const am_Num *values, unsigned seed, int step) {
    unsigned i;
    for (i = 0; i < NV; ++i) {
        am_Row *r = amR_get(S, amS_var(ids[i]));
        am_Num  want = r ? r->constant : (am_Num)0;
        if (memcmp(&want, &values[i], sizeof(am_Num)) != 0)
            fail("a loaded binding does not match the dumped tableau", seed,
                 step);
    }
}

static void verify_truncation(const char *buf, size_t len, unsigned seed,
                              int step) {
    unsigned k;
    for (k = 0; k < 3; ++k) {
        lbuf       lb;
        am_Solver *T;
        size_t     cut;
        if (k == 0)
            cut = 0;
        else if (k == 1)
            cut = len - 1;
        else
            cut = (size_t)(rnd() % (unsigned)len);
        memset(&lb, 0, sizeof(lb));
        lb.base.load_var = load_var;
        lb.base.load_cons = load_cons;
        lb.base.reader = reader;
        lb.values = lvals2;
        lb.p = buf, lb.len = cut;
        if ((T = am_load(NULL, NULL, &lb.base, 0)) != NULL) {
            am_delsolver(T);
            fail("a truncated stream loaded", seed, step);
        }
    }
}

/* ---- dump / load round trip ---- */

static am_Solver *check_roundtrip(am_Solver *S, unsigned seed, int step) {
    dbuf       db, db2;
    lbuf       lb, lb2;
    am_Solver *L, *L2;
    size_t     len;
    unsigned   i;
    int        auto_update = (int)S->auto_update;

    for (i = 0; i < NC; ++i) named[i] = cons[i];
    memset(&db, 0, sizeof(db));
    db.base.var_name = vname;
    db.base.cons_name = cname;
    db.base.writer = writer;
    db.p = dbytes;
    if (am_dump(S, &db.base) != AM_OK) fail("am_dump() failed", seed, step);
    len = db.len;

    memset(&lb, 0, sizeof(lb));
    lb.base.load_var = load_var;
    lb.base.load_cons = load_cons;
    lb.base.reader = reader;
    lb.values = lvals;
    lb.p = dbytes, lb.len = len;
    if ((L = am_load(NULL, NULL, &lb.base, 0)) == NULL)
        fail("am_load() failed", seed, step);
    if (lb.bad) fail("a load callback saw an unknown name", seed, step);
    if (L->vars.count != S->vars.count)
        fail("the loaded solver has a different variable count", seed, step);
    if (L->constraints.count != S->constraints.count)
        fail("the loaded solver has a different constraint count", seed, step);
    if (L->rows.count != S->rows.count)
        fail("the loaded solver has a different row count", seed, step);
    verify_bindings(S, vars, lvals, seed, step);
    verify_truncation(dbytes, len, seed, step);

    for (i = 0; i < NC; ++i) named[i] = lb.lcons[i];
    memset(&db2, 0, sizeof(db2));
    db2.base.var_name = vname;
    db2.base.cons_name = cname;
    db2.base.writer = writer;
    db2.p = dbytes2;
    if (am_dump(L, &db2.base) != AM_OK) fail("re-dump failed", seed, step);
    memset(&lb2, 0, sizeof(lb2));
    lb2.base.load_var = load_var;
    lb2.base.load_cons = load_cons;
    lb2.base.reader = reader;
    lb2.values = lvals2;
    lb2.p = dbytes2, lb2.len = db2.len;
    if ((L2 = am_load(NULL, NULL, &lb2.base, 0)) == NULL)
        fail("re-load failed", seed, step);
    if (lb2.bad) fail("a re-load callback saw an unknown name", seed, step);
    if (L2->vars.count != L->vars.count
        || L2->constraints.count != L->constraints.count
        || L2->rows.count != L->rows.count)
        fail("re-load changed the shape", seed, step);
    if (L2->current != L->current)
        fail("re-load changed the last issued name", seed, step);
    verify_bindings(L, lb.lvars, lvals2, seed, step);
    am_delsolver(L2);

    if (++roundtrips % 4 == 0) {
        am_delsolver(S);
        for (i = 0; i < NV; ++i) {
            if (lb.lvars[i] == 0)
                fail("a variable is missing after load", seed, step);
            vars[i] = lb.lvars[i];
            vals[i] = lvals[i];
            am_varvalue(L, vars[i], &vals[i]);
        }
        for (i = 0; i < NC; ++i) {
            cons[i] = lb.lcons[i];
            in_solver[i] = cons[i] != NULL && am_hasconstraint(cons[i]);
        }
        am_autoupdate(L, auto_update);
        return L;
    }
    am_delsolver(L);
    return S;
}

/* ---- operations ---- */

static void create_constraint(am_Solver *S, int slot) {
    int n, t;
    cons[slot] = am_newconstraint(S, strengths[rnd() % 4]);
    if (cons[slot] == NULL) return;
    n = 1 + (int)(rnd() % 3);
    for (t = 0; t < n; ++t)
        am_addterm(cons[slot], vars[rnd() % NV],
                   (am_Num)((int)(rnd() % 7) - 3));
    am_addconstant(cons[slot], (am_Num)((int)(rnd() % 41) - 20));
    am_setrelation(cons[slot], rels[rnd() % 3]);
    in_solver[slot] = (am_add(cons[slot]) == AM_OK);
}

static am_Solver *run(am_Solver *S, unsigned seed, int steps) {
    int step, i;
    for (step = 0; step < steps; ++step) {
        unsigned op = rnd() % 100;
        int      c = (int)(rnd() % NC), v = (int)(rnd() % NV);
        if (op < 18) {
            if (cons[c] == NULL)
                create_constraint(S, c);
            else if (!in_solver[c])
                in_solver[c] = (am_add(cons[c]) == AM_OK);
        } else if (op < 30) {
            if (cons[c] != NULL && in_solver[c])
                am_remove(cons[c]), in_solver[c] = 0;
        } else if (op < 36) {
            if (cons[c] != NULL)
                am_delconstraint(cons[c]), cons[c] = NULL, in_solver[c] = 0;
        } else if (op < 46) {
            if (cons[c] != NULL) am_setstrength(cons[c], strengths[rnd() % 4]);
        } else if (op < 62) {
            am_suggest(S, vars[v], (am_Num)((int)(rnd() % 201) - 100));
        } else if (op < 70) {
            am_addedit(S, vars[v], strengths[rnd() % 3]);
        } else if (op < 76) {
            am_deledit(S, vars[v]);
        } else if (op < 80) {
            am_clearedits(S);
        } else if (op < 86) {
            am_updatevars(S);
        } else if (op < 90) {
            int on = (int)S->auto_update;
            am_autoupdate(S, !on);
            if (!on) am_updatevars(S);
        } else if (op < 94) {
            am_resetsolver(S);
            for (i = 0; i < NC; ++i) in_solver[i] = 0;
        } else {
            S = check_roundtrip(S, seed, step);
        }
        check_rest(S, seed, step);
    }
    return S;
}

static void mkname(char *buf, char prefix, unsigned n) {
    buf[0] = prefix;
    if (n < 10) {
        buf[1] = (char)('0' + n), buf[2] = 0;
    } else {
        buf[1] = (char)('0' + n / 10), buf[2] = (char)('0' + n % 10);
        buf[3] = 0;
    }
}

static int model_setup(am_Solver *S) {
    unsigned i;
    for (i = 0; i < NV; ++i) {
        mkname(vnames[i], 'v', i);
        vals[i] = 0;
        if ((vars[i] = am_newvariable(S, &vals[i])) == 0) return 0;
    }
    for (i = 0; i < NC; ++i) {
        mkname(cnames[i], 'c', i);
        cons[i] = NULL, in_solver[i] = 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    unsigned   seed = 1;
    int        steps = NOPS;
    am_Solver *S;
    if (argc > 1) seed = (unsigned)strtoul(argv[1], NULL, 10);
    if (argc > 2) steps = atoi(argv[2]);
    rstate = seed * 2654435761u + 1;
    S = am_newsolver(NULL, NULL);
    if (S == NULL) return 1;
    if (!model_setup(S)) return am_delsolver(S), 1;
    S = run(S, seed, steps);
    am_delsolver(S);
    printf("ok seed=%u steps=%d\n", seed, steps);
    return 0;
}
