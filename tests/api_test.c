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
    assertnear(am_strength(soft), AM_WEAK, 1e-3);

    am_updatevars(S);
    assertnear(x, 1, 1e-4); /* the required constraint still decides */
    am_delsolver(S);
}

/* A negative strength would give an error symbol a negative weight, i.e. a
 * direction that lowers the objective for ever.  Every entry point that takes
 * a strength must reject it up front: the pointer-returning constructors
 * return NULL, the int-returning ones return AM_PARAM, and an existing
 * constraint/edit keeps its old weight.
 *
 * Regression: am_setstrength() and am_addedit() accepted it and rewrote the
 * objective coefficients, so a later amA_optimize() could see an unbounded
 * objective. */
TEST(negative_strength_is_rejected) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *soft = am_newconstraint(S, AM_STRONG);
    am_Constraint *idle = am_newconstraint(S, AM_WEAK);

    assertok(am_newconstraint(S, -1.f) == NULL);
    assertok(am_cloneconstraint(soft, -1.f) == NULL);

    am_addterm(soft, vx, 1.f), am_setrelation(soft, AM_EQUAL);
    am_addconstant(soft, 5.f);
    asserteq(am_add(soft), AM_OK);

    asserteq(am_setstrength(soft, -1.f), AM_PARAM);
    assertnear(am_strength(soft), AM_STRONG, 1e-3);
    assertok(am_hasconstraint(soft));

    asserteq(am_setstrength(idle, -1.f), AM_PARAM);
    assertnear(am_strength(idle), AM_WEAK, 1e-3);
    assertok(!am_hasconstraint(idle));

    asserteq(am_addedit(S, vx, -1.f), AM_PARAM);
    assertok(!am_hasedit(S, vx)); /* nothing was created */
    asserteq(am_addedit(S, vx, AM_MEDIUM), AM_OK);
    asserteq(am_addedit(S, vx, -1.f), AM_PARAM); /* the edit keeps its weight */

    am_updatevars(S);
    assertnear(x, 5, 1e-4); /* the installed constraint still decides */
    am_delsolver(S);
}

/* A soft inequality carries its violation in the error symbol only; the
 * marker of an inequality is a slack, never part of the objective.  Lowering
 * the strength of an added one must therefore only reweight that error.
 *
 * Regression: am_setstrength() merged the marker into the objective as well,
 * so a downgrade gave the slack a negative weight.  amA_optimize() then had a
 * direction it could follow for ever and hit assert(leave.u.id != 0). */
TEST(setstrength_soft_inequality_downgrade_is_safe) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *ge = am_newconstraint(S, AM_MEDIUM);

    am_addterm(ge, vx, 1.f), am_setrelation(ge, AM_GREATEREQUAL);
    am_addconstant(ge, 10.f); /* x >= 10 */
    asserteq(am_add(ge), AM_OK);
    am_updatevars(S);
    assertnear(x, 10, 1e-4);

    asserteq(am_setstrength(ge, AM_WEAK), AM_OK);
    am_updatevars(S);
    assertnear(x, 10, 1e-4); /* still the only constraint deciding */
    assertnear(am_error(ge), 0, 1e-4);
    am_delsolver(S);
}

/* x <= 10 and x == 0 are both satisfied at 0, so making the first constraint
 * stronger must not move the solution: what changes is the price of a
 * violation, not the price of using the slack.  The marker is a real tableau
 * variable, so a slack that leaks into the objective acts like a force and
 * drags x to the boundary.
 *
 * Regression: the same merge that aborts on a downgrade made this upgrade
 * converge to x == 10. */
TEST(setstrength_soft_inequality_upgrade_keeps_the_optimum) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *le = am_newconstraint(S, AM_WEAK);
    am_Constraint *eq = am_newconstraint(S, AM_WEAK);

    am_addterm(le, vx, 1.f), am_setrelation(le, AM_LESSEQUAL);
    am_addconstant(le, 10.f); /* x <= 10 */
    asserteq(am_add(le), AM_OK);
    am_addterm(eq, vx, 1.f), am_setrelation(eq, AM_EQUAL);
    am_addconstant(eq, 0.f); /* x == 0 */
    asserteq(am_add(eq), AM_OK);
    am_updatevars(S);
    assertnear(x, 0, 1e-4);

    asserteq(am_setstrength(le, AM_MEDIUM), AM_OK);
    am_updatevars(S);
    assertnear(x, 0, 1e-4); /* 0 satisfies both, at any weight */
    assertnear(am_error(le), 0, 1e-4);
    assertnear(am_error(eq), 0, 1e-4);
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
/* lifetime: a missing object (or a pointer the solver no longer owns) is
 * "nothing to do" instead of a dereference.  Each of these used to be an
 * assert that vanished in release builds, leaving a NULL (or recycled)
 * dereference behind. */

/* A clone either returns a fully built constraint or NULL.  When the allocator
 * refuses the new constraint there is no object to write a relation into. */
static int fail_constraint_alloc;

static void *clone_test_allocf(void **pud, void *p, size_t n, size_t o,
                               am_Type t) {
    if (n != 0 && fail_constraint_alloc && t == am_TConstraint) return NULL;
    return am_test_allocf(pud, p, n, o, t);
}

TEST(cloneconstraint_reports_alloc_failure) {
    am_Solver     *S = am_newsolver(clone_test_allocf, NULL);
    am_Num         x = 0;
    am_Id          vx;
    am_Constraint *c;
    assertok(S != NULL);

    vx = am_newvariable(S, &x);
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, vx, 1.f), am_setrelation(c, AM_EQUAL), am_addconstant(c, 1.f);
    asserteq(am_add(c), AM_OK);

    fail_constraint_alloc = 1;
    assertok(am_cloneconstraint(c, AM_REQUIRED) == NULL);
    fail_constraint_alloc = 0;

    /* the positive control: with the allocator back, cloning still works */
    c = am_cloneconstraint(c, AM_WEAK);
    assertok(c != NULL);
    asserteq(am_relation(c), AM_EQUAL);
    am_delsolver(S);
}

/* The caller's allocator must own every object: suggests are allocated one by
 * one, not carved out of an internal page pool. */
static unsigned type_allocs[8];

static void *count_test_allocf(void **pud, void *p, size_t n, size_t o,
                               am_Type t) {
    if (n != 0 && (unsigned)t < 8) ++type_allocs[t];
    return am_test_allocf(pud, p, n, o, t);
}

TEST(custom_allocator_owns_suggests) {
    am_Solver *S;
    am_Num     x = 0;
    am_Id      vx;

    S = am_newsolver(count_test_allocf, NULL);
    assertok(S != NULL);
    vx = am_newvariable(S, &x);
    memset(type_allocs, 0, sizeof(type_allocs));
    am_suggest(S, vx, 3.f);
    assertok(am_hasedit(S, vx));
    assertok(type_allocs[am_TSuggest] > 0);
    asserteq(type_allocs[am_TNone], 0);
    am_delsolver(S);
}

/* am_suggest() on a variable the solver does not know has no edit to create
 * and no value to move; the id may be stale after am_updatevars() collected
 * the variable, so this is not just a parameter error. */
TEST(suggest_unknown_var_is_noop) {
    am_Solver *S = am_test_new_solver();
    am_Num     x = 0;
    am_Id      vx = am_newvariable(S, &x);

    am_suggest(S, vx, 3.f); /* the positive control: this one does work */
    assertok(am_hasedit(S, vx));
    am_suggest(S, vx + 1000, 5.f); /* unknown id: nothing to do */
    assertok(!am_hasedit(S, vx + 1000));
    am_updatevars(S);
    assertnear(x, 3, 1e-4);
    am_delsolver(S);
}

/* A NULL solver is not a parameter error either: like free(NULL), it means
 * "nothing to do" everywhere the solver is only used, not created. */
TEST(null_solver_calls_are_noop) {
    am_Num coef = 0;

    am_updatevars(NULL);
    am_delsolver(NULL);
    am_resetsolver(NULL);
    am_clearedits(NULL);
    am_remove(NULL);
    am_resetconstraint(NULL);
    am_delconstraint(NULL);
    assertok(!am_hasedit(NULL, 1));
    asserteq(am_refcount(NULL, 1), 0);
    asserteq(am_vars(NULL, 1), 0);
    assertok(am_constraints(NULL, NULL) == NULL);
    asserteq(am_terms(NULL, 1, &coef), 0);
    asserteq(am_strength(NULL), 0);
    asserteq(am_constant(NULL), 0);
    asserteq(am_error(NULL), 0);
    asserteq(am_relation(NULL), 0);
}

