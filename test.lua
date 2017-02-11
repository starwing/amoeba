package.path = ""
local amoeba = require "amoeba"

local S = amoeba.new()
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
S:addconstraint((xm*2) :eq (xl + xr))
S:addconstraint(
   S:constraint()
      :add(xl):add(10)
      :relation "le" -- or "<="
      :add(xr)) -- (xl + 10) :le (xr)
S:addconstraint(
   S:constraint()(xr) "<=" (100)) -- (xr) :le (100)
S:addconstraint((xl) :ge (0))
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

