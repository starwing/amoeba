/* tests/api_test.c -- API contract regressions
 *
 * Each test here pins down a behaviour that the public API documents (or
 * clearly implies) but that the implementation got wrong at some point.  They
 * are deliberately small: a bug that needs 100 lines of setup is usually a
 * missing unit test, not a missing feature.
 */

#include "amoeba_test.h"

/* ---------------------------------------------------------------------- */
/* am_setstrength() on a constraint that is not in the solver.
 *
 * Documented behaviour: it changes the strength of a constraint.  Only a
 * constraint that has been added needs to be removed and re-added; an
 * unadded one must just take the new value, so the caller can add it later.
 *
 * Regression: the REQUIRED path called am_remove() + am_add() unconditionally,
 * so the function silently REGISTERED a constraint that had never been added
 * (and one that had just failed to be added), leaving the caller with
 * AM_FAILED on the next am_add(). */

TEST(setstrength_unadded_does_not_register) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *ok, *conflict;

    ok = am_newconstraint(S, AM_REQUIRED);
    am_addterm(ok, vx, 1.f);
    am_setrelation(ok, AM_EQUAL);
    am_addconstant(ok, 1.f);
    asserteq(am_add(ok), AM_OK);

    /* x == 2 cannot hold while x == 1 is required */
    conflict = am_newconstraint(S, AM_REQUIRED);
    am_addterm(conflict, vx, 1.f);
    am_setrelation(conflict, AM_EQUAL);
    am_addconstant(conflict, 2.f);
    asserteq(am_add(conflict), AM_UNSATISFIED);
    assertok(!am_hasconstraint(conflict));

    /* downgrading it must not smuggle it into the solver */
    asserteq(am_setstrength(conflict, AM_WEAK), AM_OK);
    assertok(!am_hasconstraint(conflict));

    /* ... so the caller is still the one that decides to add it */
    asserteq(am_add(conflict), AM_OK);
    am_updatevars(S);
    assertnear(x, 1, 1e-4); /* the required constraint still wins */

    am_delsolver(S);
}

/* Changing the strength of an added constraint must keep working: it is the
 * positive control for the test above. */
TEST(setstrength_added_reoptimizes) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *soft;

    soft = am_newconstraint(S, AM_WEAK);
    am_addterm(soft, vx, 1.f);
    am_setrelation(soft, AM_EQUAL);
    am_addconstant(soft, 10.f);
    asserteq(am_add(soft), AM_OK);
    am_updatevars(S);
    assertnear(x, 10, 1e-4);

    asserteq(am_setstrength(soft, AM_STRONG), AM_OK);
    am_updatevars(S);
    assertnear(x, 10, 1e-4); /* still the only constraint */

    am_delsolver(S);
}

/* Tightening a constraint to REQUIRED can fail (it may conflict with what is
 * already required).  The constraint must then be left exactly as it was,
 * i.e. still weak and still installed -- not silently dropped. */
TEST(setstrength_tighten_failure_restores) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *hard, *soft;

    hard = am_newconstraint(S, AM_REQUIRED);
    am_addterm(hard, vx, 1.f);
    am_setrelation(hard, AM_EQUAL);
    am_addconstant(hard, 1.f);
    asserteq(am_add(hard), AM_OK);

    soft = am_newconstraint(S, AM_WEAK);
    am_addterm(soft, vx, 1.f);
    am_setrelation(soft, AM_EQUAL);
    am_addconstant(soft, 2.f);
    asserteq(am_add(soft), AM_OK);

    asserteq(am_setstrength(soft, AM_REQUIRED), AM_UNSATISFIED);
    assertok(am_hasconstraint(soft));
    assertnear(soft->strength, AM_WEAK, 1e-3);

    am_updatevars(S);
    assertnear(x, 1, 1e-4); /* the required constraint still decides */
    am_delsolver(S);
}

/* A variable holds one reference for itself plus one per term that really
 * lives in a constraint expression; am_updatevars() reads that count.
 *
 * Regression: am_addterm() and am_merge() added a reference on every call, so
 * a merged, cancelled or zero term leaked an am_Var entry that no constraint
 * could ever release again. */