/* ---------------------------------------------------------------------- */
/* introspection: walking the solver's variables and constraints, and a
 * constraint's terms.
 *
 * The anchor is an element you already hold: pass 0 / NULL to start, and a
 * returned 0 / NULL means "end", so a walk is
 *
 *     for (id = 0; (id = am_vars(S, id)) != 0; )
 *
 * The order is the hash table's slot order, not creation order, so these
 * tests mark what they see and compare as a set: every element exactly once,
 * no strangers, and the walk stops after the last one. */

#define WALK_N   6
#define WALK_BIG 64

TEST(vars_walk_covers_every_variable) {
    am_Solver *S = am_test_new_solver();
    am_Num     v[WALK_N];
    am_Id      ids[WALK_N], id, last = 0;
    int        i, n = 0, seen[WALK_N];

    for (i = 0; i < WALK_N; ++i) v[i] = 0, ids[i] = am_newvariable(S, &v[i]);
    for (i = 0; i < WALK_N; ++i) seen[i] = 0;

    id = 0; /* 0 = start */
    while ((id = am_vars(S, id)) != 0) {
        for (i = 0; i < WALK_N; ++i)
            if (ids[i] == id) ++seen[i];
        last = id;
        if (++n > WALK_N) break; /* runaway: the checks below fail */
    }
    asserteq(n, WALK_N); /* the walk is as long as the table */
    for (i = 0; i < WALK_N; ++i) asserteq(seen[i], 1); /* each exactly once */
    asserteq(am_vars(S, last), 0);        /* the last one ends the walk */
    asserteq(am_vars(S, last + 1000), 0); /* an unknown anchor ends it too */
    am_delsolver(S);
}

/* Growing past the initial table size rehashes the table: the walk must still
 * visit every variable exactly once, across probe chains instead of one key
 * per slot. */
TEST(vars_walk_survives_resize) {
    am_Solver *S = am_test_new_solver();
    am_Num     v[WALK_BIG];
    am_Id      ids[WALK_BIG], id;
    int        i, n = 0, seen[WALK_BIG];

    for (i = 0; i < WALK_BIG; ++i) v[i] = 0, ids[i] = am_newvariable(S, &v[i]);
    for (i = 0; i < WALK_BIG; ++i) seen[i] = 0;

    id = 0;
    while ((id = am_vars(S, id)) != 0) {
        for (i = 0; i < WALK_BIG; ++i)
            if (ids[i] == id) ++seen[i];
        if (++n > WALK_BIG) break; /* runaway: the count check fails */
    }
    asserteq(n, WALK_BIG);
    for (i = 0; i < WALK_BIG; ++i) asserteq(seen[i], 1);
    am_delsolver(S);
}

/* am_delvariable() leaves a hole in the table; the walk must skip it rather
 * than visit the slot again or stop early. */
TEST(vars_walk_skips_deleted_variable) {
    am_Solver *S = am_test_new_solver();
    am_Num     v[WALK_N];
    am_Id      ids[WALK_N], id;
    int        i, k, n = 0, seen[WALK_N];

    for (i = 0; i < WALK_N; ++i) v[i] = 0, ids[i] = am_newvariable(S, &v[i]);
    for (i = 0; i < WALK_N; i += 2) am_delvariable(S, ids[i]); /* keep odd */
    for (i = 0; i < WALK_N; ++i) seen[i] = 0;

    id = 0;
    while ((id = am_vars(S, id)) != 0) {
        for (k = -1, i = 0; i < WALK_N; ++i)
            if (ids[i] == id) k = i;
        assertok(k >= 0 && (k & 1)); /* a deleted variable must not come back */
        ++seen[k];
        if (++n > WALK_N) break;
    }
    asserteq(n, WALK_N / 2);
    for (i = 0; i < WALK_N; ++i) asserteq(seen[i], (i & 1) ? 1 : 0);
    am_delsolver(S);
}

TEST(constraints_walk_covers_every_constraint) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *cs[WALK_N], *c, *last = 0;
    int            i, n = 0, seen[WALK_N];

    for (i = 0; i < WALK_N; ++i)
        cs[i] = am_newconstraint(S, i & 1 ? AM_WEAK : AM_MEDIUM);
    for (i = 0; i < WALK_N; ++i) seen[i] = 0;
    /* creating a constraint already registers it: adding is not required to
     * be walkable, which is what lets a caller enumerate what it built */
    am_addterm(cs[0], vx, 1.f), am_setrelation(cs[0], AM_EQUAL);
    asserteq(am_add(cs[0]), AM_OK);

    c = 0; /* NULL = start */
    while ((c = am_constraints(S, c)) != NULL) {
        for (i = 0; i < WALK_N; ++i)
            if (cs[i] == c) ++seen[i];
        last = c;
        if (++n > WALK_N) break;
    }
    asserteq(n, WALK_N);
    for (i = 0; i < WALK_N; ++i) asserteq(seen[i], 1);
    assertok(am_constraints(S, last) == NULL);
    am_delsolver(S);
}

TEST(constraints_walk_skips_deleted_constraint) {
    am_Solver     *S = am_test_new_solver();
    am_Constraint *cs[WALK_N], *c;
    int            i, k, n = 0, seen[WALK_N];

    for (i = 0; i < WALK_N; ++i) cs[i] = am_newconstraint(S, AM_MEDIUM);
    for (i = 0; i < WALK_N; ++i) seen[i] = 0;
    am_delconstraint(cs[1]), am_delconstraint(cs[4]);

    c = 0;
    while ((c = am_constraints(S, c)) != NULL) {
        for (k = -1, i = 0; i < WALK_N; ++i)
            if (cs[i] == c) k = i;
        assertok(k >= 0 && k != 1 && k != 4);
        ++seen[k];
        if (++n > WALK_N) break;
    }
    asserteq(n, WALK_N - 2);
    for (i = 0; i < WALK_N; ++i) asserteq(seen[i], (i == 1 || i == 4) ? 0 : 1);
    am_delsolver(S);
}

/* A constraint's terms are walkable before it is added: that is the "read
 * back what I configured" direction the introspection API exists for. */
TEST(terms_walk_reports_each_term_once) {
    am_Solver     *S = am_test_new_solver();
    am_Num         v[WALK_N];
    am_Id          ids[WALK_N], id, last = 0;
    am_Constraint *c = am_newconstraint(S, AM_MEDIUM);
    am_Num         coef;
    int            i, k, n = 0, seen[WALK_N];

    for (i = 0; i < WALK_N; ++i) v[i] = 0, ids[i] = am_newvariable(S, &v[i]);
    for (i = 0; i < WALK_N; ++i) seen[i] = 0;
    for (i = 0; i < WALK_N; ++i) am_addterm(c, ids[i], (am_Num)(i + 1));
    am_setrelation(c, AM_EQUAL); /* the terms written first are the LHS */

    id = 0; /* 0 = start */
    while ((id = am_terms(c, id, &coef)) != 0) {
        for (k = -1, i = 0; i < WALK_N; ++i)
            if (ids[i] == id) k = i;
        assertok(k >= 0); /* only terms we added may come back */
        ++seen[k];
        /* the expression as stored: a LHS written before the relation is
         * kept negated (notes/api-contract.md 3) */
        assertnear(coef, -(am_Num)(k + 1), 1e-5);
        last = id;
        if (++n > WALK_N) break;
    }
    asserteq(n, WALK_N);
    for (i = 0; i < WALK_N; ++i) asserteq(seen[i], 1);
    asserteq(am_terms(c, last, &coef), 0); /* the last term ends the walk */
    asserteq(am_terms(c, last + 1000, &coef), 0); /* unknown anchor */
    asserteq(am_terms(c, 0, NULL), 0);            /* coef is required */
    am_delsolver(S);
}

/* +2 cancels the stored -2, so ids[1] leaves the expression; the walk must
 * not report a term that is no longer there. */
TEST(terms_walk_skips_cancelled_term) {
    am_Solver     *S = am_test_new_solver();
    am_Num         v[WALK_N];
    am_Id          ids[WALK_N], id;
    am_Constraint *c = am_newconstraint(S, AM_MEDIUM);
    am_Num         coef;
    int            i, k, n = 0, seen[WALK_N];

    for (i = 0; i < WALK_N; ++i) v[i] = 0, ids[i] = am_newvariable(S, &v[i]);
    for (i = 0; i < WALK_N; ++i) seen[i] = 0;
    for (i = 0; i < WALK_N; ++i) am_addterm(c, ids[i], (am_Num)(i + 1));
    am_setrelation(c, AM_EQUAL);
    asserteq(am_addterm(c, ids[1], 2.f), AM_OK);

    id = 0;
    while ((id = am_terms(c, id, &coef)) != 0) {
        for (k = -1, i = 0; i < WALK_N; ++i)
            if (ids[i] == id) k = i;
        assertok(k >= 0 && k != 1); /* the cancelled term is gone */
        ++seen[k];
        if (++n > WALK_N) break;
    }
    asserteq(n, WALK_N - 1);
    for (i = 0; i < WALK_N; ++i) asserteq(seen[i], i == 1 ? 0 : 1);
    am_delsolver(S);
}

