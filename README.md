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
- or you can manually specify a new value to variable with `am_suggest()`.
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
    am_setrelation(c2, AM_GREATEREQUAL);
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


## Building and testing

The library itself needs nothing but a C compiler:

```c
#define AM_IMPLEMENTATION
#include "amoeba.h"
```

The repository is driven by [just](https://github.com/casey/just):

```sh
just check           # every suite, ASan/UBSan, one process per test
just check-release   # same suites at -O2
just check-double    # AM_USE_DOUBLE
just check-c89       # strict C89 compile check (no run)
just check-cpp03     # strict C++03 compile check (no run)
just test dump       # one suite; just test dump dumpload one test
just fuzz            # random-op fuzz, one process per seed
just cov             # lcov report
just format          # clang-format the tree
```

Test sources live in `tests/`; `misc/gen_entries.lua` turns `TEST(name)`
functions into the entry table (so a Lua interpreter is a build dependency of
the tests).  Code style is defined by `.clang-format` (clang-format >= 19).
See [notes/](notes/) for implementation notes and [TODO.md](TODO.md) for the
roadmap.

## Lua binding

`lua_amoeba.c` embeds the library into Lua.  The binding's contract -- supported
runtimes (LuaJIT 2.1 / LÖVE 11.x and Lua 5.5), lifecycle and handle validity,
names, `dump` side effects and every method -- is documented in
[amoeba.d.lua](amoeba.d.lua) (LuaLS type declarations, English).  It also covers
how to load the C module when `amoeba.lua` shadows it.

`just test-lua`, `just test-lua-luajit` and `just test-lua-double` build the
module and run the assertion suite `test.lua` against Lua 5.x, LuaJIT 2.1 and
the `AM_USE_DOUBLE` build.

## Reference

### Return value

All functions below that returns `int` are returning error codes:
- `AM_OK`: Operations success.
- `AM_FAILED`: The operation is failed: the solver rejected a valid request
  (for example, adding a constraint that is already in the solver).
- `AM_PARAM`: A parameter check failed (a null pointer, a zero variable id, a
  negative strength, an invalid relation, or constraints from different
  solvers).
- `AM_UNSATISFIED`: Specific constraints added cannot satisfied.
- `AM_UNBOUND`: The constraint conflicts with the constraints already in the
  solver (the artificial variable could not be driven to zero). It is the same
  kind of failure as `AM_UNSATISFIED`, reached through the inequality path.

### Dump & Load

Amoeba can store the whole state of a solver in binary data and load it back
into a new solver (`am_load()`).

`am_dump()` drops every suggest/edit (`am_clearedits()`): what gets dumped is the
state after re-solving without them.  `am_load()` creates a clean solver and
returns it, or `NULL` when the stream cannot be loaded.  A successful load
writes every variable binding from the loaded tableau: a basic variable gets its
row constant, a variable without a row gets 0.  `load_var()` still decides where
each binding lives.

The loader can see the new solver before the body is parsed: if
`am_Loader::solver` is set, it is called once, right after the stream header is
validated and before any variable or constraint is read.  The solver is empty at
that point and is passed as `const am_Solver *`: keep the pointer if you need
it, but do not build objects on it.  If the load fails afterwards, the solver is
destroyed, so that pointer must be dropped and must never be freed.

Only streams produced by `am_dump()` are supported.  Loading any other data is
unsupported: the loader only promises that `am_load()` itself does not crash.
What a solver does after a successful load of a non-dump stream (including
asserts or crashes in later calls) is outside the contract.  The loader performs
structural checks, not tableau-semantic validation.

`am_load()` spends less than `maxmem` bytes while loading (`0` selects
`AM_MAX_LOADMEM`, 16 MiB by default; define `AM_MAX_LOADMEM` before including
`amoeba.h` to change it).  The budget is strict: a single allocation whose
growth equals the remaining budget fails.  It bounds the bytes requested from
the allocator, not the process's actual memory use.  The budget is released
when loading succeeds.

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

  A user-defined load behavior control type.  Zero-initialize it (for example
  with `memset`) before setting the callbacks: `reader` and `load_var` are
  required, `solver` and `load_cons` are optional and are skipped when left
  null.  There is an example:

  ```c
  /* loads from a string buffer */
  typedef struct MyLoader {
      am_Loader base;
      VarMap    vars;
      ConsMap   cons;
      const am_Solver *solver; /* set by load_solver */
      const char *buf; /* the buffer to read */
      size_t remain;   /* remain bytes */
  } MyLoader;

  static void load_solver(am_Loader *l, const am_Solver *solver) {
      /* optional: the solver exists, but is still empty and read-only */
      ((MyLoader*)l)->solver = solver;
  }

  static am_Num *load_var(am_Loader *l, const char *name, unsigned idx, am_Id var) {
      /* you should returns a binding for variable named `name`, or the load
       * fails and `am_load()` returns `NULL`.
       * A common implement creates variable context into some map, returns it.
       * Notice that `name` can be `NULL` if no name avaialable, and that it is
       * only valid during this call: copy it if you keep it.  */
      MyLoader *myl = (MyLoader*)l;
      MyContext *ctx = my_create_ctx_by_idx_and_name(&myl->vars, idx, name);
      return &ctx->value;
  }

  static void load_cons(am_Loader *l, const char *name, unsigned idx, am_Constraint *cons) {
      /* same as `var_name`, but can be NULL in `am_Loader*` means do not
       * retrieve any constraints from previous state.
       * Notice that `name` can be `NULL` and is only valid during the call.  */
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
  memset(&l, 0, sizeof(l)); /* optional callbacks must start null */
  l.base.solver    = load_solver; /* optional */
  l.base.load_var  = load_var;
  l.base.load_cons = load_cons;   /* optional */
  l.base.reader    = load_reader;
  d.buf = get_some_data(&d.len);

  am_Solver *solver = am_load(NULL, NULL, &l.base, 0);
  assert(solver != NULL);
  /* now uses the solver */
  ```

  `am_load()` returns `NULL` on failure and releases the half-built solver; any
  pointer kept from `solver`, `load_var` or `load_cons` is dead then.

### Routines

- `am_Solver *am_newsolver(am_Allocf *allocf, void *ud);`

  Create a new solver with custom memory alloculator, pass NULL for use default ones.

- `void am_resetsolver(am_Solver *solver);`

  Remove all constraints from solver, all variables and constraints created are keeped.
  The tableau is dropped, so no variable has a row any more, but the value bindings
  are **not** refreshed: `*pvalue` keeps whatever value it had before the reset.
  The caller owns them from then on; `am_updatevars()` only consumes the dirty
  list, it does not re-settle the whole tableau.

- `void am_delsolver(am_Solver *solver);`

  Delete a solver and frees all memory it used, after that, all variables/constraints created from this solver are all freed.  Passing `NULL` does nothing.

- `void am_updatevars(am_Solver *solver);`

  Refresh variables' value into it's constrainted value.
  Only variables whose value may have changed since the last refresh are queued,
  and only queued bindings are written.  `am_add()` and a successful
  `am_setstrength()` queue everything their solve moved (see `am_add()` below);
  `am_remove()` writes a leaving binding immediately, and `am_load()` settles
  every binding itself.  `am_resetsolver()` queues nothing.
  You could use `am_autoupdate()` to update all variables automatically when solver changes.
  A `NULL` solver does nothing.

- `void am_autoupdate(am_Solver *solver, int auto_update);`

  Set the auto update flags.
  If setted, all variables will be updated to its latest values right after any changes done to the `solver`.

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
  MyVarContext *ctx = (MyVarContext*)am_varvalue(solver, var, NULL);
  ```

- `void am_delvariable(am_Solver *solver, am_Id var);`

  Remove this variable, after that, it cannot be added into further constraints.

- `am_Num *am_varvalue(am_Solver *solver, am_Id var, am_Num *newvalue);`

  Retrieve or reset the value binding of the variable. if `newvalue` is `NULL`,
  only retrieve the old pointer, otherwise the binding is retargeted to
  `newvalue` and the original pointer is returned.

- `int am_refcount(const am_Solver *solver, am_Id var);`

  Retrieve the internal reference count of the variable: one for the variable
  itself, plus one per live term.  It is what decides when an unreferenced
  variable is dropped; it is not a user-facing ownership count.

- `am_Id am_vars(const am_Solver *solver, am_Id var);`

  Walk the variables the solver holds.  Pass `0` for the first one, then the
  previous result; `0` means "no more".  The order is the solver's internal
  slot order, not creation order, and any change to the solver may reshuffle
  it, so start over instead of keeping an anchor across changes.

- `int am_addedit(am_Solver *solver, am_Id var, am_Num strength);`

  Prepare to change the value of variables or the `strength` value if the variable is in edit now.

- `void am_deledit(am_Solver *solver, am_Id var);`

  Cancel the modify of variable.
  The value binding by `var` will restore to the referred value according the solver.

- `void am_suggest(am_Solver *solver, am_Id var, am_Num value);`

  Actually change the value of the variable `var`.
  A variable id the solver does not know is ignored.

  After changed, other variable may changed due to the constraints in solver.
  If you do not want change the strength of suggest (default is `AM_MEDIUM`), you may call `am_suggest()` directly without `am_addedit()`.

- `int am_hasedit(const am_Solver *solver, am_Id var);`

  Check whether a variable has suggested value in the `solver`.

- `void am_clearedits(am_Solver *solver);`

  Remove all variable suggests from `solver`.

- `am_Constraint *am_newconstraint(am_Solver *solver, am_Num strength);`

  Create a new constraints. Notice that where are one pointer's space can be
  used to store context data in the head of `am_Constraint`, example:

  ```c
  am_Constraint *cons = am_newconstraint(solver, AM_REQUIRED);
  MyContext *ctx = new_my_context();
  ctx->mydata = 1;
  *(MyContext**)cons = ctx;
  ```

- `void am_resetconstraint(am_Constraint *cons);`

  Remove all terms and variables from the constraint, restore it to blank state.

- `void am_delconstraint(am_Constraint *cons);`

  Free the constraint. if it's added into solver, remove it first.

  This is free semantics: the constraint is recycled, its expression released
  and its variable references dropped.  `cons` is dead when the call returns --
  using it again (including passing it a second time) is undefined behaviour and
  is not detected.  The default allocator puts the slot back into the constraint
  pool, so a second call may still read plausible memory: that is reuse, not a
  promise.

- `am_Constraint *am_cloneconstraint(am_Constraint *other, am_Num strength);`

  Clone a new constraint from the existing one with new `strength`.
  Returns `NULL` when the new constraint cannot be allocated.

- `am_Constraint *am_constraints(const am_Solver *solver, am_Constraint *cons);`

  Walk the constraints registered in `solver` the same way: `NULL` starts the
  walk, `NULL` ends it.  A constraint is registered by `am_newconstraint()`
  and stays registered until `am_delconstraint()`, so walking does not require
  adding it.  The hidden constraints an edit creates for `am_suggest()` are
  not returned.  The anchor must be a constraint of this solver.

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
  - `AM_GREATEREQUAL`
  - `AM_LESSEQUAL`

  The terms added before `am_setrelation()` become the left hand terms of constraints, and the terms adds after call will become the right hand terms of constraints.

- `int am_addconstant(am_Constraint *cons, am_Num constant);`

  Add a constant without variable into constraint as a term.  A constant the
  solver treats as numerical zero is stored as `0` (below `AM_NUM_EPS`: 1e-4
  in the default `float` build, 1e-6 with `AM_USE_DOUBLE`); `am_constant()`
  reports the stored value.

- `int am_setstrength(am_Constraint *cons, am_Num strength);`

  Set the strength of a constraint.

- `int am_merge(am_Constraint *cons, const am_Constraint *other, am_Num coef);`

  Merge other constraints into `cons`, with a coefficient multiplying `other`.

- `int am_add(am_Constraint *cons);`

  Add constraint into the solver it's created from.  A successful add publishes
  the solution it computed: with auto update on the bindings are current when the
  call returns, with it off the next `am_updatevars()` writes them.  A failed add
  changes no binding.

- `void am_remove(am_Constraint *cons);`

  Remove added constraint.

- `int am_hasconstraint(const am_Constraint *cons);`

  Check whether a constraint has been added into the solver or not.

- `am_Id am_terms(const am_Constraint *cons, am_Id var, am_Num *coef);`

  Walk the terms of the constraint's expression: pass `0` for the first term,
  then the previous result; `0` means "no more".  `*coef` receives the
  coefficient as stored, which for a left-hand side written before
  `am_setrelation()` is the negated value (see `am_setrelation()`).  Works
  before the constraint is added to the solver.

- `int am_relation(const am_Constraint *cons);`

  The relation set by `am_setrelation()`, or `0` when none is set yet.

- `am_Num am_constant(const am_Constraint *cons);`

  The constant of the constraint's expression, in the same stored form
  `am_terms()` uses (see `am_setrelation()`): a constant written after the
  relation keeps its sign, one written before it is negated with the terms.
  A value within the solver's epsilon of zero (see `am_addconstant()`) reads
  back as `0`.

- `am_Num am_strength(const am_Constraint *cons);`

  The strength of the constraint.  It stays an `am_Num` so `AM_REQUIRED`
  (`+inf`) survives the round trip; `NULL` returns `0`.

- `am_Num am_error(const am_Constraint *cons);`

  How much the constraint is violated in the current solution, in the units of
  its stored expression: the sum of the constraint's error variables.  A
  required constraint has none and reports `0`; an inequality counts only the
  part it exceeds; a soft equality reports the absolute residual.  The value
  is read from the tableau, so it is current without `am_updatevars()`, and it
  survives `am_dump()`/`am_load()`.  `NULL` and a constraint that is not in
  the solver return `0`.  The strength is not applied — multiply by
  `am_strength()` yourself if you need the objective contribution.

- `int am_dump(am_Solver *solver, am_Dumper *d);`

  Dump whole solver state (without edits) into binary data.

  Returns `AM_FAILED` when the state holds a symbol that the dump has no index
  for.  `am_load()` accepts such a stream (it validates structure, not symbol
  ownership), so a solver loaded from a hand-edited stream can reach that
  shape; the bytes written up to the failure must be discarded.

- `am_Solver *am_load(am_Allocf *allocf, void *ud, am_Loader *l, size_t maxmem);`

  Creates a new solver, loads a dump stream into it and returns it, or `NULL`
  when the stream cannot be loaded.  `allocf`/`ud` are passed to
  `am_newsolver()`.  `maxmem` is the strict byte budget of the load (`0` =
  `AM_MAX_LOADMEM`); a single allocation may not consume the whole remaining
  budget.  Only streams produced by `am_dump()` are supported, and variable
  bindings are not refreshed yet (see "Dump & Load").

