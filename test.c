#include <stdio.h>

#define AM_BUF_LEN 1 /* touch multiple write code */
#define AM_DEBUG printf
#define AM_IMPLEMENTATION
#include "amoeba.h"

#include <assert.h>
#include <stdlib.h>
#include <setjmp.h>
#include <stdarg.h>

static jmp_buf jbuf;
static size_t allmem = 0;
static size_t maxmem = 0;
static void *END = NULL;

static void *debug_allocf(void **pud, void *ptr, size_t ns, size_t os, am_AllocType ty) {
    void *newptr = NULL;
    (void)pud, (void)ty;
    allmem += ns;
    allmem -= os;
    if (maxmem < allmem) maxmem = allmem;
    if (ns == 0) free(ptr);
    else {
        newptr = realloc(ptr, ns);
        if (newptr == NULL) longjmp(jbuf, 1);
    }
#ifdef DEBUG_MEMORY
    printf("new(%p):\t+%d, old(%p):\t-%d\n", newptr, (int)ns, ptr, (int)os);
#endif
    return newptr;
}

static void am_dumpkey(am_Symbol sym) {
    int ch = 'v';
    switch (sym.type) {
    case AM_EXTERNAL: ch = 'v'; break;
    case AM_SLACK:    ch = 's'; break;
    case AM_ERROR:    ch = 'e'; break;
    case AM_DUMMY:    ch = 'd'; break;
    }
    printf("%c%d", ch, (int)sym.id);
}

static void am_dumprow(const am_Row *row) {
    am_Iterator it = am_itertable(&row->terms);
    printf("%g", row->constant);
    while (am_nextentry(&it)) {
        am_Num *term = am_val(am_Num,it);
        am_Num multiplier = *term;
        printf(" %c ", multiplier > 0.0 ? '+' : '-');
        if (multiplier < 0.0) multiplier = -multiplier;
        if (!am_approx(multiplier, 1.0f))
            printf("%g*", multiplier);
        am_dumpkey(it.key);
    }
    printf("\n");
}

static void am_dumpsolver(am_Solver *S) {
    am_Iterator it = am_itertable(&S->rows);
    int idx = 0;
    printf("-------------------------------\n");
    printf("solver: ");
    am_dumprow(&S->objective);
    printf("rows(%d):\n", S->rows.count);
    while (am_nextentry(&it)) {
        printf("%d. ", ++idx);
        am_dumpkey(it.key);
        printf(" = ");
        am_dumprow(am_val(am_Row,it));
    }
    printf("-------------------------------\n");
}

static am_Constraint* new_constraint(am_Solver* in_solver, double in_strength,
        am_Id in_term1, double in_factor1, int in_relation,
        double in_constant, ...)
{
    int result;
    va_list argp;
    am_Constraint* c;
    assert(in_solver && in_term1);
    c = am_newconstraint(in_solver, (am_Num)in_strength);
    if(!c) return 0;
    am_addterm(c, in_term1, (am_Num)in_factor1);
    am_setrelation(c, in_relation);
    if(in_constant) am_addconstant(c, (am_Num)in_constant);
    va_start(argp, in_constant);
    while(1) {
        am_Id va_term = va_arg(argp, am_Id);
        double va_factor = va_arg(argp, double);
        if(va_term == 0) break;
        am_addterm(c, va_term, (am_Num)va_factor);
    }
    va_end(argp);
    result = am_add(c);
    assert(result == AM_OK);
    return c;
}

