#define LUA_LIB
#include <lauxlib.h>
#include <lua.h>
#include <string.h>

#define AM_STATIC_API
#include "amoeba.h"

#if LUA_VERSION_NUM < 502
/* Lua 5.1 shims */

/* clang-format off */
static int aml_relindex(int idx, int offset)
{ return idx < 0 && idx > LUA_REGISTRYINDEX ? idx - offset : idx; }

static void aml_rawsetp(lua_State *L, int idx, const void *p) {
    lua_pushlightuserdata(L, (void *)p);
    lua_insert(L, -2); /* the table index shifts by one after the push */
    lua_rawset(L, aml_relindex(idx, 1));
}

static void aml_rawgetp(lua_State *L, int idx, const void *p)
{ lua_pushlightuserdata(L, (void *)p); lua_rawget(L, aml_relindex(idx, 1)); }
/* clang-format on */
#else
# define aml_rawsetp lua_rawsetp
# define aml_rawgetp lua_rawgetp
#endif

#define aml_pushstore(L, S) aml_rawgetp((L), LUA_REGISTRYINDEX, (S))

#define AML_SOLVER_TYPE "amoeba.Solver"
#define AML_VAR_TYPE    "amoeba.Variable"
#define AML_CONS_TYPE   "amoeba.Constraint"

typedef struct aml_Var {
    am_Num      value;
    am_Id       var;
    am_Solver  *S;
    const char *name; /* the string owned by store[handle] */
} aml_Var;

/* utils */

static int aml_argferror(lua_State *L, int idx, const char *fmt, ...) {
    va_list l;
    va_start(l, fmt);
    lua_pushvfstring(L, fmt, l);
    va_end(l);
    return luaL_argerror(L, idx, lua_tostring(L, -1));
}

static int aml_typeerror(lua_State *L, int idx, const char *tname) {
    return aml_argferror(L, idx, "%s expected, got %s", tname,
                         luaL_typename(L, idx));
}

static const char *aml_codename(int ret) {
    switch (ret) {
    case AM_PARAM: return "PARAM";
    case AM_FAILED: return "FAILED";
    case AM_UNSATISFIED: return "UNSATISFIED";
    case AM_UNBOUND: return "UNBOUND";
    }
    return NULL;
}

static int aml_pusherror(lua_State *L, int ret) {
    const char *name;
    if (ret == AM_OK) return lua_settop(L, 1), 1;
    name = aml_codename(ret);
    lua_pushnil(L);
    if (name)
        lua_pushstring(L, name);
    else
        lua_pushfstring(L, "Unknown Error: %d", ret);
    lua_pushinteger(L, ret);
    return 3;
}

static int aml_raise(lua_State *L, const char *what, int ret) {
    const char *name = aml_codename(ret);
    if (name) return luaL_error(L, "%s: %s", what, name);
    return luaL_error(L, "%s: error %d", what, ret);
}

static am_Solver *aml_checksolver(lua_State *L, int idx) {
    am_Solver **S = (am_Solver **)luaL_checkudata(L, idx, AML_SOLVER_TYPE);
    luaL_argcheck(L, *S != NULL, idx, "solver deleted");
    return *S;
}

static int aml_invalidate(lua_State *L, am_Solver *S) {
    aml_Var        *lvar;
    am_Constraint **pc;
    /* handles can outlive the solver by one GC round */
    aml_rawgetp(L, LUA_REGISTRYINDEX, S);
    /* an out-of-memory hook can die before the store exists */
    if (!lua_istable(L, -1)) return lua_pop(L, 1), 0;
    for (lua_pushnil(L); lua_next(L, -2); lua_pop(L, 1)) {
        lvar = (aml_Var *)luaL_testudata(L, -1, AML_VAR_TYPE);
        pc = (am_Constraint **)luaL_testudata(L, -1, AML_CONS_TYPE);
        if (lvar != NULL) lvar->var = 0, lvar->name = NULL, lvar->S = NULL;
        if (pc != NULL) *pc = NULL;
    }
    return lua_pop(L, 1), 1;
}

static am_Id aml_checkvar(lua_State *L, am_Solver *S, int idx) {
    aml_Var    *lvar = (aml_Var *)luaL_testudata(L, idx, AML_VAR_TYPE);
    const char *name;
    if (lvar == NULL) {
        name = luaL_checkstring(L, idx);
        aml_pushstore(L, S);
        lua_getfield(L, -1, name);
        lvar = (aml_Var *)luaL_testudata(L, -1, AML_VAR_TYPE);
        lua_pop(L, 2);
        if (lvar == NULL)
            return aml_argferror(L, idx, "variable named '%s' not exists",
                                 lua_tostring(L, idx));
    }
    luaL_argcheck(L, lvar->var != 0, idx, "invalid variable");
    luaL_argcheck(L, lvar->S == S, idx, "variable from another solver");
    return lvar->var;
}

