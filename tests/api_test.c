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

/* ---------------------------------------------------------------------- */
/* am_load() must bound the arena it reads rows into.
 *
 * The stream declares how many arena slots the row tables need.  A stream
 * that understates it made amH_alloc() walk past the buffer it had just
 * allocated (heap-buffer-overflow under ASan).  Both tests below run on a
 * real am_dump() stream and only change that one number, so the short one
 * cannot be rejected by rejecting malformed input in general. */

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

/* Walk a dump stream (notes/dump-format.md) up to the rows section and return
 * the offset of its arena slot count.  The tests only need to patch that
 * number, so this skips every other payload instead of decoding it. */
typedef struct dump_reader { const unsigned char *p; } dump_reader;

static unsigned dump_readraw(dump_reader *r, int width);

static unsigned dump_readu32(dump_reader *r) {
    int ty = *r->p++;
    if (ty <= 0x7F) return (unsigned)ty;
    if (ty == 0xCC) return dump_readraw(r, 1);
    if (ty == 0xCD) return dump_readraw(r, 2);
    assert(ty == 0xCE);
    return dump_readraw(r, 4);
}

static unsigned dump_readraw(dump_reader *r, int width) {
    unsigned n = 0;
    while (width--) n = (n << 8) | (unsigned)*r->p++;
    return n;
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
    int ty = *r->p++;
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

static void dump_skiprow(dump_reader *r) {
    unsigned i, count = dump_readcount(r);
    dump_skipfloat(r);
    for (i = 1; i < count; i += 2)
        dump_readu32(r), dump_skipfloat(r);
}

static size_t dump_arena_offset(const unsigned char *buf) {
    dump_reader r;
    unsigned i, count;
    r.p = buf;
    assert(dump_readcount(&r) == 5); /* five top-level fields */
    dump_readu32(&r);                /* total */
    count = dump_readcount(&r);
    for (i = 0; i < count; ++i) dump_skipstring(&r);
    count = dump_readcount(&r);
    for (i = 0; i < count; ++i) {
        assert(dump_readcount(&r) == 6);
        dump_skipstring(&r), dump_skipfloat(&r);
        dump_readu32(&r), dump_readu32(&r), dump_readu32(&r);
        dump_skiprow(&r);
    }
    count = dump_readcount(&r); /* 1 + 2*rows (arena + sym/row pairs) */
    assert(count & 1);
    return (size_t)(r.p - buf);
}

/* top level: [total, vars, constraints, rows, objective] (5 elements).
 * One variable, one soft constraint -> one row with two terms. */
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

typedef struct stream_loader {
    am_Loader   base;
    const char *p;
    size_t      len;
} stream_loader;

static am_Num  stream_value;
static am_Num *stream_load_var(am_Loader *L, const char *name, unsigned idx,
                               am_Id var) {
    (void)L, (void)name, (void)idx, (void)var;
    stream_value = 0;
    return &stream_value;
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
    sl->base.reader = stream_read;
    sl->p = (const char *)bytes;
    sl->len = len;
}

TEST(load_accepts_correct_arena) {
    static unsigned char stream[1024];
    size_t         len = dump_one_row(stream);
    am_Solver     *S = am_test_new_solver();
    stream_loader  sl;

    stream_init(&sl, stream, len);
    asserteq(am_load(S, &sl.base), AM_OK);
    asserteq(S->rows.count, 1);
    asserteq(S->vars.count, 1);
    am_delsolver(S);
}

TEST(load_rejects_short_arena) {
    static unsigned char stream[1024];
    size_t         len = dump_one_row(stream), at = dump_arena_offset(stream);
    am_Solver     *S = am_test_new_solver();
    stream_loader  sl;

    assertok(at < len);
    asserteq(stream[at], 8); /* one row, two terms: am_calcsize(2) slots */
    stream[at] = 1;          /* the stream is otherwise byte-identical */
    stream_init(&sl, stream, len);
    asserteq(am_load(S, &sl.base), AM_FAILED);
    am_delsolver(S);
}

/* The symbol packs a 30-bit id and a 2-bit type into one unsigned; the whole
 * implementation and the dump format assume that.  (The old test.c only
 * printed these sizes; assert the one that is actually a contract.) */
TEST(symbol_fits_one_unsigned) {
    asserteq(sizeof(am_Symbol), sizeof(unsigned));
    assertok(am_null().id == 0 && am_null().type == 0);
}

#include "api_test.gen.inc"
