#define LUA_LIB
#include <lua.h>
#include <lauxlib.h>
#include <string.h>

#define AM_STATIC_API
#include "amoeba.h"

#define AML_SOLVER_TYPE "amoeba.Solver"
#define AML_VAR_TYPE    "amoeba.Variable"
#define AML_CONS_TYPE   "amoeba.Constraint"

enum aml_ItemType { AML_VAR, AML_CONS, AML_CONSTANT };

typedef struct aml_Solver {
    am_Solver *solver;
    int        ref_vars;
    int        ref_cons;
} aml_Solver;

typedef struct aml_Var {
    am_Num      value;
    am_Id       var;
    aml_Solver *S;
    const char *name;
} aml_Var;

typedef struct aml_Cons {
    am_Constraint *cons;
} aml_Cons;

typedef struct aml_Item {
    int            type;
    am_Id          var;
    am_Constraint *cons;
    am_Num         value;
} aml_Item;

/* utils */

static int aml_argferror(lua_State *L, int idx, const char *fmt, ...) {
    va_list l;
    va_start(l, fmt);
    lua_pushvfstring(L, fmt, l);
    va_end(l);
    return luaL_argerror(L, idx, lua_tostring(L, -1));
}

static int aml_typeerror(lua_State *L, int idx, const char *tname) {
    return aml_argferror(L, idx, "%s expected, got %s",
            tname, luaL_typename(L, idx));
}

static void aml_setweak(lua_State *L, const char *mode) {
    lua_createtable(L, 0, 1);
    lua_pushstring(L, mode);
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
}

static am_Id aml_checkvar(lua_State *L, aml_Solver *S, int idx) {
    aml_Var *lvar = (aml_Var*)luaL_testudata(L, idx, AML_VAR_TYPE);
    const char *name;
    if (lvar != NULL) {
        if (lvar->var == 0) luaL_argerror(L, idx, "invalid variable");
        return lvar->var;
    }
    name = luaL_checkstring(L, idx);
    lua_rawgeti(L, LUA_REGISTRYINDEX, S->ref_vars);
    if (lua_getfield(L, -2, name) == LUA_TUSERDATA)
        return lua_remove(L, -2), aml_checkvar(L, S, -1);
    lua_pop(L, 2);
    return aml_argferror(L, idx, "variable named '%s' not exists",
            lua_tostring(L, idx));
}

static aml_Cons *aml_registercons(lua_State *L, am_Constraint *cons) {
    aml_Cons *lcons = (aml_Cons*)lua_newuserdata(L, sizeof(aml_Cons));
    aml_Solver *S = *(aml_Solver**)cons;
    lcons->cons = cons;
    luaL_setmetatable(L, AML_CONS_TYPE);
    lua_rawgeti(L, LUA_REGISTRYINDEX, S->ref_cons);
    lua_pushvalue(L, -2);
    lua_rawsetp(L, -2, lcons);
    lua_pop(L, 1);
    return lcons;
}

static aml_Cons *aml_newcons(lua_State *L, aml_Solver *S, am_Num strength) {
    am_Constraint *cons = am_newconstraint(S->solver, strength);
    if (cons == NULL) luaL_error(L, "Create constraint failed");
    *(aml_Solver**)cons = S;
    return aml_registercons(L, cons);
}

static aml_Item aml_checkitem(lua_State *L, aml_Solver *S, int idx) {
    aml_Item item = { 0 };
    aml_Cons *lcons;
    aml_Var  *lvar;
    switch (lua_type(L, idx)) {
    case LUA_TSTRING:
        item.var = aml_checkvar(L, S, idx);
        item.type = AML_VAR;
        return item;
    case LUA_TNUMBER:
        item.value = lua_tonumber(L, idx);
        item.type  = AML_CONSTANT;
        return item;
    case LUA_TUSERDATA:
        lcons = (aml_Cons*)luaL_testudata(L, idx, AML_CONS_TYPE);
        if (lcons) {
            if (lcons->cons == NULL) luaL_argerror(L, idx, "invalid constraint");
            item.cons  = lcons->cons;
            item.type  = AML_CONS;
            return item;
        }
        lvar = luaL_testudata(L, idx, AML_VAR_TYPE);
        if (lvar) {
            if (lvar->var == 0) luaL_argerror(L, idx, "invalid variable");
            item.var = lvar->var;
            item.type  = AML_VAR;
            return item;
        }
        /* FALLTHROUGH */
    default:
        aml_typeerror(L, idx, "number/string/variable/constraint");
    }
    return item;
}

