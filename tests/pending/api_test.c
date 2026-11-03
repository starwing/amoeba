/* tests/pending/api_test.c -- 排期快照（不参与构建）
 *
 * 这里只剩 TODO #6 的两条用例；#5 的 arena 用例已随修复搬回 live
 * （tests/api_test.c 的 load_accepts_correct_arena / load_rejects_short_arena，
 * 改成真 dump + 只改 arena 字段）。#2、#7 更早已经搬回。
 *
 * 这个文件不在 tests/justfile 的 SUITES 里，也不要直接覆盖 live 测试。
 */

#include "amoeba_test.h"

/* am_addterm() and am_merge() must keep the variable reference
 * count in step with the terms that actually end up in the expression: the
 * count is what am_updatevars() uses to decide a variable has no row left.
 * Merging two terms into one, or cancelling a term out, used to leak a
 * reference per call, which kept am_Var entries alive forever. */
TEST(addterm_refcount_balances) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *c = am_newconstraint(S, AM_EQUAL);
    int            base = am_refcount(S, vx);

    asserteq(base, 1); /* the variable itself */

    am_addterm(c, vx, 1.f); /* new term */
    asserteq(am_refcount(S, vx), base + 1);

    am_addterm(c, vx, 2.f); /* merged into the same term */
    asserteq(am_refcount(S, vx), base + 1);

    am_addterm(c, vx, -3.f); /* 1 + 2 - 3 == 0: the term disappears */
    asserteq(am_refcount(S, vx), base);

    am_delconstraint(c);
    am_delsolver(S);
}

/* am_merge() uses the same bookkeeping as am_addterm(): merging a
 * term the target already has must not add a second reference, and a merge
 * that cancels the term out must release one. */
TEST(mergeconstraint_refcount_balances) {
    am_Solver     *S = am_test_new_solver();
    am_Num         x = 0;
    am_Id          vx = am_newvariable(S, &x);
    am_Constraint *src = am_newconstraint(S, AM_EQUAL);
    am_Constraint *dst = am_newconstraint(S, AM_EQUAL);
    int            base = am_refcount(S, vx);

    am_addterm(src, vx, 1.f); /* src: x */
    asserteq(am_refcount(S, vx), base + 1);

    asserteq(am_merge(dst, src, 2.f), AM_OK); /* dst: 2x */
    asserteq(am_refcount(S, vx), base + 2);

    asserteq(am_merge(dst, src, 3.f), AM_OK); /* dst: 5x */
    asserteq(am_refcount(S, vx), base + 2);

    asserteq(am_merge(dst, src, -5.f), AM_OK); /* cancels out */
    asserteq(am_refcount(S, vx), base + 1);

    am_delconstraint(dst);
    am_delconstraint(src);
    am_delsolver(S);
}

#include "api_test.gen.inc"