/* AM_REQUIRED is +inf and a strength is an am_Num: an accessor returning int
 * truncates it into undefined behaviour, which UBSan reports at the call. */
TEST(strength_accessor_keeps_the_number) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *req = am_newconstraint(S, AM_REQUIRED);
    am_Constraint *zero = am_newconstraint(S, 0.f);
    am_Constraint *frac = am_newconstraint(S, (am_Num)0.5);
    am_Constraint *added = am_newconstraint(S, AM_MEDIUM);

    assertok(am_strength(req) == AM_REQUIRED);  /* inf only compares exactly */
    assertok(am_strength(zero) == AM_REQUIRED); /* 0 is normalized, not kept */
    assertnear(am_strength(frac), 0.5, 0);

    am_addterm(added, vx, 1.f), am_setrelation(added, AM_EQUAL);
    asserteq(am_add(added), AM_OK);
    assertnear(am_strength(added), AM_MEDIUM, 0);
    asserteq(am_setstrength(added, AM_STRONG), AM_OK);
    assertnear(am_strength(added), AM_STRONG, 0);
    am_delsolver(S);
}

TEST(relation_accessor_reports_the_relation) {
    am_Solver     *S = am_test_new_solver();
    am_Constraint *le = am_newconstraint(S, AM_MEDIUM);
    am_Constraint *eq = am_newconstraint(S, AM_MEDIUM);
    am_Constraint *ge = am_newconstraint(S, AM_MEDIUM);
    am_Constraint *unset = am_newconstraint(S, AM_MEDIUM);

    asserteq(am_relation(unset), 0); /* no relation set yet */
    am_setrelation(le, AM_LESSEQUAL), am_setrelation(eq, AM_EQUAL);
    am_setrelation(ge, AM_GREATEREQUAL);
    asserteq(am_relation(le), AM_LESSEQUAL);
    asserteq(am_relation(eq), AM_EQUAL);
    asserteq(am_relation(ge), AM_GREATEREQUAL);
    am_delsolver(S);
}

/* The violation is the sum of the constraint's error variables, read straight
 * from the tableau: a required constraint has none, a soft equality keeps both
 * signs in two error variables, an inequality counts only the part it exceeds.
 * No am_updatevars() call is needed to make it current. */
TEST(error_reports_the_violation) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *pin = am_newconstraint(S, AM_REQUIRED); /* x == 0 */
    am_Constraint *sat = am_newconstraint(S, AM_MEDIUM);   /* x == 0 */
    am_Constraint *eq = am_newconstraint(S, AM_MEDIUM);    /* x == 10 */
    am_Constraint *ge = am_newconstraint(S, AM_WEAK);      /* x >= 5 */
    am_Constraint *le = am_newconstraint(S, AM_STRONG);    /* x <= -5 */
    am_Constraint *un = am_newconstraint(S, AM_MEDIUM);    /* not added */

    am_addterm(pin, vx, 1.f), am_setrelation(pin, AM_EQUAL);
    am_addconstant(pin, 0.f);
    asserteq(am_add(pin), AM_OK);
    am_addterm(sat, vx, 1.f), am_setrelation(sat, AM_EQUAL);
    am_addconstant(sat, 0.f);
    asserteq(am_add(sat), AM_OK);
    am_addterm(eq, vx, 1.f), am_setrelation(eq, AM_EQUAL);
    am_addconstant(eq, 10.f);
    asserteq(am_add(eq), AM_OK);
    am_addterm(ge, vx, 1.f), am_setrelation(ge, AM_GREATEREQUAL);
    am_addconstant(ge, 5.f);
    asserteq(am_add(ge), AM_OK);
    am_addterm(le, vx, 1.f), am_setrelation(le, AM_LESSEQUAL);
    am_addconstant(le, -5.f);
    asserteq(am_add(le), AM_OK);

    am_updatevars(S);
    assertnear(x, 0, 1e-4);
    assertnear(am_error(pin), 0, 1e-4); /* required: satisfied once added */
    assertnear(am_error(sat), 0, 1e-4);
    assertnear(am_error(eq), 10, 1e-4); /* |x - 10| in expression units */
    assertnear(am_error(ge), 5, 1e-4);  /* 5 - x, only the exceeded part */
    assertnear(am_error(le), 5, 1e-4);  /* x - (-5) */
    assertnear(am_error(un), 0, 0);     /* not in the solver */

    am_remove(eq);
    assertnear(am_error(eq), 0, 0); /* the marker is gone after removal */
    am_delsolver(S);
}

/* am_constant() reports the constant in the same form am_terms() uses: the
 * stored expression is "right hand side minus left hand side", so a constant
 * written after am_setrelation() keeps its sign and one written before it is
 * negated together with the terms. */
TEST(constant_matches_the_stored_expression) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *after = am_newconstraint(S, AM_MEDIUM);
    am_Constraint *before = am_newconstraint(S, AM_MEDIUM);
    am_Num         coef;

    am_addterm(after, vx, 2.f), am_setrelation(after, AM_EQUAL);
    am_addconstant(after, 3.f); /* 3 is the right hand side */
    am_addterm(before, vx, 2.f), am_addconstant(before, 3.f);
    am_setrelation(before, AM_EQUAL); /* negates both terms and constant */

    assertnear(am_constant(after), 3, 0);
    asserteq(am_terms(after, 0, &coef), vx);
    assertnear(coef, -2, 0);
    assertnear(am_constant(before), -3, 0);
    asserteq(am_terms(before, 0, &coef), vx);
    assertnear(coef, -2, 0);
    am_delsolver(S);
}

TEST(introspection_of_an_empty_solver_is_empty) {
    am_Solver     *S = am_test_new_solver();
    am_Constraint *c = am_newconstraint(S, AM_MEDIUM);
    am_Num         coef = 0;

    asserteq(am_vars(S, 0), 0);         /* no variables */
    asserteq(am_vars(S, 1), 0);         /* no such anchor either */
    asserteq(am_terms(c, 0, &coef), 0); /* no terms */
    asserteq(am_terms(c, 1, &coef), 0);
    assertok(am_constraints(S, NULL) == c); /* just the one we created */
    assertok(am_constraints(S, c) == NULL); /* and it is the last one */
    am_delsolver(S);
}

/* ---------------------------------------------------------------------- */
/* load: fresh solver, byte budget, malformed frames */

#include "stream.h"

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

/* load callbacks: one whole stream chunk, values in stream order */
typedef struct stream_loader {
    am_Loader      base;
    const char    *p;
    size_t         len;
    am_Constraint *cons;   /* set by load_cons, if the stream has one */
    am_Id          ids[8]; /* ids load_var saw, in stream order */
    am_Constraint *all[8]; /* constraints load_cons saw, in stream order */
    unsigned       ncons;
} stream_loader;

static am_Num  stream_value;
static am_Num *stream_load_var(am_Loader *L, const char *name, unsigned idx,
                               am_Id var) {
    stream_loader *sl = (stream_loader *)L;
    (void)name;
    if (idx < 8) sl->ids[idx] = var;
    stream_value = 0;
    return &stream_value;
}

static void stream_load_cons(am_Loader *L, const char *name, unsigned idx,
                             am_Constraint *cons) {
    stream_loader *sl = (stream_loader *)L;
    (void)name, (void)idx;
    if (sl->ncons < 8) sl->all[sl->ncons] = cons;
    sl->ncons += 1, sl->cons = cons;
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

static unsigned count_vars(const am_Solver *S) {
    am_Id    id = 0;
    unsigned n = 0;
    while ((id = am_vars(S, id)) != 0) ++n;
    return n;
}

static unsigned count_cons(const am_Solver *S) {
    am_Constraint *c = NULL;
    unsigned       n = 0;
    while ((c = am_constraints(S, c)) != NULL) ++n;
    return n;
}

static size_t dump_fixture(am_Solver *S, unsigned char *buf) {
    dump_buf db = AM_ZERO;

    memset(&db, 0, sizeof(db));
    db.base.var_name = dump_buf_varname;
    db.base.writer = dump_buf_writer;
    db.p = buf;
    asserteq(am_dump(S, &db.base), AM_OK);
    return db.len;
}

static size_t dump_one_row(unsigned char *buf) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *c = am_newconstraint(S, AM_MEDIUM);
    size_t         len;

    am_addterm(c, vx, 1.f);
    am_setrelation(c, AM_EQUAL);
    am_addconstant(c, 10.f);
    asserteq(am_add(c), AM_OK);
    am_updatevars(S);
    len = dump_fixture(S, buf);
    am_delsolver(S);
    return len;
}

/* The loader restores marker/other with their symbol types, so am_error()
 * still reports the violation of a loaded soft constraint. */
