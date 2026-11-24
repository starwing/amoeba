---@meta amoeba

--- Type declarations for the amoeba Lua binding (lua_amoeba.c).
---
--- Usage:
---   local amoeba = require "amoeba"
---   local S = amoeba.new(true)
---   local x = S:var "x"
---   local c = S:constraint()(x) ">=" (10)
---   S:add(c)
---
--- Notes:
--- * The module table and the solver metatable are the same table (M1); only
---   the recommended forms are declared here.
--- * Removed: `S:load` (use `amoeba.load`), `S:addconstraint`/`S:delconstraint`
---   (use `S:add`/`S:remove`), and `S:constraint(rel, a, b)` (use
---   `S:constraint()(a) "rel" (b)` or `c:add`/`c:relation`).
--- * Handles are valid only while the solver is alive; `S:delete()` or GC
---   invalidates all of them.
--- * Supported runtimes: LuaJIT 2.1 (LÖVE 11.x) and Lua 5.5. `just test-lua`,
---   `just test-lua-luajit` and `just test-lua-double` build the module and run
---   the assertion suite against them.
--- * `require "amoeba"` also matches the pure-Lua `amoeba.lua` in this repo,
---   which wins with the default `package.path`. Load the C module explicitly:
---   clear `package.path` first, or call
---   `package.loadlib(path, "luaopen_amoeba")()`.
--- * Names are C strings: an embedded NUL is not supported. A variable name
---   truncates at the NUL in `name()`, `dump` and `delete`, so a NUL name can
---   collide with the shortened name and `delete` leaves a stale index behind.
---   A constraint name keeps its bytes in `name()`, but `dump` truncates it.
--- * LuaLS: put `?.d.lua` before `?.lua` in `runtime.path` so this file wins
---   over the pure-Lua `amoeba.lua`.
---@class amoeba
local amoeba = {}

--- Strength: `required` equals `math.huge`; `strong`/`medium`/`weak` are
--- 1000000/1000/1. Any number >= 0 is accepted as well.
---@alias amoeba.Strength number|"required"|"strong"|"medium"|"weak"

--- Relation string; `eq`/`le`/`ge` are aliases for `==`/`<=`/`>=`.
---@alias amoeba.Relation "=="|"<="|">="|"eq"|"le"|"ge"

--- Items accepted by a constraint:
--- * number: a constant;
--- * string: a variable name;
--- * amoeba.Variable: a variable handle;
--- * amoeba.Constraint: merge its expression (its relation is not merged).
---@alias amoeba.Item number|string|amoeba.Variable|amoeba.Constraint

--- Reader for `amoeba.load`: called repeatedly, returns one string chunk;
--- return nil to finish.
---@alias amoeba.Reader fun(): string|nil

--- Create a new solver.
---
--- With `auto_update` true, `S:suggest()` updates `x:value()` immediately; with
--- false (the default), call `S:updatevars()` explicitly.
---@param auto_update? boolean
---@return amoeba.Solver
function amoeba.new(auto_update) end

--- Create a **new** solver from a dump string or a reader.
---
--- There is no receiver form: `S:load(...)` raises; use `amoeba.load(...)`.
--- On failure returns `nil, message, code`; `cons` is written only after a
--- successful load and is left untouched on failure. Numeric keys `1..n` are
--- constraint handles; named constraints also get a `name -> handle` string key.
--- The new solver has `auto_update == false`; call `L:autoupdate(true)` if needed.
---@param data string|amoeba.Reader
---@param cons? table
---@return amoeba.Solver|nil solver
---@return string|nil message
---@return integer|nil code
function amoeba.load(data, cons) end

---@class amoeba.Solver
local Solver = {}

--- Create or look up a variable by name; a number is only used to look up an
--- existing variable by id.
---
--- The name must be 1..255 bytes; repeating a name returns the same handle.
--- Names are the dump/load identity and must not contain NUL.
---@param name string|integer
---@return amoeba.Variable
function Solver:var(name) end