TEST(addterm_refcount_balances) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *c = am_newconstraint(S, AM_REQUIRED);
    int            base = am_refcount(S, vx);

    asserteq(base, 1); /* the variable itself */

    am_addterm(c, vx, 1.f); /* a new term */
    asserteq(am_refcount(S, vx), base + 1);

    am_addterm(c, vx, 2.f); /* merged into the term that was already there */
    asserteq(am_refcount(S, vx), base + 1);

    am_addterm(c, vx, -3.f); /* 1 + 2 - 3 == 0: the term goes away */
    asserteq(am_refcount(S, vx), base);

    am_addterm(c, vx, 0.f); /* a zero coefficient never becomes a term */
    asserteq(am_refcount(S, vx), base);

    am_delconstraint(c);
    asserteq(am_refcount(S, vx), base); /* nothing left to release */

    am_delvariable(S, vx); /* the leaked reference kept this from happening */
    asserteq(am_refcount(S, vx), 0);
    am_delsolver(S);
}

/* am_merge() feeds am_addterm()'s bookkeeping: merging a term the target
 * already has must not take a second reference, and a merge that cancels the
 * term out must release one. */
TEST(mergeconstraint_refcount_balances) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *src = am_newconstraint(S, AM_REQUIRED);
    am_Constraint *dst = am_newconstraint(S, AM_REQUIRED);
    int            base = am_refcount(S, vx);

    am_addterm(src, vx, 1.f); /* src: x */
    asserteq(am_refcount(S, vx), base + 1);

    asserteq(am_merge(dst, src, 2.f), AM_OK); /* dst: 2x */
    asserteq(am_refcount(S, vx), base + 2);

    asserteq(am_merge(dst, src, 3.f), AM_OK); /* dst: 5x, still one term */
    asserteq(am_refcount(S, vx), base + 2);

    asserteq(am_merge(dst, src, -5.f), AM_OK); /* 5x - 5x: the term goes */
    asserteq(am_refcount(S, vx), base + 1);

    am_delconstraint(dst);
    am_delconstraint(src);
    asserteq(am_refcount(S, vx), base);
    am_delsolver(S);
}

/* Parameter checks must report AM_PARAM, so a caller can tell a bad call
 * from a solver that rejected a valid constraint (AM_FAILED). */
TEST(param_checks_return_am_param) {
    am_Solver     *S = am_test_new_solver();
    am_Solver     *S2 = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *c = am_newconstraint(S, AM_REQUIRED);
    am_Constraint *c2 = am_newconstraint(S2, AM_WEAK);

    asserteq(am_add(NULL), AM_PARAM);
    asserteq(am_addterm(NULL, vx, 1.f), AM_PARAM);
    asserteq(am_addterm(c, 0, 1.f), AM_PARAM);
    asserteq(am_addconstant(NULL, 1.f), AM_PARAM);
    asserteq(am_setrelation(NULL, AM_EQUAL), AM_PARAM);
    asserteq(am_setrelation(c, 0), AM_PARAM);
    asserteq(am_setstrength(NULL, AM_WEAK), AM_PARAM);
    asserteq(am_addedit(S, 0, AM_MEDIUM), AM_PARAM);
    asserteq(am_merge(NULL, c, 1.f), AM_PARAM);
    asserteq(am_merge(c, c2, 1.f), AM_PARAM);
    asserteq(am_dump(NULL, NULL), AM_PARAM);

    /* a rejected operation is not a parameter error */
    am_addterm(c, vx, 1.f);
    am_setrelation(c, AM_EQUAL);
    am_addconstant(c, 1.f);
    asserteq(am_add(c), AM_OK);
    asserteq(am_add(c), AM_FAILED);

    am_delsolver(S2);
    am_delsolver(S);
}

/* ---------------------------------------------------------------------- */
/* load: fresh solver, byte budget, malformed frames */

typedef struct dump_buf {
    am_Dumper      base;
    unsigned char *p;
    size_t         len;
} dump_buf;

static const char *dump_buf_varname(am_Dumper *d, unsigned idx, am_Id var,
                                    am_Num *value) {
    (void)d, (void)idx, (void)var, (void)value;
    return "x";
}