static size_t dump_violated_soft(unsigned char *buf) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *pin = am_newconstraint(S, AM_REQUIRED); /* x == 0 */
    am_Constraint *soft = am_newconstraint(S, AM_MEDIUM);  /* x == 10 */
    size_t         len;

    am_addterm(pin, vx, 1.f), am_setrelation(pin, AM_EQUAL);
    am_addconstant(pin, 0.f);
    asserteq(am_add(pin), AM_OK);
    am_addterm(soft, vx, 1.f), am_setrelation(soft, AM_EQUAL);
    am_addconstant(soft, 10.f);
    asserteq(am_add(soft), AM_OK);
    am_updatevars(S);
    len = dump_fixture(S, buf);
    am_delsolver(S);
    return len;
}

static size_t dump_two_soft(unsigned char *buf) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0, y = 0;
    am_Id          vx = am_newvariable(S, &x), vy = am_newvariable(S, &y);
    am_Constraint *c = am_newconstraint(S, AM_MEDIUM);
    size_t         len;

    am_addterm(c, vx, 1.f);
    am_setrelation(c, AM_EQUAL);
    am_addconstant(c, 10.f);
    asserteq(am_add(c), AM_OK);
    c = am_newconstraint(S, AM_MEDIUM);
    am_addterm(c, vy, 1.f);
    am_setrelation(c, AM_EQUAL);
    am_addconstant(c, 20.f);
    asserteq(am_add(c), AM_OK);
    len = dump_fixture(S, buf);
    am_delsolver(S);
    return len;
}

static size_t dump_unconfigured(unsigned char *buf, int relation) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *c = am_newconstraint(S, AM_MEDIUM);
    size_t         len;

    am_addterm(c, vx, 1.f);
    if (relation) am_setrelation(c, AM_EQUAL);
    am_addconstant(c, 10.f);
    len = dump_fixture(S, buf);
    am_delsolver(S);
    return len;
}

static size_t dump_edited(unsigned char *buf) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0, y = 0;
    am_Id          vx = am_newvariable(S, &x), vy = am_newvariable(S, &y);
    am_Constraint *c = am_newconstraint(S, AM_REQUIRED);
    size_t         len;

    am_addterm(c, vx, 1.f), am_addterm(c, vy, 1.f);
    am_setrelation(c, AM_EQUAL), am_addconstant(c, 10.f);
    asserteq(am_add(c), AM_OK);
    asserteq(am_addedit(S, vx, AM_STRONG), AM_OK);
    am_suggest(S, vx, 3);
    asserteq(am_addedit(S, vy, AM_STRONG), AM_OK);
    am_suggest(S, vy, 7);
    am_updatevars(S);
    len = dump_fixture(S, buf);
    am_delsolver(S);
    return len;
}

/* The same rule holds for the suggests rebuilt by am_load(). */
TEST(custom_allocator_owns_loaded_suggests) {
    static unsigned char stream[4096];
    size_t               len;
    stream_loader        sl;
    am_Solver           *S;

    len = dump_edited(stream);
    stream_init(&sl, stream, len);
    memset(type_allocs, 0, sizeof(type_allocs));
    S = am_load(count_test_allocf, NULL, &sl.base, 0);
    assertok(S != NULL);
    assertok(type_allocs[am_TSuggest] > 0);
    asserteq(type_allocs[am_TNone], 0);
    am_delsolver(S);
}

static size_t dump_constant(am_Num v, unsigned char *buf) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *c = am_newconstraint(S, AM_REQUIRED);
    size_t         len;

    am_addterm(c, vx, 1.f), am_addconstant(c, -v);
    am_setrelation(c, AM_EQUAL);
    asserteq(am_add(c), AM_OK);
    am_updatevars(S);
    asserteq(x, v);
    len = dump_fixture(S, buf);
    am_delsolver(S);
    return len;
}

/* The cons array header, then the fixed fields of the first record. */
static size_t stream_cons_at(const unsigned char *buf) {
    stream_cur c;
    unsigned   i, n;
    c.p = buf;
    assertok(stream_count(&c) == 6);
    assertok(stream_count(&c) == 3);
    stream_skipstr(&c), stream_u32(&c), stream_u32(&c);
    stream_u32(&c);
    n = stream_count(&c);
    for (i = 0; i < n; ++i) {
        assertok(stream_count(&c) == 2);
        stream_skipnum(&c), stream_skipstr(&c); /* [value, name] */
    }
    n = stream_count(&c); /* the cons array header */
    assertok(n > 0);
    return (size_t)(c.p - buf);
}

static size_t stream_cons_constant_at(const unsigned char *buf) {
    stream_cur c;
    c.p = buf + stream_cons_at(buf);
    assertok(stream_count(&c) == 5);
    stream_skipnum(&c);             /* strength */
    stream_u32(&c), stream_u32(&c); /* relation, in_solver */
    return (size_t)(c.p - buf);
}

/* offset of an edit record's var_seq field */
static size_t stream_edit_at(const unsigned char *buf, unsigned which) {
    stream_spot spot;
    stream_cur  c;
    unsigned    i, n;
    stream_scan(buf, &spot);
    c.p = buf + spot.edits_at;
    n = stream_count(&c);
    assertok(which < n);
    for (i = 0; i < which; ++i) {
        assertok(stream_count(&c) == 3);
        stream_u32(&c), stream_skipnum(&c), stream_skipnum(&c);
    }
    assertok(stream_count(&c) == 3);
    return (size_t)(c.p - buf);
}

static size_t stream_edit_strength_at(const unsigned char *buf,
                                      unsigned             which) {
    stream_cur c;
    c.p = buf + stream_edit_at(buf, which);
    stream_u32(&c); /* var_seq */
    return (size_t)(c.p - buf);
}

static size_t stream_meta_name_at(const unsigned char *buf) {
    stream_spot spot;
    stream_scan(buf, &spot);
    return spot.version_at - 6; /* "amoeba" is a 6-byte fixstr */
}

/* Rewrite the total field as a uint32, whatever its original encoding. */
static size_t dump_patch_total(unsigned char *buf, size_t len, unsigned value) {
    unsigned char out[2048];
    stream_spot   spot;
    size_t        at, oldlen, n;

    assertok(len < sizeof(out));
    stream_scan(buf, &spot);
    at = spot.total_at, oldlen = spot.total_len;
    memcpy(out, buf, at);
    out[at] = 0xCE, stream_putu32(out + at + 1, value);
    n = at + 5;
    memcpy(out + n, buf + at + oldlen, len - at - oldlen);
    n += len - at - oldlen;
    memcpy(buf, out, n);
    return n;
}

/* More than 0xFFFF symbols make the writer use the 32-bit count encoding
 * (0xDD); the total then needs the 32-bit value encoding (0xCE). */
#define BIG_VARS 65536
static am_Num        big_values[BIG_VARS];
static unsigned char big_stream[1024 * 1024];

static am_Num *big_load_var(am_Loader *L, const char *name, unsigned idx,
                            am_Id var) {
    (void)L, (void)name, (void)var;
    return &big_values[idx];
}

TEST(load_accepts_real_dump) {
    static unsigned char stream[2048];
    size_t               len = dump_one_row(stream);
    stream_loader        sl;
    am_Solver           *S;

    stream_init(&sl, stream, len);
    assertok((S = load_stream(&sl, 0)) != NULL);
    asserteq(am_refcount(S, sl.ids[0]), 2); /* variable + one term */
    asserteq(count_vars(S), 1);
    asserteq(count_cons(S), 1);
    am_delsolver(S);
}

TEST(error_survives_dump_load) {
    static unsigned char stream[2048];
    size_t               len = dump_violated_soft(stream);
    stream_loader        sl;
    am_Solver           *L;
    am_Constraint       *c;
    int                  soft = 0;

    stream_init(&sl, stream, len);
    assertok((L = load_stream(&sl, 0)) != NULL);
    for (c = NULL; (c = am_constraints(L, c)) != NULL;) {
        if (am_strength(c) < AM_REQUIRED) {
            assertnear(am_error(c), 10, 1e-4); /* x == 10 against pinned 0 */
            ++soft;
        }
    }
    asserteq(soft, 1);
    am_delsolver(L);
}

/* Sixteen variables make the writer use the 16-bit array count (0xDC); a
 * one-chunk reader then has the whole two-byte count available at once. */
