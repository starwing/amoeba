
local function meta(name, parent)
   t = {}
   t.__name  = name
   t.__index = t
   return setmetatable(t, parent)
end

local function approx(a, b)
   if a > b then return a - b < 1e-6 end
   return b - a < 1e-6
end

local function near_zero(n)
   return approx(n, 0.0)
end

local function default(t, k, nv)
   local v = t[k]
   if not v then v = nv or {}; t[k] = v end
   return v
end

local Variable, Expression, Constraint do

Variable   = meta "Variable"
Expression = meta "Expression"
Constraint = meta "Constraint"

Constraint.REQUIRED = 1000000000.0
Constraint.STRONG   = 1000000.0
Constraint.MEDIUM   = 1000.0
Constraint.WEAK     = 1.0

function Variable:__neg(other) return Expression.new(self, -1.0) end
function Expression:__neg() self:multiply(-1.0) end

function Variable:__add(other) return Expression.new(self) + other end
function Variable:__sub(other) return Expression.new(self) - other end
function Variable:__mul(other) return Expression.new(self) * other end
function Variable:__div(other) return Expression.new(self) / other end

function Variable:le(other) return Expression.new(self):le(other) end
function Variable:eq(other) return Expression.new(self):eq(other) end
function Variable:ge(other) return Expression.new(self):ge(other) end

function Expression:__add(other) return Expression.new(self):add(other) end
function Expression:__sub(other) return Expression.new(self):add(-other) end
function Expression:__mul(other) return Expression.new(self):multiply(other) end
function Expression:__div(other) return Expression.new(self):multiply(1.0/other) end

function Expression:le(other) return Constraint.new("<=", self, other) end
function Expression:eq(other) return Constraint.new("==", self, other) end
function Expression:ge(other) return Constraint.new(">=", self, other) end

function Constraint:__call(...) return self:add(...) end

function Variable.new(name, type, id)
   type = type or "external"
   assert(type == "external" or
          type == "slack" or
          type == "error" or
          type == "dummy", type)
   local self = {
      id    = id,
      name  = name,
      value = 0.0,
      type  = type or "external",
      is_dummy        = type == "dummy",
      is_slack        = type == "slack",
      is_error        = type == "error",
      is_external     = type == "external",
      is_pivotable    = type == "slack" or type == "error",
      is_restricted   = type ~= "external",
   }
   return setmetatable(self, Variable)
end

function Variable:__tostring()
   return ("amoeba.Variable: %s = %g"):format(self.name, self.value)
end

function Expression.new(other, multiplier, constant)
   local self = setmetatable({}, Expression)
   return self:add(other, multiplier, constant)
end