static am_Constraint *aml_checkcons(lua_State *L, int idx) {
    am_Constraint **pc;
    pc = (am_Constraint **)luaL_checkudata(L, idx, AML_CONS_TYPE);
    return luaL_argcheck(L, *pc != NULL, idx, "invalid constraint"), *pc;
}

static void aml_setconsname(lua_State *L, int udidx, int nameidx) {
    am_Constraint *cons = aml_checkcons(L, udidx);
    aml_pushstore(L, *(am_Solver **)cons);
    lua_pushvalue(L, udidx), lua_pushvalue(L, nameidx);
    lua_rawset(L, -3), lua_pop(L, 1); /* the store owns the name */
}

static am_Constraint **aml_registercons(lua_State *L, am_Constraint *cons) {
    am_Constraint **pc = (am_Constraint **)lua_newuserdata(L, sizeof(*pc));
    am_Solver      *S = *(am_Solver **)cons;
    *pc = cons;
    luaL_setmetatable(L, AML_CONS_TYPE);
    aml_pushstore(L, S);
    lua_pushvalue(L, -2), aml_rawsetp(L, -2, cons);
    lua_pop(L, 1);
    return pc;
}

static am_Constraint *aml_newcons(lua_State *L, am_Solver *S, am_Num strength) {
    am_Constraint *cons = am_newconstraint(S, strength);
    if (cons == NULL) luaL_error(L, "Create constraint failed");
    *(am_Solver **)cons = S;
    return aml_registercons(L, cons), cons;
}

static int aml_additem(lua_State *L, am_Constraint *c, int idx, am_Num coef) {
    am_Solver      *S = *(am_Solver **)c;
    aml_Var        *v;
    am_Constraint **o;
    if (lua_type(L, idx) == LUA_TNUMBER)
        return am_addconstant(c, (am_Num)lua_tonumber(L, idx) * coef);
    if (lua_type(L, idx) == LUA_TSTRING)
        return am_addterm(c, aml_checkvar(L, S, idx), coef);
    if ((v = (aml_Var *)luaL_testudata(L, idx, AML_VAR_TYPE)) != NULL) {
        luaL_argcheck(L, v->var != 0, idx, "invalid variable");
        luaL_argcheck(L, v->S == S, idx, "variable from another solver");
        return am_addterm(c, v->var, coef);
    }
    o = (am_Constraint **)luaL_testudata(L, idx, AML_CONS_TYPE);
    if (o != NULL) {
        luaL_argcheck(L, *o != NULL, idx, "invalid constraint");
        luaL_argcheck(L, *(am_Solver **)*o == S, idx,
                      "constraint from another solver");
        return am_merge(c, *o, coef);
    }
    return aml_typeerror(L, idx, "number/variable/constraint");
}

static am_Num aml_checkstrength(lua_State *L, int idx, am_Num def) {
    int         type = lua_type(L, idx);
    const char *s;
    if (type == LUA_TNONE || type == LUA_TNIL) return def;
    if (type == LUA_TNUMBER) {
        am_Num r = lua_tonumber(L, idx);
        luaL_argcheck(L, r >= 0, idx, "strength must be non-negative");
        return r;
    }
    if (!(s = lua_tostring(L, idx)))
        return aml_typeerror(L, idx, "number/string"), 0.0f;
    if (strcmp(s, "required") == 0) return AM_REQUIRED;
    if (strcmp(s, "strong") == 0) return AM_STRONG;
    if (strcmp(s, "medium") == 0) return AM_MEDIUM;
    if (strcmp(s, "weak") == 0) return AM_WEAK;
    return aml_argferror(L, idx, "invalid strength value '%s'", s), 0.0f;
}

static int aml_checkrelation(lua_State *L, int idx) {
    const char *op = luaL_checkstring(L, idx);
    if (strcmp(op, "==") == 0) return AM_EQUAL;
    if (strcmp(op, "<=") == 0) return AM_LESSEQUAL;
    if (strcmp(op, ">=") == 0) return AM_GREATEREQUAL;
    if (strcmp(op, "eq") == 0) return AM_EQUAL;
    if (strcmp(op, "le") == 0) return AM_LESSEQUAL;
    if (strcmp(op, "ge") == 0) return AM_GREATEREQUAL;
    return aml_argferror(L, idx, "invalid relation operator: '%s'", op);
}

static int aml_isrelation(lua_State *L, int idx) {
    const char *s;
    if (lua_type(L, idx) != LUA_TSTRING) return 0;
    s = lua_tostring(L, idx);
    return s[0] == '<' || s[0] == '>' || s[0] == '=';
}

static int aml_dumpkey(luaL_Buffer *B, int idx, am_Symbol sym) {
    lua_State *L = B->L;
    aml_Var   *lvar;
    int        ch = 'v';
    lua_rawgeti(L, idx, sym.u.id);
    lvar = (aml_Var *)luaL_testudata(L, -1, AML_VAR_TYPE);
    lua_pop(L, 1);
    if (lvar) return luaL_addstring(B, lvar->name), 0;
    switch (sym.u.type) {
    case AM_EXTERNAL: ch = 'v'; break;
    case AM_SLACK: ch = 's'; break;
    case AM_ERROR: ch = 'e'; break;
    case AM_DUMMY: ch = 'd'; break;
    }
    lua_pushfstring(L, "%c%d", ch, sym.u.id);
    return luaL_addvalue(B), 1;
}