TEST(load_reads_u16_in_one_chunk) {
    static unsigned char stream[2048];
    am_Num               values[16];
    am_Solver           *S, *S2;
    am_Id                vars[16];
    am_Constraint       *c;
    dump_buf             db = AM_ZERO;
    stream_loader        sl;
    unsigned             i;
    size_t               len;

    memset(values, 0, sizeof(values));
    S = am_test_new_solver();
    for (i = 0; i < 16; ++i) vars[i] = am_newvariable(S, &values[i]);
    c = am_newconstraint(S, AM_REQUIRED);
    for (i = 0; i < 16; ++i) asserteq(am_addterm(c, vars[i], 1), AM_OK);
    asserteq(am_addconstant(c, -16), AM_OK);
    asserteq(am_setrelation(c, AM_EQUAL), AM_OK);
    asserteq(am_add(c), AM_OK);
    am_updatevars(S);

    memset(&db, 0, sizeof(db));
    db.base.var_name = dump_buf_varname;
    db.base.writer = dump_buf_writer;
    db.p = stream;
    asserteq(am_dump(S, &db.base), AM_OK);
    len = db.len;

    stream_init(&sl, stream, len);
    assertok((S2 = load_stream(&sl, 0)) != NULL);
    asserteq(count_vars(S2), 16);
    am_delsolver(S2);
    am_delsolver(S);
}

/* A dump with 65536 variables crosses both the 0xDD count and the 0xCE
 * value encodings; load it back with a reader that returns one big chunk. */
TEST(dump_load_large_counts) {
    am_Solver    *S, *S2;
    dump_buf      db = AM_ZERO;
    stream_loader sl;
    unsigned      i;
    size_t        len;

    S = am_test_new_solver();
    for (i = 0; i < BIG_VARS; ++i)
        assertok(am_newvariable(S, &big_values[i]) != 0);

    memset(&db, 0, sizeof(db));
    db.base.var_name = dump_buf_varname;
    db.base.writer = dump_buf_writer;
    db.p = big_stream;
    asserteq(am_dump(S, &db.base), AM_OK);
    len = db.len;
    assertok(len > 0 && len < sizeof(big_stream));

    stream_init(&sl, big_stream, len);
    sl.base.load_var = big_load_var;
    assertok((S2 = load_stream(&sl, 0)) != NULL);
    asserteq(count_vars(S2), BIG_VARS);
    asserteq(count_cons(S2), 0);
    am_delsolver(S2);
    am_delsolver(S);
}

TEST(load_budget_rejects_tiny) {
    static unsigned char stream[2048];
    size_t               len = dump_one_row(stream);
    stream_loader        sl;

    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 1) == NULL);
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
    budget = peak; /* the solver object itself is outside the budget */

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

TEST(load_accepts_unadded_constraint) {
    static unsigned char stream[2048];
    size_t               len = dump_unconfigured(stream, 1);
    stream_loader        sl;
    am_Solver           *S;

    stream_init(&sl, stream, len);
    assertok((S = load_stream(&sl, 0)) != NULL);
    assertok(sl.cons != NULL);
    /* loaded unadded: walking and adding it both work */
    asserteq(am_hasconstraint(sl.cons), 0);
    assertnear(am_constant(sl.cons), 10, 0);
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
    asserteq(am_relation(sl.cons), 0);
    am_delsolver(S);
}

TEST(load_rejects_oversized_vars) {
    static unsigned char stream[4096];
    size_t               len = dump_two_soft(stream);
    stream_spot          spot;
    stream_loader        sl;

    stream_scan(stream, &spot);
    asserteq(spot.total_len, 1); /* total = 6 */
    stream[spot.total_at] = 1;   /* vars count is 2 */
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

TEST(load_rejects_wrong_total) {
    static unsigned char stream[4096];
    size_t               len = dump_two_soft(stream); /* total = 6 */
    stream_loader        sl;

    len = dump_patch_total(stream, len, 7); /* one id too many */
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);

    len = dump_patch_total(stream, len, 5); /* one id too few */
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_rejects_old_constraint_record) {
    static unsigned char stream[4096];
    size_t               len = dump_two_soft(stream), at;
    stream_loader        sl;

    at = stream_cons_at(stream);
    assertok(at < len);
    asserteq(stream[at], 0x90 + 5);
    stream[at] = 0x90 + 4; /* a short record */
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);

    len = dump_two_soft(stream);
    at = stream_cons_at(stream);
    stream[at] = 0x90 + 6; /* the old six-field record carried a blob */
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

/* A cons record is [strength, relation, in_solver, constant, name].  The API
 * can only produce relation 1..3 and in_solver 0/1, so the loader's range
 * checks are reachable through a tampered stream alone. */
static size_t stream_cons_relation_at(const unsigned char *buf) {
    stream_cur c;
    c.p = buf + stream_cons_at(buf);
    assertok(stream_count(&c) == 5);
    stream_skipnum(&c); /* strength */
    return (size_t)(c.p - buf);
}

static size_t stream_cons_insolver_at(const unsigned char *buf) {
    stream_cur c;
    c.p = buf + stream_cons_relation_at(buf);
    stream_u32(&c); /* relation */
    return (size_t)(c.p - buf);
}

TEST(load_rejects_bad_constraint_relation) {
    static unsigned char stream[4096];
    size_t               len = dump_one_row(stream), at;
    stream_loader        sl;

    at = stream_cons_relation_at(stream);
    assertok(at < len);
    asserteq(stream[at], AM_EQUAL);
    stream[at] = 6; /* past AM_GREATEREQUAL; six would truncate to AM_EQUAL */
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_rejects_bad_constraint_insolver) {
    static unsigned char stream[4096];
    size_t               len = dump_one_row(stream), at;
    stream_loader        sl;

    at = stream_cons_insolver_at(stream);
    assertok(at < len);
    asserteq(stream[at], 1);
    stream[at] = 2;
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

/* The scratch bin ends the stream: [bin header][lens][terms][cache].  The
 * loader derives every section size from the lens, so a tampered bin can move
 * the record/term/cache boundaries in ways am_dump() never writes. */

static size_t stream_scratch_at(const unsigned char *buf) {
    stream_spot spot;
    stream_cur  c;
    stream_scan(buf, &spot);
    c.p = buf + spot.edits_at;
    stream_skipedits(&c);
    return (size_t)(c.p - buf);
}

static size_t stream_scratch_len(const unsigned char *buf, size_t at,
                                 size_t *head) {
    unsigned ty = buf[at], n;
    if (ty == 0xC4) return *head = 2, (size_t)buf[at + 1];
    if (ty == 0xC5)
        return *head = 3, (size_t)(((unsigned)buf[at + 1] << 8) | buf[at + 2]);
    assertok(ty == 0xC6);
    n = ((unsigned)buf[at + 1] << 24) | ((unsigned)buf[at + 2] << 16)
      | ((unsigned)buf[at + 3] << 8) | buf[at + 4];
    return *head = 5, (size_t)n;
}

static void stream_scratch_setlen(unsigned char *buf, size_t at, size_t head,
                                  size_t n) {
    if (head == 2)
        buf[at + 1] = (unsigned char)n;
    else if (head == 3)
        buf[at + 1] = (unsigned char)(n >> 8), buf[at + 2] = (unsigned char)n;
    else
        buf[at + 1] = (unsigned char)(n >> 24),
                 buf[at + 2] = (unsigned char)(n >> 16),
                 buf[at + 3] = (unsigned char)(n >> 8),
                 buf[at + 4] = (unsigned char)n;
}

static unsigned stream_bin_u32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16)
         | ((unsigned)p[3] << 24);
}

static void stream_bin_putu32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}

/* Every byte after the header is accounted for by the lens.  Appending 5
 * bytes and growing the bin length keeps the records intact, yet the loader's
 * exact-size check must reject the gap. */