static aml_Solver *aml_checkitems(lua_State *L, int start, aml_Item *items) {
    aml_Var *lvar;
    aml_Cons *lcons;
    if ((lcons = (aml_Cons*)luaL_testudata(L, start, AML_CONS_TYPE)) != NULL) {
        aml_Solver *S = *(aml_Solver**)lcons->cons;
        items[0].type = AML_CONS, items[0].cons = lcons->cons;
        items[1] = aml_checkitem(L, S, start+1);
        return S;
    }
    if ((lcons = (aml_Cons*)luaL_testudata(L, start+1, AML_CONS_TYPE)) != NULL) {
        aml_Solver *S = *(aml_Solver**)lcons->cons;
        items[1].type = AML_CONS, items[1].cons = lcons->cons;
        items[0] = aml_checkitem(L, S, start);
        return S;
    }
    if ((lvar = (aml_Var*)luaL_testudata(L, start, AML_VAR_TYPE)) != NULL) {
        if (lvar->var == 0) luaL_argerror(L, start, "invalid variable");
        items[0].type = AML_VAR, items[0].var = lvar->var;
        items[1] = aml_checkitem(L, lvar->S, start+1);
        return lvar->S;
    }
    if ((lvar = (aml_Var*)luaL_testudata(L, start+1, AML_VAR_TYPE)) != NULL) {
        if (lvar->var == 0) luaL_argerror(L, start+1, "invalid variable");
        items[1].type = AML_VAR, items[1].var = lvar->var;
        items[0] = aml_checkitem(L, lvar->S, start);
        return lvar->S;
    }
    aml_typeerror(L, start, "variable/constraint");
    return NULL;
}

static int aml_pusherror(lua_State *L, int ret) {
    if (ret == AM_OK) return lua_settop(L, 1), 1;
    lua_pushnil(L);
    switch (ret) {
    default:             lua_pushfstring(L, "Unknown Error: %d", ret); break;
    case AM_FAILED:      lua_pushliteral(L, "FAILED"); break;
    case AM_UNSATISFIED: lua_pushliteral(L, "UNSATISFIED"); break;
    case AM_UNBOUND:     lua_pushliteral(L, "UNBOUND"); break;
    }
    lua_pushinteger(L, ret);
    return 3;
}

static void aml_performitem(lua_State *L, am_Constraint *cons, aml_Item *item, am_Num coef) {
    int ret = AM_FAILED;
    switch (item->type) {
    case AML_CONSTANT: ret = am_addconstant(cons, item->value*coef); break;
    case AML_VAR:      ret = am_addterm(cons, item->var, coef); break;
    case AML_CONS:     ret = am_mergeconstraint(cons, item->cons, coef); break;
    }
    if (ret != AM_OK) aml_pusherror(L, ret), lua_pop(L, 1), lua_error(L);
}

static am_Num aml_checkstrength(lua_State *L, int idx, am_Num def) {
    int type = lua_type(L, idx);
    const char *s;
    switch (type) {
    case LUA_TSTRING:
        s = lua_tostring(L, idx);
        if (strcmp(s, "required") == 0) return AM_REQUIRED;
        if (strcmp(s, "strong")   == 0) return AM_STRONG;
        if (strcmp(s, "medium")   == 0) return AM_MEDIUM;
        if (strcmp(s, "weak")     == 0) return AM_WEAK;
        aml_argferror(L, idx, "invalid strength value '%s'", s);
        break;
    case LUA_TNONE:
    case LUA_TNIL:    return def;
    case LUA_TNUMBER: return lua_tonumber(L, idx);
    }
    aml_typeerror(L, idx, "number/string");
    return 0.0f;
}

