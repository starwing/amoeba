-- Regression test for the C Lua binding (lua_amoeba.c).
--
-- Driven by `just test-lua` / `test-lua-luajit` / `test-lua-double`.  Silent on
-- success: any failure raises and the recipe fails.
--
-- `package.path` is cleared so `require "amoeba"` can only load the C module,
-- never the pure-Lua amoeba.lua.
--
-- LuaLS: `---@diagnostic` marks the deliberate misuse the error-path tests
-- need; `--[[@as X]]` asserts the success the dump/load tests rely on.
package.path = ""

local amoeba = require "amoeba"

local function check(cond, fmt, ...)
    if not cond then error(string.format(fmt, ...), 3) end
end

local function eq(got, want, what)
    check(got == want, "%s: got %s, want %s", what, tostring(got),
          tostring(want))
end

local function near(got, want, what)
    local eps = 1e-3
    check(math.abs(got - want) <= eps, "%s: got %s, want %s", what,
          tostring(got), tostring(want))
end

local function errors(what, fn)
    local ok, err = pcall(fn)
    check(not ok, "%s: expected an error", what)
    check(type(err) == "string", "%s: error value is %s", what, type(err))
    return err
end

-- variables

local S = amoeba.new(true)
check(type(S) == "userdata", "amoeba.new returns %s", type(S))

local xl = S:var "xl"
local xm = S:var "xm"
local xr = S:var "xr"
check(type(xl) == "userdata", "S:var returns %s", type(xl))
eq(S:var("xl"), xl, "S:var(name) is idempotent")
eq(xl:value(), 0, "new variable value")
check(tostring(xl):find("xl", 1, true) ~= nil, "variable tostring has a name")

errors("S:var(unknown id)", function() S:var(99) end)
---@diagnostic disable-next-line: param-type-mismatch
errors("S:var(bad type)", function() S:var(true) end)

-- constraints

-- one C constraint per user constraint: terms first, relation last
local two = S:constraint()
two:add(xm, 2)
two:relation "=="
two:add(xl)
two:add(xr)

local le = S:constraint():add(xl):add(10)
le:relation "le" -- the method form, as opposed to the __call form below
le:add(xr)

local cons = {
    two,
    le,
    S:constraint()(xr) "<=" (100),
    S:constraint():add(xl):relation "ge",
    S:constraint("weak"):add(xm):add(-1):relation "ge",
}
for i = 1, #cons do S:add(cons[i]) end

check(tostring(S):find("amoeba.Solver", 1, true) ~= nil, "solver tostring")
check(tostring(cons[1]):find("amoeba.Constraint", 1, true) ~= nil,
      "constraint tostring")

-- the constraints hold
near(xm:value() * 2, xl:value() + xr:value(), "2*xm == xl + xr")
check(xl:value() <= xr:value() - 10, "xl + 10 <= xr")
check(xr:value() <= 100, "xr <= 100")
check(xl:value() >= 0, "xl >= 0")
check(xm:value() >= 1, "xm >= 1")

-- suggest and deledit

S:suggest(xm, 70)
near(xm:value(), 70, "suggested value")
near(xm:value() * 2, xl:value() + xr:value(), "2*xm == xl + xr after suggest")
check(xl:value() <= xr:value() - 10, "xl + 10 <= xr after suggest")
check(xr:value() <= 100, "xr <= 100 after suggest")

S:deledit(xm)
S:clearedits()
S:suggest(xm, 50)
near(xm:value(), 50, "suggest after deledit and clearedits")
check(xl:value() <= xr:value() - 10, "constraints still hold")

-- error paths

---@diagnostic disable-next-line: undefined-field
errors("constraint requires a solver", function() return amoeba.constraint() end)
---@diagnostic disable-next-line: missing-parameter
errors("load without data", function() return amoeba.load() end)
errors("relation operator", function()
    ---@diagnostic disable-next-line: param-type-mismatch
    return S:constraint():relation "!="
end)

-- reset drops every constraint and edit; the handles are reusable

S:reset()
for i = 1, #cons do S:add(cons[i]) end
S:suggest(xm, 70)
near(xm:value(), 70, "suggest after reset")

-- dump / load