/* clang-format off */
static am_Num *aml_slot(const am_Table *t, am_Size i)
{ return (am_Num *)((char *)t->vals + (size_t)i * t->value_size); }
/* clang-format on */

static void aml_dumprow(luaL_Buffer *B, int idx, am_Row *row, am_Num sign) {
    lua_State *L = B->L;
    am_Size    i;
    lua_pushfstring(L, "%f", row->constant * sign), luaL_addvalue(B);
    for (i = 0; i < row->terms.size; ++i) {
        am_Symbol key = row->terms.keys[i].sym;
        am_Num    coef;
        if (key.u.id == 0) continue;
        coef = *aml_slot(&row->terms, i) * sign;
        lua_pushfstring(L, " %c ", coef > 0.0f ? '+' : '-'), luaL_addvalue(B);
        if (coef < 0.0f) coef = -coef;
        if (coef != 1.0f) lua_pushfstring(L, "%f*", coef), luaL_addvalue(B);
        aml_dumpkey(B, idx, key);
    }
}

/* variable */

static void aml_bindvar(lua_State *L, aml_Var *lvar, int nameidx) {
    int h;
    luaL_setmetatable(L, AML_VAR_TYPE);
    h = lua_gettop(L);
    aml_pushstore(L, lvar->S);
    lua_pushvalue(L, nameidx);
    lvar->name = lua_tostring(L, -1);
    lua_pushvalue(L, -1), lua_pushvalue(L, h);
    lua_rawset(L, -4); /* the store keeps the handle reachable by name */
    lua_pushvalue(L, h);
    lua_rawseti(L, -3, (lua_Integer)lvar->var);
    lua_pushvalue(L, h), lua_pushvalue(L, -2);
    lua_rawset(L, -4); /* the store keeps lvar->name alive */
    lua_pop(L, 2);
}

static aml_Var *aml_newvar(lua_State *L, am_Solver *S, int nameidx) {
    aml_Var *lvar = (aml_Var *)lua_newuserdata(L, sizeof(aml_Var));
    lvar->value = 0; /* am_newvariable() does not initialise the binding */
    lvar->var = am_newvariable(S, &lvar->value);
    if (lvar->var == 0) luaL_error(L, "Create variable failed");
    return (lvar->S = S), aml_bindvar(L, lvar, nameidx), lvar;
}

static int Lvar_new(lua_State *L) {
    am_Solver *S = aml_checksolver(L, 1);
    int        type = lua_type(L, 2);
    size_t     len;
    luaL_argcheck(L, type == LUA_TSTRING || type == LUA_TNUMBER, 2,
                  "variable name expected");
    aml_pushstore(L, S);
    lua_pushvalue(L, 2), lua_rawget(L, -2);
    if (lua_type(L, -1) != LUA_TNIL) return 1;
    if (type == LUA_TNUMBER)
        aml_argferror(L, 2, "variable#%d not exists", lua_tointeger(L, 2));
    lua_pop(L, 1);
    luaL_checklstring(L, 2, &len);
    luaL_argcheck(L, len != 0 && len < 256, 2,
                  "variable name must be 1..255 bytes");
    return aml_newvar(L, S, 2), 1;
}

static int Lvar_delete(lua_State *L) {
    aml_Var   *lvar = (aml_Var *)luaL_testudata(L, 1, AML_VAR_TYPE);
    am_Solver *S;
    if (lvar == NULL) return 0;
    S = lvar->S;
    if (lvar->var == 0 || S == NULL)
        return (lvar->var = 0, lvar->name = NULL, lvar->S = NULL), 0;
    /* a variable used by a constraint stays alive */
    luaL_argcheck(L, am_refcount(S, lvar->var) <= 1, 1,
                  "variable is used by a constraint");
    aml_pushstore(L, S);
    lua_pushnil(L), lua_setfield(L, -2, lvar->name);
    lua_pushnil(L), lua_rawseti(L, -2, (lua_Integer)lvar->var);
    lua_pushvalue(L, 1), lua_pushnil(L), lua_rawset(L, -3);
    lua_pop(L, 1);
    am_delvariable(S, lvar->var);
    return (lvar->var = 0, lvar->name = NULL, lvar->S = NULL), 0;
}

static int Lvar_value(lua_State *L) {
    aml_Var *lvar = (aml_Var *)luaL_checkudata(L, 1, AML_VAR_TYPE);
    luaL_argcheck(L, lvar->var != 0, 1, "invalid variable");
    return lua_pushnumber(L, lvar->value), 1;
}