function Expression:tostring()
   local t = { ("%g"):format(self.constant or 0.0) }
   for k, v in self:iter_vars() do
      t[#t+1] = v < 0.0 and ' - ' or ' + '
      v = math.abs(v)
      if not approx(v, 1.0) then
         t[#t+1] = ("%g*"):format(v)
      end
      t[#t+1] = k.name
   end
   return table.concat(t)
end

function Expression:__tostring()
   return "Exp: "..self:tostring()
end

function Expression:add(other, multiplier, constant)
   if other == nil then return self end
   self.constant = (self.constant or 0.0) + (constant or 0.0)
   multiplier = multiplier or 1.0
   if tonumber(other) then
      self.constant = self.constant + other*multiplier
      return self
   end
   local mt = getmetatable(other)
   if mt == Variable then
      local multiplier = (self[other] or 0.0) + multiplier
      self[other] = not near_zero(multiplier) and multiplier or nil
   elseif mt == Expression then
      for k, v in pairs(other) do
         local multiplier = (self[k] or 0.0) + multiplier * v
         self[k] = not near_zero(multiplier) and multiplier or nil
      end
      self.constant = self.constant or 0.0
   else
      error("constant/variable/expression expected")
   end
   return self
end

function Expression:multiply(other)
   if tonumber(other) then
      for k, v in pairs(self) do
         self[k] = v * other
      end
      return self
   end
   local mt = getmetatable(other)
   if mt == Variable then
      return self:multiply(Expression.new(other))
   elseif mt == Expression then
      if other:is_constant() then
         return self:multiply(other.constant)
      elseif self.constant then
         local constant = self.constant
         self.constant = 0.0
         return self:add(other):multiply(constant)
      end
      error("attempt to multiply two non-constant expression")
   else
      error("number/variable/constant expression expected")
   end
end

function Expression:choose_pivotable()
   for k, v in pairs(self) do
      if k.is_pivotable then
         return k
      end
   end
end

function Expression:is_constant()
   for k, v in self:iter_vars() do
      return false
   end
   return true
end

function Expression:solve_for(new, old)
   -- expr: old ==    a[n] *new +        constant +    a[i]*      v[i]...
   -- =>    new == (1/a[n])*old - 1/a[n]*constant - (1/a[n])*a[i]*v[i]...
   local multiplier = assert(self[new])
   assert(new ~= old and not near_zero(multiplier))
   self[new] = nil
   local reciprocal = 1.0 / multiplier
   self:multiply(-reciprocal)
   if old then self[old] = reciprocal end
   return new
end

function Expression:substitute_out(var, expr)
   assert(var ~= "constant")
   local multiplier = self[var]
   if not multiplier then return end
   self[var] = nil
   self:add(expr, multiplier)
end

function Expression:iter_vars()
   return function(self, k)
      local k, v = next(self, k)
      if k == 'constant' then
         return next(self, k)
      end
      return k, v
   end, self
end

function Constraint.new(op, expr1, expr2, strength)
   local self = setmetatable({}, Constraint)
   if not op then
      self.expression = Expression.new()
   else
      self:relation(op)
      if self.op == '<=' then
         self.expression = Expression.new(expr2 or 0.0):add(expr1, -1.0)
      else
         self.expression = Expression.new(expr1 or 0.0):add(expr2, -1.0)
      end
   end
   return self:strength(strength or Constraint.REQUIRED)
end

function Constraint:__tostring()
   local repr = "amoeba.Constraint: ["..self.expression:tostring()
   if self.is_inequality then
      repr = repr .. " >= 0.0]"
   else
      repr = repr .. " == 0.0]"
   end
   return repr
end

function Constraint:add(other, multiplier, constant)
   if other == ">=" or other == "<=" or other == "==" then
      self:relation(other)
   else
      multiplier = multiplier or 1.0
      if self.op == '>=' then multiplier = -multiplier end
      self.expression:add(other, multiplier, constant)
   end
   return self
end

function Constraint:relation(op)
   assert(op == '==' or op == '<=' or op == '>=' or
          op == 'eq' or op == 'le' or op == 'ge',
          "op must be '==', '>=' or '<='")
   if op == 'eq'     then op = '=='
   elseif op == 'le' then op = '<='
   elseif op == 'ge' then op = '>=' end
   self.op = op
   if self.op ~= '==' then
      self.is_inequality = true
   end
   if self.op ~= '>=' and self.expression then
      self.expression:multiply(-1.0)
   end
   return self
end

function Constraint:strength(strength)
   if self.solver then
      self.solver:setstrength(self, strength)
   else
      self.weight = Constraint[strength] or tonumber(strength) or self.weight
      self.is_required = self.weight >= Constraint.REQUIRED
   end
   return self
end

function Constraint:clone(strength)
   local new = Constraint.new():strength(strength)
   new:add(self)
   new.op            = self.op
   new.is_inequality = self.is_inequality
   return new
end

end

local SimplexSolver = meta "SimplexSolver" do

-- implements

local function update_external_variables(self)
   for var in pairs(self.vars) do
      local row = self.rows[var]
      var.value = row and row.constant or 0.0
   end
end

local function substitute_out(self, var, expr)
   for k, row in pairs(self.rows) do
      row:substitute_out(var, expr)
      if k.is_restricted and row.constant < 0.0 then
         self.infeasible_rows[#self.infeasible_rows+1] = k
      end
   end
   self.objective:substitute_out(var, expr)
end

local function optimize(self, objective)
   objective = objective or self.objective
   while true do
      local entry, exit
      for var, multiplier in objective:iter_vars() do
         if not var.is_dummy and multiplier < 0.0 then
            entry = var
            break
         end
      end
      if not entry then return end

      local r = 0.0
      local min_ratio = math.huge
      for var, row in pairs(self.rows) do
         local multiplier = row[entry]
         if multiplier and var.is_pivotable and multiplier < 0.0 then
            r = -row.constant / multiplier
            if r < min_ratio or (approx(r, min_ratio) and
                                 var.id < exit.id) then
               min_ratio, exit = r, var
            end
         end
      end
      assert(exit, "objective function is unbounded")

      -- do pivot
      local row = self.rows[exit]
      self.rows[exit] = nil
      row:solve_for(entry, exit)
      substitute_out(self, entry, row)
      if objective ~= self.objective then
         objective:substitute_out(entry, row)
      end
      self.rows[entry] = row
   end
end

local function make_variable(self, type)
   local id = self.last_varid
   self.last_varid = id + 1
   local prefix = type == "eplus" and "ep" or
                  type == "eminus" and "em" or
                  type == "dummy" and "d" or
                  type == "artificial" and "a" or "s"
   if not type or type == "artificial" then
      type = "slack"
   elseif type == "eplus" or type == "eminus" then
      type = "error"
   end
   return Variable.new(prefix..id, type, id)
end

local function make_expression(self, cons)
   local expr = Expression.new(cons.expression.constant)
   local var1, var2
   for k, v in cons.expression:iter_vars() do
      if not k.id then
         k.id = self.last_varid
         self.last_varid = k.id + 1
      end
      if not self.vars[k] then
         self.vars[k] = true
      end
      expr:add(self.rows[k] or k, v)
   end
   if cons.is_inequality then
      var1 = make_variable(self) -- slack
      expr[var1] = -1.0
      if not cons.is_required then
         var2 = make_variable(self, "eminus")
         expr[var2] = 1.0
         self.objective[var2] = cons.weight
      end
   elseif cons.is_required then
      var1 = make_variable(self, 'dummy')
      expr[var1] = 1.0
   else
      var1 = make_variable(self, 'eplus')
      var2 = make_variable(self, 'eminus')
      expr[var1] = -1.0
      expr[var2] =  1.0
      self.objective[var1] = cons.weight
      self.objective[var2] = cons.weight
   end
   if expr.constant < 0.0 then expr:multiply(-1.0) end
   return expr, var1, var2
end

local function choose_subject(self, expr, var1, var2)
   for k, v in expr:iter_vars() do
      if k.is_external then return k end
   end
   if var1 and var1.is_pivotable then return var1 end
   if var2 and var2.is_pivotable then return var2 end
   for k, v in expr:iter_vars() do
      if not k.is_dummy then return nil end -- no luck
   end
   if not near_zero(expr.constant) then
      return nil, "unsatisfiable required constraint added"
   end
   return var1
end

local function add_with_artificial_variable(self, expr)
   local a = make_variable(self, 'artificial')
   self.last_varid = self.last_varid - 1

   self.rows[a] = expr
   optimize(self, expr)
   local row = self.rows[a]
   self.rows[a] = nil

   local success = near_zero(expr.constant)
   if row then
      if row:is_constant() then
         return success
      end
      local entering = row:choose_pivotable()
      if not entering then return false end

      row:solve_for(entering, a)
      self.rows[entering] = row
   end
   
   for var, row in pairs(self.rows) do row[a] = nil end
   self.objective[a] = nil
   return success
end

local function get_marker_leaving_row(self, marker)
   local r1, r2 = math.huge, math.huge
   local first, second, third
   for var, row in pairs(self.rows) do
      local multiplier = row[marker]
      if multiplier then
         if var.is_external then
            third = var
         elseif multiplier < 0.0 then
            local r = -row.constant / multiplier
            if r < r1 then r1 = r; first = var end
         else
            local r = row.constant / multiplier
            if r < r2 then r2 = r; second = var end
         end
      end
   end
   return first or second or third
end

local function delta_edit_constant(self, delta, var1, var2)
   local row = self.rows[var1]
   if row then
      row.constant = row.constant - delta
      if row.constant < 0.0 then
         self.infeasible_rows[#self.infeasible_rows+1] = var1
      end
      return
   end
   local row = self.rows[var2]
   if row then
      row.constant = row.constant + delta
      if row.constant < 0.0 then
         self.infeasible_rows[#self.infeasible_rows+1] = var2
      end
      return
   end
   for var, row in pairs(self.rows) do
      row.constant = row.constant + (row[var1] or 0.0)*delta
      if var.is_restricted and row.constant < 0.0 then
         self.infeasible_rows[#self.infeasible_rows+1] = var
      end
   end
end

local function dual_optimize(self)
   while true do
      local count = #self.infeasible_rows
      if count == 0 then return end
      local exit = self.infeasible_rows[count]
      self.infeasible_rows[count] = nil

      local row = self.rows[exit]
      if row and row.constant < 0.0 then
         local entry
         local min_ratio = math.huge
         for var, multiplier in row:iter_vars() do
            if multiplier > 0.0 and not var.is_dummy then
               local r = (self.objective[var] or 0.0) / multiplier
               if r < min_ratio then
                  min_ratio, entry = r, var
               end
            end
         end
         assert(entry, "dual optimize failed")

         -- pivot
         self.rows[exit] = nil
         row:solve_for(entry, exit)
         substitute_out(self, entry, row)
         self.rows[entry] = row
      end
   end
end

-- interface

function SimplexSolver:hasvariable(var) return self.vars[var] end
function SimplexSolver:hasconstraint(cons) return self.constraints[cons] end
function SimplexSolver:hasedit(var) return self.edits[var] end
function SimplexSolver:var(...) return Variable.new(...) end
function SimplexSolver:constraint(...) return Constraint.new(...) end

function SimplexSolver.new()
   local self = {}
   self.last_varid = 1

   self.vars        = {}
   self.edits       = {}
   self.constraints = {}

   self.objective       = Expression.new()
   self.rows            = {}
   self.infeasible_rows = {}

   return setmetatable(self, SimplexSolver)
end

function SimplexSolver:__tostring()
   local t = { "amoeba.Solver: {\n" }
   t[#t+1] = ("  objective = %s\n"):format(self.objective:tostring())
   if next(self.rows) then
      t[#t+1] =  "  rows:\n"
      local keys = {}
      for k, v in pairs(self.rows) do
         keys[#keys+1] = k
      end
      table.sort(keys, function(a, b) return a.id < b.id end)
      for idx, k in ipairs(keys) do local v = self.rows[k]
         t[#t+1] = ("    %d. %s(%g) = %s\n"):format(idx, k.name, k.value, v:tostring())
      end
   end
   if next(self.edits) then
      t[#t+1] = "  edits:\n"
      local idx = 1
      for k, v in pairs(self.edits) do
         t[#t+1] = ("    %d. %s = %s; info = { %s, %s, %g }\n"):format(
            idx, k.name, k.value, tostring(v.plus), tostring(v.minus),
            v.prev_constant)
         idx = idx + 1
      end
   end
   if #self.infeasible_rows ~= 0 then
      t[#t+1] = "  infeasible_rows: {"
      for _, var in ipairs(self.infeasible_rows) do
         t[#t+1] = (" %s"):format(var.name)
      end
      t[#t+1] = " }\n"
   end
   if #self.vars ~= 0 then
      t[#t+1] = " vars: {"
      for var in pairs(self.vars) do
         t[#t+1] = (" %s"):format(var.name)
      end
      t[#t+1] = " }\n"
   end
   t[#t+1] = "}"
   return table.concat(t)
end

function SimplexSolver:addconstraint(cons, ...)
   if getmetatable(cons) ~= Constraint then
      cons = Constraint.new(cons, ...)
   end
   if self.constraints[cons] then return cons end
   local expr, var1, var2 = make_expression(self, cons)
   local subject, err = choose_subject(self, expr, var1, var2)
   if subject then
      expr:solve_for(subject)
      substitute_out(self, subject, expr)
      self.rows[subject] = expr
   elseif err then
      return nil, err
   elseif not add_with_artificial_variable(self, expr) then
      return nil, "constraint added may unbounded"
   end
   self.constraints[cons] = {
      marker = var1,
      other = var2,
   }
   cons.solver = self
   optimize(self)
   update_external_variables(self)
   return cons
end

function SimplexSolver:delconstraint(cons)
   local info = self.constraints[cons]
   if not info then return end
   self.constraints[cons] = nil

   if info.marker and info.marker.is_error then
      self.objective:add(self.rows[info.marker] or info.marker, -cons.weight)
   end
   if info.other and info.other.is_error then
      self.objective:add(self.rows[info.other] or info.other, -cons.weight)
   end
   if self.objective:is_constant() then
      self.objective.constant = 0.0
   end

   local row = self.rows[info.marker]
   if row then
      self.rows[info.marker] = nil
   else
      local var = assert(get_marker_leaving_row(self, info.marker),
                         "failed to find leaving row")
      local row = self.rows[var]
      self.rows[var] = nil
      row:solve_for(info.marker, var)
      substitute_out(self, info.marker, row)
   end
   cons.solver = nil
   optimize(self)
   update_external_variables(self)
   return cons
end

function SimplexSolver:addedit(var, strength)
   if self.edits[var] then return end
   strength = strength or Constraint.MEDIUM
   assert(strength < Constraint.REQUIRED, "attempt to edit a required var")
   local cons = Constraint.new("==", var, var.value, strength)
   assert(self:addconstraint(cons))
   local info = self.constraints[cons]
   self.edits[var] = {
      constraint = cons,
      plus = info.marker,
      minus = info.other, 
      prev_constant = var.value or 0.0,
   }
   return self
end

function SimplexSolver:deledit(var)
   local info = self.edits[var]
   if info then
      self:delconstraint(info.constraint)
      self.edits[var] = nil
   end
end

function SimplexSolver:suggest(var, value)
   local info = self.edits[var]
   if not info then self:addedit(var); info = self.edits[var] end
   local delta = value - info.prev_constant
   info.prev_constant = value
   delta_edit_constant(self, delta, info.plus, info.minus)
   dual_optimize(self)
   update_external_variables(self)
end

function SimplexSolver:setstrength(cons, strength)
   local info = self.constraints[cons]
   if not info then cons.weight = strength end
   assert(info.marker and info.marker.is_error, "attempt to change required strength")
   local multiplier = strength / cons.strength
   cons.weight = strength
   self.is_required = self.weight >= Constraint.REQUIRED
   if near_zero(diff) then return self end

   self.objective:add(self.rows[info.marker] or info.marker, multiplier)
   self.objective:add(self.rows[info.other] or info.other, multiplier)
   optimize(self)
   update_external_variables(self)
   return self
end

function SimplexSolver:resolve()
   dual_optimize(self)
   set_external_variables()
   reset_stay_constant(self)
   self.infeasible_rows = {}
end

function SimplexSolver:set_constant(cons, constant)
   local info = self.constraints[cons]
   if not info then return end
   local delta = info.prev_constant - constant
   info.prev_constant = constant

   if info.marker.is_slack or cons.is_required then
      for var, row in pairs(self.rows) do
         row:add((row[info.marker] or 0.0) * -delta)
         if var.is_restricted and row.constant < 0.0 then
            self.infeasible_rows[#self.infeasible_rows+1] = var
         end
      end
   else
      delta_edit_constant(self, delta, info.marker, info.other)
   end
   dual_optimize(self)
   update_external_variables(self)
end

end

return SimplexSolver