static int aml_checkrelation(lua_State *L, int idx) {
    const char *op = luaL_checkstring(L, idx);
    if (strcmp(op, "==") == 0)      return AM_EQUAL;
    else if (strcmp(op, "<=") == 0) return AM_LESSEQUAL;
    else if (strcmp(op, ">=") == 0) return AM_GREATEQUAL;
    else if (strcmp(op, "eq") == 0)  return AM_EQUAL;
    else if (strcmp(op, "le") == 0)  return AM_LESSEQUAL;
    else if (strcmp(op, "ge") == 0)  return AM_GREATEQUAL;
    return aml_argferror(L, 2, "invalid relation operator: '%s'", op);
}

static aml_Cons *aml_makecons(lua_State *L, aml_Solver *S, int start) {
    aml_Cons *lcons;
    int op = aml_checkrelation(L, start);
    am_Num strength = aml_checkstrength(L, start+3, AM_REQUIRED);
    aml_Item items[2];
    aml_checkitems(L, start+1, items);
    lcons = aml_newcons(L, S, strength);
    aml_performitem(L, lcons->cons, &items[0], 1.0f);
    am_setrelation(lcons->cons, op);
    aml_performitem(L, lcons->cons, &items[1], 1.0f);
    return lcons;
}

static void aml_dumpkey(luaL_Buffer *B, int idx, am_Symbol sym) {
    lua_State *L = B->L;
    aml_Var *lvar;
    lua_rawgeti(L, idx, sym.id);
    lvar = (aml_Var*)luaL_testudata(L, -1, AML_VAR_TYPE);
    lua_pop(L, 1);
    if (lvar) luaL_addstring(B, lvar->name);
    else {
        int ch = 'v';
        switch (sym.type) {
        case AM_EXTERNAL: ch = 'v'; break;
        case AM_SLACK:    ch = 's'; break;
        case AM_ERROR:    ch = 'e'; break;
        case AM_DUMMY:    ch = 'd'; break;
        }
        lua_pushfstring(L, "%c%d", ch, sym.id);
        luaL_addvalue(B);
    }
}

static void aml_dumprow(luaL_Buffer *B, int idx, am_Row *row) {
    am_Iterator it = am_itertable(&row->terms);
    lua_State *L = B->L;
    lua_pushfstring(L, "%f", row->constant);
    luaL_addvalue(B);
    while ((am_nextentry(&it))) {
        am_Num coef = *am_val(am_Num,it);
        lua_pushfstring(L, " %c ", coef > 0.0f ? '+' : '-');
        luaL_addvalue(B);
        if (coef < 0.0f) coef = -coef;
        if (!am_approx(coef, 1.0f)) {
            lua_pushfstring(L, "%f*", coef);
            luaL_addvalue(B);
        }
        aml_dumpkey(B, idx, it.key);
    }
}

/* expression */

static int Lexpr_neg(lua_State *L) {
    aml_Cons *lcons = (aml_Cons*)luaL_checkudata(L, 1, AML_CONS_TYPE);
    aml_Solver *S = *(aml_Solver**)lcons;
    aml_Cons *newcons = aml_newcons(L, S, AM_REQUIRED);
    return aml_pusherror(L, am_mergeconstraint(newcons->cons, lcons->cons, -1.0f));
}

static int Lexpr_add(lua_State *L) {
    aml_Cons *lcons;
    aml_Item items[2];
    aml_Solver *S = aml_checkitems(L, 1, items);
    lcons = aml_newcons(L, S, AM_REQUIRED);
    aml_performitem(L, lcons->cons, &items[0], 1.0f);
    aml_performitem(L, lcons->cons, &items[1], 1.0f);
    return 1;
}

static int Lexpr_sub(lua_State *L) {
    aml_Cons *lcons;
    aml_Item items[2];
    aml_Solver *S = aml_checkitems(L, 1, items);
    lcons = aml_newcons(L, S, AM_REQUIRED);
    aml_performitem(L, lcons->cons, &items[0], 1.0f);
    aml_performitem(L, lcons->cons, &items[1], -1.0f);
    return 1;
}

static int Lexpr_mul(lua_State *L) {
    aml_Item items[2];
    aml_Solver *S = aml_checkitems(L, 1, items);
    if (items[0].type == AML_CONSTANT) {
        aml_Cons *lcons = aml_newcons(L, S, AM_REQUIRED);
        aml_performitem(L, lcons->cons, &items[1], items[0].value);
    }
    else if (items[1].type == AML_CONSTANT) {
        aml_Cons *lcons = aml_newcons(L, S, AM_REQUIRED);
        aml_performitem(L, lcons->cons, &items[0], items[1].value);
    }
    else luaL_error(L, "attempt to multiply two expression");
    return 1;
}