static void test_all(void) {
    am_Solver *S;
    am_Id xl, xm, xr, xd;
    am_Num vxl, vxm, vxr, vxd;
    am_Constraint *c1, *c2, *c3, *c4, *c5, *c6;
    am_Constraint *cs[50];
    int i, ret = setjmp(jbuf);
    printf("\n\n==========\ntest all\n");
    printf("ret = %d\n", ret);
    if (ret < 0) { perror("setjmp"); return; }
    else if (ret != 0) { printf("out of memory!\n"); return; }

    S = am_newsolver(NULL, NULL);
    assert(S != NULL);
    for (i = 0; i < 50; ++i) {
        cs[i] = am_newconstraint(S, AM_REQUIRED);
        assert(cs[i] != NULL);
    }
    for (i = 0; i < 50; ++i) 
        am_delconstraint(cs[i]);
    am_delsolver(S);
    assert(allmem == 0);

    S = am_newsolver(debug_allocf, NULL);
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
    assert(am_setstrength(c1, AM_REQUIRED-10) == AM_OK);
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

static void test_binarytree(void) {
    const int NUM_ROWS = 9;
    const int X_OFFSET = 0;
    int nPointsCount, nResult, nRow;
    int nCurrentRowPointsCount = 1;
    int nCurrentRowFirstPointIndex = 0;
    am_Constraint *pC;
    am_Solver *pSolver;
    am_Id *arrX, *arrY;
    am_Num *numX, *numY;

    printf("\n\n==========\ntest binarytree\n");
    arrX = (am_Id*)malloc(2048 * sizeof(am_Id));
    if (arrX == NULL) return;
    arrY = arrX + 1024;

    numX = (am_Num*)malloc(2048 * sizeof(am_Num));
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

    pSolver = am_newsolver(debug_allocf, NULL);

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
                am_addterm(pC, arrX[nCurrentRowFirstPointIndex + nPoint - 1], 1.0);
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
                am_addterm(pC, arrX[nPreviousRowFirstPointIndex + nParentPoint], 1.0);
                am_setrelation(pC, AM_EQUAL);
                am_addterm(pC, arrX[nCurrentRowFirstPointIndex + nPoint], 0.5);
                am_addterm(pC, arrX[nCurrentRowFirstPointIndex + nPoint - 1], 0.5);
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

static void test_unbounded(void) {
    am_Solver *S;
    am_Id x, y;
    am_Num vx, vy;
    am_Constraint *c;
    int ret = setjmp(jbuf);
    printf("\n\n==========\ntest unbounded\n");
    printf("ret = %d\n", ret);
    if (ret < 0) { perror("setjmp"); return; }
    else if (ret != 0) { printf("out of memory!\n"); return; }

    S = am_newsolver(debug_allocf, NULL);
    x = am_newvariable(S, &vx);
    y = am_newvariable(S, &vy);

    /* 10.0 == 0.0 */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addconstant(c, 10.0);
    am_setrelation(c, AM_EQUAL);
    ret = am_add(c);
    printf("ret = %d\n", ret);
    assert(ret == AM_UNSATISFIED);
    am_dumpsolver(S);

    /* 0.0 == 0.0 */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addconstant(c, 0.0);
    am_setrelation(c, AM_EQUAL);
    ret = am_add(c);
    printf("ret = %d\n", ret);
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
    printf("ret = %d\n", ret);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    /* x == 2*y */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, x, 1.0);
    am_setrelation(c, AM_EQUAL);
    am_addterm(c, y, 2.0);
    ret = am_add(c);
    printf("ret = %d\n", ret);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    /* y == 3*x */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, y, 1.0);
    am_setrelation(c, AM_EQUAL);
    am_addterm(c, x, 3.0);
    ret = am_add(c);
    printf("ret = %d\n", ret);
    assert(ret == AM_UNBOUND);
    am_dumpsolver(S);

    am_resetsolver(S);

    /* x >= 10.0 */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, x, 1.0);
    am_setrelation(c, AM_GREATEQUAL);
    am_addconstant(c, 10.0);
    ret = am_add(c);
    printf("ret = %d\n", ret);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    /* x <= 0.0 */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, x, 1.0);
    am_setrelation(c, AM_LESSEQUAL);
    ret = am_add(c);
    printf("ret = %d\n", ret);
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
    printf("ret = %d\n", ret);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    /* x == 20.0 */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, x, 1.0);
    am_setrelation(c, AM_EQUAL);
    am_addconstant(c, 20.0);
    ret = am_add(c);
    printf("ret = %d\n", ret);
    assert(ret == AM_UNSATISFIED);
    am_dumpsolver(S);

    /* x == 10.0 */
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, x, 1.0);
    am_setrelation(c, AM_EQUAL);
    am_addconstant(c, 10.0);
    ret = am_add(c);
    printf("ret = %d\n", ret);
    assert(ret == AM_OK);
    am_dumpsolver(S);

    am_delsolver(S);
    printf("allmem = %d\n", (int)allmem);
    printf("maxmem = %d\n", (int)maxmem);
    assert(allmem == 0);
    maxmem = 0;
}