static int dump_buf_writer(am_Dumper *d, const void *buf, size_t len) {
    dump_buf *db = (dump_buf *)d;
    memcpy(db->p + db->len, buf, len);
    return db->len += len, AM_OK;
}

/* byte cursor; locators return one field's offset */
typedef struct dump_reader {
    const unsigned char *p;
} dump_reader;

static unsigned dump_readraw(dump_reader *r, int width) {
    unsigned n = 0;
    while (width--) n = (n << 8) | (unsigned)*r->p++;
    return n;
}

static unsigned dump_readu32(dump_reader *r) {
    int ty = *r->p++;
    if (ty <= 0x7F) return (unsigned)ty;
    if (ty == 0xCC) return dump_readraw(r, 1);
    if (ty == 0xCD) return dump_readraw(r, 2);
    assert(ty == 0xCE);
    return dump_readraw(r, 4);
}

static size_t dump_u32len(const unsigned char *p) {
    if (*p <= 0x7F) return 1;
    if (*p == 0xCC) return 2;
    if (*p == 0xCD) return 3;
    assert(*p == 0xCE);
    return 5;
}

static unsigned dump_readcount(dump_reader *r) {
    int ty = *r->p++;
    if (ty >= 0x90 && ty <= 0x9F) return (unsigned)(ty - 0x90);
    if (ty == 0xDC) return dump_readraw(r, 2);
    assert(ty == 0xDD);
    return dump_readraw(r, 4);
}

static void dump_skipstring(dump_reader *r) {
    unsigned size;
    int      ty = *r->p++;
    if (ty >= 0xA0 && ty <= 0xBF)
        size = (unsigned)(ty - 0xA0);
    else
        assert(ty == 0xD9), size = (unsigned)*r->p++;
    r->p += size;
}

static void dump_skipfloat(dump_reader *r) {
    assert(*r->p++ == 0xCA);
    r->p += 4;
}

static void dump_skiprowbody(dump_reader *r, unsigned count) {
    unsigned i;
    dump_skipfloat(r);
    for (i = 1; i < count; i += 2) dump_readu32(r), dump_skipfloat(r);
}

static void dump_skiprow(dump_reader *r) {
    dump_skiprowbody(r, dump_readcount(r));
}

static void dump_skipheader(dump_reader *r) {
    unsigned i, count;
    assert(dump_readcount(r) == 5); /* [total, vars, constraints, rows, obj] */
    dump_readu32(r);
    count = dump_readcount(r);
    for (i = 0; i < count; ++i) dump_skipstring(r);
    count = dump_readcount(r);
    for (i = 0; i < count; ++i) {
        assert(dump_readcount(r) == 6);
        dump_skipstring(r), dump_skipfloat(r);
        dump_readu32(r), dump_readu32(r), dump_readu32(r);
        dump_skiprow(r);
    }
}

static size_t dump_total_offset(const unsigned char *buf, size_t *plen) {
    dump_reader r;
    r.p = buf;
    assert(dump_readcount(&r) == 5);
    *plen = dump_u32len(r.p);
    return (size_t)(r.p - buf);
}

static size_t dump_arena_offset(const unsigned char *buf) {
    dump_reader r;
    unsigned    count;
    r.p = buf;
    dump_skipheader(&r);
    count = dump_readcount(&r);
    assert(count & 1);
    return (size_t)(r.p - buf);
}

static size_t dump_row_key_offset(const unsigned char *buf, int which) {
    dump_reader r;
    unsigned    i, count;
    r.p = buf;
    dump_skipheader(&r);
    count = dump_readcount(&r);
    assert(count & 1);
    dump_readu32(&r);
    for (i = 1; i < count; i += 2) {
        if ((int)(i / 2) == which) return (size_t)(r.p - buf);
        dump_readu32(&r);
        dump_skiprow(&r);
    }
    assert(0);
    return 0;
}

