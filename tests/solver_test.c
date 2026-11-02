/* tests/solver_test.c -- the constraint solving suites
 *
 * Ported verbatim from the old single-file test.c (sections "test all",
 * "test binarytree", "test unbounded", "test strength", "test suggest",
 * "test dirty", "test cycling"); only the harness plumbing changed:
 * setjmp/OOM handling is gone (each test runs in its own process) and the
 * solvers use the accounting allocator.
 */

#include "amoeba_test.h"

/* new_constraint() terminates its va_arg list with this sentinel */
static void *END = NULL;

static void am_dumpkey(am_Symbol sym) {
    int ch = 'v';
    switch (sym.type) {
    case AM_EXTERNAL: ch = 'v'; break;
    case AM_SLACK: ch = 's'; break;
    case AM_ERROR: ch = 'e'; break;
    case AM_DUMMY: ch = 'd'; break;
    }
    printf("%c%d", ch, (int)sym.id);
}

/* Iterating a table reads am_Table fields directly: tests may inspect the
 * library's internals, but must not call its static helpers. */
static const void *am_test_slot(const am_Table *t, am_Size i)
{ return (const char*)t->vals + (size_t)i * t->value_size; }

static void am_dumprow(const am_Row *row) {
    const am_Table *t = &row->terms;
    am_Size i;
    printf("%g", row->constant);
    for (i = 0; i < t->size; ++i) {
        am_Symbol key = t->keys[i].sym;
        am_Num    multiplier;
        if (key.id == 0) continue;
        multiplier = *(const am_Num*)am_test_slot(t, i);
        printf(" %c ", multiplier > 0.0 ? '+' : '-');
        if (multiplier < 0.0) multiplier = -multiplier;
        if (multiplier != 1.0f) printf("%g*", multiplier);
        am_dumpkey(key);
    }
    printf("\n");
}

static void am_dumpsolver(am_Solver *S) {
    const am_Table *t = &S->rows;
    am_Size i;
    int     idx = 0;
    printf("-------------------------------\n");
    printf("solver: ");
    am_dumprow(&S->objective);
    printf("rows(%d):\n", S->rows.count);
    for (i = 0; i < t->size; ++i) {
        am_Symbol key = t->keys[i].sym;
        if (key.id == 0) continue;
        printf("%d. ", ++idx);
        am_dumpkey(key);
        printf(" = ");
        am_dumprow((const am_Row*)am_test_slot(t, i));
    }
    printf("-------------------------------\n");
}

static am_Constraint *new_constraint(am_Solver *in_solver, double in_strength,
                                     am_Id in_term1, double in_factor1,
                                     int in_relation, double in_constant, ...) {
    int            result;
    va_list        argp;
    am_Constraint *c;
    assert(in_solver && in_term1);
    c = am_newconstraint(in_solver, (am_Num)in_strength);
    if (!c) return 0;
    am_addterm(c, in_term1, (am_Num)in_factor1);
    am_setrelation(c, in_relation);
    if (in_constant) am_addconstant(c, (am_Num)in_constant);
    va_start(argp, in_constant);
    while (1) {
        am_Id  va_term = va_arg(argp, am_Id);
        double va_factor = va_arg(argp, double);
        if (va_term == 0) break;
        am_addterm(c, va_term, (am_Num)va_factor);
    }
    va_end(argp);
    result = am_add(c);
    assert(result == AM_OK);
    return c;
}

