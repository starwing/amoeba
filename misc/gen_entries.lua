-- misc/gen_entries.lua -- generate the entries table + main for an amoeba test.
--
-- Scans a test source for `TEST(name)` definitions (one per line, at column
-- 0) and writes a .gen.inc file containing the test_entry array and main().
-- The test source includes it at the end:
--
--   TEST(foo) { ... }
--   #include "foo_test.gen.inc"
--
-- Conditional compilation is evaluated for the small subset the suite uses:
--   #if <expr>, #ifdef NAME, #ifndef NAME, #elif, #else, #endif
-- with <expr> built from defined(NAME), !, &&, ||, parentheses, integers and
-- macro names.  The -DNAME / -DNAME=value arguments given to this script
-- mirror the compiler defines, so backend-specific tests are only emitted for
-- the backend being built:
--
--   lua misc/gen_entries.lua tests/dump_test.c dump_test.gen.inc \
--       -DAM_USE_DOUBLE
--
-- The generated file is a build artifact; never edit it.

local src = arg[1]
local out = arg[2]
if not src or not out then
    io.stderr:write("usage: lua gen_entries.lua <src.c> <out.inc> [-DNAME...]\n")
    os.exit(1)
end

-- Define table -----------------------------------------------------------

local defs = {}
for i = 3, #arg do
    local name = arg[i]:match("^%-D([%w_]+)")
    if name then defs[name] = true end
end

-- Evaluate a preprocessor condition.  We translate to a Lua expression after
-- replacing defined()/identifiers with boolean sentinels.  The sentinels are
-- control bytes, not identifiers or digits, so later substitution passes never
-- rewrite them; they become Lua true/false at the very end.  Using booleans
-- (not 0/1) is what makes `!`, `&&`, `||` behave like C: in Lua `not 0` is
-- false because 0 is truthy.
local TRUE_MARK = string.char(1)
local FALSE_MARK = string.char(2)

local function evalcond(text)
    local expr = text

    local function mark(v) return v and TRUE_MARK or FALSE_MARK end

    expr = expr:gsub("defined%s*%(%s*([%w_]+)%s*%)", function(n)
        return mark(defs[n])
    end)
    expr = expr:gsub("defined%s+([%w_]+)", function(n)
        return mark(defs[n])
    end)
    expr = expr:gsub("[%a_][%w_]*", function(n)
        return mark(defs[n])
    end)
    expr = expr:gsub("%d+", function(n)
        return mark(tonumber(n) ~= 0)
    end)
    expr = expr:gsub("&&", " and ")
    expr = expr:gsub("||", " or ")
    expr = expr:gsub("!=", "~=")
    expr = expr:gsub("!", " not ")
    expr = expr:gsub(TRUE_MARK, "true")
    expr = expr:gsub(FALSE_MARK, "false")

    local chunk, err = load("return (" .. expr .. ")", "=cond", "t", {})
    if not chunk then
        error("gen_entries: cannot parse #if condition: " .. text ..
              " (" .. tostring(err) .. ")")
    end
    local ok, value = pcall(chunk)
    if not ok then
        error("gen_entries: cannot evaluate #if condition: " .. text)
    end
    return value and true or false
end

-- Scan -------------------------------------------------------------------

local names = {}
local stack = {} -- entries: { parent = bool, taken = bool, active = bool }

local function active()
    return #stack == 0 or stack[#stack].active
end

local f = assert(io.open(src, "r"))
for line in f:lines() do
    local dir, rest = line:match("^%s*#%s*(%w+)(.*)$")
    if dir == "if" then
        local parent = active()
        local cond = evalcond(rest)
        stack[#stack + 1] = {
            parent = parent,
            taken = cond,
            active = parent and cond,
        }
    elseif dir == "ifdef" or dir == "ifndef" then
        local name = rest:match("^%s*([%w_]+)")
        local cond = name and defs[name] or false
        if dir == "ifndef" then cond = not cond end
        local parent = active()
        stack[#stack + 1] = {
            parent = parent,
            taken = cond,
            active = parent and cond,
        }
    elseif dir == "elif" then
        local top = stack[#stack]
        if not top then
            error("gen_entries: #elif without #if in " .. src)
        end
        local cond = evalcond(rest)
        top.active = top.parent and not top.taken and cond
        top.taken = top.taken or cond
    elseif dir == "else" then
        local top = stack[#stack]
        if not top then
            error("gen_entries: #else without #if in " .. src)
        end
        top.active = top.parent and not top.taken
        top.taken = true
    elseif dir == "endif" then
        if not stack[#stack] then
            error("gen_entries: #endif without #if in " .. src)
        end
        stack[#stack] = nil
    elseif active() then
        local name = line:match("^TEST%(([%w_]+)%)")
        if name then names[#names + 1] = name end
    end
end
f:close()

if #stack ~= 0 then
    error("gen_entries: unterminated #if in " .. src)
end

-- Banner: dump_test.c -> [amoeba dump] -----------------------------------

local base = src:match("([^/\\]+)%.c$") or src
local tag = base:match("^(%a+)_test$")
local banner = tag and ("[amoeba " .. tag .. "]") or ("[amoeba " .. base .. "]")

-- Emit -------------------------------------------------------------------

local o = assert(io.open(out, "w"))
o:write("/* generated by misc/gen_entries.lua from " .. src .. " */\n")
o:write("/* do not edit */\n")
o:write("static const test_entry test_entries[] = {\n")
for _, n in ipairs(names) do
    o:write("    {\"" .. n .. "\", test_" .. n .. "},\n")
end
o:write("    {NULL, NULL}\n};\n")
o:write("\nint main(int argc, char *argv[]) {\n")
o:write("    return test_run(\"" .. banner ..
        "\", test_entries, argc, argv);\n")
o:write("}\n")
o:close()