static size_t dump_row_term_offset(const unsigned char *buf, int which) {
    dump_reader r;
    unsigned    i, count, rowcount;
    r.p = buf;
    dump_skipheader(&r);
    count = dump_readcount(&r);
    assert(count & 1);
    dump_readu32(&r);
    for (i = 1; i < count; i += 2) {
        dump_readu32(&r);
        rowcount = dump_readcount(&r);
        if (rowcount >= 3) {
            unsigned t;
            dump_skipfloat(&r);
            for (t = 0; t < (unsigned)which; ++t)
                dump_readu32(&r), dump_skipfloat(&r);
            return (size_t)(r.p - buf);
        }
        dump_skiprowbody(&r, rowcount);
    }
    assert(0);
    return 0;
}

static size_t dump_first_marker_offset(const unsigned char *buf) {
    dump_reader r;
    unsigned    count;
    r.p = buf;
    assert(dump_readcount(&r) == 5);
    dump_readu32(&r);
    count = dump_readcount(&r);
    while (count--) dump_skipstring(&r);
    assert(dump_readcount(&r) >= 1);
    assert(dump_readcount(&r) == 6);
    dump_skipstring(&r), dump_skipfloat(&r);
    return (size_t)(r.p - buf);
}

static size_t dump_one_row(unsigned char *buf) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *c = am_newconstraint(S, AM_MEDIUM);
    dump_buf       db;

    am_addterm(c, vx, 1.f);
    am_setrelation(c, AM_EQUAL);
    am_addconstant(c, 10.f);
    asserteq(am_add(c), AM_OK);
    am_updatevars(S);
    memset(&db, 0, sizeof(db));
    db.base.var_name = dump_buf_varname;
    db.base.writer = dump_buf_writer;
    db.p = buf;
    asserteq(am_dump(S, &db.base), AM_OK);
    am_delsolver(S);
    return db.len;
}

static size_t dump_two_soft(unsigned char *buf) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0, y = 0;
    am_Id          vx = am_newvariable(S, &x), vy = am_newvariable(S, &y);
    am_Constraint *c = am_newconstraint(S, AM_MEDIUM);
    dump_buf       db;

    am_addterm(c, vx, 1.f);
    am_setrelation(c, AM_EQUAL);
    am_addconstant(c, 10.f);
    asserteq(am_add(c), AM_OK);
    c = am_newconstraint(S, AM_MEDIUM);
    am_addterm(c, vy, 1.f);
    am_setrelation(c, AM_EQUAL);
    am_addconstant(c, 20.f);
    asserteq(am_add(c), AM_OK);
    memset(&db, 0, sizeof(db));
    db.base.var_name = dump_buf_varname;
    db.base.writer = dump_buf_writer;
    db.p = buf;
    asserteq(am_dump(S, &db.base), AM_OK);
    am_delsolver(S);
    return db.len;
}

static size_t dump_unconfigured(unsigned char *buf, int relation) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *c = am_newconstraint(S, AM_MEDIUM);
    dump_buf       db;

    am_addterm(c, vx, 1.f);
    if (relation) am_setrelation(c, AM_EQUAL);
    am_addconstant(c, 10.f);
    memset(&db, 0, sizeof(db));
    db.base.var_name = dump_buf_varname;
    db.base.writer = dump_buf_writer;
    db.p = buf;
    asserteq(am_dump(S, &db.base), AM_OK);
    am_delsolver(S);
    return db.len;
}

static size_t dump_encode_u32(unsigned char *p, unsigned v) {
    if (v <= 0x7F) {
        p[0] = (unsigned char)v;
        return 1;
    }
    if (v <= 0xFF) {
        p[0] = 0xCC;
        p[1] = (unsigned char)v;
        return 2;
    }
    if (v <= 0xFFFF) {
        p[0] = 0xCD;
        p[1] = (unsigned char)(v >> 8);
        p[2] = (unsigned char)v;
        return 3;
    }
    p[0] = 0xCE;
    p[1] = (unsigned char)(v >> 24);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 8);
    p[4] = (unsigned char)v;
    return 5;
}

static size_t dump_patch_total(unsigned char *buf, size_t len, unsigned value) {
    unsigned char out[2048];
    size_t        at, oldlen, n;
    assert(len < sizeof(out));
    at = dump_total_offset(buf, &oldlen);
    memcpy(out, buf, at);
    n = at + dump_encode_u32(out + at, value);
    memcpy(out + n, buf + at + oldlen, len - at - oldlen);
    n += len - at - oldlen;
    memcpy(buf, out, n);
    return n;
}