TEST(all) {
    am_Solver     *S;
    am_Id          xl, xm, xr, xd;
    am_Num         vxl = 0, vxm = 0, vxr = 0, vxd = 0;
    am_Constraint *c1, *c2, *c3, *c4, *c5, *c6;
    am_Constraint *cs[50];
    int            i, ret;

    S = am_test_new_solver();
    assert(S != NULL);
    for (i = 0; i < 50; ++i) {
        cs[i] = am_newconstraint(S, AM_REQUIRED);
        assert(cs[i] != NULL);
    }
    for (i = 0; i < 50; ++i) am_delconstraint(cs[i]);
    am_delsolver(S);
    assert(allmem == 0);

    S = am_test_new_solver();
    xl = am_newvariable(S, &vxl);
    xm = am_newvariable(S, &vxm);
    xr = am_newvariable(S, &vxr);

    assert(!am_hasedit(S, 0));
    assert(!am_hasedit(S, xl));
    assert(!am_hasedit(S, xm));
    assert(!am_hasedit(S, xr));
    assert(am_varvalue(S, xl, NULL) == &vxl);
    assert(am_varvalue(S, xm, NULL) == &vxm);
    assert(am_varvalue(S, xr, NULL) == &vxr);
    assert(!am_hasconstraint(NULL));

    xd = am_newvariable(S, &vxd);
    am_delvariable(S, xd);

    assert(am_setrelation(NULL, AM_GREATEQUAL) == AM_FAILED);

    c1 = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c1, xl, 1.0);
    am_setrelation(c1, AM_GREATEQUAL);
    ret = am_add(c1);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    assert(am_setrelation(c1, AM_GREATEQUAL) == AM_FAILED);
    assert(AM_REQUIRED - 10 == AM_REQUIRED);
    assert(am_setstrength(c1, AM_STRONG) == AM_OK);
    assert(am_setstrength(c1, AM_REQUIRED) == AM_OK);

    assert(am_hasconstraint(c1));
    assert(!am_hasedit(S, xl));

    c2 = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c2, xl, 1.0);
    am_setrelation(c2, AM_EQUAL);
    ret = am_add(c2);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    am_resetsolver(S);
    am_delconstraint(c1);
    am_delconstraint(c2);
    am_dumpsolver(S);

    /* c1: 2*xm == xl + xr */
    c1 = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c1, xm, 2.0);
    am_setrelation(c1, AM_EQUAL);
    am_addterm(c1, xl, 1.0);
    am_addterm(c1, xr, 1.0);
    ret = am_add(c1);
    printf("c1: marker=%d other=%d\n", c1->marker.id, c1->other.id);
    assert(c1->marker.type == AM_DUMMY && c1->other.type == AM_EXTERNAL);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    /* c2: xl + 10 <= xr */
    c2 = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c2, xl, 1.0);
    am_addconstant(c2, 10.0);
    am_setrelation(c2, AM_LESSEQUAL);
    am_addterm(c2, xr, 1.0);
    ret = am_add(c2);
    printf("c2: marker=%d other=%d\n", c2->marker.id, c2->other.id);
    assert(c2->marker.type == AM_SLACK && c2->other.type == AM_EXTERNAL);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    /* c3: xr <= 100 */
    c3 = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c3, xr, 1.0);
    am_setrelation(c3, AM_LESSEQUAL);
    am_addconstant(c3, 100.0);
    ret = am_add(c3);
    printf("c3: marker=%d other=%d\n", c3->marker.id, c3->other.id);
    assert(c3->marker.type == AM_SLACK && c3->other.type == AM_EXTERNAL);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    /* c4: xl >= 0 */
    c4 = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c4, xl, 1.0);
    am_setrelation(c4, AM_GREATEQUAL);
    am_addconstant(c4, 0.0);
    ret = am_add(c4);
    printf("c4: marker=%d other=%d\n", c4->marker.id, c4->other.id);
    assert(c4->marker.type == AM_SLACK && c4->other.type == AM_EXTERNAL);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    c5 = am_cloneconstraint(c4, AM_REQUIRED);
    ret = am_add(c5);
    assert(ret == AM_OK);
    am_dumpsolver(S);
    am_remove(c5);

    c5 = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c5, xl, 1.0);
    am_setrelation(c5, AM_EQUAL);
    am_addconstant(c5, 0.0);
    ret = am_add(c5);
    assert(ret == AM_OK);

    c6 = am_cloneconstraint(c4, AM_REQUIRED);
    ret = am_add(c6);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    am_resetconstraint(c6);
    am_delconstraint(c6);

    am_remove(c1);
    am_remove(c2);
    am_remove(c3);
    am_remove(c4);
    am_dumpsolver(S);
    ret |= am_add(c4);
    ret |= am_add(c3);
    ret |= am_add(c2);
    ret |= am_add(c1);
    assert(ret == AM_OK);

    am_clearedits(S);
    am_resetsolver(S);
    printf("after reset\n");

    am_dumpsolver(S);
    ret |= am_add(c1);
    ret |= am_add(c2);
    ret |= am_add(c3);
    ret |= am_add(c4);
    assert(ret == AM_OK);

    printf("after initialize\n");
    am_dumpsolver(S);
    am_updatevars(S);
    printf("xl: %f, xm: %f, xr: %f\n", vxl, vxm, vxr);

    am_addedit(S, xm, AM_MEDIUM);
    am_dumpsolver(S);
    am_updatevars(S);
    printf("xl: %f, xm: %f, xr: %f\n", vxl, vxm, vxr);

    assert(am_hasedit(S, xm));
    am_clearedits(S);

    printf("suggest to 0.0\n");
    am_suggest(S, xm, 0.0);
    am_dumpsolver(S);
    am_updatevars(S);
    printf("xl=%f xm=%f xr=%f\n", vxl, vxm, vxr);
    assert(vxl == 0.f && vxm == 5.f && vxr == 10.f);

    /* make other as infeasible */
    printf("suggest to 100.0\n");
    am_suggest(S, xm, 100.0);
    am_dumpsolver(S);
    am_updatevars(S);
    assert(vxl == 90.f && vxm == 95.f && vxr == 100.f);

    printf("suggest to 70.0\n");
    am_suggest(S, xm, 70.0);
    am_updatevars(S);
    am_dumpsolver(S);
    assert(vxl == 65.f && vxm == 70.f && vxr == 75.f);

    printf("restore xr to 10.0\n");
    am_deledit(S, xm);
    am_suggest(S, xr, 10.0);
    am_updatevars(S);
    am_dumpsolver(S);
    assert(vxl == 0.f && vxm == 5.f && vxr == 10.f);
    am_deledit(S, xr);

    printf("suggest to 60.0\n");
    am_suggest(S, xm, 60.0);
    am_updatevars(S);
    am_dumpsolver(S);
    assert(vxl == 20.f && vxm == 60.f && vxr == 100.f);

    printf("suggest to 50.0\n");
    am_suggest(S, xm, 50.0);
    am_updatevars(S);
    am_dumpsolver(S);
    assert(vxl == 0.f && vxm == 50.f && vxr == 100.f);

    printf("suggest to 40.0\n");
    am_suggest(S, xm, 40.0);
    am_updatevars(S);
    am_dumpsolver(S);
    assert(vxl == 0.f && vxm == 40.f && vxr == 80.f);

    am_deledit(S, xm);
    am_updatevars(S);
    am_dumpsolver(S);

    printf("xl: %f, xm: %f, xr: %f\n", vxl, vxm, vxr);

    /* test dead vars */
    assert(am_refcount(S, xm) == 2); /* xm & c1 */
    am_suggest(S, xm, 50);
    assert(am_refcount(S, xm) == 4); /* xm & c1 & edit & dirty */
    am_deledit(S, xm);
    assert(am_refcount(S, xm) == 3); /* xm & c1 & dirty */
    am_delvariable(S, xm);
    assert(am_refcount(S, xm) == 2); /* c1 & dirty */
    am_delconstraint(c1);
    assert(am_refcount(S, xm) == 1); /* dirty */
    am_updatevars(S);
    am_dumpsolver(S);

    am_delsolver(S);
    printf("allmem = %d\n", (int)allmem);
    printf("maxmem = %d\n", (int)maxmem);
    assert(allmem == 0);
    maxmem = 0;
}