--- Create an empty constraint; fill it with `c:add` / `c:relation` / `c:strength`.
---
--- Binary relation shorthand (use "==", "<=", or ">="):
---   S:constraint()(a) "<=" (b)
--- is `c:add(a)`, `c:relation "<="`, `c:add(b)` in that order. Note that
--- `c:add(a):add(b):relation "le"` sets the relation last and has different
--- semantics.
---@param strength? amoeba.Strength defaults to "required"
---@return amoeba.Constraint
function Solver:constraint(strength) end

--- Enable a constraint (`am_add`).
---
--- Returns the solver on success; on failure returns `nil, message, code`
--- (duplicate add, unsatisfiable, unbounded, ...) and the constraint stays
--- disabled. Use `S:hasconstraint(c)` to check the result.
---@param cons amoeba.Constraint
---@return amoeba.Solver|nil solver
---@return string|nil message
---@return integer|nil code
function Solver:add(cons) end

--- Disable a constraint (`am_remove`); this does not destroy it. The constraint
--- stays in `S:constraints()` and in the dump and can be re-added later.
--- Use `c:delete()` to destroy it.
---@param cons amoeba.Constraint
---@return amoeba.Solver
function Solver:remove(cons) end

--- Whether the constraint is enabled (added). Returns false for created,
--- disabled, or never-added constraints.
---@param cons amoeba.Constraint
---@return boolean
function Solver:hasconstraint(cons) end

--- Delete the solver; idempotent. Every variable/constraint handle becomes
--- invalid and later methods raise "solver deleted".
function Solver:delete() end

--- Set the auto-update flag. Returns no values, so it cannot be chained.
---@param enabled boolean
function Solver:autoupdate(enabled) end

--- Write solver values back to the variable handles; needed when `auto_update`
--- is false.
---@return amoeba.Solver
function Solver:updatevars() end

--- Clear edits and disable all constraints, keeping variables and handles;
--- constraints can be added again afterwards.
---@return amoeba.Solver
function Solver:reset() end

--- Clear all edits/suggestions.
---@return amoeba.Solver
function Solver:clearedits() end

--- Add an edit for a variable (the drag path); the default strength is
--- `"medium"`. An existing edit changes strength instead.
--- Returns the solver on success, `nil, message, code` on failure.
---@param var string|amoeba.Variable
---@param strength? amoeba.Strength
---@return amoeba.Solver|nil solver
---@return string|nil message
---@return integer|nil code
function Solver:addedit(var, strength) end

--- Remove the edit for a variable.
---@param var string|amoeba.Variable
---@return amoeba.Solver
function Solver:deledit(var) end

--- Suggest a value for a variable; with `auto_update` true it is reflected
--- immediately in `x:value()`.
---@param var string|amoeba.Variable
---@param value number
---@return amoeba.Solver
function Solver:suggest(var, value) end

--- Array of all variables; hash-slot order, not creation order.
---@return amoeba.Variable[]
function Solver:vars() end

--- Array of all constraints, including disabled and never-added ones;
--- hash-slot order, not creation order.
---@return amoeba.Constraint[]
function Solver:constraints() end

--- Serialize the solver to a string.  The stream includes every edit; the
--- call does not clear them.  If `auto_update` is false, `am_dump()` first
--- calls `am_updatevars()`, which can change values.
--- On failure returns `nil, message, code` (for example a symbol the dump
--- cannot index, or an allocation failure).
---@return string|nil dump
---@return string|nil message
---@return integer|nil code
function Solver:dump() end

---@return string
function Solver:__tostring() end

---@class amoeba.Variable
local Variable = {}

--- Current value.
---@return number
function Variable:value() end

--- Variable name; raises "invalid variable" after the handle is invalidated.
--- An embedded NUL truncates the returned name (see the header notes).
---@return string
function Variable:name() end

--- Delete the variable. Refused while a constraint references it (raises
--- "variable is used by a constraint"); a no-op on an invalid handle.
function Variable:delete() end