static int Lexpr_div(lua_State *L) {
    aml_Item items[2];
    aml_Solver *S = aml_checkitems(L, 1, items);
    if (items[0].type == AML_CONSTANT)
        luaL_error(L, "attempt to divide a expression");
    if (items[1].type == AML_CONSTANT) {
        aml_Cons *lcons = aml_newcons(L, S, AM_REQUIRED);
        aml_performitem(L, lcons->cons, &items[0], 1.0f/items[1].value);
    }
    else luaL_error(L, "attempt to divide two expression");
    return 1;
}

static int Lexpr_cmp(lua_State *L, int op) {
    aml_Item items[2];
    aml_Solver *S = aml_checkitems(L, 1, items);
    aml_Cons *lcons = aml_newcons(L, S, AM_REQUIRED);
    aml_performitem(L, lcons->cons, &items[0], 1.0f);
    am_setrelation(lcons->cons, op);
    aml_performitem(L, lcons->cons, &items[1], 1.0f);
    return 1;
}

static int Lexpr_le(lua_State *L) { return Lexpr_cmp(L, AM_LESSEQUAL); }
static int Lexpr_eq(lua_State *L) { return Lexpr_cmp(L, AM_EQUAL); }
static int Lexpr_ge(lua_State *L) { return Lexpr_cmp(L, AM_GREATEQUAL); }

/* variable */

static aml_Var *aml_newvar(lua_State *L, aml_Solver *S, am_Id var, const char *name) {
    aml_Var *lvar = (aml_Var*)lua_newuserdata(L, sizeof(aml_Var));
    if (var == 0) {
        var = am_newvariable(S->solver, &lvar->value);
        if (var == 0) luaL_error(L, "Create variable failed");
    }
    if (name == NULL) {
        lua_settop(L, 1);
        lua_pushfstring(L, "v%d", var);
        name = lua_tostring(L, 2);
    }
    lvar->S    = S;
    lvar->var  = var;
    lvar->name = name;
    luaL_setmetatable(L, AML_VAR_TYPE);
    lua_rawgeti(L, LUA_REGISTRYINDEX, S->ref_vars);
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, name);
    lua_pushvalue(L, -2);
    lua_rawseti(L, -2, var);
    lua_pop(L, 1);
    return lvar;
}

static int Lvar_new(lua_State *L) {
    aml_Solver *S = (aml_Solver*)luaL_checkudata(L, 1, AML_SOLVER_TYPE);
    int type = lua_type(L, 2);
    if (type != LUA_TNONE || type != LUA_TNIL) {
        if (type != LUA_TSTRING && type != LUA_TNUMBER)
            return aml_typeerror(L, 2, "number/string");
        lua_rawgeti(L, LUA_REGISTRYINDEX, S->ref_vars);
        lua_pushvalue(L, 2);
        if (lua_rawget(L, -2) != LUA_TNIL) return 1;
        if (type == LUA_TNUMBER)
            aml_argferror(L, 2, "variable#%d not exists", lua_tointeger(L, 2));
        lua_pop(L, 1);
    }
    return aml_newvar(L, S, 0, lua_tostring(L, 2)), 1;
}

static int Lvar_delete(lua_State *L) {
    aml_Var *lvar = (aml_Var*)luaL_checkudata(L, 1, AML_VAR_TYPE);
    if (lvar->var == 0) return 0;
    am_delvariable(lvar->S->solver, lvar->var);
    lua_rawgeti(L, LUA_REGISTRYINDEX, lvar->S->ref_vars);
    lua_pushnil(L);
    lua_setfield(L, -2, lvar->name);
    lvar->var  = 0;
    lvar->name = NULL;
    return 0;
}

static int Lvar_value(lua_State *L) {
    aml_Var *lvar = (aml_Var*)luaL_checkudata(L, 1, AML_VAR_TYPE);
    if (lvar->var == 0) luaL_argerror(L, 1, "invalid variable");
    return lua_pushnumber(L, lvar->value), 1;
}