TEST(binarytree) {
    const int      NUM_ROWS = 9;
    const int      X_OFFSET = 0;
    int            nPointsCount, nResult, nRow;
    int            nCurrentRowPointsCount = 1;
    int            nCurrentRowFirstPointIndex = 0;
    am_Constraint *pC;
    am_Solver     *pSolver;
    am_Id         *arrX, *arrY;
    am_Num        *numX, *numY;

    arrX = (am_Id *)calloc(2048, sizeof(am_Id));
    if (arrX == NULL) return;
    arrY = arrX + 1024;

    numX = (am_Num *)calloc(2048, sizeof(am_Num));
    if (numX == NULL) return;
    numY = numX + 1024;

    /* Create set of rules to distribute vertexes of a binary tree like this one:
     *      0
     *     / \
     *    /   \
     *   1     2
     *  / \   / \
     * 3   4 5   6
     */

    pSolver = am_test_new_solver();

    /* Xroot=500, Yroot=10 */
    arrX[0] = am_newvariable(pSolver, &numX[0]);
    arrY[0] = am_newvariable(pSolver, &numY[0]);
    am_addedit(pSolver, arrX[0], AM_STRONG);
    am_addedit(pSolver, arrY[0], AM_STRONG);
    am_suggest(pSolver, arrX[0], 500.0f + X_OFFSET);
    am_suggest(pSolver, arrY[0], 10.0f);

    for (nRow = 1; nRow < NUM_ROWS; nRow++) {
        int nPreviousRowFirstPointIndex = nCurrentRowFirstPointIndex;
        int nPoint, nParentPoint = 0;
        nCurrentRowFirstPointIndex += nCurrentRowPointsCount;
        nCurrentRowPointsCount *= 2;

        for (nPoint = 0; nPoint < nCurrentRowPointsCount; nPoint++) {
            int nIndex = nCurrentRowFirstPointIndex + nPoint;
            arrX[nIndex] = am_newvariable(pSolver, &numX[nIndex]);
            arrY[nIndex] = am_newvariable(pSolver, &numY[nIndex]);

            /* Ycur = Yprev_row + 15 */
            pC = am_newconstraint(pSolver, AM_REQUIRED);
            am_addterm(pC, arrY[nCurrentRowFirstPointIndex + nPoint], 1.0);
            am_setrelation(pC, AM_EQUAL);
            am_addterm(pC, arrY[nCurrentRowFirstPointIndex - 1], 1.0);
            am_addconstant(pC, 15.0);
            nResult = am_add(pC);
            assert(nResult == AM_OK);

            if (nPoint > 0) {
                /* Xcur >= XPrev + 5 */
                pC = am_newconstraint(pSolver, AM_REQUIRED);
                am_addterm(pC, arrX[nCurrentRowFirstPointIndex + nPoint], 1.0);
                am_setrelation(pC, AM_GREATEQUAL);
                am_addterm(pC, arrX[nCurrentRowFirstPointIndex + nPoint - 1],
                           1.0);
                am_addconstant(pC, 5.0);
                nResult = am_add(pC);
                assert(nResult == AM_OK);
            } else {
                /* When these lines added it crashes at the line 109 */
                pC = am_newconstraint(pSolver, AM_REQUIRED);
                am_addterm(pC, arrX[nCurrentRowFirstPointIndex + nPoint], 1.0);
                am_setrelation(pC, AM_GREATEQUAL);
                am_addconstant(pC, 0.0);
                nResult = am_add(pC);
                assert(nResult == AM_OK);
            }

            if ((nPoint % 2) == 1) {
                /* Xparent = 0.5 * Xcur + 0.5 * Xprev */
                pC = am_newconstraint(pSolver, AM_REQUIRED);
                am_addterm(pC, arrX[nPreviousRowFirstPointIndex + nParentPoint],
                           1.0);
                am_setrelation(pC, AM_EQUAL);
                am_addterm(pC, arrX[nCurrentRowFirstPointIndex + nPoint], 0.5);
                am_addterm(pC, arrX[nCurrentRowFirstPointIndex + nPoint - 1],
                           0.5);
                /* It crashes here (at the 3rd call of am_add(...))!  */
                nResult = am_add(pC);
                assert(nResult == AM_OK);

                nParentPoint++;
            }
        }
    }
    nPointsCount = nCurrentRowFirstPointIndex + nCurrentRowPointsCount;
    (void)nPointsCount;

    /*{
        int i;
        for (i = 0; i < nPointsCount; i++)
            printf("Point %d: (%f, %f)\n", i,
                    am_value(arrX[i]), am_value(arrY[i]));
    }*/

    am_delsolver(pSolver);
    printf("allmem = %d\n", (int)allmem);
    printf("maxmem = %d\n", (int)maxmem);
    assert(allmem == 0);
    free(arrX);
    maxmem = 0;
}

