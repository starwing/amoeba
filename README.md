# Amoeba -- the constraint solving algorithm in pure C

English | [中文](README.zh.md)

[![CI](https://github.com/starwing/amoeba/actions/workflows/test.yml/badge.svg?branch=master)](https://github.com/starwing/amoeba/actions/workflows/test.yml)
[![Coverage Status](https://coveralls.io/repos/github/starwing/amoeba/badge.svg?branch=master)](https://coveralls.io/github/starwing/amoeba?branch=master)

Amoeba is a pure-C implementation of the Cassowary algorithm. It:
- Is written in Clean C, the common subset of ANSI C89 and C++ that Lua also
  uses.
- Is a single-file library. For more single-file libraries, see the stb
  project [here][1].
- Is largely inspired by [kiwi][2], the C++ implementation of the Cassowary
  algorithm, and by the algorithm [paper][3].
- Has a Lua binding.
- Uses the same license as the [Lua language][4].

[1]: https://github.com/nothings/stb
[2]: https://github.com/nucleic/kiwi
[3]: http://constraints.cs.washington.edu/solvers/uist97.html
[4]: https://www.lua.org/license.html

## Why Amoeba

Amoeba is a Cassowary auto-layout engine that is faster than Facebook Yoga where
a GUI actually spends its time: a warm round of edits is ~12x faster, and
restoring a dumped solver and applying the first round is ~2.2x faster than
rebuilding the Yoga tree and laying it out.  As a Cassowary engine, it is far
more flexible than flexbox: any set of linear constraints can drive the layout.

The benchmark scene is the DeepSeek Harness window (272 boxes, 1104
constraints), measured with `just bench` and `just bench-yoga` at `-O2` on an
Apple M4:

| operation | Amoeba | Yoga | |
|---|---:|---:|---|
| cold build | 2.2 ms | 83 us | 26x slower |
| build + first edit | 1.9 ms | 130 us | 15x slower |
| warm edit (9 suggestions) | 4.0 us | 47 us | **12x faster** |
| load a dumped solver | 22 us | 83 us (build) | **3.8x faster** |
| load + first edit | 59 us | 130 us (build + first edit) | **2.2x faster** |

Cassowary pays for constraint compilation once, at build time: that one-time
cost is why the cold build is slower.  The design turns it into a cache.
`am_dump()` writes the solved tableau (135 KB here) in 44 us; `am_load()`
restores it in 22 us.  The first call that needs the term tables materializes
them in one pass with zero allocations, so restoring and applying the first
edit is faster than Yoga's build + first edit.

A suggest would otherwise scan every row to find the ones that contain the
edited variable.  Each edit keeps a cache of exactly those rows, so a warm
suggest walks only them (4.0 us for nine edits instead of 47 us).  Dump writes
that cache as a hint; load validates and fills it, so the first suggest after
load does not rescan either.

Yoga still wins the one-time cold build (83 us vs 2.2 ms).  The win starts
once a layout is built, which is the common case for a window that resizes,
scrolls and edits.  Numbers drift with the machine; the shape is the point.

Improvement relative to Kiwi:
- A single-header library that is easy to distribute.
- A Lua binding, where kiwi offers Python.
- Dumping and loading the whole solver state.
- A per-edit suggest cache and a zero-allocation wake (see above).

## How To Use

The library exposes a constraint solver interface.  To solve a constraint
problem, work through these steps:

- Create a solver: `am_newsolver()`.
- Create variables: `am_newvariable()` binds an `am_Num` value to each one.
- Create constraints: `am_newconstraint()`.
- Build a constraint's equation with:
  - `am_addterm()`: adds an $a \times variable$ term.
  - `am_setrelation()`: sets the relation (`=`, `>=` or `<=`) in the middle
    of the equation.
  - `am_addconstant()`: adds a constant term.
  - `am_setstrength()`: sets the constraint's priority among all constraints.
- Once the constraint is built, add it to the solver with `am_add()`.
- Read each variable's result from its bound `am_Num`.
- Set a new value for a variable with `am_suggest()` when needed.
- Free everything with `am_delsolver()` when done.

A tiny example:

```c
/* include the implementation and the interface */
#define AM_IMPLEMENTATION
#include "amoeba.h"

int main(void) {
    /* create a solver */
    am_Solver *S = am_newsolver(NULL, NULL);
    int ret;

    /* create variables */
    am_Num l, m, r;
    am_Id vl = am_newvariable(S, &l);
    am_Id vm = am_newvariable(S, &m);
    am_Id vr = am_newvariable(S, &r);

    /* create constraints */
    am_Constraint *c1 = am_newconstraint(S, AM_REQUIRED);
    am_Constraint *c2 = am_newconstraint(S, AM_REQUIRED);

    /* c1: m is halfway between l and r, i.e. 2*m = l + r */
    am_addterm(c1, vm, 2.f);
    am_setrelation(c1, AM_EQUAL);
    am_addterm(c1, vl, 1.f);
    am_addterm(c1, vr, 1.f);
    /* add c1 */
    ret = am_add(c1);
    assert(ret == AM_OK);

    /* c2: r - l >= 100 */
    am_addterm(c2, vr, 1.f);
    am_addterm(c2, vl, -1.f);
    am_setrelation(c2, AM_GREATEREQUAL);
    am_addconstant(c2, 100.f);
    /* add c2 */
    ret = am_add(c2);
    assert(ret == AM_OK);

    /* set l to 20 */
    am_suggest(S, vl, 20.f);

    /* read back m and r */
    am_updatevars(S);

    /* r should be 20 + 100 == 120 */
    assert(r == 120.f);

    /* m should be halfway between l and r */
    assert(m == 70.f);

    /* done with the solver */
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
just test            # every suite, ASan/UBSan, one process per test
just test-release    # same suites at -O2
just test-double     # AM_USE_DOUBLE
just test-c89        # strict C89 compile check (no run)
just test-cpp03      # strict C++03 compile check (no run)
just test dump       # one suite; just test dump dumpload one test
just fuzz            # random-op fuzz, one process per seed
just cov             # lcov report
just format          # clang-format the tree
```

Test sources live in `tests/`; `misc/gen_entries.lua` turns `TEST(name)`
functions into the entry table (so a Lua interpreter is a build dependency of
the tests).  Code style is defined by `.clang-format` (clang-format >= 19,
pinned to 23.1.5 in CI).  See [TODO.md](TODO.md) for the roadmap.

## Benchmarks

`just bench` runs a GUI layout scene in pure C; `just bench-yoga` builds the
same tree with [Yoga](https://github.com/facebook/yoga) (point `YOGA_SRC` at a
Yoga source tree, default `~/Work/Sources/yoga`) for comparison.  The scene is
the DeepSeek Harness window: 272 text/container boxes and 1104 constraints,
driven by six resize edits (window width/height, both sidecar widths, the
transcript text insets) and three scroll edits for the two-layer lists.  Text
sizes are intrinsic inputs, so both engines measure layout, not shaping.

The Yoga port needs 288 nodes (a few flex spacers are structural).  The
comparison is in [Why Amoeba](#why-amoeba), measured on an Apple Silicon
machine with `-O2` on 2026-11-23; numbers drift with the machine, the shape is
the point.

`just layout 2748 1263` dumps every box as `kind x y w h clip name`;
`misc/render_layout.py` renders that dump as a PNG and can overlay the
reference screenshot.

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

Every function below that returns `int` returns an error code:
- `AM_OK`: The operation succeeded.
- `AM_FAILED`: The operation failed: the solver rejected a valid request
  (for example, adding a constraint that is already in the solver).
- `AM_PARAM`: A parameter check failed (a null pointer, a zero variable id, a
  negative strength, an invalid relation, or constraints from different
  solvers).
- `AM_UNSATISFIED`: The constraints being added cannot be satisfied.
- `AM_UNBOUND`: The constraint conflicts with the constraints already in the
  solver (the artificial variable could not be driven to zero). It is the same
  kind of failure as `AM_UNSATISFIED`, reached through the inequality path.

### Dump & Load

Amoeba can store the whole state of a solver in binary data and load it back
into a new solver (`am_load()`).

`am_dump()` writes the whole solver state, including every suggest/edit: the
edit's variable, strength and value are in the stream.  `am_load()` creates a
clean solver and returns it, or `NULL` when the stream cannot be loaded.  The
edits are rebuilt with their hidden equality constraints, so `am_hasedit()` is
true immediately and `am_suggest()` continues from the dumped value.  Bindings
are restored from the stream too: every variable gets its dumped value, and
`load_var()` decides where that value lives.

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
structural checks and does not validate tableau semantics.

`am_load()` spends less than `maxmem` bytes while loading (`0` selects
`AM_MAX_LOADMEM`, 16 MiB by default; define `AM_MAX_LOADMEM` before including
`amoeba.h` to change it).  The budget is strict: a single allocation whose
growth equals the remaining budget fails.  It bounds the bytes requested from
the allocator; the process's memory use can be higher.  The budget is released
when loading succeeds.

To dump or load, prepare an `am_Dumper` or an `am_Loader` to control the
operation.

- `struct am_Dumper`

  A user-defined dump behavior.  `writer` and `var_name` are required;
  `cons_name` is optional.  For example:

  ```c
  /* dump into a fixed-size buffer */
  typedef struct MyDumper {
      am_Dumper base;
      char buf[MY_BUF_SIZE];
      size_t len;
  } MyDumper;

  static int my_writer(am_Dumper *d, const void *buf, size_t len) {
      MyDumper *w = (MyDumper*)d;
      if (len > MY_BUF_SIZE - w->len) return AM_FAILED;
      memcpy(w->buf + w->len, buf, len);
      w->len += len;
      return AM_OK;
  }

  static const char *my_varname(am_Dumper *d, unsigned i, am_Id v, am_Num *value) {
      (void)d, (void)i, (void)v;
      return ((MyContext*)value)->name; /* MyContext wraps the bound am_Num */
  }

  MyDumper w;
  memset(&w, 0, sizeof(w));
  w.base.writer   = my_writer;
  w.base.var_name = my_varname;  /* required */
  /* w.base.cons_name = my_consname;  optional */

  int ret = am_dump(solver, &w.base);
  /* now use w.buf and w.len */
  ```

- `struct am_Loader`

  A user-defined load behavior.  Zero-initialize it (for example with
  `memset`) before setting the callbacks: `reader` and `load_var` are
  required; `solver` and `load_cons` are optional and skipped when null.
  For example:

  ```c
  /* load from a memory block */
  typedef struct MyLoader {
      am_Loader base;
      const char *buf;
      size_t len;
  } MyLoader;

  static const char *my_reader(am_Loader *l, size_t *plen) {
      MyLoader *r = (MyLoader*)l;
      const char *p = r->buf;
      *plen = r->len;
      r->buf = NULL, r->len = 0; /* hand the data out once */
      return p;
  }

  static am_Num *my_loadvar(am_Loader *l, const char *name, unsigned i, am_Id v) {
      (void)l, (void)i;
      return &my_var(name, v)->value; /* name may be NULL; copy it if kept */
  }

  MyLoader r;
  memset(&r, 0, sizeof(r));
  r.base.reader   = my_reader;
  r.base.load_var = my_loadvar;  /* required */
  /* r.base.solver = my_solver; r.base.load_cons = my_loadcons;  optional */
  r.buf = data, r.len = size;

  am_Solver *solver = am_load(NULL, NULL, &r.base, 0);
  /* now use solver */
  ```

  `am_load()` returns `NULL` on failure and releases the half-built solver; any
  pointer kept from `solver`, `load_var` or `load_cons` is dead then.

### Routines

- `am_Solver *am_newsolver(am_Allocf *allocf, void *ud);`

  Create a solver with a custom allocator; pass NULL to use the default.

- `void am_resetsolver(am_Solver *solver);`

  Remove all constraints from the solver; the variables and constraints
  themselves are kept.
  The tableau is dropped, so no variable has a row any more, but the value bindings
  are **not** refreshed: `*pvalue` keeps whatever value it had before the reset.
  The caller owns them from then on; `am_updatevars()` only consumes the dirty
  list, it does not re-settle the whole tableau.

- `void am_delsolver(am_Solver *solver);`

  Delete a solver and free all memory it used; every variable and constraint
  created from it is freed.  Passing `NULL` does nothing.

- `void am_updatevars(am_Solver *solver);`

  Refresh each variable's value to what the solver computed.
  Only variables whose value may have changed since the last refresh are queued,
  and only queued bindings are written.  `am_add()` and a successful
  `am_setstrength()` queue everything their solve moved (see `am_add()` below);
  `am_remove()` writes a leaving binding immediately, and `am_load()` writes
  every binding itself.  `am_resetsolver()` queues nothing.
  You can use `am_autoupdate()` to update all variables automatically whenever
  the solver changes.
  A `NULL` solver does nothing.

- `void am_autoupdate(am_Solver *solver, int auto_update);`

  Set the auto update flags.
  When set, all variables are updated to their latest values right after any
  change to the `solver`.

- `am_Id am_newvariable(am_Solver *solver, am_Num *pvalue);`

  Create a variable bound to `pvalue`.  Returns `0` on error.

  The `pvalue` pointer comes back from `am_dump()` or `am_varvalue()`, so it
  can anchor context data:

  ```c
  typedef struct MyVarContext {
      am_Num value;
      const char *name;
      /* etc.. */
  } MyVarContext;
  MyVarContext *ctx = new_context();
  am_Id var = am_newvariable(solver, &ctx->value);
  /* retrieve the context: */
  MyVarContext *ctx = (MyVarContext*)am_varvalue(solver, var, NULL);
  ```

- `void am_delvariable(am_Solver *solver, am_Id var);`

  Remove the variable; it cannot be used in later constraints.

- `am_Num *am_varvalue(am_Solver *solver, am_Id var, am_Num *newvalue);`

  Retrieve or reset the value binding of the variable.  If `newvalue` is
  `NULL`, only retrieve the old pointer; otherwise the binding is retargeted
  to `newvalue` and the original pointer is returned.

- `int am_refcount(const am_Solver *solver, am_Id var);`

  Retrieve the internal reference count of the variable: one for the variable
  itself, plus one per live term.  It decides when an unreferenced variable is
  dropped; it does not express ownership.

- `am_Id am_vars(const am_Solver *solver, am_Id var);`

  Walk the variables the solver holds.  Pass `0` for the first one, then the
  previous result; `0` means "no more".  The order follows internal solver
  slots, which may differ from creation order, and any change to the solver may
  reshuffle it, so start over after a change; anchors do not stay valid.

- `int am_addedit(am_Solver *solver, am_Id var, am_Num strength);`

  Prepare to change a variable's value, or its `strength` when it is already in
  edit.

- `void am_deledit(am_Solver *solver, am_Id var);`

  Cancel the variable's edit.
  The binding of `var` is restored to the value the solver gives it.

- `void am_suggest(am_Solver *solver, am_Id var, am_Num value);`

  Change the value of the variable `var`.
  A variable id the solver does not know is ignored.

  Other variables may change afterwards because of the solver's constraints.
  If the suggest strength should stay at its default (`AM_MEDIUM`), call
  `am_suggest()` directly without `am_addedit()`.

- `int am_hasedit(const am_Solver *solver, am_Id var);`

  Check whether a variable has a suggested value in the `solver`.

- `void am_clearedits(am_Solver *solver);`

  Remove all variable suggests from `solver`.

- `am_Constraint *am_newconstraint(am_Solver *solver, am_Num strength);`

  Create a new constraint.  The head of an `am_Constraint` has room for one
  pointer, which can hold context data:

  ```c
  am_Constraint *cons = am_newconstraint(solver, AM_REQUIRED);
  MyContext *ctx = new_my_context();
  ctx->mydata = 1;
  *(MyContext**)cons = ctx;
  ```

- `void am_resetconstraint(am_Constraint *cons);`

  Remove all terms and variables from the constraint and restore it to a blank
  state.

- `void am_delconstraint(am_Constraint *cons);`

  Free the constraint.  If it is in the solver, remove it first.

  This is free semantics: the constraint is recycled, its expression released
  and its variable references dropped.  `cons` is dead when the call returns --
  using it again (including passing it a second time) is undefined behaviour and
  not detected.  The default allocator puts the slot back into the constraint
  pool, so a second call may still read plausible memory: that is slot reuse, so
  it proves nothing.

- `am_Constraint *am_cloneconstraint(am_Constraint *other, am_Num strength);`

  Clone the constraint with a new `strength`.
  Returns `NULL` when the new constraint cannot be allocated.

- `am_Constraint *am_constraints(const am_Solver *solver, am_Constraint *cons);`

  Walk the constraints registered in `solver` the same way: `NULL` starts the
  walk, `NULL` ends it.  A constraint is registered by `am_newconstraint()`
  and stays registered until `am_delconstraint()`, so walking does not require
  adding it.  The hidden constraints an edit creates for `am_suggest()` are
  not returned.  The anchor must be a constraint of this solver.

- `int am_addterm(am_Constraint *cons, am_Id var, am_Num multiplier);`

  Add a term to the constraint.

  For example, `2*m == x+y` has the terms `2*m`, `1*x` and `1*y`.  Build it
  like this:

  ```c
  /* 2*m == x + y */
  am_addterm(c, m, 2.0); /* 2*m */
  am_setrelation(c, AM_EQUAL); /* = */
  am_addterm(c, x, 1.0); /* x */
  am_addterm(c, y, 1.0); /* y */
  ```

- `int am_setrelation(am_Constraint *cons, int relation);`

  Set the constraint's relation, one of:

  - `AM_EQUAL`
  - `AM_GREATEREQUAL`
  - `AM_LESSEQUAL`

  Terms added before `am_setrelation()` form the left-hand side; terms added
  after it form the right-hand side.

- `int am_addconstant(am_Constraint *cons, am_Num constant);`

  Add a constant term to the constraint.  A constant that the
  solver treats as numerical zero is stored as `0` (below `AM_NUM_EPS`: 1e-4
  in the default `float` build, 1e-6 with `AM_USE_DOUBLE`); `am_constant()`
  reports the stored value.

- `int am_setstrength(am_Constraint *cons, am_Num strength);`

  Set the strength of a constraint.

- `int am_merge(am_Constraint *cons, const am_Constraint *other, am_Num coef);`

  Merge `other` into `cons`, multiplied by `coef`.

- `int am_add(am_Constraint *cons);`

  Add the constraint to the solver it was created from.  A successful add
  publishes the solution it computed: with auto update on, the bindings are
  current when the call returns; with it off, the next `am_updatevars()` writes
  them.  A failed add changes no binding.

- `void am_remove(am_Constraint *cons);`

  Remove an added constraint.

- `int am_hasconstraint(const am_Constraint *cons);`

  Check whether a constraint has been added to the solver.

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

  Dump the whole solver state (edits included) into binary data.

  Returns `AM_FAILED` when the state holds a symbol that the dump has no index
  for.  `am_load()` accepts such a stream (it validates structure and leaves
  symbol ownership unchecked), so a solver loaded from a hand-edited stream can
  reach that shape; discard the bytes written up to the failure.

- `am_Solver *am_load(am_Allocf *allocf, void *ud, am_Loader *l, size_t maxmem);`

  Creates a new solver, loads a dump stream into it and returns it, or `NULL`
  when the stream cannot be loaded.  `allocf`/`ud` are passed to
  `am_newsolver()`.  `maxmem` is the strict byte budget of the load (`0` =
  `AM_MAX_LOADMEM`); a single allocation may not consume the whole remaining
  budget.  Only streams produced by `am_dump()` are supported, and variable
  bindings are restored from the stream (see "Dump & Load").

