# Amoeba -- the constraint solving algorithm in pure C

[![Build Status](https://img.shields.io/github/actions/workflow/status/starwing/amoeba/test.yml?branch=master)](https://github.com/starwing/amoeba/actions?query=branch%3Amaster)[![Coverage Status](https://img.shields.io/coveralls/github/starwing/amoeba)](https://coveralls.io/github/starwing/amoeba?branch=master)

Amoeba is a pure C implement of Cassowary algorithm. It:
- Uses Clean C, which is the cross set of ANSI C89 and C++, like the Lua language.
- Is a single-file library. For more single-file library, see the stb project [here][1].
- Largely impressed by [kiwi][2], the C++ implement of Cassowary algorithm, and the algorithm [paper][3].
- With a Lua binding.
- With the same license as the [Lua language][4].

[1]: https://github.com/nothings/stb
[2]: https://github.com/nucleic/kiwi
[3]: http://constraints.cs.washington.edu/solvers/uist97.html
[4]: https://www.lua.org/license.html

Improvement relative to Kiwi:
- Single header library that easy to distribution.
- Lua binding vs. Python binding.
- Suggest cache makes suggest performance much better than kiwi.
- Dump/load support improves the performance of building solver.

## How To Use

This libary export a constraint solver interface, to solve a constraint problem, you should use it in steps:

- create a new solver: `am_newsolver()`
- create some variables: `am_newvariable()` binding the `am_Num` values.
- create some constraints that may use variables: `am_newconstraint()`.
- make constraints by construct equation using:
  - `am_addterm()` add a $a \times variable$ term to constraint equation items.
  - `am_setrelation()` to specify the equal/greater/less sign in center of equation.
  - `am_addconstant()` to add a number without variable.
  - `am_setstrength()` to specify the priority of this constraint in all constraints.
- after make up a constraint, you could add it into solver by `am_add()`.
- and you can read out the result of each variable from the binding `am_Num`.
- or you can manually specify a new value to variable with `am_sugguest()`.
- after done, use `am_delsolver()` to free al memory.

below is a tiny example to demonstrate the steps:

```c
#define AM_IMPLEMENTATION // include implementations of library
#include "amoeba.h"       // and interface

int main(void) {
    // first, create a solver:
    am_Solver *S = am_newsolver(NULL, NULL);
    int ret;

    // create some variable:
    am_Num l, m, r;
    am_Id vl = am_newvariable(S, &l);
    am_Id vm = am_newvariable(S, &m);
    am_Id vr = am_newvariable(S, &r);

    // create the constraint: 
    am_Constraint *c1 = am_newconstraint(S, AM_REQUIRED);
    am_Constraint *c2 = am_newconstraint(S, AM_REQUIRED);

    // c1: m is in middle of l and r:
    //     i.e. m = (l + r) / 2, or 2*m = l + r
    am_addterm(c1, vm, 2.f);
    am_setrelation(c1, AM_EQUAL);
    am_addterm(c1, vl, 1.f);
    am_addterm(c1, vr, 1.f);
    // apply c1
    ret = am_add(c1);
    assert(ret == AM_OK);

    // c2: r - l >= 100
    am_addterm(c2, vr, 1.f);
    am_addterm(c2, vl, -1.f);
    am_setrelation(c2, AM_GREATEQUAL);
    am_addconstant(c2, 100.f);
    // apply c2
    ret = am_add(c2);
    assert(ret == AM_OK);

    // now we set variable l to 20
    am_suggest(S, vl, 20.f);

    // and see the value of m and r:
    am_updatevars(S);

    // r should by 20 + 100 == 120:
    assert(r == 120.f);

    // and m should in middle of l and r:
    assert(m == 70.f);
    
    // done with solver
    am_delsolver(S);
    return 0;
}
```


## Reference

### Return value

All functions below that returns `int` are returning error codes:
- `AM_OK`: Operations success.
- `AM_FAILED`: The operation is failed, most caused by invalid arguments.
- `AM_UNSATISFIED`: Specific constraints added cannot satisfied.
- `AM_UNBOUND`: Failed to add constraints because there are unbound variables in it

### Dump & Load

Amoeba supports to store whole state of solver into bianry data and to load
back in new solver. Notice that the state of loaded solver will be earsed. 

To perform a dump or load operations, you should prepare `am_Dumper` or
`am_Loader` to control the behavior in operations.

- `struct am_Dumper`

  A user-defined dump behavior control type.  The user must provide `reader`
  and `var_name` function pointers in it. there is a example:

  ```c
  /* dumps into a string buffer */
  typedef struct MyDumper {
      am_Dumper base;
      char buf[MY_BUF_SIZE]; /* buffer */
      size_t len;            /* and length */
  } MyDumper;

  static const char *dump_varname(am_Dumper *d, unsigned idx, am_Id var, am_Num *value) {
      /* you should returns a var name here, or NULL if you do not want a name
       * for this variable.
       * A common implement retrives variable context from value pointer, and
       * get name from it.  */
      MyContext *ctx = (MyContext*)value;
      return ctx->name;
  }

  static const char *dump_consname(am_Dumper *d, unsigned idx, am_Constraint *cons) {
      /* Same as `var_name`, but retrieve constraint's name */
      MyContext *ctx = *(MyContext**)cons;
      return ctx->name;
  }

  static int dump_writer(am_Dumper *d, const void *buf, size_t len) {
      /* Write the actually data */
      MyDumper *myd = (MyDumper*)d;
      if (myd->len + len > MY_BUF_SIZE)
          return AM_FAILED; /* data out of buffer, errors returned */
      memcpy(myd->buf + myd->len, buf, len);
      myd->len += len;
      /* All return value other than `AM_OK` will be returned directly by
       * `am_dump()`. */
      return AM_OK;
  }

  /* do actually dump */
  MyDumper d;
  d.base.var_name  = dump_varname;
  d.base.cons_name = dump_consname;
  d.base.writer    = dump_writer;
  d.len = 0;

  int ret = am_dump(solver, &d.base);
  assert(ret == AM_OK);
  /* now uses the d.buf & d.len */
  ```

- `struct am_Loader`

  A user-defined dump behavior control type.  The user must provide `reader`
  and `load_var` function pointers in it. there is a example:

  ```c
  /* loads from a string buffer */
  typedef struct MyLoader {
      am_Loader base;
      VarMap    vars;
      ConsMap   cons;
      const char *buf; /* the buffer to read */
      size_t remain;   /* remain bytes */
  } MyLoader;

  static am_Num *load_var(am_Loader *l, const char *name, unsigned idx, am_Id var) {
      /* you should returns a binding for variable named `name`, or `AM_FAILED`
       * will be returned by `am_load()`.
       * A common implement creates variable context into some map, returns it.
       * Notice that `name` can be `NULL` if no name avaialable.  */
      MyLoader *myl = (MyLoader*)l;
      MyContext *ctx = my_create_ctx_by_idx_and_name(&myl->vars, idx, name);
      return &ctx->value;
  }

  static void load_cons(am_Loader *l, const char *name, unsigned idx, am_Constraint *cons) {
      /* same as `var_name`, but can be NULL in `am_Loader*` means do not
       * retrieve any constraints from previous state.
       * Notice that `name` can be `NULL`.  */
      MyLoader *myl = (MyLoader*)l;
      MyContext *ctx = save_cons_and_create_context(&myl->cons, idx, name, cons);
      *(MyContext**)cons = ctx;
  }

  static const char *load_reader(am_Loader *l, size_t *plen) {
      /* Get the data to reads, if returns `NULL` or store `0` into `plen`,
       * stops reading */
      MyLoader *myl = (MyLoader*)l;
      const char *buf = myl->buf;
      *plen = myl->len;
      if (buf) {
          my->buf = NULL;
          my->len = 0;
      }
      return buf;
  }

  /* do actually load */
  MyLoader l;
  l.base.load_var  = load_var;
  l.base.load_cons = load_cons; /* optional */
  l.base.reader    = load_reader;
  d.buf = get_some_data(&d.len);

  am_Solver *solver = am_newsolver(NULL, NULL);
  int ret = am_load(solver, &d.base);
  assert(ret == AM_OK);
  /* now uses the solver */
  ```

### Routines

- `am_Solver *am_newsolver(am_Allocf *allocf, void *ud);`

  Create a new solver with custom memory alloculator, pass NULL for use default ones.

- `void am_resetsolver(am_Solver *solver);`

  Remove all constraints from solver, all variables and constraints created are keeped.

- `void am_delsolver(am_Solver *solver);`

  Delete a solver and frees all memory it used, after that, all variables/constraints created from this solver are all freed.

- `void am_updatevars(am_Solver *solver);`

  Refresh variables' value into it's constrainted value.
  You could use `am_autoupdate()` to update all variables automatically when solver changes.

- `void am_autoupdate(am_Solver *solver, int auto_update);`

  Set the auto update flags.
  If setted, all variables will be updated to its latest values right after any changes done to the `solver`.

- `int am_dump(am_Solver *solver, am_Dumper *d);`

  Dump whole solver state (without edits) into binary data.

- `int am_load(am_Solver *solver, am_Loader *l);`

  Loads binary data back into solver. Note that all previous states are
  discarded, but previous vars & constraints are keeped. i.e. the
  `am_resetsolver()` called before loaded.

- `am_Id am_newvariable(am_Solver *solver, am_Num *pvalue);`

  Create a new variable with a value `pvalue` binding to it. Returns `0` on
  error.

  Notice that the pvalue pointer can be retrived in `am_dump()` or
  `am_varvalue()`, so you could put context data here:

  ```c
  typedef struct MyVarContext {
      am_Num value;
      const char *name;
      /* etc.. */
  } MyVarContext;
  MyVarContext *ctx = new_context();
  am_Id var = am_newvariable(solver, &ctx->value);
  /* retrive the context: */
  MyVarContext *ctx = (MyVarContext*)am_varvalue(var, NULL);
  ```

- `void am_delvariable(am_Solver *solver, am_Id var);`

  Remove this variable, after that, it cannot be added into further constraints.

- `void am_refcount(am_Solver *solver, am_Id var);`

  Retrieve the refcount of the variable.

- `void am_varvalue(am_Solver *solver, am_Id var, am_Num *newvalue);`

  Retrieve or reset the value binding of the variable. if `newvalue` is `NULL`,
  Only retrieve the old value, otherwise the `newvalue` will be setted into
  `var` and original value returned.

- `int am_add(am_Constraint *cons);`

  Add constraint into the solver it's created from.

- `int am_hasconstraint(am_Constraint *cons);`

  Check whether a constraint has been added into the solver or not.

- `void am_remove(am_Constraint *cons);`

  Remove added constraint.

- `int am_clearedits(am_Solver *solver);`

  Remove all variable suggests from `solver`.

- `int am_hasedit(am_Solver *solver, am_Id var);`

  Check whether a variable has suggested value in the `solver`.

- `int am_addedit(am_Solver *solver, am_Id var, am_Num strength);`

  Prepare to change the value of variables or the `strength` value if the variable is in edit now.

- `void am_suggest(am_Solver *solver, am_Id var, am_Num value);`

  Actually change the value of the variable `var`.

  After changed, other variable may changed due to the constraints in solver.
  If you do not want change the strength of suggest (default is `AM_MEDIUM`), you may call `am_suggest()` directly without `am_addedit()`.

- `void am_deledit(am_Solver *solver, am_Id var);`

  Cancel the modify of variable.
  The value binding by `var` will restore to the referred value according the solver.

- `am_Constraint *am_newconstraint(am_Solver *solver, am_Num strength);`

  Create a new constraints. Notice that where are one pointer's space can be
  used to store context data in the head of `am_Constraint`, example:

  ```c
  am_Constraint *cons = am_newconstraint(solver, AM_REQUIRED);
  MyContext *ctx = new_my_context();
  ctx->mydata = 1;
  *(MyContext**)cons = ctx;
  ```

- `am_Constraint *am_cloneconstraint(am_Constraint *other, am_Num strength);`

  Clone a new constraint from the existing one with new `strength`.

- `void am_resetconstraint(am_Constraint *cons);`

  Remove all terms and variables from the constraint, restore it to blank state.

- `void am_delconstraint(am_Constraint *cons);`

  Free the constraint. if it's added into solver, remove it first.

- `int am_addterm(am_Constraint *cons, am_Id var, am_Num multiplier);`

  Add a term into constraint.

  Example: a constraint like `2*m == x+y`, has terms `2*m`, `1*x` and `1*y`.
  So to makeup this constraint, you could:

  ```c
  // 2*m == x + y
  am_addterm(c, m, 2.0); // 2*m
  am_setrelation(c, AM_EQUAL); // =
  am_addterm(c, x, 1.0); // x
  am_addterm(c, y, 1.0); // y
  ```

- `int am_setrelation(am_Constraint *cons, int relation);`

  Set the relations of constraint, could be one of these:

  - `AM_EQUAL`
  - `AM_GREATEQUAL`
  - `AM_LESSEQUAL`

  The terms added before `am_setrelation()` become the left hand terms of constraints, and the terms adds after call will become the right hand terms of constraints.

- `int am_addconstant(am_Constraint *cons, am_Num constant);`

  Add a constant without variable into constraint as a term.

- `int am_setstrength(am_Constraint *cons, am_Num strength);`

  Set the strength of a constraint.

- `int am_mergeconstraint(am_Constraint *cons, const am_Constraint *other, am_Num multiplier);`

  Merge other constraints into `cons`, with a multiplier multiples with `other`.

