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
 * The stream declares the number of arena slots it needs.  A stream that
 * understates it made amH_alloc() walk past the buffer it had just allocated
 * (heap-buffer-overflow under ASan).  Both streams below are byte-identical
 * except for that number, so "reject the short one" cannot be achieved by
 * rejecting malformed input in general. */

#define ROWS_AT(arena_slots)                                                 \
    0x93, arena_slots, 0x01, 0x95, 0xCA, 0x00, 0x00, 0x00, 0x00, 0x05, 0xCA, \
            0x3F, 0x80, 0x00, 0x00, 0x09, 0xCA, 0x3F, 0x80, 0x00, 0x00,

/* [5, total, vars, constraints, rows, objective] -- one variable, no
 * constraints, one row with two terms, empty objective. */
static const unsigned char stream_ok_arena[] = {
        0x95, 0x04, 0x91, 0xA1, 'x',  0x90, ROWS_AT(0x08) 0x91,
        0xCA, 0x00, 0x00, 0x00, 0x00,
};

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
    am_Solver    *S = am_test_new_solver();
    stream_loader sl;

    stream_init(&sl, stream_ok_arena, sizeof stream_ok_arena);
    asserteq(am_load(S, &sl.base), AM_OK);
    asserteq(S->rows.count, 1);
    asserteq(S->vars.count, 1);
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
