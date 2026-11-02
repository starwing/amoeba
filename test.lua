package.path = ""
local amoeba = require "amoeba"

local S = amoeba.new(true)
print(S)
local xl, xm, xr =
   S:var "xl", S:var "xm", S:var "xr"
print(xl)
print(xm)
print(xr)
print(S:constraint()
      :add(xl):add(10)
      :relation "le" -- or "<="
      :add(xr))
-- The binding owns constraints through their userdata; keep them alive.
local constraints = {
   (xm*2) :eq (xl + xr),
   S:constraint()
      :add(xl):add(10)
      :relation "le" -- or "<="
      :add(xr), -- (xl + 10) :le (xr)
   S:constraint()(xr) "<=" (100), -- (xr) :le (100)
   (xl) :ge (0)
}
for i = 1, #constraints do S:addconstraint(constraints[i]) end
print(S)
print(xl)
print(xm)
print(xr)

print('suggest xm to 0')
S:suggest(xm, 0)
print(S)
print(xl)
print(xm)
print(xr)

print('suggest xm to 70')
S:suggest(xm, 70)
print(S)
print(xl)
print(xm)
print(xr)

print('delete edit xm')
S:deledit(xm)
print(S)
print(xl)
print(xm)
print(xr)

print('dump/load into a new solver')
local data = S:dump()
local cons = {}
local S2 = amoeba.load(data, cons)
print(S2)
print(S2:var("xm"))
print(cons[1])

print('dump/load with a reader function')
local pos = 1
local S3 = amoeba.load(function()
   if pos > #data then return nil end
   local chunk = data:sub(pos, pos + 15)
   pos = pos + 16
   return chunk
end)
print(S3:var("xr"))