typedef struct stream_loader {
    am_Loader      base;
    const char    *p;
    size_t         len;
    am_Constraint *cons; /* set by load_cons, if the stream has one */
} stream_loader;

static am_Num  stream_value;
static am_Num *stream_load_var(am_Loader *L, const char *name, unsigned idx,
                               am_Id var) {
    (void)L, (void)name, (void)idx, (void)var;
    stream_value = 0;
    return &stream_value;
}

static void stream_load_cons(am_Loader *L, const char *name, unsigned idx,
                             am_Constraint *cons) {
    stream_loader *sl = (stream_loader *)L;
    (void)name, (void)idx;
    sl->cons = cons;
}

static const char *stream_read(am_Loader *L, size_t *plen) {
    stream_loader *sl = (stream_loader *)L;
    const char    *p = sl->p;
    *plen = sl->len;
    sl->p = NULL, sl->len = 0;
    return p;
}

static void stream_init(stream_loader *sl, const unsigned char *bytes,
                        size_t len) {
    memset(sl, 0, sizeof(*sl));
    sl->base.load_var = stream_load_var;
    sl->base.load_cons = stream_load_cons;
    sl->base.reader = stream_read;
    sl->p = (const char *)bytes;
    sl->len = len;
}

static am_Solver *load_stream(stream_loader *sl, size_t budget) {
    return am_load(am_test_allocf, NULL, &sl->base, budget);
}

TEST(load_accepts_real_dump) {
    static unsigned char stream[2048];
    size_t               len = dump_one_row(stream);
    stream_loader        sl;
    am_Solver           *S;

    stream_init(&sl, stream, len);
    assertok((S = load_stream(&sl, 0)) != NULL);
    asserteq(S->rows.count, 1);
    asserteq(S->vars.count, 1);
    asserteq(S->constraints.count, 1);
    am_delsolver(S);
}

TEST(load_rejects_short_arena) {
    static unsigned char stream[2048];
    size_t        len = dump_one_row(stream), at = dump_arena_offset(stream);
    stream_loader sl;

    assertok(at < len);
    asserteq(stream[at], 8); /* one row, two terms: am_calcsize(2) slots */
    stream[at] = 1;
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_budget_rejects_tiny) {
    static unsigned char stream[2048];
    size_t               len = dump_one_row(stream);
    stream_loader        sl;

    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 1) == NULL);
}

TEST(load_budget_matches_allocator_peak) {
    static unsigned char stream[4096];
    size_t               len = dump_two_soft(stream), budget, peak;
    stream_loader        sl;
    am_Solver           *S;

    maxmem = 0; /* measure the load only */
    stream_init(&sl, stream, len);
    assertok((S = load_stream(&sl, 0)) != NULL);
    peak = maxmem;
    am_delsolver(S);
    assertok(peak > sizeof(am_Solver));
    budget = peak - sizeof(am_Solver);

    stream_init(&sl, stream, len);
    assertok((S = load_stream(&sl, budget + 1)) != NULL);
    am_delsolver(S);

    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, budget) == NULL);
}

TEST(load_budget_released_after_load) {
    static unsigned char stream[4096];
    size_t               len = dump_two_soft(stream), budget, peak;
    stream_loader        sl;
    am_Solver           *S;
    unsigned             i;

    maxmem = 0;
    stream_init(&sl, stream, len);
    assertok((S = load_stream(&sl, 0)) != NULL);
    peak = maxmem;
    am_delsolver(S);
    budget = peak - sizeof(am_Solver);

    stream_init(&sl, stream, len);
    assertok((S = load_stream(&sl, budget + 1)) != NULL);
    /* force table growths: they would fail if maxmem were still armed */
    for (i = 0; i < 64; ++i) {
        am_Num v = 0;
        assertok(am_newvariable(S, &v) != 0);
    }
    am_delsolver(S);
}