local data = S:dump() --[[@as string]]
check(type(data) == "string" and #data > 0, "dump returns a string")

local loaded_cons = {}
local S2 = amoeba.load(data, loaded_cons) --[[@as amoeba.Solver]]
check(type(S2) == "userdata", "load returns a solver")
check(loaded_cons[1] ~= nil, "load fills the constraint table")
near(S2:var("xm"):value(), xm:value(), "loaded value")

local S3 = amoeba.load(data) --[[@as amoeba.Solver]]
near(S3:var("xr"):value(), xr:value(), "loaded value without a table")

-- a reader function, one small chunk at a time
local pos = 1
local S4 = amoeba.load(function()
    if pos > #data then return nil end
    local chunk = data:sub(pos, pos + 15)
    pos = pos + 16
    return chunk
end) --[[@as amoeba.Solver]]
near(S4:var("xl"):value(), xl:value(), "loaded value from a reader")

local Sjunk, lerr, lcode = amoeba.load("not a dump")
check(Sjunk == nil and type(lerr) == "string", "load rejects junk")
eq(lcode, -2, "load junk result code") -- AM_FAILED

-- names, relations, cross-solver guards

---@diagnostic disable-next-line: missing-parameter
errors("S:var() needs a name", function() return S:var() end)
---@diagnostic disable-next-line: param-type-mismatch
errors("S:var(nil) needs a name", function() return S:var(nil) end)

S:addedit("xl") -- by name, not handle
S:deledit("xl")

local byname = S:constraint():add("xl"):add(10)
byname:relation "le"
byname:add("xr")
check(tostring(byname):find("xl", 1, true) ~= nil, "tostring keeps the name")

local cle = S:constraint()(xl) "<=" (xr)
check(tostring(cle):find("<= 0.0", 1, true) ~= nil, "<= tostring")
local ceq = S:constraint()(xl) "==" (xr)
check(tostring(ceq):find("== 0.0", 1, true) ~= nil, "== tostring")
local cge = S:constraint()(xl) ">=" (xr)
check(tostring(cge):find(">= 0.0", 1, true) ~= nil, ">= tostring")

local other = amoeba.new(true)
local ov = other:var "ov"
local oc = other:constraint():add(ov):add(1):relation "ge"
errors("build with a foreign variable", function() return S:constraint():add(ov) end)
errors("build with a foreign constraint", function()
    return S:constraint():add(oc)
end)
errors("add a foreign constraint", function() return S:add(oc) end)
errors("delete a foreign constraint", function() return S:remove(oc) end)
errors("hasconstraint with a foreign constraint", function()
    return S:hasconstraint(oc)
end)
errors("addedit with a foreign variable", function() return S:addedit(ov) end)
errors("deledit with a foreign variable", function() return S:deledit(ov) end)
errors("suggest with a foreign variable", function() return S:suggest(ov, 1) end)

local M = amoeba.new(false)
local mx = M:var "mx"
local mc = M:constraint():add(mx):relation "ge":add(10)
M:add(mc)
eq(mx:value(), 0, "no auto update before updatevars")
M:updatevars()
eq(mx:value(), 10, "updatevars updates the bound value")

do -- autoupdate(true) writes the value; false leaves it alone
    local AU = amoeba.new(false)
    local au = AU:var "au"
    AU:autoupdate(true)
    AU:add(AU:constraint():add(au):add(10):relation "ge")
    near(au:value(), -10, "autoupdate(true) writes the bound value")
    AU:autoupdate(false)
    AU:add(AU:constraint():add(au):add(20):relation "ge")
    near(au:value(), -10, "autoupdate(false) keeps the bound value")
end

-- constraints are built only through the API: S:constraint(), add(), relation()

local P = amoeba.new(true)
local px = P:var "px"
local py = P:var "py"
P:add(P:constraint():add(px, 2):add(py, -1):add(3):relation "==")
eq(#P:constraints(), 1, "one constraint from one builder")
near(py:value() - px:value() * 2, 3, "2*px == py - 3")

local three = P:constraint()(px) ">=" (-5) -- the __call chain
three:name "three"
P:add(three)
check(P:hasconstraint(three), "__call chain adds")

errors("no variable arithmetic", function() return px + 1 end)
---@diagnostic disable-next-line: undefined-field
errors("no comparison methods", function() return px:eq(0) end)

do
    local c = P:constraint():add(px, 2):add(-1):relation "le"
    check(tostring(c):find("px", 1, true) ~= nil, "coefficient in a builder")
end

do -- __bor is the registered strength setter; LuaJIT has no `|` operator
    local c = P:constraint()
    eq(getmetatable(c).__bor(c, "weak"), c, "__bor returns the constraint")
    eq(c:strength(), 1, "__bor sets the strength")
    errors("__bor bad strength", function()
        return getmetatable(c).__bor(c, true)
    end)
end

do -- c:add takes several operands: terms, constants, constraints, relation
    local A = amoeba.new(true)
    local ax, ay = A:var "ax", A:var "ay"

    local c1 = A:constraint():add(ax, "<=", 10)
    eq(c1:relation(), "<=", "add: relation between the operands")
    near(c1:constant(), 10, "add: trailing number is a constant")
    near(c1:terms()[1].coef, -1, "add: term left of the relation")

    local c2 = A:constraint():add(ax, 2, "<=", 10)
    near(c2:terms()[1].coef, -2, "add: number after a variable is its coef")
    near(c2:constant(), 10, "add: constant after the relation")

    local c3 = A:constraint():add("<=", ax, 10) -- 0 <= 10*ax
    eq(c3:relation(), "<=", "add: relation first")
    near(c3:terms()[1].coef, 10, "add: a number after a variable is its coef")
    near(c3:constant(), 0, "add: the number is not a constant here")

    local c4 = A:constraint():add(10, 2)
    near(c4:constant(), 20, "add: a number after a number keeps the old coef")

    local c5 = A:constraint():add(ax, 2, ay, -1, 3)
    eq(#c5:terms(), 2, "add: several terms in one call")
    near(c5:constant(), 3, "add: trailing constant after the terms")

    local c6 = A:constraint()(ax, ">=", 0) -- __call passes every operand on
    eq(c6:relation(), ">=", "add: __call with several operands")

    eq(#A:constraint():add(ax, nil):terms(), 1, "add: nil is omitted")
    local c7 = A:constraint():add(ax, nil, 5) -- nil breaks the item/coef pair
    near(c7:constant(), 5, "add: after nil a number is a constant")
    eq(#A:constraint():add():terms(), 0, "add: no arguments is a no-op")

    local err = errors("add: two relations", function()
        return A:constraint():add(ax, "<=", 10, ">=")
    end)
    check(tostring(err):find("relation already set", 1, true) ~= nil,
          "two relations: %s", err)
end

-- names + introspection: what the demo needs to rebuild its doc from a dump

local N = amoeba.new(true)
local na = N:var "v1.left"
local nb = N:var "v1.right"
local n1 = N:constraint():add(na):add(10):relation "le":add(nb)
n1:name "c1"
N:add(n1)
local n2 = N:constraint("weak"):add(na):add(-1):relation "ge"
n2:name "c2" -- created but not added: a disabled constraint

eq(na:name(), "v1.left", "variable name")
eq(n1:name(), "c1", "constraint name")
eq(n2:name(), "c2", "disabled constraint name")
eq(n1:relation(), "<=", "relation getter")
eq(n1:strength(), math.huge, "required strength is +inf")
eq(n2:strength(), 1, "weak strength")
eq(n1:error(), 0, "required constraint has no error")
near(n1:constant(), -10, "constant is the storage form")
check(N:hasconstraint(n1), "added constraint is enabled")
check(not N:hasconstraint(n2), "created constraint is disabled")
eq(#N:vars(), 2, "vars enumerates both variables")
eq(#N:constraints(), 2, "constraints includes the disabled one")

do
    local seen = {}
    for _, entry in ipairs(n1:terms()) do
        seen[entry.var:name()] = entry.coef
    end
    near(seen["v1.left"], -1, "term coefficient in storage form")
    near(seen["v1.right"], 1, "term coefficient in storage form")
end

local ndata = N:dump() --[[@as string]]
local ncons = {}
local N2 = amoeba.load(ndata, ncons) --[[@as amoeba.Solver]]
check(ncons["c1"] ~= nil, "load fills cons[name]")
eq(ncons["c1"]:name(), "c1", "loaded constraint keeps its name")
local named = {}
for _, c in ipairs(N2:constraints()) do named[c:name() --[[@as string]]] = c end
check(named["c1"] ~= nil and named["c2"] ~= nil, "both names survive dump/load")
check(N2:hasconstraint(named["c1"]), "enabled flag survives")
check(not N2:hasconstraint(named["c2"]), "disabled flag survives")
eq(named["c1"]:relation(), "<=", "relation survives")
near(named["c1"]:constant(), -10, "constant survives")
eq(named["c1"]:strength(), math.huge, "strength survives")
do
    local seen = {}
    for _, entry in ipairs(named["c1"]:terms()) do
        seen[entry.var:name()] = entry.coef
    end
    near(seen["v1.left"], -1, "terms survive dump/load")
end
eq(N2:var("v1.right"):name(), "v1.right", "loaded variable name")

do -- dump -> load -> dump: the second load still has the same names and flags
    local n2dump = N2:dump() --[[@as string]]
    local N3 = amoeba.load(n2dump) --[[@as amoeba.Solver]]
    eq(#N3:vars(), 2, "second load keeps the variables")
    eq(#N3:constraints(), 2, "second load keeps the constraints")
    local named3 = {}
    for _, c in ipairs(N3:constraints()) do
        named3[c:name() --[[@as string]]] = c
    end
    check(named3["c1"] ~= nil and named3["c2"] ~= nil,
          "second load keeps the constraint names")
    check(N3:hasconstraint(named3["c1"]), "second load keeps the enabled flag")
    check(not N3:hasconstraint(named3["c2"]), "second load keeps disabled ones")
end

-- explicit deletion: a variable still used by a constraint refuses to go

local D = amoeba.new(true)
local dx = D:var "dx"
local dc = D:constraint():add(dx):add(10):relation "ge"
D:add(dc)
errors("delete a used variable", function() return dx:delete() end)
check(tostring(dc):find("(added:", 1, true) ~= nil,
      "an added constraint shows its marker")
D:remove(dc)
check(tostring(dc):find("(added:", 1, true) == nil,
      "a removed constraint must not claim to be added")
dc:delete()
dx:delete()
errors("deleted variable value", function() return dx:value() end)

-- error paths and the remaining entry points

do -- the solver methods match the C API: add/remove, no old names
    check(type(S.add) == "function" and type(S.remove) == "function",
          "add/remove exist")
    ---@diagnostic disable-next-line: undefined-field
    check(S.addconstraint == nil and S.delconstraint == nil,
          "old constraint method names are gone")
end

errors("name lookup miss", function() return S:addedit("nope") end)
---@diagnostic disable-next-line: param-type-mismatch
errors("bad strength string", function() return S:constraint "nope" end)
---@diagnostic disable-next-line: param-type-mismatch
errors("bad strength type", function() return S:constraint(true) end)
do -- a bad relation leaves the constraint untouched
    local badcons = S:constraint()
    ---@diagnostic disable-next-line: param-type-mismatch
    errors("bad relation operator", function() return badcons:relation "!=" end)
    eq(#badcons:terms(), 0, "failed relation keeps the empty constraint")
    badcons:delete()
end
do -- a failed constructor leaves no constraint behind (P1-3)
    local G = amoeba.new(true)
    local before = #G:constraints()
    errors("constructor: bad strength type", function()
        ---@diagnostic disable-next-line: param-type-mismatch
        return G:constraint(true)
    end)
    errors("constructor: bad strength string", function()
        ---@diagnostic disable-next-line: param-type-mismatch
        return G:constraint "nope"
    end)
    eq(#G:constraints(), before, "failed constructors add nothing")
end
---@diagnostic disable-next-line: param-type-mismatch
errors("bad add operand", function() return S:constraint():add(true) end)
errors("add of a deleted constraint", function()
    return S:add(dc)
end)
errors("over-long constraint name", function()
    return S:constraint():name(string.rep("x", 256))
end)

do -- a constraint can be merged into another constraint's expression
    local Q = amoeba.new(true)
    local qx = Q:var "qx"
    local qc = Q:constraint():add(qx):add(10):relation "ge"
    Q:add(Q:constraint():add(qx, 2):add(qc, -1):relation "le")
    local e2 = Q:constraint():add(qc, 1):relation "le"
    check(e2:relation() == "<=", "a constraint can be merged in")
end

do -- failure returns from am_add, and mutations of an added constraint
    local R = amoeba.new(true)
    local rx = R:var "rx"
    local rc = R:constraint():add(rx):add(1):relation "eq"
    R:add(rc)
    local ok, _, code = R:add(rc)
    check(ok == nil and code == -2, "adding twice fails with AM_FAILED")
    local err = errors("relation on an added constraint", function()
        return rc:relation "le"
    end)
    check(err:find("added to solver", 1, true) ~= nil,
          "relation on an added constraint: %s", err)
    err = errors("add to an added constraint", function() return rc:add(1) end)
    check(err:find("cannot add the item: FAILED", 1, true) ~= nil,
          "add to an added constraint: %s", err)
    err = errors("second relation through add", function()
        return rc:add "<="
    end)
    check(err:find("relation already set", 1, true) ~= nil,
          "second relation: %s", err)
    local R0 = amoeba.new(true)
    local r0 = R0:constraint():add(R0:var "r0x")
    check(R0:add(r0) ~= nil, "a relation-less constraint is accepted as >=")
    err = errors("relation through add on an added relation-less one",
                 function() return r0:add "<=" end)
    check(err:find("added to solver", 1, true) ~= nil,
          "relation on an added relation-less one: %s", err)
    r0:delete()
    local r2 = R:constraint():add(rx):add(3):relation "eq"
    err = errors("second relation on a builder", function()
        return r2:relation "le"
    end)
    check(err:find("relation already set", 1, true) ~= nil,
          "second relation on a builder: %s", err)
    r2:delete()
    local rc2 = R:constraint():add(rx):add(2):relation "eq"
    local ok2, _, code2 = R:add(rc2)
    check(ok2 == nil and code2 ~= 0, "contradicting constraints fail")
    rc2:delete()
end

do -- an unconfigured constraint has no relation (P2)
    local U0 = amoeba.new(true)
    local c0 = U0:constraint():add(1)
    eq(c0:relation(), nil, "no relation yet")
    check(tostring(c0):find(">= 0.0", 1, true) ~= nil,
          "tostring shows the effective >= form")
    local U1 = amoeba.load(U0:dump()) --[[@as amoeba.Solver]]
    eq(U1:constraints()[1]:relation(), nil, "no relation survives dump/load")
    c0:delete()
    local c1 = U0:constraint():add(1):relation "=="
    eq(c1:relation(), "==", "an equality reads back as ==")
    c1:delete()
end

do -- reset and strength changes
    local T = amoeba.new(true)
    local tx = T:var "tx"
    local tc = T:constraint():add(tx):add(5):relation "ge"
    tc:reset()
    eq(#tc:terms(), 0, "reset drops the terms")
    T:add(tc)
    tc:strength "weak" -- re-adds a soft constraint
    near(tc:strength(), 1, "strength after change")
    check(tc:error() >= 0, "soft error is non-negative")
end

do -- soft-to-soft strength changes and a failed tightening
    local SS = amoeba.new(true)
    local sx = SS:var "sx"
    local sc = SS:constraint("strong"):add(sx):add(-10):relation "ge"
    SS:add(sc)
    SS:addedit(sx, "strong")
    SS:suggest(sx, 20)
    near(sx:value(), 20, "soft constraint value before the strength change")
    sc:strength "weak"
    eq(sc:strength(), 1, "soft downgrade")
    near(sx:value(), 20, "downgrade keeps the optimum")
    sc:strength "strong"
    eq(sc:strength(), 1000000, "soft upgrade")
    near(sx:value(), 20, "upgrade keeps the optimum")

    local F = amoeba.new(true)
    local fx = F:var "fx"
    F:add(F:constraint():add(fx):relation "eq") -- fx == 0
    local fs = F:constraint("weak"):add(fx):add(-10):relation "ge"
    F:add(fs)
    local err = errors("tighten a soft constraint to required", function()
        return fs:strength "required"
    end)
    check(err:find("cannot set the strength: ", 1, true) ~= nil
              and err:find("added to solver", 1, true) == nil,
          "failed tightening: %s", err)
    eq(fs:strength(), 1, "failed tightening keeps the old strength")
    check(F:hasconstraint(fs), "failed tightening keeps the constraint")
end

do -- load is a module function; the receiver form is rejected (P1-4)
    local src = amoeba.new(false)
    src:var "lv"
    local dump = src:dump() --[[@as string]]
    local loaded_solver = amoeba.load(dump) --[[@as amoeba.Solver]]
    eq(#loaded_solver:vars(), 1, "amoeba.load returns a new solver")
    check(loaded_solver ~= src, "load returns a new solver")
    ---@diagnostic disable-next-line: undefined-field
    local ok, err = pcall(function() return src:load(dump) end)
    check(not ok and tostring(err):find("amoeba.load", 1, true) ~= nil,
          "S:load is rejected")
    eq(#src:vars(), 1, "a rejected load leaves the receiver alone")
end

do -- a failing reader is reported, not thrown
    local ok, err = amoeba.load(function() error("reader blew up") end)
    check(ok == nil and type(err) == "string", "reader failure is returned")
    ---@diagnostic disable-next-line: return-type-mismatch
    local ok2, err2 = amoeba.load(function() return {} end)
    check(ok2 == nil and type(err2) == "string", "reader must return a string")
end

do -- long names exercise the dump's non-short forms and must not dangle
    local W = amoeba.new(true)
    local long = W:var(string.rep("n", 40))
    local sizes = {41, 64, 255}
    for i = 1, #sizes do
        -- the name string is temporary: only the handle may keep it alive
        local v = W:var(string.rep("v", sizes[i]))
        collectgarbage()
        local _ = {}
        for k = 1, 100 do _[k] = string.rep("g", 100) .. k end
        eq(v:name(), string.rep("v", sizes[i]), "name " .. sizes[i] .. " kept")
    end
    local con = W:constraint():add(long):add(1):relation "ge"
    con:name(string.rep("c", 200))
    collectgarbage()
    eq(con:name(), string.rep("c", 200), "a long constraint name survives gc")
    W:add(con)
    local wdump = W:dump() --[[@as string]]
    local W2 = amoeba.load(wdump) --[[@as amoeba.Solver]]
    eq(W2:var(string.rep("n", 40)):value(), long:value(), "long name round-trip")
    for i = 1, #sizes do
        eq(W2:var(string.rep("v", sizes[i])):name(), string.rep("v", sizes[i]),
           "name " .. sizes[i] .. " round-trips")
    end
    eq(W2:constraints()[1]:name(), string.rep("c", 200),
       "long constraint name round-trip")

    local many = amoeba.new(true)
    for i = 1, 30 do many:var("v" .. i) end
    local d2 = many:dump() --[[@as string]]
    local many2 = amoeba.load(d2) --[[@as amoeba.Solver]]
    eq(#many2:vars(), 30, "many variables round-trip")
end

do -- a truncated dump must fail cleanly at every cut point
    local X = amoeba.new(true)
    local x1 = X:var "x1"
    X:add(X:constraint():add(x1):add(10):relation "ge")
    local d = X:dump() --[[@as string]]
    for i = 0, #d - 1 do
        local t = {marker = true}
        local r, msg, code = amoeba.load(d:sub(1, i), t)
        check(r == nil and type(msg) == "string" and code ~= 0,
              "truncated dump at %d is rejected", i)
        check(t.marker == true and next(t) == "marker",
              "truncated dump leaves the table alone at %d", i)
    end
end

do -- a failed load must not leave a solver store behind
    local Y = amoeba.new(true)
    local y = Y:var "y"
    local c = Y:constraint():add(y):add(1):relation "ge"
    c:name(string.rep("n", 100))
    Y:add(c)
    local d = Y:dump() --[[@as string]]
    local function stores()
        local n = 0
        for k in pairs(debug.getregistry()) do
            if type(k) == "userdata" then n = n + 1 end
        end
        return n
    end
    local before = stores()
    for _ = 1, 20 do
        local sent = false
        local r = amoeba.load(function()
            if not sent then sent = true; return d:sub(1, #d - 1) end
            error("stop")
        end)
        check(r == nil, "named load failure is reported")
    end
    eq(stores(), before, "failed load leaves no store behind")
end

do -- names are owned by the solver's store, not by registry refs
    local vn, cn = string.rep("z", 200), string.rep("c", 200)
    local function refs()
        local n = 0
        for k, v in pairs(debug.getregistry()) do
            if type(k) == "number" and k > 0 and (v == vn or v == cn) then
                n = n + 1
            end
        end
        return n
    end
    local before = refs()
    local Z = amoeba.new(true)
    local z = Z:var(vn)
    local zc = Z:constraint():add(z):relation "ge"
    zc:name(cn)
    Z:add(zc)
    eq(refs(), before, "names make no registry refs")
    eq(Z:var(vn):name(), vn, "a long variable name is kept")
    eq(Z:constraints()[1]:name(), cn, "a long constraint name is kept")
    Z:delete()

    local Z2 = amoeba.new(true)
    local zv = Z2:var(vn)
    eq(#Z2:vars(), 1, "long-named variable exists")
    zv:delete()
    eq(Z2:var(vn):value(), 0, "a deleted long-named variable is recreated")
    eq(#Z2:vars(), 1, "recreated variable count")
end

do -- names are C strings: NUL truncates the dump; duplicates share one key
    local A = amoeba.new(true)
    local ax = A:var "ax"
    local ac = A:constraint():add(ax):add(1):relation "ge"
    ac:name("a\0b")
    eq(ac:name(), "a\0b", "a constraint name keeps the NUL bytes")
    A:add(ac)
    local A2 = amoeba.load(A:dump()) --[[@as amoeba.Solver]]
    eq(A2:constraints()[1]:name(), "a", "the dump truncates at the NUL")

    local B = amoeba.new(true)
    local bx = B:var "bx"
    local b1 = B:constraint():add(bx):add(1):relation "ge"
    local b2 = B:constraint():add(bx):add(2):relation "ge"
    b1:name "dup"
    b2:name "dup"
    B:add(b1)
    B:add(b2)
    local t = {}
    local B2 = amoeba.load(B:dump(), t) --[[@as amoeba.Solver]]
    local all = B2:constraints()
    eq(#all, 2, "duplicate names keep both constraints")
    check(t.dup ~= nil, "duplicate names fill one table key")
    local named, entry = 0, false
    for i = 1, #all do
        if all[i]:name() == "dup" then named = named + 1 end
        if all[i] == t.dup then entry = true end
    end
    eq(named, 2, "both duplicates keep the name")
    check(entry, "the table entry is one of the duplicates")
end

do -- a handle missing from the store is an internal error, not a nil hole
    local function store_of(handle)
        local found
        for _, value in pairs(debug.getregistry()) do
            if type(value) == "table" then
                for _, entry in pairs(value) do
                    if entry == handle then found = value end
                end
            end
        end
        return found
    end
    local H = amoeba.new(true)
    local hv = H:var "hole"
    local hc = H:constraint():add(hv):relation "ge"
    H:add(hc)
    local vstore, cstore = store_of(hv), store_of(hc)
    check(vstore ~= nil and cstore ~= nil, "both stores are reachable")
    for k, entry in pairs(vstore) do
        if entry == hv then vstore[k] = nil end
    end
    local err = errors("vars with a missing handle", function()
        return H:vars()
    end)
    check(err:find("has no handle", 1, true) ~= nil, "missing var: %s", err)
    for k, entry in pairs(cstore) do
        if entry == hc then cstore[k] = nil end
    end
    err = errors("constraints with a missing handle", function()
        return H:constraints()
    end)
    check(err:find("has no handle", 1, true) ~= nil, "missing cons: %s", err)
    err = errors("terms with a missing handle", function() return hc:terms() end)
    check(err:find("has no handle", 1, true) ~= nil, "missing term: %s", err)
end

-- drag path: edits, cached suggests, and soft-constraint errors

do
    local G = amoeba.new(true)
    local gx = G:var "gx"
    local soft = G:constraint("strong"):add(gx):add(-10):relation "ge"
    G:add(soft)
    G:addedit(gx, "strong")
    G:suggest(gx, 20)
    G:suggest(gx, 30) -- same age: uses the suggest cache
    G:suggest(gx, 40)
    near(gx:value(), 40, "cached suggest")
    G:deledit(gx)
    G:updatevars()
    G:clearedits()

    local weakc = G:constraint("weak"):add(gx):add(-2000):relation "ge"
    G:add(weakc)
    G:addedit(gx, "strong")
    G:suggest(gx, 0)
    check(weakc:error() ~= 0, "violated soft constraint has an error")
end

do -- big solver: dump buffer flushes and multi-byte counts
    local B = amoeba.new(false)
    for i = 1, 300 do
        local v = B:var("bv" .. i)
        B:add(B:constraint():add(v):add(i):relation "ge")
    end
    B:updatevars()
    local d = B:dump() --[[@as string]]
    check(#d > 4096, "dump is larger than the write buffer")
    local B2 = amoeba.load(d) --[[@as amoeba.Solver]]
    eq(#B2:vars(), 300, "big solver round-trip")
    eq(#B2:constraints(), 300, "big solver constraint count")

    local bpos = 1
    local B3 = amoeba.load(function() -- one byte at a time: the slow reader paths
        if bpos > #d then return nil end
        local ch = d:sub(bpos, bpos)
        bpos = bpos + 1
        return ch
    end) --[[@as amoeba.Solver]]
    eq(#B3:vars(), 300, "big solver from a byte reader")
end

-- more entry points: the remaining errors

do
    local U = amoeba.new(true)
    local ux = U:var "ux"
    local u1 = U:constraint():add(ux):relation "ge":add(10)
    U:add(u1)
    local u2 = U:constraint():add(ux):relation "le":add(0)
    local ok, _, code = U:add(u2)
    check(ok == nil and code ~= 0, "contradicting inequalities fail")
    u2:delete()

    local s15 = U:constraint(1.5) -- numeric strength
    s15:delete()

    U:add(U:constraint():add(ux, 2):relation "ge")
    U:add(U:constraint():add(ux):add(ux, -1):relation "==")
    U:add(U:constraint():add(ux, -1):relation "le")
    check(#U:constraints() >= 4, "builder forms add")

    local named_u = U:constraint():add(ux):relation "ge"
    named_u:name "temp"
    named_u:delete() -- drops the registry name

    local rn = U:constraint():add(ux):relation "ge"
    rn:name "n1"
    rn:name "n2" -- replaces the old name
    eq(rn:name(), "n2", "name replacement")
    rn:delete()

    local ac = U:constraint():add(ux):relation "ge"
    U:add(ac)
    errors("relation through add on an added constraint", function()
        return ac:add "<="
    end)

    errors("empty variable name", function()
        return amoeba.new(true):var ""
    end)
    errors("over-long variable name", function()
        return amoeba.new(true):var(string.rep("q", 256))
    end)

    ---@diagnostic disable-next-line: param-type-mismatch
    errors("load of a boolean", function() return amoeba.load(true) end)
    ---@diagnostic disable-next-line: param-type-mismatch
    errors("add of a number", function() return U:add(42) end)
    errors("delete a foreign constraint", function() return U:remove(oc) end)

    local unnamed = U:constraint():add(ux):relation "ge"
    eq(unnamed:name(), nil, "unnamed constraint")
    unnamed:delete()
    errors("foreign constraint in add", function()
        return U:constraint():add(oc)
    end)
    local er, emsg = amoeba.load(function() return nil end)
    check(er == nil and type(emsg) == "string", "empty reader is rejected")
    local E = amoeba.new(true)
    local ex = E:var "ex"
    E:suggest(ex, 5)
    E:deledit(ex)
    E:updatevars() -- a variable used only by an edit may be dropped by the library
    check(#E:vars() <= 1, "edit-only variable state")
end

-- lifecycle: the solver owns handle validity, either side may die first

check(type(getmetatable(S).__gc) == "function", "solver has a __gc")

-- dropping a handle changes nothing: the solver owns it
do
    local A = amoeba.new(true)
    A:var "keep" -- the handle is dropped at once
    collectgarbage()
    collectgarbage()
    local again = A:var "keep" -- same variable, same handle
    check(tostring(again):find("keep", 1, true) ~= nil, "handle survives")
    A:suggest(again, 7)
    near(again:value(), 7, "solver survives handle collection")
end

-- solver dies first: the handles go invalid without touching freed memory
local deadvar, deadcons
do
    local T = amoeba.new(true)
    deadvar = T:var "dead"
    deadcons = T:constraint():add(deadvar):add(10):relation "ge"
    T:add(deadcons)
end
collectgarbage()
collectgarbage()
eq(tostring(deadvar), "amoeba.Variable: deleted", "dead variable tostring")
eq(tostring(deadcons), "amoeba.Constraint: deleted", "dead constraint tostring")
errors("dead variable value", function() return deadvar:value() end)
errors("dead constraint add", function() return deadcons:add(1) end)
deadvar:delete()  -- no-ops, must not touch the freed solver
deadcons:delete()
deadvar, deadcons = nil, nil
collectgarbage()

-- S:delete() invalidates the solver and every handle; nothing may crash
do
    local T = amoeba.new(true)
    local tx = T:var "tx"
    local tc = T:constraint():add(tx):add(1):relation "ge"
    T:add(tc)
    T:delete()
    local functions = {
        function() return T:var "ty" end,
        function() return T:constraint() end,
        function() return T:reset() end,
        function() return T:clearedits() end,
        function() return T:autoupdate(false) end,
        function() return T:add(tc) end,
        function() return T:remove(tc) end,
        function() return T:addedit(tx) end,
        function() return T:deledit(tx) end,
        function() return T:suggest(tx, 1) end,
        function() return T:updatevars() end,
        function() return T:vars() end,
        function() return T:constraints() end,
        function() return T:hasconstraint(tc) end,
        function() return T:dump() end,
        function() return tostring(T) end,
    }
    for i = 1, #functions do
        local ok, err = pcall(functions[i])
        check(not ok and tostring(err):find("solver deleted", 1, true),
              "method %d after delete: %s", i, tostring(err))
    end
    do -- load is not a solver method, even on a deleted solver
        ---@diagnostic disable-next-line: undefined-field
        local ok, err = pcall(function() return T:load("x") end)
        check(not ok and tostring(err):find("amoeba.load", 1, true) ~= nil,
              "load after delete: %s", tostring(err))
    end
    T:delete() -- idempotent
    tx:delete()
    tc:delete()
end
collectgarbage()
collectgarbage()

-- nothing may leak on the Lua side; 10k solver lifecycles
local before = collectgarbage("count")
for i = 1, 10000 do
    local G = amoeba.new(true)
    local gv = G:var "v"
    local gc = G:constraint():add(gv):add(1):relation "ge"
    G:add(gc)
    G:suggest(gv, i)
end
collectgarbage()
collectgarbage()
local grew = collectgarbage("count") - before
check(grew < 2048, "GC pressure grew the Lua heap by %d KB", grew)