static int Lvar_tostring(lua_State *L) {
    aml_Var *lvar = (aml_Var*)luaL_checkudata(L, 1, AML_VAR_TYPE);
    if (lvar->var) lua_pushfstring(L, AML_VAR_TYPE "(%p): %s = %f",
                lvar->var, lvar->name, lvar->value);
    else lua_pushstring(L, AML_VAR_TYPE ": deleted");
    return 1;
}

static void open_variable(lua_State *L) {
    luaL_Reg libs[] = {
        { "__neg", Lexpr_neg },
        { "__add", Lexpr_add },
        { "__sub", Lexpr_sub },
        { "__mul", Lexpr_mul },
        { "__div", Lexpr_div },
        { "le", Lexpr_le },
        { "eq", Lexpr_eq },
        { "ge", Lexpr_ge },
        { "__tostring", Lvar_tostring },
        { "__gc", Lvar_delete },
#define ENTRY(name) { #name, Lvar_##name }
        ENTRY(new),
        ENTRY(delete),
        ENTRY(value),
#undef  ENTRY
        { NULL, NULL }
    };
    if (luaL_newmetatable(L, AML_VAR_TYPE)) {
        luaL_setfuncs(L, libs, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }
}

/* constraint */

static int Lcons_new(lua_State *L) {
    aml_Solver *S = (aml_Solver*)luaL_checkudata(L, 1, AML_SOLVER_TYPE);
    if (lua_gettop(L) >= 3) aml_makecons(L, S, 2);
    else aml_newcons(L, S, aml_checkstrength(L, 2, AM_REQUIRED));
    return 1;
}

static int Lcons_delete(lua_State *L) {
    aml_Cons *lcons = (aml_Cons*)luaL_checkudata(L, 1, AML_CONS_TYPE);
    aml_Solver *S = *(aml_Solver**)lcons->cons;
    if (lcons->cons == NULL) return 0;
    am_delconstraint(lcons->cons);
    lcons->cons = NULL;
    lua_rawgeti(L, LUA_REGISTRYINDEX, S->ref_vars);
    lua_pushnil(L);
    lua_rawsetp(L, -2, lcons);
    return 0;
}

static int Lcons_reset(lua_State *L) {
    aml_Cons *lcons = (aml_Cons*)luaL_checkudata(L, 1, AML_CONS_TYPE);
    if (lcons->cons == NULL) luaL_argerror(L, 1, "invalid constraint");
    am_resetconstraint(lcons->cons);
    return lua_settop(L, 1), 1;
}

static int Lcons_add(lua_State *L) {
    aml_Cons *lcons = (aml_Cons*)luaL_checkudata(L, 1, AML_CONS_TYPE);
    aml_Solver *S = *(aml_Solver**)lcons;
    aml_Item item;
    int ret;
    luaL_argcheck(L, lcons->cons, 1, "invalid constraint");
    if (lua_type(L, 2) == LUA_TSTRING) {
        const char *s = lua_tostring(L, 2);
        if (s[0] == '<' || s[0] == '>' || s[0] == '=') {
            ret = am_setrelation(lcons->cons, aml_checkrelation(L, 2));
            if (ret != AM_OK) luaL_error(L, "constraint has been added to solver!");
            return lua_settop(L, 1), 1;
        }
    }
    item = aml_checkitem(L, S, 2);
    aml_performitem(L, lcons->cons, &item, 1.0f);
    return lua_settop(L, 1), 1;
}

static int Lcons_relation(lua_State *L) {
    aml_Cons *lcons = (aml_Cons*)luaL_checkudata(L, 1, AML_CONS_TYPE);
    int op = aml_checkrelation(L, 2);
    if (lcons->cons == NULL) luaL_argerror(L, 1, "invalid constraint");
    if (am_setrelation(lcons->cons, op) != AM_OK)
        luaL_error(L, "constraint has been added to solver!");
    return lua_settop(L, 1), 1;
}

static int Lcons_strength(lua_State *L) {
    aml_Cons *lcons = (aml_Cons*)luaL_checkudata(L, 1, AML_CONS_TYPE);
    am_Num strength = aml_checkstrength(L, 2, AM_REQUIRED);
    if (lcons->cons == NULL) luaL_argerror(L, 1, "invalid constraint");
    if (am_setstrength(lcons->cons, strength) != AM_OK)
        luaL_error(L, "constraint has been added to solver!");
    return lua_settop(L, 1), 1;
}

static int Lcons_tostring(lua_State *L) {
    aml_Cons *lcons = (aml_Cons*)luaL_checkudata(L, 1, AML_CONS_TYPE);
    aml_Solver *S = *(aml_Solver**)lcons;
    luaL_Buffer B;
    if (lcons->cons == NULL) {
        lua_pushstring(L, AML_CONS_TYPE ": deleted");
        return 1;
    }
    lua_settop(L, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, S->ref_vars);
    luaL_buffinit(L, &B);
    lua_pushfstring(L, AML_CONS_TYPE "(%p): [", lcons->cons);
    luaL_addvalue(&B);
    aml_dumprow(&B, 2, &lcons->cons->expression);
    if (lcons->cons->relation == AM_EQUAL)
        luaL_addstring(&B, " == 0.0]");
    else
        luaL_addstring(&B, " >= 0.0]");
    if (lcons->cons->marker.id != 0) {
        luaL_addstring(&B, "(added:");
        aml_dumpkey(&B, 2, lcons->cons->marker);
        luaL_addchar(&B, '-');
        aml_dumpkey(&B, 2, lcons->cons->other);
        luaL_addchar(&B, ')');
    }
    luaL_pushresult(&B);
    return 1;
}

static void open_constraint(lua_State *L) {
    luaL_Reg libs[] = {
        { "__call", Lcons_add },
        { "__neg", Lexpr_neg },
        { "__add", Lexpr_add },
        { "__sub", Lexpr_sub },
        { "__mul", Lexpr_mul },
        { "__div", Lexpr_div },
        { "le", Lexpr_le },
        { "eq", Lexpr_eq },
        { "ge", Lexpr_ge },
        { "__bor", Lcons_strength },
        { "__tostring", Lcons_tostring },
        { "__gc", Lcons_delete },
#define ENTRY(name) { #name, Lcons_##name }
        ENTRY(new),
        ENTRY(delete),
        ENTRY(reset),
        ENTRY(add),
        ENTRY(relation),
        ENTRY(strength),
#undef  ENTRY
        { NULL, NULL }
    };
    if (luaL_newmetatable(L, AML_CONS_TYPE)) {
        luaL_setfuncs(L, libs, 0);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
    }
}

/* dump & load */

typedef struct aml_Dumper {
    am_Dumper base;
    luaL_Buffer *B;
} aml_Dumper;

static const char *aml_varname(am_Dumper *d, unsigned idx, am_Id var, am_Num *value)
{ return (void)d, (void)idx, (void)var, ((aml_Var*)value)->name; }

static int aml_writer(am_Dumper *d, const void *buf, size_t len) {
    /* Write the actually data */
    aml_Dumper *ld = (aml_Dumper*)d;
    luaL_addlstring(ld->B, buf, len);
    return AM_OK;
}

static int Ldump(lua_State *L) {
    aml_Solver *S = (aml_Solver*)luaL_checkudata(L, 1, AML_SOLVER_TYPE);
    luaL_Buffer B;
    aml_Dumper d;
    int ret;
    memset(&d, 0, sizeof(d));
    d.base.var_name = aml_varname;
    d.base.writer = aml_writer;
    d.B = &B;
    luaL_buffinit(L, &B);
    ret = am_dump(S->solver, &d.base);
    if (ret == AM_OK)
        return luaL_pushresult(&B), 1;
    return aml_pusherror(L, ret);
}

typedef struct aml_Loader {
    am_Loader  base;
    lua_State  *L;
    aml_Solver *S;
    const char *s;
    size_t      len;
    int         cons_index;
    int         loader_index;
} aml_Loader;

static am_Num *aml_loadvar(am_Loader *l, const char *name, unsigned idx, am_Id var) {
    aml_Loader *ll = (aml_Loader*)l;
    aml_Var *lvar = aml_newvar(ll->L, ll->S, var, name);
    return (void)idx, &lvar->value;
}

static void aml_loadcons(am_Loader *l, const char *name, unsigned idx, am_Constraint *cons) {
    aml_Loader *ll = (aml_Loader*)l;
    if (ll->cons_index == 0) return;
    (void)name;
    *(aml_Solver**)cons = ll->S;
    aml_registercons(ll->L, cons);
    lua_rawseti(ll->L, ll->cons_index, idx);
}

static const char *aml_reader(am_Loader *l, size_t *plen) {
    aml_Loader *ll = (aml_Loader*)l;
    lua_State *L = ll->L;
    if (ll->loader_index == 0) {
        const char *p = ll->s;
        *plen = ll->len;
        ll->s = NULL, ll->len = 0;
        return p;
    }
    luaL_checkstack(L, 2, "too many nested functions");
    lua_pushvalue(L, 1);
    lua_call(L, 0, 1);
    if (lua_isnil(L, -1))
        return lua_pop(L, 1), *plen = 0, NULL;
    else if (!lua_isstring(L, -1))
        luaL_error(L, "reader function must return a string");
    lua_replace(L, ll->loader_index);  /* save string in reserved slot */
    return lua_tolstring(L, ll->loader_index, plen);
}

static int Lload(lua_State *L) {
    aml_Solver *S = (aml_Solver*)luaL_checkudata(L, 1, AML_SOLVER_TYPE);
    aml_Loader ll;
    memset(&ll, 0, sizeof(ll));
    ll.s = lua_tolstring(L, 1, &ll.len);
    ll.L = L, ll.S = S;
    ll.cons_index = (!lua_isnone(L, 2) ? 2 : 0);
    ll.base.load_var = aml_loadvar;
    ll.base.load_cons = aml_loadcons;
    ll.base.reader = aml_reader;
    if (ll.s == NULL) {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        ll.loader_index = 3;
        lua_settop(L, 3);  /* create reserved slot */
    }
    return aml_pusherror(L, am_load(S->solver, &ll.base));
}

/* solver */

static int Lnew(lua_State *L) {
    aml_Solver *S = lua_newuserdata(L, sizeof(aml_Solver));
    if ((S->solver = am_newsolver(NULL, NULL)) == NULL)
        return 0;
    lua_createtable(L, 0, 4); aml_setweak(L, "v");
    S->ref_vars = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_createtable(L, 0, 4); aml_setweak(L, "v");
    S->ref_cons = luaL_ref(L, LUA_REGISTRYINDEX);
    luaL_setmetatable(L, AML_SOLVER_TYPE);
    am_autoupdate(S->solver, lua_toboolean(L, 1));
    return 1;
}

static int Ldelete(lua_State *L) {
    aml_Solver *S = (aml_Solver*)luaL_checkudata(L, 1, AML_SOLVER_TYPE);
    if (S->solver == NULL) return 0;
    lua_rawgeti(L, LUA_REGISTRYINDEX, S->ref_vars);
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        aml_Var *lvar = (aml_Var*)luaL_testudata(L, -1, AML_VAR_TYPE);
        if (lvar && lvar->var) {
            am_delvariable(lvar->S->solver, lvar->var);
            lvar->var  = 0, lvar->name = NULL;
        }
        lua_pop(L, 1);
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, S->ref_cons);
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        aml_Cons *lcons = (aml_Cons*)luaL_testudata(L, -1, AML_CONS_TYPE);
        if (lcons && lcons->cons) {
            am_delconstraint(lcons->cons);
            lcons->cons = NULL;
        }
        lua_pop(L, 1);
    }
    luaL_unref(L, LUA_REGISTRYINDEX, S->ref_vars);
    luaL_unref(L, LUA_REGISTRYINDEX, S->ref_cons);
    am_delsolver(S->solver);
    S->solver = NULL;
    return 0;
}