static void test_strength(void) {
    am_Solver *S;
    am_Id x, y;
    am_Num vx, vy;
    am_Constraint *c;
    int ret = setjmp(jbuf);
    printf("\n\n==========\ntest strength\n");
    printf("ret = %d\n", ret);
    if (ret < 0) { perror("setjmp"); return; }
    else if (ret != 0) { printf("out of memory!\n"); return; }

    S = am_newsolver(debug_allocf, NULL);
    am_autoupdate(S, 1);
    x = am_newvariable(S, &vx);
    y = am_newvariable(S, &vy);

    /* x <= y */
    new_constraint(S, AM_STRONG, x, 1.0, AM_LESSEQUAL, 0.0,
            y, 1.0, END);
    new_constraint(S, AM_MEDIUM, x, 1.0, AM_EQUAL, 50, END);
    c = new_constraint(S, AM_MEDIUM-10, y, 1.0, AM_EQUAL, 40, END);
    printf("%f, %f\n", vx, vy);
    assert(vx == 50);
    assert(vy == 50);

    am_setstrength(c, AM_MEDIUM+10);
    printf("%f, %f\n", vx, vy);
    assert(vx == 40);
    assert(vy == 40);

    am_setstrength(c, AM_MEDIUM-10);
    printf("%f, %f\n", vx, vy);
    assert(vx == 50);
    assert(vy == 50);

    am_delsolver(S);
    printf("allmem = %d\n", (int)allmem);
    printf("maxmem = %d\n", (int)maxmem);
    assert(allmem == 0);
    maxmem = 0;
}

