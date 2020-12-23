# Amoeba -- the constraint solving algorithm in pure C

[![Build Status](https://travis-ci.org/starwing/amoeba.svg?branch=master)](https://travis-ci.org/starwing/amoeba)
[![Coverage Status](https://coveralls.io/repos/github/starwing/amoeba/badge.svg?branch=master)](https://coveralls.io/github/starwing/amoeba?branch=master)

Amoeba is a pure C implement of Cassowary algorithm.
Amoeba use Clean C, which is the cross set of ANSI C89 and C++, like
the Lua language.

Amoeba is a single-file library, for more single-file library, see the
stb project [here][1].

Amoeba largely impressed by [kiwi][2], the C++ implement of Cassowary
algorithm, and the algorithm [paper][3].

Amoeba ships a hand written Lua binding.

Amoeba has the same license with the [Lua language][4].

[1]: https://github.com/nothings/stb
[2]: https://github.com/nucleic/kiwi
[3]: http://constraints.cs.washington.edu/solvers/uist97.html
[4]: https://www.lua.org/license.html

## How To Use

This libary export a constraint solver interface, to solve a constraint problem, you should use it in steps:

- create a new solver: `am_newsolver()`
- create some variables: `am_newvariable()`
- create some constraints that may use variables: `am_newconstraint()`
- make constraints by construct equation using:
  - `am_addterm()` add a $a \times variable$ term to constraint equation items
  - `am_setrelation()` to specify the equal/greater/less sign in center of equation
  - `am_addconstant()` to add a number without variable
  - `am_setstrength()` to specify the priority of this constraint in all constraints
- after make up a constraint, you could add it into solver by `am_add()`
- and you can read out the result of each variable with `am_value()`
- or you can manually specify a new value to variable with `am_sugguest()`
- after done, use `am_delsolver()` to free al memory

below is a tiny example to demonstrate the steps:

```c
#define AM_IMPLEMENTATION // include implementations of library
#include "amoeba.h"       // and interface

int main(void)
{
    // first, create a solver:
    am_Solver *S = am_newsolver(NULL, NULL);

    // create some variable:
    am_Var *l = am_newvariable(S);
    am_Var *m = am_newvariable(S);
    am_Var *r = am_newvariable(S);

    // create the constraint: 
    am_Constraint *c1 = am_newconstraint(S, AM_REQUIRED);
    am_Constraint *c2 = am_newconstraint(S, AM_REQUIRED);

    // c1: m is in middle of l and r:
    //     i.e. m = (l + r) / 2, or 2*m = l + r
    am_addterm(c1, m, 2.f);
    am_setrelation(c1, AM_EQUAL);
    am_addterm(c1, l, 1.f);
    am_addterm(c1, r, 1.f);
    // apply c1
    am_add(c1);

    // c2: r - l >= 100
    am_addterm(c2, r, 1.f);
    am_addterm(c2, l, -1.f);
    am_setrelation(c2, AM_GREATEQUAL);
    am_addconstant(c2, 100.f);
    // apply c2
    am_add(c2);

    // now we set variable l to 20
    am_suggest(l, 20.f);

    // and see the value of m and r:
    am_updatevars(S);

    // r should by 20 + 100 == 120:
    assert(am_value(r) == 120.f);

    // and m should in middle of l and r:
    assert(am_value(m) == 70.f);
    
    // done with solver
    am_delsolver(S);
    return 0;
}
```



## Reference

All functions below that returns `int` may return error codes:

- `AM_OK`: the operations success.
- `AM_FAILED`: the operation fail
- `AM_UNSATISFIED`: can not add specific constraints into solver
- `AM_UNBOUND`: add specific constraints failed because variables in constraints unbound

Routines:

- `am_Solver *am_newsolver(am_Allocf *allocf, void *ud);`

  create a new solver with custom memory alloculator, pass NULL for use default ones.

- `void am_resetsolver(am_Solver *solver, int clear_constraints);`

  remove all variable suggests from solver.

  if `clear_constraints` is nonzero, also remove and delete all constraints from solver.

- `void am_delsolver(am_Solver *solver);`

  delete a solver and frees all memory it used, after that, all variables/constraints created from this solver are all freed.

- `void am_updatevars(am_Solver *solver);`

  refresh variables' value into it's constrainted value, you could use `am_autoupdate()` to avoid call this routine every time on changing constraints in solver.

- `void am_autoupdate(am_Solver *solver, int auto_update);`

  set auto update flags, if set, all variable will auto update to its' latest value after any changes to solver.

- `int am_hasedit(am_Var *var);`

  check whether a variable has suggested value in solver.

- `int am_hasconstraint(am_Constraint *cons);`

  check whether a constraint has been added into solver.

- `int am_add(am_Constraint *cons);`

  add constraint into solver it's created from.

- `void am_remove(am_Constraint *cons);`

  remove added constraint.

- `int am_addedit(am_Var *var, am_Num strength);`

  prepare to change the value of variables or the `strength` value if the variable is in edit now.

- `void am_suggest(am_Var *var, am_Num value);`

  actually change the value of the variable `var`, after changed, other variable may changed due to the constraints in solver. if you do not want change the strength of suggest (default is `AM_MEDIUM`), you may call this routine directly.

- `void am_deledit(am_Var *var);`

  cancel the modify of variable, the value will restore to the referred value according the solver.

- `am_Var *am_newvariable(am_Solver *solver);`

  create a new variable. variable is reference counting since it may used in serveral constraints,

  so if you want store it in multiple place, call `am_usevariable()` before. 

- `void am_usevariable(am_Var *var);`

  add the reference counting of a variable to avoid it been freed.

- `void am_delvariable(am_Var *var);`

  sub the reference counting of a variable, and free it when the count down to 0.

- `int am_variableid(am_Var *var);`

  return a unqiue id (within solver) of the variable `var`.

- `am_Num am_value(am_Var *var);`

  fetch the current value of variable `var`, note that if auto update not set and `am_updatevars()` not called, the value may not the latest values that inferred by solver.

- `am_Constraint *am_newconstraint(am_Solver *solver, am_Num strength);`

  create a new constraints.

- `am_Constraint *am_cloneconstraint(am_Constraint *other, am_Num strength);`

  make a new constraints from existing one, with new `strength`

- `void am_resetconstraint(am_Constraint *cons);`

  remove all terms and variables from the constraint.

- `void am_delconstraint(am_Constraint *cons);`

  frees the constraint. if it's added into solver, remove it first.

- `int am_addterm(am_Constraint *cons, am_Var *var, am_Num multiplier);`

  add a term into constraint, e.g. a constraint like $2*m = x + y$, the terms are $2*m$, $1*x$ and $1*y$.

  so makeup this constraint you could:

  ```c
  am_addterm(c, m, 2.0); // 2*m
  am_setrelation(c, AM_EQUAL); // =
  am_addterm(c, x, 1.0); // x
  am_addterm(c, y, 1.0); // y
  ```

- `int am_setrelation(am_Constraint *cons, int relation);`

  set the relations of constraint, could be one of these:

  - `AM_EQUAL`
  - `AM_GREATEQUAL`
  - `AM_LESSEQUAL`

  the terms added before `am_setrelation()` become the left hand terms of constraints, and the terms adds after call will become the right hand terms of constraints.

- `int am_addconstant(am_Constraint *cons, am_Num constant);`

  add a constant without variable into constraint as a term.

- `int am_setstrength(am_Constraint *cons, am_Num strength);`

  set the strength of a constraint.

- `am_mergeconstraint(am_Constraint *cons, const am_Constraint *other, am_Num multiplier);`

  merge other constraints into `cons`, with a multiplier multiples with `other`.