TEST(load_rejects_a_gap_after_the_last_record) {
    static unsigned char stream[4096];
    size_t               len = dump_one_row(stream), at, head, blen;
    stream_loader        sl;

    at = stream_scratch_at(stream);
    blen = stream_scratch_len(stream, at, &head);
    assertok((size_t)(at + head + blen) == len); /* the bin ends the stream */
    assertok(head == 2 || head == 3);            /* the length still fits */
    stream_scratch_setlen(stream, at, head, blen + 5);
    memset(stream + len, 0, 5), len += 5;
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

/* The term area is exactly what the record lengths add up to.  Growing the
 * objective's length by one keeps every other length legal but overruns the
 * terms, so the cache entries no longer end at the bin's end. */
TEST(load_rejects_a_term_area_that_is_too_long) {
    static unsigned char stream[4096];
    size_t               len = dump_edited(stream), at, head, blen;
    stream_loader        sl;

    at = stream_scratch_at(stream);
    blen = stream_scratch_len(stream, at, &head);
    assertok((size_t)(at + head + blen) == len);
    assertok(head == 2 || head == 3);
    assertok(stream_bin_u32(stream + at + head) >= 2); /* the objective + */
    stream_bin_putu32(stream + at + head + 4,
                      stream_bin_u32(stream + at + head + 4) + 1);
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(dump_writes_numbers_as_floats) {
    static unsigned char stream[2048];
    static const am_Num  values[] = {0, 127, -32769, (am_Num)(1.0 / 3.0)};
    unsigned             i;

    for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        size_t        len = dump_constant(values[i], stream);
        size_t        at = stream_cons_constant_at(stream);
        stream_loader sl;
        am_Solver    *S;

        asserteq(stream[at], sizeof(am_Num) == sizeof(float) ? 0xCA : 0xCB);
        stream_init(&sl, stream, len);
        assertok((S = load_stream(&sl, 0)) != NULL);
        assertok(memcmp(&stream_value, &values[i], sizeof(am_Num)) == 0);
        am_delsolver(S);
    }
}

TEST(load_rejects_bad_meta_version) {
    static unsigned char stream[4096];
    size_t               len = dump_one_row(stream);
    stream_spot          spot;
    stream_loader        sl;

    stream_scan(stream, &spot);
    asserteq(stream[spot.version_at], 5);
    stream[spot.version_at] = 4;
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);

    len = dump_one_row(stream);
    stream_scan(stream, &spot);
    stream[spot.version_at] = 6;
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_rejects_v2_stream) {
    /* a v2 header is a 7-element array with a 2-element meta */
    static const unsigned char v2[] = {0x97, 0x92, 0xA6, 'a', 'm',
                                       'o',  'e',  'b',  'a', 0x02};
    stream_loader              sl;

    stream_init(&sl, v2, sizeof(v2));
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_rejects_foreign_meta) {
    static unsigned char stream[4096];
    size_t               len = dump_one_row(stream);
    size_t               at = stream_meta_name_at(stream);
    stream_loader        sl;

    asserteq(stream[at], 'a');
    stream[at] = 'b'; /* "bmoeba": not our stream */
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

/* Each edit record is [var_seq, strength, value]; the loader rejects a seq
 * that is not a loaded variable, a duplicate edit and a strength outside
 * (0, AM_STRONG]. */
TEST(load_rejects_edit_on_a_constraint) {
    static unsigned char stream[4096];
    size_t               len = dump_edited(stream);
    size_t               at = stream_edit_at(stream, 0);
    stream_loader        sl;

    assertok(at < len);
    assertok(stream[at] == 1 || stream[at] == 2); /* a variable seq */
    stream[at] = 3; /* two variables: seq 3 is the constraint */
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_rejects_edit_on_a_ghost_variable) {
    static unsigned char stream[4096];
    size_t               len = dump_edited(stream);
    size_t               at = stream_edit_at(stream, 0);
    stream_loader        sl;

    stream[at] = 0; /* the seq is 1-based */
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_rejects_duplicate_edit) {
    static unsigned char stream[4096];
    size_t               len = dump_edited(stream);
    size_t               a = stream_edit_at(stream, 0);
    size_t               b = stream_edit_at(stream, 1);
    stream_loader        sl;

    assertok(b < len);
    stream[b] = stream[a]; /* two edits on the same variable */
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_rejects_bad_edit_strength) {
    static unsigned char stream[4096];
    stream_loader        sl;
    size_t               len, at;

    len = dump_edited(stream);
    at = stream_edit_strength_at(stream, 0); /* strength is a float */
    asserteq(stream[at], sizeof(am_Num) == sizeof(float) ? 0xCA : 0xCB);
    memset(stream + at + 1, 0, sizeof(am_Num)); /* 0 is not a soft strength */
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);

    len = dump_edited(stream);
    at = stream_edit_strength_at(stream, 0);
    memset(stream + at + 1, 0xFF, sizeof(am_Num)); /* NaN fails the range */
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
}

TEST(load_rejects_bad_edit_count) {
    static unsigned char stream[4096];
    size_t               len = dump_edited(stream), at;
    stream_spot          spot;
    stream_loader        sl;

    stream_scan(stream, &spot);
    at = spot.edits_at;
    asserteq(stream[at], 0x90 + 2);
    stream[at] = 0x90 + 15; /* more edits than total allows */
    stream_init(&sl, stream, len);
    assertok(load_stream(&sl, 0) == NULL);
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

/* A required equality that conflicts with a required inequality must fail
 * cleanly even when a soft constraint is present: the failed add leaves the
 * tableau as it was, so the solver stays usable and the old solution is
 * intact.
 *
 * Regression: the failed add left a stale entry on the infeasible-row list.
 * The next primal optimize asserted that the list is empty (debug), and in
 * release the entry outlived every later operation. */
TEST(add_unsat_with_soft_keeps_solver_usable) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *soft, *ge, *eq;

    soft = am_newconstraint(S, AM_STRONG); /* x == 11, soft */
    am_addterm(soft, vx, 1.f);
    am_addconstant(soft, -11.f);
    am_setrelation(soft, AM_EQUAL);
    asserteq(am_add(soft), AM_OK);

    ge = am_newconstraint(S, AM_REQUIRED); /* x >= 14.25 */
    am_addterm(ge, vx, 1.f);
    am_addconstant(ge, -14.25f);
    am_setrelation(ge, AM_GREATEREQUAL);
    asserteq(am_add(ge), AM_OK);
    am_updatevars(S);
    assertnear(x, 14.25, 1e-4);

    eq = am_newconstraint(S, AM_REQUIRED); /* x == -58 conflicts */
    am_addterm(eq, vx, 1.f);
    am_addconstant(eq, 58.f);
    am_setrelation(eq, AM_EQUAL);
    asserteq(am_add(eq), AM_UNBOUND);
    assertok(!am_hasconstraint(eq));
    am_updatevars(S);
    assertnear(x, 14.25, 1e-4); /* the old solution is untouched */

    eq = am_newconstraint(S, AM_REQUIRED); /* the solver still works */
    am_addterm(eq, vx, 1.f);
    am_addconstant(eq, -30.f);
    am_setrelation(eq, AM_EQUAL);
    asserteq(am_add(eq), AM_OK);
    am_updatevars(S);
    assertnear(x, 30, 1e-4);

    am_delsolver(S);
}

/* The same failed add followed by am_resetsolver(): the reset frees every row,
 * so it must drop the infeasible-row list with them.
 *
 * Regression: the reset only asserted that the list was empty.  In release the
 * stale id survived it and the next suggest() ran the dual simplex over a row
 * that had been freed and reused (hang or crash). */
TEST(reset_after_unsat_add_leaves_a_clean_solver) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *soft, *ge, *eq;

    soft = am_newconstraint(S, AM_STRONG); /* x == 11, soft */
    am_addterm(soft, vx, 1.f);
    am_addconstant(soft, -11.f);
    am_setrelation(soft, AM_EQUAL);
    asserteq(am_add(soft), AM_OK);

    ge = am_newconstraint(S, AM_REQUIRED); /* x >= 14.25 */
    am_addterm(ge, vx, 1.f);
    am_addconstant(ge, -14.25f);
    am_setrelation(ge, AM_GREATEREQUAL);
    asserteq(am_add(ge), AM_OK);

    eq = am_newconstraint(S, AM_REQUIRED); /* x == -58 conflicts */
    am_addterm(eq, vx, 1.f);
    am_addconstant(eq, 58.f);
    am_setrelation(eq, AM_EQUAL);
    asserteq(am_add(eq), AM_UNBOUND);

    am_resetsolver(S);

    soft = am_newconstraint(S, AM_STRONG); /* the solver still works */
    am_addterm(soft, vx, 1.f);
    am_addconstant(soft, -10.f);
    am_setrelation(soft, AM_EQUAL);
    asserteq(am_add(soft), AM_OK);
    asserteq(am_addedit(S, vx, AM_WEAK), AM_OK);
    am_suggest(S, vx, 100);
    am_updatevars(S);
    assertnear(x, 10, 1e-4); /* the strong x == 10 still wins */

    am_delsolver(S);
}

/* Removing a soft edit while a strength-scaled row is being pivoted through
 * the objective must not mistake float rounding for an unbounded direction.
 *
 * Regression: the strong row's 1e6 term cancels in the objective and leaves a
 * tiny negative cost on a column that no row can block.  amA_optimize() then
 * found no leaving row: debug tripped assert(leave.u.id != 0), release spun. */
TEST(remove_edit_after_strong_row_keeps_solver_usable) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *strong, *req;

    am_suggest(S, vx, -9); /* the edit holds x at -9 */

    strong = am_newconstraint(S, AM_STRONG); /* 3*x <= 6 */
    am_addterm(strong, vx, 2);
    am_addterm(strong, vx, 1);
    am_addconstant(strong, -6);
    am_setrelation(strong, AM_LESSEQUAL);
    asserteq(am_add(strong), AM_OK);
    am_updatevars(S);
    assertnear(x, -9, 1e-4); /* satisfied by the edit's value */

    req = am_newconstraint(S, AM_REQUIRED); /* x >= 9 overrides the edit */
    am_addterm(req, vx, -1);
    am_addconstant(req, 9);
    am_setrelation(req, AM_LESSEQUAL);
    asserteq(am_add(req), AM_OK);
    am_updatevars(S);
    assertnear(x, 9, 1e-4);

    /* the removal pivots the strong row's 1e6 term out of the objective */
    am_remove(req);
    am_clearedits(S);
    am_updatevars(S);
    assertnear(x, 2, 1e-4); /* only 3*x <= 6 is left */

    am_delsolver(S);
}