static int Ltostring(lua_State *L) {
    aml_Solver *S = (aml_Solver*)luaL_checkudata(L, 1, AML_SOLVER_TYPE);
    luaL_Buffer B;
    lua_settop(L, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, S->ref_vars);
    luaL_buffinit(L, &B);
    lua_pushfstring(L, AML_SOLVER_TYPE "(%p): {", S->solver);
    luaL_addvalue(&B);
    luaL_addstring(&B, "\n  objective = ");
    aml_dumprow(&B, 2, &S->solver->objective);
    if (S->solver->rows.count != 0) {
        am_Iterator it = am_itertable(&S->solver->rows);
        int idx = 0;
        lua_pushfstring(L, "\n  rows(%d):", S->solver->rows.count);
        luaL_addvalue(&B);
        while (am_nextentry(&it)) {
            am_Row *row = am_val(am_Row,it);
            lua_pushfstring(L, "\n    %d. ", ++idx);
            luaL_addvalue(&B);
            aml_dumpkey(&B, 2, it.key);
            luaL_addstring(&B, " = ");
            aml_dumprow(&B, 2, row);
        }
    }
    if (S->solver->infeasible_rows.id != 0) {
        am_Symbol key = S->solver->infeasible_rows;
        am_Row *row = (am_Row*)am_gettable(&S->solver->rows, key.id);
        luaL_addstring(&B, "\n  infeasible rows: ");
        aml_dumpkey(&B, 2, key);
        while (row != NULL) {
            luaL_addstring(&B, ", ");
            aml_dumpkey(&B, 2, key);
            row = (am_Row*)am_gettable(&S->solver->rows, row->infeasible_next.id);
        }
    }
    luaL_addstring(&B, "\n}");
    luaL_pushresult(&B);
    return 1;
}