TEST(unbounded) {
    am_Solver     *S;
    am_Id          x, y;
    am_Num         vx = 0, vy = 0;
    am_Constraint *c;
    int            ret;

    S = am_test_new_solver();
    x = am_newvariable(S, &vx);
    y = am_newvariable(S, &vy);

    /* 10.0 == 0.0 */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addconstant(c, 10.0);
    am_setrelation(c, AM_EQUAL);
    ret = am_add(c);
    assert(ret == AM_UNSATISFIED);
    am_dumpsolver(S);

    /* 0.0 == 0.0 */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addconstant(c, 0.0);
    am_setrelation(c, AM_EQUAL);
    ret = am_add(c);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    am_resetsolver(S);

    /* x >= 10.0 */
    c = am_newconstraint(S, AM_REQUIRED);
    ret = am_addterm(c, x, 1.0);
    printf("addterm = %d\n", ret);
    assert(ret == AM_OK);
    am_setrelation(c, AM_GREATEQUAL);
    am_addconstant(c, 10.0);
    ret = am_add(c);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    /* x == 2*y */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, x, 1.0);
    am_setrelation(c, AM_EQUAL);
    am_addterm(c, y, 2.0);
    ret = am_add(c);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    /* y == 3*x */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, y, 1.0);
    am_setrelation(c, AM_EQUAL);
    am_addterm(c, x, 3.0);
    ret = am_add(c);
    assert(ret == AM_UNBOUND);
    am_dumpsolver(S);

    am_resetsolver(S);

    /* x >= 10.0 */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, x, 1.0);
    am_setrelation(c, AM_GREATEQUAL);
    am_addconstant(c, 10.0);
    ret = am_add(c);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    /* x <= 0.0 */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, x, 1.0);
    am_setrelation(c, AM_LESSEQUAL);
    ret = am_add(c);
    assert(ret == AM_UNBOUND);
    am_dumpsolver(S);

    printf("x: %f\n", vx);

    am_resetsolver(S);

    /* x == 10.0 */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, x, 1.0);
    am_setrelation(c, AM_EQUAL);
    am_addconstant(c, 10.0);
    ret = am_add(c);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    /* x == 20.0 */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, x, 1.0);
    am_setrelation(c, AM_EQUAL);
    am_addconstant(c, 20.0);
    ret = am_add(c);
    assert(ret == AM_UNSATISFIED);
    am_dumpsolver(S);

    /* x == 10.0 */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, x, 1.0);
    am_setrelation(c, AM_EQUAL);
    am_addconstant(c, 10.0);
    ret = am_add(c);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    am_delsolver(S);
    printf("allmem = %d\n", (int)allmem);
    printf("maxmem = %d\n", (int)maxmem);
    assert(allmem == 0);
    maxmem = 0;
}