/* A row that leaves the infeasible work list must be queueable again.
 *
 * Regression: amA_flushinfeasible() emptied S->infeasible_rows but left each
 * visited row's infeasible_next pointing at its old neighbour.  That link is
 * also the "already queued" mark (amA_infeasible), so a flushed row could
 * never be queued again: the next negative constant was dropped silently and
 * the dual simplex skipped the repair.  Here the edit's error row is flushed
 * by the unsatisfiable add, then turns negative on the last suggest. */
TEST(flushed_row_can_be_queued_again) {
    am_Solver     *S = am_test_new_solver();
    am_Num         v[6] = {0, 0, 0, 0, 0, 0};
    am_Id          x[6];
    am_Constraint *c;
    int            i;

    for (i = 0; i < 6; ++i) x[i] = am_newvariable(S, &v[i]);
    am_suggest(S, x[4], -38);
    am_resetsolver(S);
    am_suggest(S, x[2], -39);

    c = am_newconstraint(S, AM_WEAK); /* -x0 + 17 <= 0 */
    am_addterm(c, x[0], -1);
    am_addconstant(c, 17);
    am_setrelation(c, AM_LESSEQUAL);
    asserteq(am_add(c), AM_OK);

    c = am_newconstraint(S, AM_STRONG); /* 2*x3 - 12 >= 0 */
    am_addterm(c, x[3], 2);
    am_addconstant(c, -12);
    am_setrelation(c, AM_GREATEREQUAL);
    asserteq(am_add(c), AM_OK);

    c = am_newconstraint(S, AM_MEDIUM); /* -2*x1 + 17 >= 0 */
    am_addterm(c, x[1], -2);
    am_addconstant(c, 17);
    am_setrelation(c, AM_GREATEREQUAL);
    asserteq(am_add(c), AM_OK);

    c = am_newconstraint(S, AM_REQUIRED); /* x3 - 19 >= 0 */
    am_addterm(c, x[3], 1);
    am_addconstant(c, -19);
    am_setrelation(c, AM_GREATEREQUAL);
    asserteq(am_add(c), AM_OK);

    c = am_newconstraint(S, AM_REQUIRED); /* -x1 - 5 >= 0 */
    am_addterm(c, x[1], -1);
    am_addconstant(c, -5);
    am_setrelation(c, AM_GREATEREQUAL);
    asserteq(am_add(c), AM_OK);

    c = am_newconstraint(S, AM_STRONG); /* x5 + 2 <= 0 */
    am_addterm(c, x[5], 1);
    am_addconstant(c, 2);
    am_setrelation(c, AM_LESSEQUAL);
    asserteq(am_add(c), AM_OK);

    asserteq(am_addedit(S, x[3], AM_MEDIUM), AM_OK);

    c = am_newconstraint(S, AM_REQUIRED); /* x3 == 4: unsatisfiable */
    am_addterm(c, x[3], 1);
    am_addconstant(c, -4);
    am_setrelation(c, AM_EQUAL);
    asserteq(am_add(c), AM_UNBOUND);

    am_suggest(S, x[3], 30);
    am_updatevars(S);
    assertnear(v[3], 30, 1e-4); /* the edit's error row was repaired */

    am_delsolver(S);
}

/* read a row out of S->rows without going through an internal helper */
static am_Row *row_of(am_Solver *S, am_Id id) {
    unsigned i;
    for (i = 0; i < S->rows.size; ++i)
        if (S->rows.keys[i].sym.u.id == id)
            return (am_Row *)((char *)S->rows.vals + i * S->rows.value_size);
    return NULL;
}

/* A stale entry on the infeasible work list must be unmarked by the flush
 * that amA_optimize() runs before it starts.  The state is built directly:
 * a non-negative row is queued, then add() starts an optimize. */
TEST(flush_drops_a_stale_queue_entry) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0, y = 0;
    am_Id          vx, vy;
    am_Constraint *c;
    am_Symbol      queued, sentinel;
    am_Row        *r;

    vx = am_newvariable(S, &x), vy = am_newvariable(S, &y);
    c = am_newconstraint(S, AM_REQUIRED);
    asserteq(am_addterm(c, vx, 1), AM_OK);
    asserteq(am_setrelation(c, AM_EQUAL), AM_OK);
    asserteq(am_addconstant(c, 0), AM_OK);
    asserteq(am_add(c), AM_OK);
    r = row_of(S, vx);
    assertok(r != NULL);
    assertnear(r->constant, 0, 1e-6);

    queued.bits = vx; /* an external variable's row */
    sentinel.bits = 0, sentinel.u.type = AM_DUMMY;
    S->infeasible_rows = queued;
    r->infeasible_next = sentinel;

    c = am_newconstraint(S, AM_WEAK);
    asserteq(am_addterm(c, vy, 1), AM_OK);
    asserteq(am_setrelation(c, AM_EQUAL), AM_OK);
    asserteq(am_add(c), AM_OK); /* amA_optimize() flushes the stale entry */
    asserteq(S->infeasible_rows.u.id, 0);
    r = row_of(S, vx);
    if (r) asserteq(r->infeasible_next.bits, 0);
    am_delsolver(S);
}

/* A failed add must not take a row that sits on the infeasible work list.
 *
 * Regression: the artificial stage of a failed add installed its row as a
 * tableau row, and the amA_substitute() on the way queued the edit's negative
 * error row; the am_remove() that followed took that very row as its leaving
 * row and dropped it, so the queue kept an id without a row.  The next
 * amA_flushinfeasible() aborted on it (release cut the list off silently).
 * The sequence is the reduced form of fuzz seed 1112. */
TEST(failed_add_does_not_strand_a_queued_row) {
    am_Solver     *S = am_test_new_solver();
    am_Num         v[6] = {0, 0, 0, 0, 0, 0};
    am_Id          x[6];
    am_Constraint *c;
    int            i;

    for (i = 0; i < 6; ++i) x[i] = am_newvariable(S, &v[i]);

    c = am_newconstraint(S, AM_REQUIRED); /* -x4 + x0 + 5 <= 0 */
    am_addterm(c, x[4], -1);
    am_addterm(c, x[0], 1);
    am_addconstant(c, 5);
    am_setrelation(c, AM_LESSEQUAL);
    asserteq(am_add(c), AM_OK);

    am_suggest(S, x[0], -5);
    asserteq(am_addedit(S, x[4], AM_WEAK), AM_OK);

    c = am_newconstraint(S, AM_REQUIRED); /* x0 - x4 - 3 == 0: conflicts */
    am_addterm(c, x[0], 1);
    am_addterm(c, x[4], -1);
    am_addconstant(c, -3);
    am_setrelation(c, AM_EQUAL);
    asserteq(am_add(c), AM_UNBOUND);

    am_updatevars(S);
    assertnear(v[0] - v[4], -5, 1e-3); /* the old solution is intact */

    c = am_newconstraint(S, AM_REQUIRED); /* the solver still works */
    am_addterm(c, x[1], 1);
    am_addconstant(c, -7);
    am_setrelation(c, AM_EQUAL);
    asserteq(am_add(c), AM_OK);
    am_updatevars(S);
    assertnear(v[1], 7, 1e-4);

    am_delsolver(S);
}

/* The zero-strength edit of a pinned variable loses to the required value
 * instead of queueing a required dummy row (assert(enter.u.id != 0)). */
TEST(addedit_zero_keeps_a_required_pin) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *req = am_newconstraint(S, AM_REQUIRED);

    am_addterm(req, vx, 1.f), am_setrelation(req, AM_EQUAL);
    am_addconstant(req, 10.f);
    asserteq(am_add(req), AM_OK);
    asserteq(am_addedit(S, vx, 0), AM_OK);
    am_suggest(S, vx, 50);
    am_updatevars(S);
    assertnear(x, 10, 1e-4); /* the required equality still decides */
    am_delsolver(S);
}

/* A zero-strength edit must not move a variable out of a required equality
 * shared with another variable.
 *
 * Regression: the REQUIRED edit's `other` was EXTERNAL, so amA_editdelta()
 * took the "other row" fast path and bumped x's own basic row: x == y broke
 * silently (x = 50, y = 0). */
