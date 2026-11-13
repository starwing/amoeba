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
/* lifetime: a missing object (or a pointer the solver no longer owns) is
 * "nothing to do" instead of a dereference.  Each of these used to be an
 * assert that vanished in release builds, leaving a NULL (or recycled)
 * dereference behind. */

/* A clone either returns a fully built constraint or NULL.  When the allocator
 * refuses the new constraint there is no object to write a relation into. */
static int fail_constraint_alloc;

static void *clone_test_allocf(void **pud, void *p, size_t n, size_t o,
                               am_Type t) {
    if (n != 0 && fail_constraint_alloc && t == am_AllocConstraint) return NULL;
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
    asserteq(c->relation, AM_EQUAL);
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
    am_setrelation(ge, AM_GREATEQUAL);
    asserteq(am_relation(le), AM_LESSEQUAL);
    asserteq(am_relation(eq), AM_EQUAL);
    asserteq(am_relation(ge), AM_GREATEQUAL);
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
    am_addterm(ge, vx, 1.f), am_setrelation(ge, AM_GREATEQUAL);
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
    assertok(ty == 0xCE);
    return dump_readraw(r, 4);
}

static size_t dump_u32len(const unsigned char *p) {
    if (*p <= 0x7F) return 1;
    if (*p == 0xCC) return 2;
    if (*p == 0xCD) return 3;
    assertok(*p == 0xCE);
    return 5;
}

static unsigned dump_readcount(dump_reader *r) {
    int ty = *r->p++;
    if (ty >= 0x90 && ty <= 0x9F) return (unsigned)(ty - 0x90);
    if (ty == 0xDC) return dump_readraw(r, 2);
    assertok(ty == 0xDD);
    return dump_readraw(r, 4);
}

static void dump_skipstring(dump_reader *r) {
    unsigned size;
    int      ty = *r->p++;
    if (ty >= 0xA0 && ty <= 0xBF)
        size = (unsigned)(ty - 0xA0);
    else {
        assertok(ty == 0xD9);
        size = (unsigned)*r->p++;
    }
    r->p += size;
}

static void dump_skipfloat(dump_reader *r) {
    int ty = *r->p++; /* float32 or float64, depending on the build's am_Num */
    assertok(ty == 0xCA || ty == 0xCB);
    r->p += ty == 0xCA ? 4 : 8;
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
    asserteq(dump_readcount(r), 5); /* [total, vars, constraints, rows, obj] */
    dump_readu32(r);
    count = dump_readcount(r);
    for (i = 0; i < count; ++i) dump_skipstring(r);
    count = dump_readcount(r);
    for (i = 0; i < count; ++i) {
        assertok(dump_readcount(r) == 6);
        dump_skipstring(r), dump_skipfloat(r);
        dump_readu32(r), dump_readu32(r), dump_readu32(r);
        dump_skiprow(r);
    }
}

static size_t dump_total_offset(const unsigned char *buf, size_t *plen) {
    dump_reader r;
    r.p = buf;
    assertok(dump_readcount(&r) == 5);
    *plen = dump_u32len(r.p);
    return (size_t)(r.p - buf);
}

static size_t dump_arena_offset(const unsigned char *buf) {
    dump_reader r;
    unsigned    count;
    r.p = buf;
    dump_skipheader(&r);
    count = dump_readcount(&r);
    assertok(count & 1);
    return (size_t)(r.p - buf);
}

static size_t dump_row_key_offset(const unsigned char *buf, int which) {
    dump_reader r;
    unsigned    i, count;
    r.p = buf;
    dump_skipheader(&r);
    count = dump_readcount(&r);
    assertok(count & 1);
    dump_readu32(&r);
    for (i = 1; i < count; i += 2) {
        if ((int)(i / 2) == which) return (size_t)(r.p - buf);
        dump_readu32(&r);
        dump_skiprow(&r);
    }
    assertok(0);
    return 0;
}

static size_t dump_row_term_offset(const unsigned char *buf, int which) {
    dump_reader r;
    unsigned    i, count, rowcount;
    r.p = buf;
    dump_skipheader(&r);
    count = dump_readcount(&r);
    assertok(count & 1);
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
    assertok(0);
    return 0;
}