TEST(strength) {
    am_Solver     *S;
    am_Id          x, y;
    am_Num         vx = 0, vy = 0;
    am_Constraint *c;

    S = am_test_new_solver();
    am_autoupdate(S, 1);
    x = am_newvariable(S, &vx);
    y = am_newvariable(S, &vy);

    /* x <= y */
    new_constraint(S, AM_STRONG, x, 1.0, AM_LESSEQUAL, 0.0, y, 1.0, END);
    new_constraint(S, AM_MEDIUM, x, 1.0, AM_EQUAL, 50, END);
    c = new_constraint(S, AM_MEDIUM - 10, y, 1.0, AM_EQUAL, 40, END);
    printf("%f, %f\n", vx, vy);
    assert(vx == 50);
    assert(vy == 50);

    am_setstrength(c, AM_MEDIUM + 10);
    printf("%f, %f\n", vx, vy);
    assert(vx == 40);
    assert(vy == 40);

    am_setstrength(c, AM_MEDIUM - 10);
    printf("%f, %f\n", vx, vy);
    assert(vx == 50);
    assert(vy == 50);

    am_delsolver(S);
    printf("allmem = %d\n", (int)allmem);
    printf("maxmem = %d\n", (int)maxmem);
    assert(allmem == 0);
    maxmem = 0;
}