static int Lvar_name(lua_State *L) {
    aml_Var *lvar = (aml_Var *)luaL_checkudata(L, 1, AML_VAR_TYPE);
    luaL_argcheck(L, lvar->var != 0, 1, "invalid variable");
    return lua_pushstring(L, lvar->name), 1;
}

static int Lvar_tostring(lua_State *L) {
    aml_Var *lvar = (aml_Var *)luaL_checkudata(L, 1, AML_VAR_TYPE);
    if (!lvar->var) return lua_pushstring(L, AML_VAR_TYPE ": deleted"), 1;
    lua_pushfstring(L, AML_VAR_TYPE "(%p): %s = %f", lvar->var, lvar->name,
                    lvar->value);
    return 1;
}

static void open_variable(lua_State *L) {
    luaL_Reg libs[] = {{"__tostring", Lvar_tostring},
#define ENTRY(name) {#name, Lvar_##name}
                       ENTRY(new),
                       ENTRY(delete),
                       ENTRY(value),
                       ENTRY(name),
#undef ENTRY
                       {NULL, NULL}};
    if (luaL_newmetatable(L, AML_VAR_TYPE)) {
        luaL_setfuncs(L, libs, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }
}

/* constraint */

static int Lcons_new(lua_State *L) {
    am_Solver *S = aml_checksolver(L, 1);
    return aml_newcons(L, S, aml_checkstrength(L, 2, AM_REQUIRED)), 1;
}

static int Lcons_delete(lua_State *L) {
    am_Constraint **pc = (am_Constraint **)luaL_testudata(L, 1, AML_CONS_TYPE);
    am_Constraint  *cons;
    am_Solver      *S;
    if (pc == NULL || *pc == NULL) return 0;
    cons = *pc, S = *(am_Solver **)cons;
    *pc = NULL; /* every later call sees "invalid constraint" */
    /* unregister first: am_delconstraint() recycles the pointer immediately */
    aml_pushstore(L, S);
    lua_pushnil(L), aml_rawsetp(L, -2, cons);
    lua_pushvalue(L, 1), lua_pushnil(L), lua_rawset(L, -3);
    lua_pop(L, 1);
    return am_delconstraint(cons), 0;
}

static int Lcons_add(lua_State *L) {
    am_Constraint *cons = aml_checkcons(L, 1);
    am_Num         coef;
    int            i, idx, ret, top = lua_gettop(L);
    for (i = 2; i <= top; ++i) {
        if (aml_isrelation(L, i)) {
            if (am_relation(cons) != 0)
                return aml_argferror(L, i, "relation already set");
            if (am_hasconstraint(cons))
                return luaL_error(L, "constraint has been added to solver");
            ret = am_setrelation(cons, aml_checkrelation(L, i));
            luaL_argcheck(L, ret == AM_OK, i, "cannot set the relation");
        } else if (lua_type(L, i) != LUA_TNIL) { /* nil means omitted */
            idx = i, coef = 1.0f;
            if (lua_type(L, i + 1) == LUA_TNUMBER)
                coef = (am_Num)lua_tonumber(L, ++i);
            ret = aml_additem(L, cons, idx, coef);
            if (ret != AM_OK) return aml_raise(L, "cannot add the item", ret);
        }
    }
    return lua_settop(L, 1), 1;
}

static int Lcons_relation(lua_State *L) {
    am_Constraint *cons = aml_checkcons(L, 1);
    int            rel, ret;
    if (lua_isnoneornil(L, 2)) {
        switch (rel = am_relation(cons)) {
        case AM_EQUAL: return lua_pushstring(L, "=="), 1;
        case AM_LESSEQUAL: return lua_pushstring(L, "<="), 1;
        case AM_GREATEREQUAL: return lua_pushstring(L, ">="), 1;
        }
        return lua_pushnil(L), 1; /* 0 from am_relation() = no relation set */
    }
    rel = aml_checkrelation(L, 2);
    if (am_hasconstraint(cons))
        return luaL_error(L, "constraint has been added to solver");
    if (am_relation(cons) != 0) return luaL_error(L, "relation already set");
    ret = am_setrelation(cons, rel);
    if (ret != AM_OK) return aml_raise(L, "cannot set the relation", ret);
    return lua_settop(L, 1), 1;
}

static int Lcons_strength(lua_State *L) {
    am_Constraint *cons = aml_checkcons(L, 1);
    am_Num         strength;
    int            ret;
    if (lua_isnoneornil(L, 2)) return lua_pushnumber(L, am_strength(cons)), 1;
    strength = aml_checkstrength(L, 2, AM_REQUIRED);
    ret = am_setstrength(cons, strength);
    if (ret != AM_OK) return aml_raise(L, "cannot set the strength", ret);
    return lua_settop(L, 1), 1;
}

/* clang-format off */
static int Lcons_reset(lua_State *L)
{ return am_resetconstraint(aml_checkcons(L, 1)), lua_settop(L, 1), 1; }

static int Lcons_constant(lua_State *L)
{ return lua_pushnumber(L, am_constant(aml_checkcons(L, 1))), 1; }

static int Lcons_error(lua_State *L)
{ return lua_pushnumber(L, am_error(aml_checkcons(L, 1))), 1; }
/* clang-format on */

static int Lcons_terms(lua_State *L) {
    am_Constraint *cons = aml_checkcons(L, 1);
    am_Solver     *S = *(am_Solver **)cons;
    am_Id          var = 0;
    am_Num         coef;
    int            n = 0;
    lua_createtable(L, 0, 4);
    while ((var = am_terms(cons, var, &coef)) != 0) {
        lua_createtable(L, 0, 2);
        aml_pushstore(L, S);
        lua_rawgeti(L, -1, (lua_Integer)var);
        lua_remove(L, -2);
        if (luaL_testudata(L, -1, AML_VAR_TYPE) == NULL)
            return luaL_error(L, "variable %d has no handle", (int)var);
        lua_setfield(L, -2, "var");
        lua_pushnumber(L, coef), lua_setfield(L, -2, "coef");
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

static int Lcons_name(lua_State *L) {
    am_Constraint *cons = aml_checkcons(L, 1);
    size_t         len;
    if (lua_isnoneornil(L, 2)) {
        aml_pushstore(L, *(am_Solver **)cons);
        lua_pushvalue(L, 1), lua_rawget(L, -2);
        return lua_remove(L, -2), 1;
    }
    luaL_checklstring(L, 2, &len);
    luaL_argcheck(L, len != 0 && len < AM_NAME_LEN, 2, "name length exceed");
    return aml_setconsname(L, 1, 2), lua_settop(L, 1), 1;
}

static int Lcons_tostring(lua_State *L) {
    am_Constraint **pc;
    am_Constraint  *cons;
    am_Solver      *S;
    int             rel;
    luaL_Buffer     B;
    pc = (am_Constraint **)luaL_checkudata(L, 1, AML_CONS_TYPE);
    if ((cons = *pc) == NULL)
        return lua_pushstring(L, AML_CONS_TYPE ": deleted"), 1;
    S = *(am_Solver **)cons;
    lua_settop(L, 1);
    aml_pushstore(L, S);
    luaL_buffinit(L, &B);
    lua_pushfstring(L, AML_CONS_TYPE "(%p): [", cons), luaL_addvalue(&B);
    rel = am_relation(cons);
    /* <= and == rows are stored negated, so undo that for display */
    aml_dumprow(&B, 2, &cons->expression,
                rel == AM_LESSEQUAL || rel == AM_EQUAL ? -1.0f : 1.0f);
    if (rel == AM_EQUAL)
        luaL_addstring(&B, " == 0.0]");
    else if (rel == AM_LESSEQUAL)
        luaL_addstring(&B, " <= 0.0]");
    else
        luaL_addstring(&B, " >= 0.0]");
    if (cons->marker.u.id != 0) {
        luaL_addstring(&B, "(added:");
        aml_dumpkey(&B, 2, cons->marker);
        luaL_addchar(&B, '-');
        aml_dumpkey(&B, 2, cons->other);
        luaL_addchar(&B, ')');
    }
    return luaL_pushresult(&B), 1;
}

static void open_constraint(lua_State *L) {
    luaL_Reg libs[] = {{"__call", Lcons_add},
                       {"__bor", Lcons_strength},
                       {"__tostring", Lcons_tostring},
#define ENTRY(name) {#name, Lcons_##name}
                       ENTRY(new),
                       ENTRY(delete),
                       ENTRY(reset),
                       ENTRY(add),
                       ENTRY(relation),
                       ENTRY(strength),
                       ENTRY(constant),
                       ENTRY(error),
                       ENTRY(terms),
                       ENTRY(name),
#undef ENTRY
                       {NULL, NULL}};
    if (luaL_newmetatable(L, AML_CONS_TYPE)) {
        luaL_setfuncs(L, libs, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }
}

/* dump & load */

typedef struct aml_Dumper {
    am_Dumper    base;
    luaL_Buffer *B;
    am_Solver   *S;
} aml_Dumper;

/* clang-format off */
static const char *aml_varname(am_Dumper *d, unsigned i, am_Id var, am_Num *v)
{ return (void)d, (void)i, (void)var, ((aml_Var *)v)->name; }
/* clang-format on */

static const char *aml_consname(am_Dumper *d, unsigned i, am_Constraint *cons) {
    aml_Dumper *ld = (aml_Dumper *)d;
    lua_State  *L = ld->B->L;
    const char *name = NULL;
    (void)i;
    aml_pushstore(L, ld->S);
    aml_rawgetp(L, -1, cons);
    if (lua_type(L, -1) == LUA_TUSERDATA) {
        lua_pushvalue(L, -1), lua_rawget(L, -3);
        name = lua_tostring(L, -1); /* kept alive by the store */
        lua_pop(L, 1);
    }
    lua_pop(L, 2);
    return name;
}

static int aml_writer(am_Dumper *d, const void *buf, size_t len) {
    aml_Dumper *ld = (aml_Dumper *)d;
    return luaL_addlstring(ld->B, buf, len), AM_OK;
}

static int Ldump(lua_State *L) {
    am_Solver  *S = aml_checksolver(L, 1);
    luaL_Buffer B;
    aml_Dumper  d;
    int         ret;
    memset(&d, 0, sizeof(d));
    d.base.var_name = aml_varname;
    d.base.cons_name = aml_consname;
    d.base.writer = aml_writer;
    d.S = S;
    d.B = &B;
    luaL_buffinit(L, &B);
    ret = am_dump(S, &d.base);
    if (ret == AM_OK) return luaL_pushresult(&B), 1;
    return aml_pusherror(L, ret);
}

typedef struct aml_Loader {
    am_Loader   base;
    lua_State  *L;
    const char *s;
    size_t      len;

    int reader_index;  /* stack index of the function, 0 for a string */
    int reserve_index; /* stack slot holding the last reader chunk */
    int cons_index;    /* caller's table, 0 when absent */
    int pending_index; /* handles to copy out once the load succeeds */
    int solver_index;  /* solver userdata, 0 before the hook runs */

    int        errref; /* registry ref to a reader error, LUA_NOREF */
    am_Solver *S;      /* the solver handed over by the hook */
} aml_Loader;

static void aml_lsolver(am_Loader *l, const am_Solver *S) {
    aml_Loader *ll = (aml_Loader *)l;
    lua_State  *L = ll->L;
    am_Solver **ps = (am_Solver **)lua_newuserdata(L, sizeof(*ps));
    *ps = (am_Solver *)S;
    ll->S = (am_Solver *)S;
    ll->solver_index = lua_gettop(L);
    /* __gc must be reachable before any later allocation fails */
    luaL_setmetatable(L, AML_SOLVER_TYPE);
    lua_createtable(L, 0, 8);
    aml_rawsetp(L, LUA_REGISTRYINDEX, S);
    if (ll->cons_index)
        lua_createtable(L, 0, 8), ll->pending_index = lua_gettop(L);
}

static am_Num *aml_lvar(am_Loader *l, am_Name name, unsigned i, am_Id var) {
    aml_Loader *ll = (aml_Loader *)l;
    lua_State  *L = ll->L;
    aml_Var    *lvar;
    int         nameidx;
    (void)i;
    lua_pushstring(L, name ? name : "");
    nameidx = lua_gettop(L);
    lvar = (aml_Var *)lua_newuserdata(L, sizeof(aml_Var));
    lvar->value = 0, lvar->var = var, lvar->S = ll->S;
    aml_bindvar(L, lvar, nameidx);
    lua_pop(L, 2);       /* the store keeps the handle and its name */
    return &lvar->value; /* the loader writes through it */
}

static void aml_lcons(am_Loader *l, am_Name n, unsigned i, am_Constraint *c) {
    aml_Loader *ll = (aml_Loader *)l;
    lua_State  *L = ll->L;
    int         ud;
    *(am_Solver **)c = ll->S;
    aml_registercons(L, c);
    ud = lua_gettop(L);
    if (n != NULL) {
        lua_pushstring(L, n);
        aml_setconsname(L, ud, lua_gettop(L));
        lua_pop(L, 1);
    }
    if (ll->pending_index != 0) {
        lua_pushvalue(L, ud);
        lua_rawseti(L, ll->pending_index, (lua_Integer)i + 1);
        if (n != NULL) {
            lua_pushstring(L, n), lua_pushvalue(L, ud);
            lua_settable(L, ll->pending_index);
        }
    }
    lua_pop(L, 1);
}

static const char *aml_readererror(lua_State *L, aml_Loader *ll, size_t *plen) {
    if (ll->errref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ll->errref);
    ll->errref = luaL_ref(L, LUA_REGISTRYINDEX);
    return *plen = 0, NULL;
}

static const char *aml_reader(am_Loader *l, size_t *plen) {
    aml_Loader *ll = (aml_Loader *)l;
    lua_State  *L = ll->L;
    int         status;
    if (ll->reader_index == 0) {
        const char *p = ll->s;
        return (*plen = ll->len, ll->s = NULL, ll->len = 0), p;
    }
    luaL_checkstack(L, 2, "too many nested functions");
    lua_pushvalue(L, ll->reader_index);
    status = lua_pcall(L, 0, 1, 0);
    if (status != 0) return aml_readererror(L, ll, plen);
    if (lua_isnil(L, -1)) return lua_pop(L, 1), (*plen = 0), NULL;
    if (!lua_isstring(L, -1)) {
        lua_pop(L, 1);
        lua_pushliteral(L, "reader function must return a string");
        return aml_readererror(L, ll, plen);
    }
    lua_replace(L, ll->reserve_index); /* keep the chunk reachable until read */
    return lua_tolstring(L, ll->reserve_index, plen);
}

static int aml_loadfail(lua_State *L, aml_Loader *ll) {
    if (ll->errref != LUA_NOREF) {
        int err = ll->errref;
        ll->errref = LUA_NOREF;
        lua_pushnil(L), lua_rawgeti(L, LUA_REGISTRYINDEX, err);
        luaL_unref(L, LUA_REGISTRYINDEX, err);
        lua_pushinteger(L, AM_FAILED);
        return 3;
    }
    lua_pushnil(L);
    lua_pushliteral(L, "FAILED");
    lua_pushinteger(L, AM_FAILED);
    return 3;
}

static int Lload(lua_State *L) {
    aml_Loader ll;
    am_Solver *S;
    /* module table == solver metatable, so a solver argument is S:load */
    if (luaL_testudata(L, 1, AML_SOLVER_TYPE) != NULL)
        return luaL_error(L,
                          "load is a module function: call amoeba.load(...)");
    luaL_argcheck(L, !lua_isnoneornil(L, 1), 1, "data or reader expected");
    memset(&ll, 0, sizeof(ll)), ll.L = L, ll.errref = LUA_NOREF;
    if (lua_type(L, 1) == LUA_TSTRING) {
        ll.s = lua_tolstring(L, 1, &ll.len);
    } else {
        luaL_argcheck(L, lua_type(L, 1) == LUA_TFUNCTION, 1,
                      "string or function expected");
        ll.reader_index = 1;
        lua_pushnil(L);
        ll.reserve_index = lua_gettop(L);
    }
    if (!lua_isnone(L, 2) && !lua_isnil(L, 2)) {
        luaL_checktype(L, 2, LUA_TTABLE);
        ll.cons_index = 2;
    }
    ll.base.solver = aml_lsolver;
    ll.base.load_var = aml_lvar;
    ll.base.load_cons = aml_lcons;
    ll.base.reader = aml_reader;
    S = am_load(NULL, NULL, &ll.base, 0);
    if (S == NULL) {
        if (ll.solver_index != 0) { /* the hook ran: drop the early handle */
            am_Solver **ps = (am_Solver **)lua_touserdata(L, ll.solver_index);
            *ps = NULL;
            aml_invalidate(L, ll.S); /* the store owns the handles */
            lua_pushnil(L), aml_rawsetp(L, LUA_REGISTRYINDEX, ll.S);
        }
        return aml_loadfail(L, &ll);
    }
    if (ll.pending_index != 0) { /* commit the table only on success */
        for (lua_pushnil(L); lua_next(L, ll.pending_index); lua_pop(L, 1)) {
            lua_pushvalue(L, -2), lua_pushvalue(L, -2);
            lua_settable(L, ll.cons_index);
        }
    }
    return lua_settop(L, ll.solver_index), 1;
}

/* solver */

static int Lnew(lua_State *L) {
    am_Solver **S = (am_Solver **)lua_newuserdata(L, sizeof(*S));
    if ((*S = am_newsolver(NULL, NULL)) == NULL)
        return luaL_error(L, "create solver failed");
    lua_createtable(L, 0, 8);
    aml_rawsetp(L, LUA_REGISTRYINDEX, *S);
    luaL_setmetatable(L, AML_SOLVER_TYPE);
    return am_autoupdate(*S, lua_toboolean(L, 1)), 1;
}

static int Ldelete(lua_State *L) {
    am_Solver *S, **pS = (am_Solver **)luaL_testudata(L, 1, AML_SOLVER_TYPE);
    /* also __gc: runs on a possibly deleted solver, and can repeat */
    if (pS == NULL || *pS == NULL) return 0;
    S = *pS, *pS = NULL; /* every later call sees "deleted" */
    aml_invalidate(L, S);
    lua_pushnil(L), aml_rawsetp(L, LUA_REGISTRYINDEX, S);
    return am_delsolver(S), 0;
}

static int Ltostring(lua_State *L) {
    am_Solver  *S = aml_checksolver(L, 1);
    luaL_Buffer B;
    lua_settop(L, 1);
    aml_pushstore(L, S);
    luaL_buffinit(L, &B);
    lua_pushfstring(L, AML_SOLVER_TYPE "(%p): {", S);
    luaL_addvalue(&B);
    luaL_addstring(&B, "\n  objective = ");
    aml_dumprow(&B, 2, &S->objective, 1.0f);
    if (S->rows.count != 0) {
        am_Size i;
        int     idx = 0;
        lua_pushfstring(L, "\n  rows(%d):", S->rows.count);
        luaL_addvalue(&B);
        for (i = 0; i < S->rows.size; ++i) {
            am_Symbol key = S->rows.keys[i].sym;
            if (key.u.id == 0) continue;
            lua_pushfstring(L, "\n    %d. ", ++idx);
            luaL_addvalue(&B);
            aml_dumpkey(&B, 2, key);
            luaL_addstring(&B, " = ");
            aml_dumprow(&B, 2,
                        (am_Row *)((char *)S->rows.vals
                                   + (size_t)i * S->rows.value_size),
                        1.0f);
        }
    }
    luaL_addstring(&B, "\n}");
    return luaL_pushresult(&B), 1;
}

/* clang-format off */
static int Lreset(lua_State *L)
{ return am_resetsolver(aml_checksolver(L, 1)), lua_settop(L, 1), 1; }

static int Lclearedits(lua_State *L)
{ return am_clearedits(aml_checksolver(L, 1)), lua_settop(L, 1), 1; }

static int Lautoupdate(lua_State *L)
{ return am_autoupdate(aml_checksolver(L, 1), lua_toboolean(L, 2)), 0; }

static int Lupdatevars(lua_State *L)
{ return am_updatevars(aml_checksolver(L, 1)), lua_settop(L, 1), 1; }
/* clang-format on */

static int Lvars(lua_State *L) {
    am_Solver *S = aml_checksolver(L, 1);
    am_Id      var = 0;
    int        n = 0;
    lua_createtable(L, 0, 8);
    while ((var = am_vars(S, var)) != 0) {
        aml_pushstore(L, S);
        lua_rawgeti(L, -1, (lua_Integer)var);
        lua_remove(L, -2);
        if (luaL_testudata(L, -1, AML_VAR_TYPE) == NULL)
            return luaL_error(L, "variable %d has no handle", (int)var);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

static int Lconstraints(lua_State *L) {
    am_Solver     *S = aml_checksolver(L, 1);
    am_Constraint *cons = NULL;
    int            n = 0;
    lua_createtable(L, 0, 8);
    while ((cons = am_constraints(S, cons)) != NULL) {
        aml_pushstore(L, S);
        aml_rawgetp(L, -1, cons);
        lua_remove(L, -2);
        if (luaL_testudata(L, -1, AML_CONS_TYPE) == NULL)
            return luaL_error(L, "constraint %d has no handle",
                              (int)cons->sym.u.id);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}

static int Lhasconstraint(lua_State *L) {
    am_Solver     *S = aml_checksolver(L, 1);
    am_Constraint *cons = aml_checkcons(L, 2);
    luaL_argcheck(L, *(am_Solver **)cons == S, 2,
                  "constraint from another solver");
    return lua_pushboolean(L, am_hasconstraint(cons)), 1;
}

static int Ladd(lua_State *L) {
    am_Solver     *S = aml_checksolver(L, 1);
    am_Constraint *cons = aml_checkcons(L, 2);
    luaL_argcheck(L, *(am_Solver **)cons == S, 2,
                  "constraint from another solver");
    return aml_pusherror(L, am_add(cons));
}

static int Lremove(lua_State *L) {
    am_Solver     *S = aml_checksolver(L, 1);
    am_Constraint *cons = aml_checkcons(L, 2);
    luaL_argcheck(L, *(am_Solver **)cons == S, 2,
                  "constraint from another solver");
    am_remove(cons);
    return lua_settop(L, 1), 1;
}

static int Laddedit(lua_State *L) {
    am_Solver *S = aml_checksolver(L, 1);
    am_Id      var = aml_checkvar(L, S, 2);
    am_Num     strength = aml_checkstrength(L, 3, AM_MEDIUM);
    return aml_pusherror(L, am_addedit(S, var, strength));
}

static int Ldeledit(lua_State *L) {
    am_Solver *S = aml_checksolver(L, 1);
    am_Id      var = aml_checkvar(L, S, 2);
    am_deledit(S, var);
    return lua_settop(L, 1), 1;
}

static int Lsuggest(lua_State *L) {
    am_Solver *S = aml_checksolver(L, 1);
    am_Id      var = aml_checkvar(L, S, 2);
    am_Num     value = (am_Num)luaL_checknumber(L, 3);
    am_suggest(S, var, value);
    return lua_settop(L, 1), 1;
}

LUALIB_API int luaopen_amoeba(lua_State *L) {
    luaL_Reg libs[] = {{"var", Lvar_new},
                       {"constraint", Lcons_new},
                       {"__tostring", Ltostring},
                       {"__gc", Ldelete},
#define ENTRY(name) {#name, L##name}
                       ENTRY(new),
                       ENTRY(delete),
                       ENTRY(autoupdate),
                       ENTRY(updatevars),
                       ENTRY(reset),
                       ENTRY(add),
                       ENTRY(remove),
                       ENTRY(clearedits),
                       ENTRY(addedit),
                       ENTRY(deledit),
                       ENTRY(suggest),
                       ENTRY(vars),
                       ENTRY(constraints),
                       ENTRY(hasconstraint),
                       ENTRY(dump),
                       ENTRY(load),
#undef ENTRY
                       {NULL, NULL}};
    open_variable(L);
    open_constraint(L);
    if (luaL_newmetatable(L, AML_SOLVER_TYPE)) {
        luaL_setfuncs(L, libs, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }
    return 1;
}

/* maccc: flags+='-undefined dynamic_lookup -bundle -O2' output='amoeba.so'
 * win32cc: flags+='-DLUA_BUILD_AS_DLL -shared -O3' libs+='-llua54' output='amoeba.dll' */