TEST(load_budget_covers_pool) {
    static unsigned char stream[1024];
    size_t               len = dump_one_row(stream);
    stream_loader        sl;
    am_Solver           *S;

    stream_init(&sl, stream, len);
    assertok(am_load(NULL, NULL, &sl.base, 4096) == NULL);
    stream_init(&sl, stream, len);
    assertok((S = am_load(NULL, NULL, &sl.base, 64 * 1024)) != NULL);
    am_delsolver(S);
}

TEST(load_rejects_bad_row_key) {
    static unsigned char stream[4096];
    size_t len = dump_two_soft(stream), at = dump_row_key_offset(stream, 0);
    stream_loader sl;

    assertok(at < len);
    stream[at] = 0x7F; /* index 31 >= total (6) */
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_rejects_bad_term) {
    static unsigned char stream[4096];
    size_t len = dump_two_soft(stream), at = dump_row_term_offset(stream, 0);
    stream_loader sl;

    assertok(at < len);
    stream[at] = 0x7F;
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_rejects_bad_marker) {
    static unsigned char stream[4096];
    size_t len = dump_two_soft(stream), at = dump_first_marker_offset(stream);
    stream_loader sl;

    assertok(at < len);
    stream[at] = 0x7F;
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_rejects_duplicate_term) {
    static unsigned char stream[4096];
    size_t               len = dump_two_soft(stream);
    size_t               t0 = dump_row_term_offset(stream, 0);
    size_t               t1 = dump_row_term_offset(stream, 1);
    stream_loader        sl;

    assertok(t1 < len);
    stream[t1] = stream[t0];
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_rejects_duplicate_row_key) {
    static unsigned char stream[4096];
    size_t               len = dump_two_soft(stream);
    size_t               k0 = dump_row_key_offset(stream, 0);
    size_t               k1 = dump_row_key_offset(stream, 1);
    stream_loader        sl;

    assertok(k1 < len);
    stream[k1] = stream[k0];
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_rejects_oversized_vars) {
    static unsigned char stream[4096];
    size_t               len = dump_two_soft(stream);
    size_t               at, enclen;
    stream_loader        sl;

    at = dump_total_offset(stream, &enclen);
    asserteq(enclen, 1); /* total = 6 */
    stream[at] = 1;      /* vars count is 2 */
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_rejects_wrapping_total) {
    static unsigned char stream[8192];
    size_t               len = dump_two_soft(stream);
    stream_loader        sl;

    len = dump_patch_total(stream, len, 0xFFFFFFFFu);
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_accepts_zero_marker) {
    static unsigned char stream[2048];
    size_t               len = dump_unconfigured(stream, 1);
    stream_loader        sl;
    am_Solver           *S;

    stream_init(&sl, stream, len);
    assertok((S = load_stream(&sl, 0)) != NULL);
    assertok(sl.cons != NULL);
    asserteq(sl.cons->marker_id, 0); /* 0 = absent, not offset + 0 */
    asserteq(sl.cons->other_id, 0);
    asserteq(am_add(sl.cons), AM_OK);
    am_delsolver(S);
}

TEST(load_accepts_unconfigured_constraint) {
    static unsigned char stream[2048];
    size_t               len = dump_unconfigured(stream, 0);
    stream_loader        sl;
    am_Solver           *S;

    stream_init(&sl, stream, len);
    assertok((S = load_stream(&sl, 0)) != NULL);
    assertok(sl.cons != NULL);
    asserteq(sl.cons->relation, 0);
    am_delsolver(S);
}

TEST(load_rejects_garbage) {
    static const unsigned char junk[] = {'h', 'e', 'l', 'l', 'o'};
    stream_loader              sl;

    stream_init(&sl, junk, sizeof(junk));
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_rejects_every_truncation) {
    static unsigned char stream[4096];
    size_t               len = dump_two_soft(stream), i;
    stream_loader        sl;

    for (i = 0; i < len; ++i) {
        stream_init(&sl, stream, i);
        assertok(load_stream(&sl, 0) == NULL);
    }
}

/* The symbol packs a 30-bit id and a 2-bit type into one unsigned; the whole
 * implementation and the dump format assume that.  (The old test.c only
 * printed these sizes; assert the one that is actually a contract.) */
TEST(symbol_fits_one_unsigned) {
    asserteq(sizeof(am_Symbol), sizeof(unsigned));
}

#include "api_test.gen.inc"