static void test_suggest(void) {
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
    am_Num delta = 0;
    am_Num pos;
    am_Solver *S;
    am_Id splitter_l,     splitter_w,     splitter_r;
    am_Id left_child_l,   left_child_w,   left_child_r;
    am_Id splitter_bar_l, splitter_bar_w, splitter_bar_r;
    am_Id right_child_l,  right_child_w,  right_child_r;
    am_Num vsplitter_l,     vsplitter_w,     vsplitter_r;
    am_Num vleft_child_l,   vleft_child_w,   vleft_child_r;
    am_Num vsplitter_bar_l, vsplitter_bar_w, vsplitter_bar_r;
    am_Num vright_child_l,  vright_child_w,  vright_child_r;
    int ret = setjmp(jbuf);
    printf("\n\n==========\ntest suggest\n");
    printf("ret = %d\n", ret);
    if (ret < 0) { perror("setjmp"); return; }
    else if (ret != 0) { printf("out of memory!\n"); return; }

    S = am_newsolver(debug_allocf, NULL);
    splitter_l     = am_newvariable(S, &vsplitter_l);
    splitter_w     = am_newvariable(S, &vsplitter_w);
    splitter_r     = am_newvariable(S, &vsplitter_r);
    left_child_l   = am_newvariable(S, &vleft_child_l);
    left_child_w   = am_newvariable(S, &vleft_child_w);
    left_child_r   = am_newvariable(S, &vleft_child_r);
    splitter_bar_l = am_newvariable(S, &vsplitter_bar_l);
    splitter_bar_w = am_newvariable(S, &vsplitter_bar_w);
    splitter_bar_r = am_newvariable(S, &vsplitter_bar_r);
    right_child_l  = am_newvariable(S, &vright_child_l);
    right_child_w  = am_newvariable(S, &vright_child_w);
    right_child_r  = am_newvariable(S, &vright_child_r);

    /* splitter_r = splitter_l + splitter_w */
    /* left_child_r = left_child_l + left_child_w */
    /* splitter_bar_r = splitter_bar_l + splitter_bar_w */
    /* right_child_r = right_child_l + right_child_w */
    new_constraint(S, AM_REQUIRED, splitter_r, 1.0, AM_EQUAL, 0.0,
            splitter_l, 1.0, splitter_w, 1.0, END);
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
    new_constraint(S, AM_REQUIRED, splitter_bar_l, 1.0, AM_GREATEQUAL,
            delta, splitter_l, 1.0, END);
    new_constraint(S, AM_REQUIRED, splitter_bar_r, 1.0, AM_LESSEQUAL,
            -delta, splitter_r, 1.0, END);
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

    printf("\n\n==========\ntest suggest\n");
    for(pos = -10; pos < 86; pos++) {
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

static void test_dirty(void) {
    am_Solver *S;
    am_Id xl, xr, xw, xwc;
    am_Num vxl, vxr, vxw, vxwc;
    am_Constraint *c1, *c2;
    int ret = setjmp(jbuf);
    printf("\n\n==========\ntest dirty\n");
    printf("ret = %d\n", ret);
    if (ret < 0) { perror("setjmp"); return; }
    else if (ret != 0) { printf("out of memory!\n"); return; }

    S = am_newsolver(debug_allocf, NULL);

    xl  = am_newvariable(S, &vxl);
    xr  = am_newvariable(S, &vxr);
    xw  = am_newvariable(S, &vxw);
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

void test_cycling(void) {
    am_Solver *S;
    am_Id va, vb, vc, vd;
    am_Num vva, vvb, vvc, vvd;

    int ret = setjmp(jbuf);
    printf("\n\n==========\ntest cycling\n");
    printf("ret = %d\n", ret);
    if (ret < 0) { perror("setjmp"); return; }
    else if (ret != 0) { printf("out of memory!\n"); return; }

    S = am_newsolver(debug_allocf, NULL);
    va = am_newvariable(S, &vva);
    vb = am_newvariable(S, &vvb);
    vc = am_newvariable(S, &vvc);
    vd = am_newvariable(S, &vvd);

    am_addedit(S, va, AM_STRONG);
    printf("after edit\n");
    am_dumpsolver(S);

    /* vb == va */
    {
        am_Constraint * c = am_newconstraint(S, AM_REQUIRED);
        int ret = 0;
        ret |= am_addterm(c, vb, 1.0);
        ret |= am_setrelation(c, AM_EQUAL);
        ret |= am_addterm(c, va, 1.0);
        ret |= am_add(c);
        assert(ret == AM_OK);
        am_dumpsolver(S);
    }

    /* vb == vc */
    {
        am_Constraint * c = am_newconstraint(S, AM_REQUIRED);
        int ret = 0;
        ret |= am_addterm(c, vb, 1.0);
        ret |= am_setrelation(c, AM_EQUAL);
        ret |= am_addterm(c, vc, 1.0);
        ret |= am_add(c);
        assert(ret == AM_OK);
        am_dumpsolver(S);
    }

    /* vc == vd */
    {
        am_Constraint * c = am_newconstraint(S, AM_REQUIRED);
        int ret = 0;
        ret |= am_addterm(c, vc, 1.0);
        ret |= am_setrelation(c, AM_EQUAL);
        ret |= am_addterm(c, vd, 1.0);
        ret |= am_add(c);
        assert(ret == AM_OK);
        am_dumpsolver(S);
    }

    /* vd == va */
    {
        am_Constraint * c = am_newconstraint(S, AM_REQUIRED);
        int ret = 0;
        ret |= am_addterm(c, vd, 1.0);
        ret |= am_setrelation(c, AM_EQUAL);
        ret |= am_addterm(c, va, 1.0);
        ret |= am_add(c);
        assert(ret == AM_OK); /* asserts here */
        am_dumpsolver(S);
    }

    am_delsolver(S);
}

#define TEST_BUFSIZE 16384

typedef struct MyDumper {
    am_Dumper base;
    char *p;
    size_t *len;
} MyDumper;

static const char *dump_varname(am_Dumper *d, unsigned idx, am_Id var, am_Num *value) {
    const char *names[] = { "width", "height", "a", "" };
    (void)d, (void)var, (void)value;
    return idx < 4 ? names[idx] : NULL;
}

static const char *dump_consname(am_Dumper *d, unsigned idx, am_Constraint *cons) {
    const char *name = "a-very-long-name-that-excceed-32-bytes";
    (void)d, (void)idx, (void)cons;
    return idx == 0 ? name : NULL;
}

static int dump_writer(am_Dumper *d, const void *buf, size_t len) {
    MyDumper *md = (MyDumper*)d;
    assert(*md->len + len <= TEST_BUFSIZE);
    if (*md->len + len > TEST_BUFSIZE) return AM_FAILED;
    memcpy(md->p + *md->len, buf, len);
    *md->len += len;
    return AM_OK;
}

typedef struct MyLoader {
    am_Loader base;
    am_Num values[37];
    const char *p;
    size_t len;
} MyLoader;

static am_Num *load_var(am_Loader *l, const char *name, unsigned i, am_Id var) {
    MyLoader *ml = (MyLoader*)l;
    return (void)name, (void)var, &ml->values[i];
}

void load_cons(am_Loader *l, const char *name, unsigned i, am_Constraint *cons)
{ (void)l, (void)name, (void)i, (void)cons; }

const char *load_reader(am_Loader *l, size_t *plen) {
    MyLoader *ml = (MyLoader*)l;
    /* reads from char by char */
    if (ml->len == 0) return NULL;
    ml->len -= 1;
    return *plen = 1, ml->p++;
}

static void build_solver(am_Solver* S, am_Id width, am_Id height, am_Num *values) {
    /* Create custom strength */
    am_Num mmedium = AM_MEDIUM * 1.25;
    am_Num smedium = AM_MEDIUM * 100;

    /* Create the variable */
    am_Id left            = am_newvariable(S, &values[0]);
    am_Id top             = am_newvariable(S, &values[1]);
    am_Id contents_top    = am_newvariable(S, &values[2]);
    am_Id contents_bottom = am_newvariable(S, &values[3]);
    am_Id contents_left   = am_newvariable(S, &values[4]);
    am_Id contents_right  = am_newvariable(S, &values[5]);
    am_Id midline         = am_newvariable(S, &values[6]);
    am_Id ctleft          = am_newvariable(S, &values[7]);
    am_Id ctheight        = am_newvariable(S, &values[8]);
    am_Id cttop           = am_newvariable(S, &values[9]);
    am_Id ctwidth         = am_newvariable(S, &values[10]);
    am_Id lb1left         = am_newvariable(S, &values[11]);
    am_Id lb1height       = am_newvariable(S, &values[12]);
    am_Id lb1top          = am_newvariable(S, &values[13]);
    am_Id lb1width        = am_newvariable(S, &values[14]);
    am_Id lb2left         = am_newvariable(S, &values[15]);
    am_Id lb2height       = am_newvariable(S, &values[16]);
    am_Id lb2top          = am_newvariable(S, &values[17]);
    am_Id lb2width        = am_newvariable(S, &values[18]);
    am_Id lb3left         = am_newvariable(S, &values[19]);
    am_Id lb3height       = am_newvariable(S, &values[20]);
    am_Id lb3top          = am_newvariable(S, &values[21]);
    am_Id lb3width        = am_newvariable(S, &values[22]);
    am_Id fl1left         = am_newvariable(S, &values[23]);
    am_Id fl1height       = am_newvariable(S, &values[24]);
    am_Id fl1top          = am_newvariable(S, &values[25]);
    am_Id fl1width        = am_newvariable(S, &values[26]);
    am_Id fl2left         = am_newvariable(S, &values[27]);
    am_Id fl2height       = am_newvariable(S, &values[28]);
    am_Id fl2top          = am_newvariable(S, &values[29]);
    am_Id fl2width        = am_newvariable(S, &values[30]);
    am_Id fl3left         = am_newvariable(S, &values[31]);
    am_Id fl3height       = am_newvariable(S, &values[32]);
    am_Id fl3top          = am_newvariable(S, &values[33]);
    am_Id fl3width        = am_newvariable(S, &values[34]);

#ifdef __GNUC__
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmissing-field-initializers"
# pragma GCC diagnostic ignored "-Wc99-extensions"
#endif

    /* Add the constraints */
    const struct Info {
        struct Item {
            am_Id  var;
            am_Num mul;
        } term[5];
        am_Num constant;
        int    relation;
        am_Num strength;
    } constraints[] = {
        { {{left}},                                                -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{height}},                                              0,            AM_EQUAL,      AM_MEDIUM   },
        { {{top}},                                                 -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{width}},                                               -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{height}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{top,-1},{contents_top}},                               -10,          AM_EQUAL,      AM_REQUIRED },
        { {{lb3height}},                                           -16,          AM_EQUAL,      AM_STRONG   },
        { {{lb3height}},                                           -16,          AM_GREATEQUAL, AM_STRONG   },
        { {{ctleft}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{cttop}},                                               -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{ctwidth}},                                             -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{ctheight}},                                            -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl3left}},                                             0,            AM_GREATEQUAL, AM_REQUIRED },
        { {{ctheight}},                                            -24,          AM_GREATEQUAL, smedium     },
        { {{ctwidth}},                                             -1.67772e+07, AM_LESSEQUAL,  smedium     },
        { {{ctheight}},                                            -24,          AM_LESSEQUAL,  smedium     },
        { {{fl3top}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl3width}},                                            -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl3height}},                                           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1width}},                                            -67,          AM_EQUAL,      AM_WEAK     },
        { {{lb2width}},                                            -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2height}},                                           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl2height}},                                           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb3left}},                                             -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl2width}},                                            -125,         AM_GREATEQUAL, AM_STRONG   },
        { {{fl2height}},                                           -21,          AM_EQUAL,      AM_STRONG   },
        { {{fl2height}},                                           -21,          AM_GREATEQUAL, AM_STRONG   },
        { {{lb3top}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb3width}},                                            -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1left}},                                             -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl1width}},                                            -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1width}},                                            -67,          AM_GREATEQUAL, AM_STRONG   },
        { {{fl2left}},                                             -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2width}},                                            -66,          AM_EQUAL,      AM_WEAK     },
        { {{lb2width}},                                            -66,          AM_GREATEQUAL, AM_STRONG   },
        { {{lb2height}},                                           -16,          AM_EQUAL,      AM_STRONG   },
        { {{fl1height}},                                           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl1top}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2top}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2top,-1},{lb3top},{lb2height,-1}},                   -10,          AM_EQUAL,      mmedium     },
        { {{lb3top,-1},{lb3height,-1},{fl3top}},                   -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{lb3top,-1},{lb3height,-1},{fl3top}},                   -10,          AM_EQUAL,      mmedium     },
        { {{contents_bottom},{fl3height,-1},{fl3top,-1}},          -0,           AM_EQUAL,      mmedium     },
        { {{fl1top},{contents_top,-1}},                            0,            AM_GREATEQUAL, AM_REQUIRED },
        { {{fl1top},{contents_top,-1}},                            0,            AM_EQUAL,      mmedium     },
        { {{contents_bottom},{fl3height,-1},{fl3top,-1}},          -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{left,-1},{width,-1},{contents_right}},                 10,           AM_EQUAL,      AM_REQUIRED },
        { {{top,-1},{height,-1},{contents_bottom}},                10,           AM_EQUAL,      AM_REQUIRED },
        { {{left,-1},{contents_left}},                             -10,          AM_EQUAL,      AM_REQUIRED },
        { {{lb3left},{contents_left,-1}},                          0,            AM_EQUAL,      mmedium     },
        { {{fl1left},{midline,-1}},                                0,            AM_EQUAL,      AM_STRONG   },
        { {{fl2left},{midline,-1}},                                0,            AM_EQUAL,      AM_STRONG   },
        { {{ctleft},{midline,-1}},                                 0,            AM_EQUAL,      AM_STRONG   },
        { {{fl1top},{fl1height,0.5},{lb1top,-1},{lb1height,-0.5}}, 0,            AM_EQUAL,      AM_STRONG   },
        { {{lb1left},{contents_left,-1}},                          0,            AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1left},{contents_left,-1}},                          0,            AM_EQUAL,      mmedium     },
        { {{lb1left,-1},{fl1left},{lb1width,-1}},                  -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1left,-1},{fl1left},{lb1width,-1}},                  -10,          AM_EQUAL,      mmedium     },
        { {{fl1left,-1},{contents_right},{fl1width,-1}},           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{width}},                                               0,            AM_EQUAL,      AM_MEDIUM   },
        { {{fl1top,-1},{fl2top},{fl1height,-1}},                   -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{fl1top,-1},{fl2top},{fl1height,-1}},                   -10,          AM_EQUAL,      mmedium     },
        { {{cttop},{fl2top,-1},{fl2height,-1}},                    -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{ctheight,-1},{cttop,-1},{fl3top}},                     -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{contents_bottom},{fl3height,-1},{fl3top,-1}},          -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{cttop},{fl2top,-1},{fl2height,-1}},                    -10,          AM_EQUAL,      mmedium     },
        { {{fl1left,-1},{contents_right},{fl1width,-1}},           -0,           AM_EQUAL,      mmedium     },
        { {{lb2top,-1},{lb2height,-0.5},{fl2top},{fl2height,0.5}}, 0,            AM_EQUAL,      AM_STRONG   },
        { {{contents_left,-1},{lb2left}},                          0,            AM_GREATEQUAL, AM_REQUIRED },
        { {{contents_left,-1},{lb2left}},                          0,            AM_EQUAL,      mmedium     },
        { {{fl2left},{lb2width,-1},{lb2left,-1}},                  -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{ctheight,-1},{cttop,-1},{fl3top}},                     -10,          AM_EQUAL,      mmedium     },
        { {{contents_bottom},{fl3height,-1},{fl3top,-1}},          -0,           AM_EQUAL,      mmedium     },
        { {{lb1top}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1width}},                                            -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1height}},                                           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl2left},{lb2width,-1},{lb2left,-1}},                  -10,          AM_EQUAL,      mmedium     },
        { {{fl2left,-1},{fl2width,-1},{contents_right}},           -0,           AM_EQUAL,      mmedium     },
        { {{fl2left,-1},{fl2width,-1},{contents_right}},           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb3left},{contents_left,-1}},                          0,            AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1left}},                                             -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{ctheight,0.5},{cttop},{lb3top,-1},{lb3height,-0.5}},   0,            AM_EQUAL,      AM_STRONG   },
        { {{ctleft},{lb3left,-1},{lb3width,-1}},                   -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{ctwidth,-1},{ctleft,-1},{contents_right}},             -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{ctleft},{lb3left,-1},{lb3width,-1}},                   -10,          AM_EQUAL,      mmedium     },
        { {{fl3left},{contents_left,-1}},                          0,            AM_GREATEQUAL, AM_REQUIRED },
        { {{fl3left},{contents_left,-1}},                          0,            AM_EQUAL,      mmedium     },
        { {{ctwidth,-1},{ctleft,-1},{contents_right}},             -0,           AM_EQUAL,      mmedium     },
        { {{fl3left,-1},{contents_right},{fl3width,-1}},           -0,           AM_EQUAL,      mmedium     },
        { {{contents_top,-1},{lb1top}},                            0,            AM_GREATEQUAL, AM_REQUIRED },
        { {{contents_top,-1},{lb1top}},                            0,            AM_EQUAL,      mmedium     },
        { {{fl3left,-1},{contents_right},{fl3width,-1}},           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2top},{lb1top,-1},{lb1height,-1}},                   -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2top,-1},{lb3top},{lb2height,-1}},                   -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2top},{lb1top,-1},{lb1height,-1}},                   -10,          AM_EQUAL,      mmedium     },
        { {{fl1height}},                                           -21,          AM_EQUAL,      AM_STRONG   },
        { {{fl1height}},                                           -21,          AM_GREATEQUAL, AM_STRONG   },
        { {{lb2left}},                                             -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2height}},                                           -16,          AM_GREATEQUAL, AM_STRONG   },
        { {{fl2top}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl2width}},                                            -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1height}},                                           -16,          AM_GREATEQUAL, AM_STRONG   },
        { {{lb1height}},                                           -16,          AM_EQUAL,      AM_STRONG   },
        { {{fl3width}},                                            -125,         AM_GREATEQUAL, AM_STRONG   },
        { {{fl3height}},                                           -21,          AM_EQUAL,      AM_STRONG   },
        { {{fl3height}},                                           -21,          AM_GREATEQUAL, AM_STRONG   },
        { {{lb3height}},                                           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{ctwidth}},                                             -119,         AM_GREATEQUAL, smedium     },
        { {{lb3width}},                                            -24,          AM_EQUAL,      AM_WEAK     },
        { {{lb3width}},                                            -24,          AM_GREATEQUAL, AM_STRONG   },
        { {{fl1width}},                                            -125,         AM_GREATEQUAL, AM_STRONG   },
    };