TEST(addedit_zero_keeps_another_required_equality) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0, y = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Id          vy = am_newvariable(S, &y);
    am_Constraint *req = am_newconstraint(S, AM_REQUIRED);

    am_addterm(req, vx, 1.f), am_addterm(req, vy, -1.f);
    am_setrelation(req, AM_EQUAL);
    asserteq(am_add(req), AM_OK);
    asserteq(am_addedit(S, vx, 0), AM_OK);
    am_suggest(S, vx, 50);
    am_updatevars(S);
    assertnear(x, 50, 1e-4);
    assertnear(y, 50, 1e-4); /* x == y still holds */
    am_delsolver(S);
}

TEST(cached_suggest_is_dropped_after_another_edit_pivots) {
    am_Solver     *S = am_test_new_solver();
    am_Num         v0 = 0, v1 = 0;
    am_Id          x0, x1;
    am_Constraint *c;

    assertok(S != NULL);
    am_autoupdate(S, 1);
    x0 = am_newvariable(S, &v0);
    x1 = am_newvariable(S, &v1);
    am_suggest(S, x0, -6);
    c = am_newconstraint(S, AM_STRONG);
    am_addterm(c, x1, -2);
    am_addterm(c, x0, 1);
    am_setrelation(c, AM_LESSEQUAL);
    assertok(am_add(c) == AM_OK);
    am_addedit(S, x1, AM_MEDIUM);
    am_suggest(S, x0, -1);
    am_suggest(S, x1, 48);
    am_suggest(S, x0, -15);
    assertok(v0 == -15.f && v1 == 48.f);
    am_delsolver(S);
}

/* A dirtyset that cannot grow must not stay marked fresh with only part of the
 * rows in it: the rows already took the delta, so the next am_suggest() would
 * replay it over the partial set and drop the update.  am_suggest() is void, so
 * the suggestion itself has to land regardless; only the cache is lost. */
static int fail_vec_alloc;

static void *vec_test_allocf(void **pud, void *p, size_t n, size_t o,
                             am_Type t) {
    if (n != 0 && fail_vec_alloc && t == am_TVec) return NULL;
    return am_test_allocf(pud, p, n, o, t);
}

TEST(suggest_survives_a_failed_dirtyset_grow) {
    am_Solver     *S = am_newsolver(vec_test_allocf, NULL);
    am_Num         x = 0, y = 0;
    am_Id          vx, vy;
    am_Constraint *sum, *floor;

    assertok(S != NULL);
    vx = am_newvariable(S, &x), vy = am_newvariable(S, &y);
    sum = am_newconstraint(S, AM_REQUIRED);
    am_addterm(sum, vx, 1.f), am_addterm(sum, vy, 1.f);
    am_setrelation(sum, AM_EQUAL), am_addconstant(sum, 10.f);
    asserteq(am_add(sum), AM_OK);
    floor = am_newconstraint(S, AM_REQUIRED);
    am_addterm(floor, vy, 1.f), am_setrelation(floor, AM_GREATEREQUAL);
    am_addconstant(floor, 0.f);
    asserteq(am_add(floor), AM_OK);
    asserteq(am_addedit(S, vx, AM_STRONG), AM_OK);

    fail_vec_alloc = 1; /* the dirtyset cannot grow from here on */
    am_suggest(S, vx, 5), am_updatevars(S);
    assertnear(x, 5, 1e-4);
    assertnear(y, 5, 1e-4);

    /* no usable cache: this must rescan every row, not replay a partial set */
    am_suggest(S, vx, 3), am_updatevars(S);
    assertnear(x, 3, 1e-4); /* the suggestion is honoured ... */
    assertnear(y, 7, 1e-4); /* ... and x + y == 10 still holds */

    /* a row that goes infeasible under OOM must still be queued and dualized */
    am_suggest(S, vx, 20), am_updatevars(S);
    assertnear(x, 10, 1e-4); /* the required floor y >= 0 wins ... */
    assertnear(y, 0, 1e-4);  /* ... over the strong edit */

    fail_vec_alloc = 0; /* with room again the cache is rebuilt */
    am_suggest(S, vx, 8), am_updatevars(S);
    assertnear(x, 8, 1e-4);
    assertnear(y, 2, 1e-4);
    am_delsolver(S);
}

/* ---------------------------------------------------------------------- */
/* am_add() must publish the solution it computes.  The pivot subject is an
 * external variable that becomes basic, so its value changes from 0 to the
 * row constant; nothing marked that change dirty and the binding kept its old
 * value until an unrelated operation moved the variable again (BUG A). */

TEST(add_publishes_the_variable_it_pivots_in) {
    am_Solver     *S = am_test_new_solver();
    am_Num         l = 0, r = 0;
    am_Id          vl, vr;
    am_Constraint *link, *bound;

    assertok(S != NULL);
    am_autoupdate(S, 1);
    vl = am_newvariable(S, &l);
    vr = am_newvariable(S, &r);

    /* r - l == 120 */
    link = am_newconstraint(S, AM_REQUIRED);
    am_addterm(link, vr, -1.f), am_addterm(link, vl, 1.f);
    am_addconstant(link, 120.f), am_setrelation(link, AM_EQUAL);
    asserteq(am_add(link), AM_OK);

    /* l >= 20: merging the r-l row pivots r in; its binding used to stay 0 */
    bound = am_newconstraint(S, AM_REQUIRED);
    am_addterm(bound, vl, 1.f), am_addconstant(bound, -20.f);
    am_setrelation(bound, AM_GREATEREQUAL);
    asserteq(am_add(bound), AM_OK);

    am_updatevars(S); /* nothing is left to publish */
    assertnear(r, l + 120.f, 1e-4);
    assertok(l >= 20.f - 1e-4);
    am_delsolver(S);
}

/* The same read path on a layout-shaped model: four builtins plus one user
 * constraint, where the merged row brings `right` into the basis. */
TEST(add_publishes_every_layout_binding) {
    am_Solver     *S = am_test_new_solver();
    am_Num         v[6]; /* left, top, width, height, right, bottom */
    am_Id          id[6];
    am_Constraint *c;
    int            i;

    assertok(S != NULL);
    am_autoupdate(S, 1);
    for (i = 0; i < 6; ++i) v[i] = 0, id[i] = am_newvariable(S, &v[i]);

    c = am_newconstraint(S, AM_REQUIRED); /* right == left + width */
    am_addterm(c, id[4], 1.f), am_addterm(c, id[0], -1.f);
    am_addterm(c, id[2], -1.f), am_setrelation(c, AM_EQUAL);
    asserteq(am_add(c), AM_OK);

    c = am_newconstraint(S, AM_REQUIRED); /* bottom == top + height */
    am_addterm(c, id[5], 1.f), am_addterm(c, id[1], -1.f);
    am_addterm(c, id[3], -1.f), am_setrelation(c, AM_EQUAL);
    asserteq(am_add(c), AM_OK);

    c = am_newconstraint(S, AM_REQUIRED); /* width >= 0 */
    am_addterm(c, id[2], 1.f), am_setrelation(c, AM_GREATEREQUAL);
    asserteq(am_add(c), AM_OK);

    c = am_newconstraint(S, AM_REQUIRED); /* height >= 0 */
    am_addterm(c, id[3], 1.f), am_setrelation(c, AM_GREATEREQUAL);
    asserteq(am_add(c), AM_OK);

    c = am_newconstraint(S, AM_REQUIRED); /* left >= 10 */
    am_addterm(c, id[0], 1.f), am_addconstant(c, -10.f);
    am_setrelation(c, AM_GREATEREQUAL);
    asserteq(am_add(c), AM_OK);

    am_updatevars(S);
    assertnear(v[4], v[0] + v[2], 1e-4); /* right == left + width */
    assertnear(v[5], v[1] + v[3], 1e-4); /* bottom == top + height */
    assertok(v[0] >= 10.f - 1e-4);
    am_delsolver(S);
}

TEST(remove_drops_the_second_error_column) {
    static unsigned char stream[1024];
    am_Solver           *S = am_test_new_solver();
    am_Num               v = 0;
    am_Id                x;
    am_Constraint       *c;
    dump_buf             db = AM_ZERO;

    assertok(S != NULL);
    x = am_newvariable(S, &v);
    /* a soft inequality plus an edit: the marker substitution cancels the other
     * error column only up to rounding, which leaves it in the objective */
    c = am_newconstraint(S, AM_STRONG); /* 3x + 2 >= 0 */
    am_addterm(c, x, 3.f), am_addconstant(c, 2.f);
    am_setrelation(c, AM_GREATEREQUAL);
    asserteq(am_add(c), AM_OK);
    asserteq(am_addedit(S, x, AM_MEDIUM), AM_OK);
    am_remove(c);

    memset(&db, 0, sizeof(db));
    db.base.var_name = dump_buf_varname;
    db.base.writer = dump_buf_writer;
    db.p = stream;
    asserteq(am_dump(S, &db.base), AM_OK); /* no ownerless column is left */
    am_delsolver(S);
}

#include "api_test.gen.inc"