static size_t dump_first_marker_offset(const unsigned char *buf) {
    dump_reader r;
    unsigned    count;
    r.p = buf;
    assertok(dump_readcount(&r) == 5);
    dump_readu32(&r);
    count = dump_readcount(&r);
    while (count--) dump_skipstring(&r);
    assertok(dump_readcount(&r) >= 1);
    assertok(dump_readcount(&r) == 6);
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
    assertok(len < sizeof(out));
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

/* A solver with more than 0xFFFF symbols makes the writer use the 32-bit
 * count/value encodings; loading the same stream drives the matching reader
 * paths.  Both sides are exercised through am_dump()/am_load(), not by
 * hand-writing msgpack. */
#define BIG_VARS 65536
static am_Num        big_values[BIG_VARS];
static unsigned char big_stream[256 * 1024];

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
    asserteq(S->rows.count, 1);
    asserteq(S->vars.count, 1);
    asserteq(S->constraints.count, 1);
    am_delsolver(S);
}

/* A row with eight terms has a count of 17, which the writer encodes as a
 * msgpack uint16.  When the reader hands the loader the whole stream at once,
 * the loader's two-byte read path runs without a refill. */
/* The loader restores marker/other with their symbol types, so am_error()
 * still reports the violation of a loaded soft constraint. */
static size_t dump_violated_soft(unsigned char *buf) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *pin = am_newconstraint(S, AM_REQUIRED); /* x == 0 */
    am_Constraint *soft = am_newconstraint(S, AM_MEDIUM);  /* x == 10 */
    dump_buf       db;

    am_addterm(pin, vx, 1.f), am_setrelation(pin, AM_EQUAL);
    am_addconstant(pin, 0.f);
    asserteq(am_add(pin), AM_OK);
    am_addterm(soft, vx, 1.f), am_setrelation(soft, AM_EQUAL);
    am_addconstant(soft, 10.f);
    asserteq(am_add(soft), AM_OK);
    am_updatevars(S);

    memset(&db, 0, sizeof(db));
    db.base.var_name = dump_buf_varname;
    db.base.writer = dump_buf_writer;
    db.p = buf;
    asserteq(am_dump(S, &db.base), AM_OK);
    am_delsolver(S);
    return db.len;
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

TEST(load_reads_u16_in_one_chunk) {
    static unsigned char stream[2048];
    am_Num               values[8] = {0};
    am_Solver           *S, *S2;
    am_Id                vars[8];
    am_Constraint       *c;
    dump_buf             db;
    stream_loader        sl;
    unsigned             i;
    size_t               len;

    S = am_test_new_solver();
    for (i = 0; i < 8; ++i) vars[i] = am_newvariable(S, &values[i]);
    c = am_newconstraint(S, AM_REQUIRED);
    for (i = 0; i < 8; ++i) asserteq(am_addterm(c, vars[i], 1), AM_OK);
    asserteq(am_addconstant(c, -8), AM_OK);
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
    asserteq(S2->vars.count, 8);
    am_delsolver(S2);
    am_delsolver(S);
}

/* A dump with 65536 variables crosses both the 0xDD count and the 0xCE
 * value encodings; load it back with a reader that returns one big chunk. */
TEST(dump_load_large_counts) {
    am_Solver    *S, *S2;
    dump_buf      db;
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
    asserteq(S2->vars.count, BIG_VARS);
    asserteq(S2->constraints.count, 0);
    am_delsolver(S2);
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

/* am_load() accepts a stream whose constraint no longer owns its marker while
 * the rows still reference that symbol: the loader checks structure, not
 * ownership.  Such a solver has no stream index for that symbol, so am_dump()
 * must report AM_FAILED instead of dereferencing a missing map entry -- the
 * dump side cannot assume the load side was fed a real dump. */
TEST(dump_reports_symbol_outside_the_map) {
    static unsigned char stream[2048];
    static unsigned char out[2048];
    size_t               len = dump_one_row(stream);
    size_t               at = dump_first_marker_offset(stream);
    stream_loader        sl;
    dump_buf             db;
    am_Solver           *S;

    assertok(len < sizeof(stream));
    assertok(dump_u32len(stream + at) == 1); /* small dump: one byte */
    stream[at] = 0; /* the constraint loses its marker */
    stream_init(&sl, stream, len);
    assertok((S = load_stream(&sl, 0)) != NULL);
    assertok(sl.cons != NULL);
    asserteq(sl.cons->marker_id, 0);

    memset(&db, 0, sizeof(db));
    db.base.var_name = dump_buf_varname;
    db.base.writer = dump_buf_writer;
    db.p = out;
    asserteq(am_dump(S, &db.base), AM_FAILED);
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