---@return string
function Variable:__tostring() end

---@class amoeba.Term
---@field var amoeba.Variable
---@field coef number

--- Constraint call forms:
---   c(...)                 same as c:add(...)
---   c "<=" / ">=" / "=="  set the relation (`eq`/`le`/`ge` only via c:relation)
---   c | strength           expression form, same as c:strength(strength)
---@class amoeba.Constraint
local Constraint = {}

--- Build the constraint from the operands, or set the relation.
---
--- Call forms:
---   c:add(...)             process every argument left to right
---   c(...)                 same as c:add(...)
---   c "<=" / ">=" / "=="  set the relation (shorthand for one operand)
---
--- Operand rules, in order:
--- * a string starting with `<`, `>`, or `=` sets the relation; at most one
---   relation per constraint ("relation already set" otherwise)
--- * an item followed by a number takes it as the coefficient
---   (`c:add(x, 2)` is 2*x, `c:add(c1, -1)` merges -1*c1); otherwise 1
--- * a number is a constant; a number directly after another number is its
---   coefficient, so `c:add(10, 2)` adds the constant 20
--- * a string that is not a relation is a variable name
--- * nil is ignored
---
--- Terms before the relation are the left-hand side, terms after it are the
--- right-hand side: `c:add(a, "<=", 10)` is `a <= 10`, while
--- `c:add("<=", a, 10)` is `0 <= 10*a` (10 is a's coefficient).
---
--- The constraint must belong to the same solver; invalid input raises. An
--- operand added after the constraint was added raises
--- "cannot add the item: <code>".
---@param ... amoeba.Item|amoeba.Relation
---@return amoeba.Constraint
function Constraint:add(...) end

--- Read or set the relation.
---
--- With no argument returns `"=="`, `"<="`, or `">="`. A constraint with no
--- relation returns nil: it was never configured. Such a constraint can still
--- be added (the solver treats it as `>=`, and `dump` keeps "no relation"), but
--- set the relation first unless that is what you mean.
---
--- Setting works once and only before the constraint is added; otherwise it
--- raises "relation already set" or "constraint has been added to solver".
---@overload fun(self: amoeba.Constraint): string|nil
---@param op amoeba.Relation
---@return amoeba.Constraint
function Constraint:relation(op) end

--- Read or set the strength.
---
--- With no argument returns a number; `required` is `math.huge`. Changing an
--- added constraint to or from "required" re-adds it; when the solver rejects
--- the re-add, this raises "cannot set the strength: <code>".
---@overload fun(self: amoeba.Constraint): number
---@param strength amoeba.Strength
---@return amoeba.Constraint
function Constraint:strength(strength) end

--- Constant of the stored expression. For `<=` and `==` the expression is
--- negated internally, so this returns the negated value.
---@return number
function Constraint:constant() end

--- Violation of a soft constraint; 0 for required or not-added constraints.
---@return number
function Constraint:error() end

--- Terms of the stored expression. For `<=`/`==` the expression is negated
--- internally; hash-slot order.
---@return amoeba.Term[]
function Constraint:terms() end

--- Read or set the constraint name (the dump/load identity).
---
--- With no argument returns the name, or nil when unnamed. The name must be
--- 1..255 bytes. Names need not be unique: on load the string key of a
--- duplicate name keeps one of them (the last in dump order, which is not
--- creation order).
---@overload fun(self: amoeba.Constraint): string|nil
---@param name string
---@return amoeba.Constraint
function Constraint:name(name) end

--- Destroy the constraint: remove it from the solver, invalidate the handle,
--- and drop it from the dump. Use `S:remove` to disable but keep it.
function Constraint:delete() end

--- Reset the constraint: remove it from the solver, clear terms and relation;
--- the handle can be reused.
---@return amoeba.Constraint
function Constraint:reset() end

--- Debug form of the stored expression with the effective relation: a
--- constraint without a relation is displayed as `>=`.
---@return string
function Constraint:__tostring() end

return amoeba