TEST(suggest) {
#if 1
    /* This should be valid but fails the (enter.id != 0) assertion in am_dual_optimize() */
    am_Num strength1 = AM_REQUIRED;
    am_Num strength2 = AM_REQUIRED;
    am_Num width = 76;
#else
    /* This mostly works, but still insists on forcing left_child_l = 0 which it should not */
    am_Num strength1 = AM_STRONG;
    am_Num strength2 = AM_WEAK;
    am_Num width = 76;
#endif
    am_Num     delta = 0;
    am_Num     pos = 0;
    am_Solver *S;
    am_Id      splitter_l, splitter_w, splitter_r;
    am_Id      left_child_l, left_child_w, left_child_r;
    am_Id      splitter_bar_l, splitter_bar_w, splitter_bar_r;
    am_Id      right_child_l, right_child_w, right_child_r;
    am_Num     vsplitter_l = 0, vsplitter_w = 0, vsplitter_r = 0;
    am_Num     vleft_child_l = 0, vleft_child_w = 0, vleft_child_r = 0;
    am_Num     vsplitter_bar_l = 0, vsplitter_bar_w = 0, vsplitter_bar_r = 0;
    am_Num     vright_child_l = 0, vright_child_w = 0, vright_child_r = 0;

    S = am_test_new_solver();
    splitter_l = am_newvariable(S, &vsplitter_l);
    splitter_w = am_newvariable(S, &vsplitter_w);
    splitter_r = am_newvariable(S, &vsplitter_r);
    left_child_l = am_newvariable(S, &vleft_child_l);
    left_child_w = am_newvariable(S, &vleft_child_w);
    left_child_r = am_newvariable(S, &vleft_child_r);
    splitter_bar_l = am_newvariable(S, &vsplitter_bar_l);
    splitter_bar_w = am_newvariable(S, &vsplitter_bar_w);
    splitter_bar_r = am_newvariable(S, &vsplitter_bar_r);
    right_child_l = am_newvariable(S, &vright_child_l);
    right_child_w = am_newvariable(S, &vright_child_w);
    right_child_r = am_newvariable(S, &vright_child_r);

    /* splitter_r = splitter_l + splitter_w */
    /* left_child_r = left_child_l + left_child_w */
    /* splitter_bar_r = splitter_bar_l + splitter_bar_w */
    /* right_child_r = right_child_l + right_child_w */
    new_constraint(S, AM_REQUIRED, splitter_r, 1.0, AM_EQUAL, 0.0, splitter_l,
                   1.0, splitter_w, 1.0, END);
    new_constraint(S, AM_REQUIRED, left_child_r, 1.0, AM_EQUAL, 0.0,
                   left_child_l, 1.0, left_child_w, 1.0, END);
    new_constraint(S, AM_REQUIRED, splitter_bar_r, 1.0, AM_EQUAL, 0.0,
                   splitter_bar_l, 1.0, splitter_bar_w, 1.0, END);
    new_constraint(S, AM_REQUIRED, right_child_r, 1.0, AM_EQUAL, 0.0,
                   right_child_l, 1.0, right_child_w, 1.0, END);

    /* splitter_bar_w = 6 */
    /* splitter_bar_l >= splitter_l + delta */
    /* splitter_bar_r <= splitter_r - delta */
    /* left_child_r = splitter_bar_l */
    /* right_child_l = splitter_bar_r */
    new_constraint(S, AM_REQUIRED, splitter_bar_w, 1.0, AM_EQUAL, 6.0, END);
    new_constraint(S, AM_REQUIRED, splitter_bar_l, 1.0, AM_GREATEQUAL, delta,
                   splitter_l, 1.0, END);
    new_constraint(S, AM_REQUIRED, splitter_bar_r, 1.0, AM_LESSEQUAL, -delta,
                   splitter_r, 1.0, END);
    new_constraint(S, AM_REQUIRED, left_child_r, 1.0, AM_EQUAL, 0.0,
                   splitter_bar_l, 1.0, END);
    new_constraint(S, AM_REQUIRED, right_child_l, 1.0, AM_EQUAL, 0.0,
                   splitter_bar_r, 1.0, END);

    /* right_child_r >= splitter_r + 1 */
    /* left_child_w = 256 */
    new_constraint(S, strength1, right_child_r, 1.0, AM_GREATEQUAL, 1.0,
                   splitter_r, 1.0, END);
    new_constraint(S, strength2, left_child_w, 1.0, AM_EQUAL, 256.0, END);

    /* splitter_l = 0 */
    /* splitter_r = 76 */
    new_constraint(S, AM_REQUIRED, splitter_l, 1.0, AM_EQUAL, 0.0, END);
    new_constraint(S, AM_REQUIRED, splitter_r, 1.0, AM_EQUAL, width, END);

    for (pos = -10; pos < 86; pos++) {
        am_suggest(S, splitter_bar_l, pos);
        /* printf("pos: %4g | ", pos);
        printf("splitter_l l=%2g, w=%2g, r=%2g | ", vsplitter_l,
                vsplitter_w, vsplitter_r);
        printf("left_child_l l=%2g, w=%2g, r=%2g | ", vleft_child_l,
                vleft_child_w, vleft_child_r);
        printf("splitter_bar_l l=%2g, w=%2g, r=%2g | ", vsplitter_bar_l,
                vsplitter_bar_w, vsplitter_bar_r);
        printf("right_child_l l=%2g, w=%2g, r=%2g | ", vright_child_l,
                vright_child_w, vright_child_r);
        printf("\n"); */
    }

    am_delsolver(S);
    printf("allmem = %d\n", (int)allmem);
    printf("maxmem = %d\n", (int)maxmem);
    assert(allmem == 0);
    maxmem = 0;
}