#ifdef __GNUC__
# pragma GCC diagnostic pop
#endif

    size_t i;
    int ret;

    /* Add the edit variables */
    ret = am_addedit(S, width, AM_STRONG);
    assert(ret == AM_OK), (void)ret;
    ret = am_addedit(S, height, AM_STRONG);
    assert(ret == AM_OK), (void)ret;

    for (i = 0; i < sizeof(constraints)/sizeof(constraints[0]); ++i) {
        am_Constraint *c = am_newconstraint(S, constraints[i].strength);
        const struct Item *p;
        for (p = constraints[i].term; p->var; ++p) {
            ret = am_addterm(c, p->var, p->mul ? p->mul : 1);
            assert(ret == AM_OK), (void)ret;
        }
        ret = am_addconstant(c, constraints[i].constant);
        assert(ret == AM_OK), (void)ret;
        ret = am_setrelation(c, constraints[i].relation);
        assert(ret == AM_OK), (void)ret;
        ret = am_add(c);
        assert(ret == AM_OK), (void)ret;
    }
}

static void test_dumpload(void) {
    am_Num w, h, values[35];
    am_Solver *S;
    am_Id width, height;
    char buf[TEST_BUFSIZE];
    size_t len = 0;
    int ret = setjmp(jbuf);
    printf("\n\n==========\ntest dumpload\n");
    printf("ret = %d\n", ret);
    if (ret < 0) { perror("setjmp"); return; }
    else if (ret != 0) { printf("out of memory!\n"); return; }

    S = am_newsolver(debug_allocf, NULL);
    assert(S != NULL);

    width = am_newvariable(S, &w);
    height = am_newvariable(S, &h);

    build_solver(S, width, height, values);

    {
        /* dump converage */
        MyDumper d;
        am_DumpCtx ctx;
        char buf[10];
        size_t len = 0;
        d.base.writer = dump_writer;
        d.p = buf;
        d.len = &len;
        ctx.dumper = &d.base;
        ctx.p = ctx.buf;

        assert(am_writeraw(NULL, 0, 0) == AM_FAILED);
        assert(am_writeuint32(&ctx, 0xFFFFFFFF) == AM_OK);
        ctx.ret = dump_writer(&d.base, ctx.buf, (ctx.p - ctx.buf));
        assert(len == 5);
        assert(memcmp(buf, "\xCE\xFF\xFF\xFF\xFF", 5) == 0);

        len = 0;
        ctx.p = ctx.buf;
        assert(am_writecount(&ctx, 0xFFFFFFFF) == AM_OK);
        ctx.ret = dump_writer(&d.base, ctx.buf, (ctx.p - ctx.buf));
        assert(len == 5);
        assert(memcmp(buf, "\xDD\xFF\xFF\xFF\xFF", 5) == 0);
    }

    {
        /* load coverage */
        am_LoadCtx ctx;
        am_Size value;
        am_Num flt;

        assert(am_readraw(NULL, NULL, 0) == AM_FAILED);
        ctx.p = "\xCE\xFF\xFF\xFF\xFF", ctx.n = 5;
        assert(am_readuint32(&ctx, &value) == AM_OK);
        assert(value == 0xFFFFFFFF && ctx.n == 0);

        ctx.p = "\xCA\x0\x0\x0\x0", ctx.n = 5;
        assert(am_readfloat32(&ctx, &flt) == AM_OK);
        assert(flt == 0.f && ctx.n == 0);

        ctx.p = "\xCC\x0\x0\x0\x0", ctx.n = 5;
        assert(am_readfloat32(&ctx, &flt) == AM_FAILED);
        assert(ctx.n == 4);

        ctx.p = "\xDD\xFF\xFF\xFF\xFF", ctx.n = 5;
        assert(am_readcount(&ctx, &value) == AM_OK);
        assert(value == 0xFFFFFFFF && ctx.n == 0);
    }

    {
        MyDumper myd;
        myd.base.var_name = dump_varname;
        myd.base.cons_name = dump_consname;
        myd.base.writer = dump_writer;
        myd.p = buf, myd.len = &len;

        printf("before dump\n");
        ret = am_dump(S, &myd.base);
        printf("after dump: ret=%d\n", ret);
        assert(ret == AM_OK);
        printf("dumpped len=%d\n", (int)len);
        assert(len == 16045);
    }

    {
        FILE *fp = fopen("dump.data", "wb");
        fwrite(buf, 1, len, fp);
        fclose(fp);
    }

    {
        MyLoader myl;
        am_Solver *S = am_newsolver(debug_allocf, NULL);
        assert(S != NULL);

        myl.base.load_var  = load_var;
        myl.base.load_cons = load_cons;
        myl.base.reader = load_reader;
        myl.p =buf, myl.len = len;

        printf("before load\n");
        ret = am_load(S, &myl.base);
        assert(ret == AM_OK);

        am_delsolver(S);
    }
    
    am_delsolver(S);
    printf("allmem = %d\n", (int)allmem);
    printf("maxmem = %d\n", (int)maxmem);
    assert(allmem == 0);
    maxmem = 0;
}

int main(void) {
    test_binarytree();
    test_unbounded();
    test_strength();
    test_suggest();
    test_cycling();
    test_dirty();
    test_dumpload();
    test_all();
    printf("constraint: %d\n", (int)(sizeof(am_Constraint)));
    printf("table: %d\n", (int)(sizeof(am_Table)));
    printf("row: %d\n", (int)(sizeof(am_Row)));
    return 0;
}

/* cc: flags='-ggdb -Wall -fprofile-arcs -ftest-coverage -O0 -Wextra -pedantic -std=c89'
 * cc: run='!rm -f *.gcda; $executable $args' */