static int Lreset(lua_State *L) {
    aml_Solver *S = (aml_Solver*)luaL_checkudata(L, 1, AML_SOLVER_TYPE);
    am_resetsolver(S->solver);
    return lua_settop(L, 1), 1;
}

static int Lclearedits(lua_State *L) {
    aml_Solver *S = (aml_Solver*)luaL_checkudata(L, 1, AML_SOLVER_TYPE);
    am_clearedits(S->solver);
    return lua_settop(L, 1), 1;
}

static int Lautoupdate(lua_State *L) {
    aml_Solver *S = (aml_Solver*)luaL_checkudata(L, 1, AML_SOLVER_TYPE);
    am_autoupdate(S->solver, lua_toboolean(L, 2));
    return 0;
}

static int Laddconstraint(lua_State *L) {
    aml_Solver *S = (aml_Solver*)luaL_checkudata(L, 1, AML_SOLVER_TYPE);
    aml_Cons *lcons = (aml_Cons*)luaL_testudata(L, 2, AML_CONS_TYPE);
    if (lcons == NULL) lcons = aml_makecons(L, S, 2);
    return aml_pusherror(L, am_add(lcons->cons));
}

static int Ldelconstraint(lua_State *L) {
    luaL_checkudata(L, 1, AML_SOLVER_TYPE);
    aml_Cons *lcons = (aml_Cons*)luaL_checkudata(L, 2, AML_CONS_TYPE);
    am_remove(lcons->cons);
    return lua_settop(L, 1), 1;
}