TEST(dirty) {
    am_Solver     *S;
    am_Id          xl, xr, xw, xwc;
    am_Num         vxl = 0, vxr = 0, vxw = 0, vxwc = 0;
    am_Constraint *c1, *c2;
    int            ret;

    S = am_test_new_solver();

    xl = am_newvariable(S, &vxl);
    xr = am_newvariable(S, &vxr);
    xw = am_newvariable(S, &vxw);
    xwc = am_newvariable(S, &vxwc);

    /* c1: xw == xr -xl */
    c1 = am_newconstraint(S, AM_REQUIRED);
    ret = 0;
    ret |= am_addterm(c1, xw, 1.0);
    ret |= am_setrelation(c1, AM_EQUAL);
    ret |= am_addterm(c1, xr, 1.0);
    ret |= am_addterm(c1, xl, -1.0);
    ret |= am_add(c1);
    assert(ret == AM_OK);

    am_updatevars(S);
    printf("xl: %f, xr:%f, xw:%f\n", vxl, vxr, vxw);

    /* c1: xwc == xw */
    c2 = am_newconstraint(S, AM_REQUIRED);
    ret = 0;
    ret |= am_addterm(c2, xwc, 1.0);
    ret |= am_setrelation(c2, AM_EQUAL);
    ret |= am_addterm(c2, xw, 1.0);
    ret |= am_add(c2);
    assert(ret == AM_OK);

    /* Sets dirty bit? Related to crash. */
    am_suggest(S, xwc, 10);

    am_updatevars(S);
    printf("xl:%f, xr:%f, xw:%f, xwc:%f\n", vxl, vxr, vxw, vxwc);

    /* Remove xwc and c2 */
    am_deledit(S, xwc);
    am_remove(c2);
    /* Adding an am_updatevars(S); here somehow solves the issue. */
    am_delconstraint(c2);
    am_delvariable(S, xwc);

    /* Causes crash: amoeba.h:482: am_sym2var: Assertion `ve != NULL' failed. */
    am_updatevars(S);
    printf("xl:%f, xr:%f, xw:%f\n", vxl, vxr, vxw);

    /* Manual cleanup */
    am_remove(c1);
    am_delconstraint(c1);

    am_delvariable(S, xl);
    am_delvariable(S, xr);
    am_delvariable(S, xw);

    am_delsolver(S);
}