static int Laddedit(lua_State *L) {
    aml_Solver *S = (aml_Solver*)luaL_checkudata(L, 1, AML_SOLVER_TYPE);
    am_Id  var = aml_checkvar(L, S, 2);
    am_Num strength = aml_checkstrength(L, 3, AM_MEDIUM);
    return aml_pusherror(L, am_addedit(S->solver, var, strength));
}

static int Ldeledit(lua_State *L) {
    aml_Solver *S = (aml_Solver*)luaL_checkudata(L, 1, AML_SOLVER_TYPE);
    am_Id var = aml_checkvar(L, S, 2);
    am_deledit(S->solver, var);
    return lua_settop(L, 1), 1;
}

static int Lsuggest(lua_State *L) {
    aml_Solver *S = (aml_Solver*)luaL_checkudata(L, 1, AML_SOLVER_TYPE);
    am_Id  var = aml_checkvar(L, S, 2);
    am_Num value = (am_Num)luaL_checknumber(L, 3);
    am_suggest(S->solver, var, value);
    return lua_settop(L, 1), 1;
}

LUALIB_API int luaopen_amoeba(lua_State *L) {
    luaL_Reg libs[] = {
        { "var",        Lvar_new  },
        { "constraint", Lcons_new },
        { "__tostring", Ltostring },
#define ENTRY(name) { #name, L##name }
        ENTRY(new),
        ENTRY(delete),
        ENTRY(autoupdate),
        ENTRY(reset),
        ENTRY(addconstraint),
        ENTRY(delconstraint),
        ENTRY(clearedits),
        ENTRY(addedit),
        ENTRY(deledit),
        ENTRY(suggest),
        ENTRY(dump),
        ENTRY(load),
#undef  ENTRY
        { NULL, NULL }
    };
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