TEST(cycling) {
    am_Solver *S;
    am_Id      va, vb, vc, vd;
    am_Num     vva = 0, vvb = 0, vvc = 0, vvd = 0;

    S = am_test_new_solver();
    va = am_newvariable(S, &vva);
    vb = am_newvariable(S, &vvb);
    vc = am_newvariable(S, &vvc);
    vd = am_newvariable(S, &vvd);

    am_addedit(S, va, AM_STRONG);
    printf("after edit\n");
    am_dumpsolver(S);

    /* vb == va */
    {
        am_Constraint *c = am_newconstraint(S, AM_REQUIRED);
        int            ret = 0;
        ret |= am_addterm(c, vb, 1.0);
        ret |= am_setrelation(c, AM_EQUAL);
        ret |= am_addterm(c, va, 1.0);
        ret |= am_add(c);
        assert(ret == AM_OK);
        am_dumpsolver(S);
    }

    /* vb == vc */
    {
        am_Constraint *c = am_newconstraint(S, AM_REQUIRED);
        int            ret = 0;
        ret |= am_addterm(c, vb, 1.0);
        ret |= am_setrelation(c, AM_EQUAL);
        ret |= am_addterm(c, vc, 1.0);
        ret |= am_add(c);
        assert(ret == AM_OK);
        am_dumpsolver(S);
    }

    /* vc == vd */
    {
        am_Constraint *c = am_newconstraint(S, AM_REQUIRED);
        int            ret = 0;
        ret |= am_addterm(c, vc, 1.0);
        ret |= am_setrelation(c, AM_EQUAL);
        ret |= am_addterm(c, vd, 1.0);
        ret |= am_add(c);
        assert(ret == AM_OK);
        am_dumpsolver(S);
    }

    /* vd == va */
    {
        am_Constraint *c = am_newconstraint(S, AM_REQUIRED);
        int            ret = 0;
        ret |= am_addterm(c, vd, 1.0);
        ret |= am_setrelation(c, AM_EQUAL);
        ret |= am_addterm(c, va, 1.0);
        ret |= am_add(c);
        assert(ret == AM_OK); /* asserts here */
        am_dumpsolver(S);
    }

    am_delsolver(S);
}

#include "solver_test.gen.inc"
