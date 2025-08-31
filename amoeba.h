#ifndef amoeba_h
#define amoeba_h

#ifndef AM_NS_BEGIN
# ifdef __cplusplus
#   define AM_NS_BEGIN extern "C" {
#   define AM_NS_END   }
# else
#   define AM_NS_BEGIN
#   define AM_NS_END
# endif
#endif /* AM_NS_BEGIN */

#ifndef AM_STATIC
# ifdef __GNUC__
#   define AM_STATIC static __attribute((unused))
# else
#   define AM_STATIC static
# endif
#endif

#ifdef AM_STATIC_API
# ifndef AM_IMPLEMENTATION
#  define AM_IMPLEMENTATION
# endif
# define AM_API AM_STATIC
#endif

#if !defined(AM_API) && defined(_WIN32)
# ifdef AM_IMPLEMENTATION
#  define AM_API __declspec(dllexport)
# else
#  define AM_API __declspec(dllimport)
# endif
#endif

#ifndef AM_API
# define AM_API extern
#endif

#define AM_OK           (0)
#define AM_FAILED       (-1)
#define AM_UNSATISFIED  (-2)
#define AM_UNBOUND      (-3)

#define AM_LESSEQUAL    (1)
#define AM_EQUAL        (2)
#define AM_GREATEQUAL   (3)

#define AM_REQUIRED     ((am_Num)1000000000)
#define AM_STRONG       ((am_Num)1000000)
#define AM_MEDIUM       ((am_Num)1000)
#define AM_WEAK         ((am_Num)1)

#include <stddef.h>

AM_NS_BEGIN


#ifdef AM_USE_FLOAT
typedef float  am_Num;
#else
typedef double am_Num;
#endif

typedef struct am_Solver     am_Solver;
typedef struct am_Constraint am_Constraint;

typedef unsigned am_Id;

typedef void *am_Allocf (void *ud, void *ptr, size_t nsize, size_t osize);

AM_API am_Solver *am_newsolver   (am_Allocf *allocf, void *ud);
AM_API void       am_resetsolver (am_Solver *solver);
AM_API void       am_delsolver   (am_Solver *solver);

AM_API void am_updatevars (am_Solver *solver);
AM_API void am_autoupdate (am_Solver *solver, int auto_update);

AM_API int  am_add    (am_Constraint *cons);
AM_API void am_remove (am_Constraint *cons);

AM_API void am_clearedits (am_Solver *solver);
AM_API int  am_hasedit (am_Solver *solver, am_Id var);
AM_API int  am_addedit (am_Solver *solver, am_Id var, am_Num strength);
AM_API void am_suggest (am_Solver *solver, am_Id var, am_Num value);
AM_API void am_deledit (am_Solver *solver, am_Id var);

AM_API am_Id am_newvariable (am_Solver *solver, am_Num *pvalue);
AM_API void  am_delvariable (am_Solver *solver, am_Id var);

AM_API am_Num *am_varvalue (am_Solver *solver, am_Id var, am_Num *newvalue);

AM_API int am_refcount      (am_Solver *solver, am_Id var);
AM_API int am_hasconstraint (am_Constraint *cons);

AM_API am_Constraint *am_newconstraint   (am_Solver *solver, am_Num strength);
AM_API am_Constraint *am_cloneconstraint (am_Constraint *other, am_Num strength);

AM_API void am_resetconstraint (am_Constraint *cons);
AM_API void am_delconstraint   (am_Constraint *cons);

AM_API int am_addterm     (am_Constraint *cons, am_Id var, am_Num multiplier);
AM_API int am_setrelation (am_Constraint *cons, int relation);
AM_API int am_addconstant (am_Constraint *cons, am_Num constant);
AM_API int am_setstrength (am_Constraint *cons, am_Num strength);

AM_API int am_mergeconstraint (am_Constraint *cons, const am_Constraint *other, am_Num multiplier);

/* dump & load */

typedef struct am_Dumper am_Dumper;
typedef struct am_Loader am_Loader;

AM_API int am_dump (am_Solver *solver, am_Dumper *dumper);
AM_API int am_load (am_Solver *solver, am_Loader *loader);

struct am_Dumper {
    const char *(*var_name)  (am_Dumper *, unsigned idx, am_Id var, am_Num *value);
    const char *(*cons_name) (am_Dumper *, unsigned idx, am_Constraint *cons);

    int (*writer) (am_Dumper *, const void *buf, size_t len);
};

struct am_Loader {
    am_Num *(*load_var)  (am_Loader *, const char *name, unsigned idx, am_Id var);
    void    (*load_cons) (am_Loader *, const char *name, unsigned idx, am_Constraint *cons);

    const char *(*reader) (am_Loader *, size_t *plen);
};


AM_NS_END

#endif /* amoeba_h */

#if defined(AM_IMPLEMENTATION) && !defined(am_implemented)
#define am_implemented

#include <assert.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define AM_EXTERNAL     (0)
#define AM_SLACK        (1)
#define AM_ERROR        (2)
#define AM_DUMMY        (3)

#define am_isexternal(key)   ((key).type == AM_EXTERNAL)
#define am_isslack(key)      ((key).type == AM_SLACK)
#define am_iserror(key)      ((key).type == AM_ERROR)
#define am_isdummy(key)      ((key).type == AM_DUMMY)
#define am_ispivotable(key)  (am_isslack(key) || am_iserror(key))

#define AM_POOLSIZE     4096
#define AM_MIN_HASHSIZE 8

#define AM_UNSIGNED_BITS (sizeof(unsigned)*CHAR_BIT)

#ifdef AM_USE_FLOAT
# define AM_NUM_MAX FLT_MAX
# define AM_NUM_EPS 1e-4f
#else
# define AM_NUM_MAX DBL_MAX
# define AM_NUM_EPS 1e-6
#endif

AM_NS_BEGIN


typedef unsigned am_Size;

typedef struct am_Symbol {
    unsigned id   : AM_UNSIGNED_BITS - 2;
    unsigned type : 2;
} am_Symbol;

typedef struct am_MemPool {
    size_t size;
    void  *freed;
    void  *pages;
} am_MemPool;

typedef struct am_Key {
    int       next;
    am_Symbol sym;
} am_Key;

typedef struct am_Table {
    am_Size size;
    am_Size count;
    am_Size value_size;
    am_Size last_free;
    am_Key *keys;
    void   *vals;
} am_Table;

typedef struct am_Iterator {
    const am_Table *t;
    am_Symbol key;
    am_Size   offset;
} am_Iterator;

typedef struct am_Row {
    am_Symbol infeasible_next;
    am_Num    constant;
    am_Table  terms;
} am_Row;

typedef struct am_Var {
    am_Symbol next;
    unsigned  refcount;
    am_Num   *pvalue;
} am_Var;

typedef struct am_Suggest {
    am_Size        age;
    am_Num         edit_value;
    am_Solver     *solver;
    am_Constraint *constraint;
    am_Table       dirtyset;
} am_Suggest;

struct am_Constraint {
    void      *ud;
    am_Row     expression;
    am_Symbol  sym;
    am_Symbol  marker;
    am_Symbol  other;
    unsigned   marker_id;
    unsigned   other_id : AM_UNSIGNED_BITS - 2;
    unsigned   relation : 2;
    am_Num     strength;
    am_Solver *solver;
};

struct am_Solver {
    am_Allocf *allocf;
    void      *ud;
    am_Row     objective;
    am_Table   vars;            /* symbol -> am_Var */
    am_Table   constraints;     /* symbol -> am_Constraint* */
    am_Table   suggests;        /* symbol -> am_Suggest */
    am_Table   rows;            /* symbol -> am_Row */
    am_MemPool conspool;
    am_Size    id_count;
    am_Size    cons_count;
    am_Size    auto_update;
    am_Size    age; /* counts of rows changed */
    am_Symbol  infeasible_rows;
    am_Symbol  dirty_vars;
};

/* utils */

static int am_approx(am_Num a, am_Num b)
{ return a > b ? a - b < AM_NUM_EPS : b - a < AM_NUM_EPS; }

static int am_nearzero(am_Num a)
{ return am_approx(a, 0.0f); }

static am_Symbol am_null(void)
{ am_Symbol null = { 0, 0 }; return null; }

static void am_initpool(am_MemPool *pool, size_t size) {
    pool->size  = size;
    pool->freed = pool->pages = NULL;
    assert(size > sizeof(void*) && size < AM_POOLSIZE/4);
}

static void am_freepool(am_Solver *solver, am_MemPool *pool) {
    const size_t offset = AM_POOLSIZE - sizeof(void*);
    while (pool->pages != NULL) {
        void *next = *(void**)((char*)pool->pages + offset);
        solver->allocf(solver->ud, pool->pages, 0, AM_POOLSIZE);
        pool->pages = next;
    }
    am_initpool(pool, pool->size);
}

static void *am_alloc(am_Solver *solver, am_MemPool *pool) {
    void *obj = pool->freed;
    if (obj == NULL) {
        const size_t offset = AM_POOLSIZE - sizeof(void*);
        void *end, *ptr = solver->allocf(solver->ud, NULL, AM_POOLSIZE, 0);
        *(void**)((char*)ptr + offset) = pool->pages;
        pool->pages = ptr;
        end = (char*)ptr + (offset/pool->size-1)*pool->size;
        while (end != ptr) {
            *(void**)end = pool->freed;
            pool->freed = (void**)end;
            end = (char*)end - pool->size;
        }
        return end;
    }
    pool->freed = *(void**)obj;
    return obj;
}

static void am_free(am_MemPool *pool, void *obj) {
    *(void**)obj = pool->freed;
    pool->freed = obj;
}

static am_Symbol am_newsymbol(am_Solver *solver, int type) {
    am_Symbol sym;
    unsigned id = ++solver->id_count;
    if (id > 0x3FFFFFFF) id = solver->id_count = 1;
    assert(type >= AM_EXTERNAL && type <= AM_DUMMY);
    sym.id   = id;
    sym.type = type;
    return sym;
}

/* hash table */

#define amH_val(t,i) ((char*)(t)->vals+(i)*(t)->value_size)
#define am_val(T,it) ((T*)amH_val((it).t,(it).offset-1))

static am_Iterator am_itertable(const am_Table *t)
{ am_Iterator it; it.t = t, it.key = am_null(), it.offset = 0; return it; }

static void am_inittable(am_Table *t, size_t value_size)
{ memset(t, 0, sizeof(*t)), t->value_size = (am_Size)value_size; }

static void am_resettable(am_Table *t)
{ t->last_free = t->count = 0; memset(t->keys, 0, t->size * sizeof(am_Key)); }

static int am_nextentry(am_Iterator *it) {
    am_Size cur = it->offset, size = it->t->size;
    am_Symbol key;
    while (cur < size && (key = it->t->keys[cur].sym).id == 0) cur++;
    if (cur == size) return it->key = am_null(), it->offset = 0;
    return it->key = key, it->offset = cur + 1;
}

static void am_freetable(const am_Solver *solver, am_Table *t) {
    size_t hsize = t->size * (sizeof(am_Key) + t->value_size);
    if (hsize == 0) return;
    solver->allocf(solver->ud, t->keys, 0, hsize);
    am_inittable(t, t->value_size);
}

static const void *am_gettable(const am_Table *t, unsigned key) {
    am_Size cur;
    if (t->size == 0) return NULL;
    cur = key & (t->size - 1);
    for (; t->keys[cur].sym.id != key; cur += t->keys[cur].next)
        if (t->keys[cur].next == 0) return NULL;
    return amH_val(t, cur);
}

static int amH_alloc(const am_Solver *solver, am_Table *t) {
    size_t need = t->count*2, size = AM_MIN_HASHSIZE, hsize;
    am_Key *keys;
    if (need > (~(am_Size)0 >> 2)) return 0;
    while (size < need) size <<= 1;
    assert((size & (size-1)) == 0);
    hsize = size * (sizeof(am_Key) + t->value_size);
    keys = (am_Key*)solver->allocf(solver->ud, NULL, hsize, 0);
    if (keys == NULL) return 0;
    t->size = (am_Size)size;
    t->keys = keys;
    t->vals = keys + t->size;
    return am_resettable(t), 1;
}

static void *amH_rawset(am_Table *t, am_Symbol key) {
    int mp = key.id & (t->size - 1);
    am_Key *ks = t->keys;
    if (ks[mp].sym.id) {
        int f = t->last_free, othern;
        while (f < (int)t->size && (ks[f].sym.id || ks[f].next))
            f += 1;
        if (f == (int)t->size) return NULL;
        t->last_free = f + 1;
        if ((othern = ks[mp].sym.id & (t->size - 1)) == mp) {
            if (ks[mp].next) ks[f].next = (mp + ks[mp].next) - f;
            ks[mp].next = (f - mp), mp = f;
        } else {
            while (othern + ks[othern].next != mp)
                othern += ks[othern].next;
            ks[othern].next = (f - othern);
            ks[f] = ks[mp], memcpy(amH_val(t,f), amH_val(t,mp), t->value_size);
            if (ks[mp].next) ks[f].next += (mp - f), ks[mp].next = 0;
        }
    }
    return ks[mp].sym = key, amH_val(t, mp);
}

static size_t amH_resize(const am_Solver *solver, am_Table *t) {
    am_Table nt = *t;
    am_Size i;
    if (!amH_alloc(solver, &nt)) return 0;
    for (i = 0; i < t->size; ++i) {
        void *value;
        if (!t->keys[i].sym.id) continue;
        value = amH_rawset(&nt, t->keys[i].sym);
        assert(value != NULL), memcpy(value, amH_val(t, i), t->value_size);
    }
    nt.count = t->count;
    am_freetable(solver, t);
    return (*t = nt), 1;
}

static void *am_settable(const am_Solver *solver, am_Table *t, am_Symbol key) {
    void *value;
    assert(key.id != 0 && am_gettable(t, key.id) == NULL);
    if (t->size == 0 && !amH_resize(solver, t)) return NULL;
    if ((value = amH_rawset(t, key)) == NULL) {
        if (!amH_resize(solver, t)) return NULL;
        value = amH_rawset(t, key); /* retry and must success */
    }
    return t->count += 1, assert(value != NULL), value;
}

static void am_deltable(am_Table *t, const void *value) {
    size_t offset = ((char*)value - (char*)t->vals)/t->value_size;
    t->count -= 1, t->keys[offset].sym = am_null();
}

/* expression (row) */

static void am_freerow(am_Solver *solver, am_Row *row)
{ am_freetable(solver, &row->terms); }

static void am_resetrow(am_Row *row)
{ row->constant = 0.0f; am_resettable(&row->terms); }

static void am_initrow(am_Row *row) {
    row->infeasible_next = am_null();
    row->constant = 0.0f;
    am_inittable(&row->terms, sizeof(am_Num));
}

static void am_multiply(am_Row *row, am_Num multiplier) {
    am_Iterator it = am_itertable(&row->terms);
    row->constant *= multiplier;
    while (am_nextentry(&it))
        *am_val(am_Num,it) *= multiplier;
}

static void am_addvar(am_Solver *solver, am_Row *row, am_Symbol sym, am_Num value) {
    am_Num *term;
    if (sym.id == 0 || am_nearzero(value)) return;
    term = (am_Num*)am_gettable(&row->terms, sym.id);
    if (term == NULL) {
        term = (am_Num*)am_settable(solver, &row->terms, sym);
        assert(term != NULL);
        *term = value;
    } else if (am_nearzero(*term += value))
        am_deltable(&row->terms, term);
}

static void am_addrow(am_Solver *solver, am_Row *row, const am_Row *other, am_Num multiplier) {
    am_Iterator it = am_itertable(&other->terms);
    row->constant += other->constant*multiplier;
    while (am_nextentry(&it))
        am_addvar(solver, row, it.key, *am_val(am_Num,it)*multiplier);
}

static void am_solvefor(am_Solver *solver, am_Row *row, am_Symbol enter, am_Symbol leave) {
    am_Num *term = (am_Num*)am_gettable(&row->terms, enter.id);
    am_Num reciprocal = 1.0f / *term;
    assert(enter.id != leave.id && !am_nearzero(*term));
    am_deltable(&row->terms, term);
    if (!am_approx(reciprocal, -1.0f)) am_multiply(row, -reciprocal);
    if (leave.id != 0) am_addvar(solver, row, leave, reciprocal);
}

static void am_eliminate(am_Solver *solver, am_Row *dst, am_Symbol out, const am_Row *row) {
    am_Num *term = (am_Num*)am_gettable(&dst->terms, out.id);
    if (!term) return;
    am_deltable(&dst->terms, term);
    am_addrow(solver, dst, row, *term);
}

/* variables & constraints */

#define am_checkvalue(t,p) if (!(p)) { am_deltable((t),(p)); return NULL; }

static am_Var *am_id2var(am_Solver *solver, am_Id var)
{ return solver ? (am_Var*)am_gettable(&solver->vars, var) : NULL; }

AM_API am_Id am_newvariable(am_Solver *solver, am_Num *pvalue) {
    am_Symbol sym;
    am_Var *ve;
    if (solver == NULL || pvalue == NULL) return 0;
    sym = am_newsymbol(solver, AM_EXTERNAL);
    ve = (am_Var*)am_settable(solver, &solver->vars, sym);
    if (ve == NULL) return 0;
    memset(ve, 0, sizeof(*ve));
    ve->pvalue   = pvalue;
    ve->refcount = 1;
    return sym.id;
}

AM_API void am_delvariable(am_Solver *solver, am_Id var) {
    am_Var *ve = am_id2var(solver, var);
    if (ve && --ve->refcount == 0) {
        assert(!am_isdummy(ve->next));
        am_deledit(solver, var);
        am_deltable(&solver->vars, ve);
    }
}

AM_API int am_refcount(am_Solver *solver, am_Id var) {
    am_Var *ve = am_id2var(solver, var);
    return ve ? ve->refcount : 0;
}

AM_API am_Num *am_varvalue(am_Solver *solver, am_Id var, am_Num *newvalue) {
    am_Var *ve = am_id2var(solver, var);
    am_Num *pvalue = ve ? ve->pvalue : NULL;
    if (ve && newvalue) ve->pvalue = newvalue;
    return pvalue;
}

AM_API am_Constraint *am_newconstraint(am_Solver *solver, am_Num strength) {
    am_Symbol sym = { 0, AM_EXTERNAL };
    am_Constraint **ce;
    if (solver == NULL || strength < 0.f) return NULL;
    sym.id = ++solver->cons_count;
    ce = (am_Constraint**)am_settable(solver, &solver->constraints, sym);
    assert(ce != NULL);
    *ce = (am_Constraint*)am_alloc(solver, &solver->conspool);
    am_checkvalue(&solver->constraints, ce);
    memset(*ce, 0, sizeof(**ce));
    (*ce)->solver   = solver;
    (*ce)->sym      = sym;
    (*ce)->strength = am_nearzero(strength) ? AM_REQUIRED : strength;
    am_initrow(&(*ce)->expression);
    return *ce;
}

AM_API void am_delconstraint(am_Constraint *cons) {
    am_Solver *solver = cons ? cons->solver : NULL;
    am_Iterator it;
    am_Constraint **ce;
    if (solver == NULL) return;
    am_remove(cons);
    ce = (am_Constraint**)am_gettable(&solver->constraints, cons->sym.id);
    assert(ce != NULL);
    am_deltable(&solver->constraints, ce);
    it = am_itertable(&cons->expression.terms);
    while (am_nextentry(&it))
        am_delvariable(cons->solver, it.key.id);
    am_freerow(solver, &cons->expression);
    am_free(&solver->conspool, cons);
}

AM_API am_Constraint *am_cloneconstraint(am_Constraint *other, am_Num strength) {
    am_Constraint *cons;
    if (other == NULL) return NULL;
    cons = am_newconstraint(other->solver,
            am_nearzero(strength) ? other->strength : strength);
    am_mergeconstraint(cons, other, 1.0f);
    cons->relation = other->relation;
    return cons;
}

AM_API int am_mergeconstraint(am_Constraint *cons, const am_Constraint *other, am_Num multiplier) {
    am_Iterator it;
    if (cons == NULL || other == NULL || cons->marker.id != 0
            || cons->solver != other->solver) return AM_FAILED;
    if (cons->relation == AM_GREATEQUAL) multiplier = -multiplier;
    cons->expression.constant += other->expression.constant*multiplier;
    it = am_itertable(&other->expression.terms);
    while (am_nextentry(&it)) {
        am_Var *ve = (am_Var*)am_gettable(&cons->solver->vars, it.key.id);
        assert(ve != NULL);
        ve->refcount += 1;
        am_addvar(cons->solver, &cons->expression, it.key,
                *am_val(am_Num,it)*multiplier);
    }
    return AM_OK;
}

AM_API void am_resetconstraint(am_Constraint *cons) {
    am_Iterator it;
    if (cons == NULL) return;
    am_remove(cons);
    cons->relation = 0;
    it = am_itertable(&cons->expression.terms);
    while (am_nextentry(&it))
        am_delvariable(cons->solver, it.key.id);
    am_resetrow(&cons->expression);
}

AM_API int am_addterm(am_Constraint *cons, am_Id var, am_Num multiplier) {
    am_Symbol sym = {0, AM_EXTERNAL};
    am_Var *ve;
    if (cons == NULL || var == 0 || cons->marker.id != 0) return AM_FAILED;
    if (cons->relation == AM_GREATEQUAL) multiplier = -multiplier;
    ve = (am_Var*)am_gettable(&cons->solver->vars, var);
    if (ve == NULL) return AM_FAILED;
    am_addvar(cons->solver, &cons->expression, (sym.id=var, sym), multiplier);
    return ve->refcount += 1, AM_OK;
}

AM_API int am_addconstant(am_Constraint *cons, am_Num constant) {
    if (cons == NULL || cons->marker.id != 0) return AM_FAILED;
    cons->expression.constant +=
        cons->relation == AM_GREATEQUAL ? -constant : constant;
    return AM_OK;
}

AM_API int am_setrelation(am_Constraint *cons, int relation) {
    assert(relation >= AM_LESSEQUAL && relation <= AM_GREATEQUAL);
    if (cons == NULL || cons->marker.id != 0 || cons->relation != 0)
        return AM_FAILED;
    if (relation != AM_GREATEQUAL) am_multiply(&cons->expression, -1.0f);
    cons->relation = relation;
    return AM_OK;
}

/* Cassowary algorithm */

AM_API int am_hasconstraint(am_Constraint *cons)
{ return cons != NULL && cons->marker.id != 0; }

AM_API void am_autoupdate(am_Solver *solver, int auto_update)
{ solver->auto_update = auto_update; }

static void am_infeasible(am_Solver *solver, am_Symbol sym, am_Row *row) {
    if (row->constant < 0.0f && !am_isdummy(row->infeasible_next)) {
        row->infeasible_next.id = solver->infeasible_rows.id;
        row->infeasible_next.type = AM_DUMMY;
        solver->infeasible_rows = sym;
    }
}

static void am_markdirty(am_Solver *solver, am_Symbol var) {
    am_Var *ve = (am_Var*)am_gettable(&solver->vars, var.id);
    assert(ve != NULL);
    if (am_isdummy(ve->next)) return;
    ve->next.id = solver->dirty_vars.id;
    ve->next.type = AM_DUMMY;
    solver->dirty_vars = var;
    ve->refcount += 1;
}

static void am_substitute_rows(am_Solver *solver, am_Symbol var, am_Row *expr) {
    am_Iterator it = am_itertable(&solver->rows);
    while (am_nextentry(&it)) {
        am_Row *row = am_val(am_Row,it);
        am_eliminate(solver, row, var, expr);
        if (am_isexternal(it.key))
            am_markdirty(solver, it.key);
        else
            am_infeasible(solver, it.key, row);
    }
    am_eliminate(solver, &solver->objective, var, expr);
}

static int am_takerow(am_Solver *solver, am_Symbol sym, am_Row *dst) {
    am_Row *row = (am_Row*)am_gettable(&solver->rows, sym.id);
    if (row == NULL) return AM_FAILED;
    am_deltable(&solver->rows, row);
    dst->constant   = row->constant;
    dst->terms      = row->terms;
    return AM_OK;
}

static int am_putrow(am_Solver *solver, am_Symbol sym, const am_Row *src) {
    am_Row *row;
    assert(am_gettable(&solver->rows, sym.id) == NULL);
    row = (am_Row*)am_settable(solver, &solver->rows, sym);
    assert(row != NULL);
    row->infeasible_next = am_null();
    row->constant = src->constant;
    row->terms    = src->terms;
    return AM_OK;
}

static void am_mergerow(am_Solver *solver, am_Row *row, am_Symbol var, am_Num multiplier) {
    am_Row *oldrow = (am_Row*)am_gettable(&solver->rows, var.id);
    if (oldrow)
        am_addrow(solver, row, oldrow, multiplier);
    else
        am_addvar(solver, row, var, multiplier);
}

static int am_optimize(am_Solver *solver, am_Row *objective) {
    for (;;) {
        am_Symbol enter = am_null(), leave = am_null();
        am_Num r, min_ratio = AM_NUM_MAX, *term;
        am_Iterator it = am_itertable(&objective->terms);
        am_Row tmp;

        assert(solver->infeasible_rows.id == 0);
        while (am_nextentry(&it))
            if (!am_isdummy(it.key) && *am_val(am_Num,it) < 0.0f)
            { enter = it.key; break; }
        if (enter.id == 0) return AM_OK;

        it = am_itertable(&solver->rows);
        while (am_nextentry(&it)) {
            am_Row *row = am_val(am_Row,it);
            if (am_isexternal(it.key)) continue;
            term = (am_Num*)am_gettable(&row->terms, enter.id);
            if (term == NULL || *term > 0.0f) continue;
            r = -row->constant / *term;
            if (r < min_ratio || (am_approx(r, min_ratio)
                        && it.key.id < leave.id))
                min_ratio = r, leave = it.key;
        }
        assert(leave.id != 0);
        if (leave.id == 0) return AM_UNBOUND;

        am_takerow(solver, leave, &tmp);
        am_solvefor(solver, &tmp, enter, leave);
        am_substitute_rows(solver, enter, &tmp);
        if (objective != &solver->objective)
            am_eliminate(solver, objective, enter, &tmp);
        am_putrow(solver, enter, &tmp);
    }
}

static am_Row am_makerow(am_Solver *solver, am_Constraint *cons) {
    am_Iterator it = am_itertable(&cons->expression.terms);
    am_Row row;
    am_initrow(&row);
    row.constant = cons->expression.constant;
    while (am_nextentry(&it)) {
        am_markdirty(solver, it.key);
        am_mergerow(solver, &row, it.key, *am_val(am_Num,it));
    }
    if (cons->relation != AM_EQUAL) {
        cons->marker.id = cons->marker_id, cons->marker.type = AM_SLACK;
        am_addvar(solver, &row, cons->marker, -1.0f);
        if (cons->strength < AM_REQUIRED) {
            cons->other.id = cons->other_id, cons->other.type = AM_ERROR;
            am_addvar(solver, &row, cons->other, 1.0f);
            am_addvar(solver, &solver->objective, cons->other, cons->strength);
        }
    } else if (cons->strength >= AM_REQUIRED) {
        cons->marker.id = cons->marker_id, cons->marker.type = AM_DUMMY;
        am_addvar(solver, &row, cons->marker, 1.0f);
    } else {
        cons->marker.id = cons->marker_id, cons->marker.type = AM_ERROR;
        cons->other.id = cons->other_id, cons->other.type = AM_ERROR;
        am_addvar(solver, &row, cons->marker, -1.0f);
        am_addvar(solver, &row, cons->other,   1.0f);
        am_addvar(solver, &solver->objective, cons->marker, cons->strength);
        am_addvar(solver, &solver->objective, cons->other,  cons->strength);
    }
    if (row.constant < 0.0f) am_multiply(&row, -1.0f);
    return row;
}

static void am_remove_errors(am_Solver *solver, am_Constraint *cons) {
    if (am_iserror(cons->marker))
        am_mergerow(solver, &solver->objective, cons->marker, -cons->strength);
    if (am_iserror(cons->other))
        am_mergerow(solver, &solver->objective, cons->other, -cons->strength);
    if (solver->objective.terms.count == 0)
        solver->objective.constant = 0.0f;
    cons->marker = cons->other = am_null();
}

static int am_add_with_artificial(am_Solver *solver, am_Row *row, am_Constraint *cons) {
    am_Symbol a = am_newsymbol(solver, AM_SLACK);
    am_Iterator it;
    am_Row tmp;
    am_Num *term;
    int ret;
    --solver->id_count; /* artificial variable will be removed */
    am_initrow(&tmp);
    am_addrow(solver, &tmp, row, 1.0f);
    am_putrow(solver, a, row);
    if (am_optimize(solver, &tmp)) return AM_UNBOUND;
    ret = am_nearzero(tmp.constant) ? AM_OK : AM_UNBOUND;
    am_freerow(solver, &tmp);
    if (am_takerow(solver, a, &tmp) == AM_OK) {
        am_Symbol enter = am_null();
        if (tmp.terms.count == 0) return am_freerow(solver, &tmp), ret;
        it = am_itertable(&tmp.terms);
        while (am_nextentry(&it))
            if (am_ispivotable(it.key)) { enter = it.key; break; }
        if (enter.id == 0) { am_freerow(solver, &tmp); return AM_UNBOUND; }
        am_solvefor(solver, &tmp, enter, a);
        am_substitute_rows(solver, enter, &tmp);
        am_putrow(solver, enter, &tmp);
    }
    it = am_itertable(&solver->rows);
    while (am_nextentry(&it)) {
        am_Row *row = am_val(am_Row,it);
        am_Num *term = (am_Num*)am_gettable(&row->terms, a.id);
        if (term) am_deltable(&row->terms, term);
    }
    term = (am_Num*)am_gettable(&solver->objective.terms, a.id);
    if (term) am_deltable(&solver->objective.terms, term);
    if (ret != AM_OK) am_remove(cons);
    return ret;
}

static int am_try_addrow(am_Solver *solver, am_Row *row, am_Constraint *cons) {
    am_Symbol subject = am_null();
    am_Iterator it = am_itertable(&row->terms);
    while (am_nextentry(&it))
        if (am_isexternal(it.key)) { subject = it.key; break; }
    if (subject.id == 0 && am_ispivotable(cons->marker)) {
        am_Num *term = (am_Num*)am_gettable(&row->terms, cons->marker.id);
        if (*term < 0.0f) subject = cons->marker;
    }
    if (subject.id == 0 && am_ispivotable(cons->other)) {
        am_Num *term = (am_Num*)am_gettable(&row->terms, cons->other.id);
        if (*term < 0.0f) subject = cons->other;
    }
    if (subject.id == 0) {
        it = am_itertable(&row->terms);
        while (am_nextentry(&it))
            if (!am_isdummy(it.key)) break;
        if (it.offset == 0) {
            if (am_nearzero(row->constant))
                subject = cons->marker;
            else {
                am_freerow(solver, row);
                return AM_UNSATISFIED;
            }
        }
    }
    if (subject.id == 0)
        return am_add_with_artificial(solver, row, cons);
    am_solvefor(solver, row, subject, am_null());
    am_substitute_rows(solver, subject, row);
    am_putrow(solver, subject, row);
    return AM_OK;
}

static am_Symbol am_get_leaving_row(am_Solver *solver, am_Symbol marker) {
    am_Symbol first = am_null(), second = am_null(), third = am_null();
    am_Num r1 = AM_NUM_MAX, r2 = AM_NUM_MAX;
    am_Iterator it = am_itertable(&solver->rows);
    while (am_nextentry(&it)) {
        am_Row *row = am_val(am_Row,it);
        am_Num *term = (am_Num*)am_gettable(&row->terms, marker.id);
        if (term == NULL) continue;
        if (am_isexternal(it.key))
            third = it.key;
        else if (*term < 0.0f) {
            am_Num r = -row->constant / *term;
            if (r < r1) r1 = r, first = it.key;
        } else {
            am_Num r = row->constant / *term;
            if (r < r2) r2 = r, second = it.key;
        }
    }
    return first.id ? first : second.id ? second : third;
}

static void *am_default_allocf(void *ud, void *ptr, size_t nsize, size_t osize) {
    void *newptr;
    (void)ud, (void)osize;
    if (nsize == 0) { free(ptr); return NULL; }
    newptr = realloc(ptr, nsize);
    if (newptr == NULL) abort();
    return newptr;
}

AM_API am_Solver *am_newsolver(am_Allocf *allocf, void *ud) {
    am_Solver *solver;
    if (allocf == NULL) allocf = am_default_allocf;
    if ((solver = (am_Solver*)allocf(ud, NULL, sizeof(am_Solver), 0)) == NULL)
        return NULL;
    memset(solver, 0, sizeof(*solver));
    solver->allocf = allocf;
    solver->ud     = ud;
    am_initrow(&solver->objective);
    am_inittable(&solver->vars, sizeof(am_Var));
    am_inittable(&solver->constraints, sizeof(am_Constraint*));
    am_inittable(&solver->suggests, sizeof(am_Suggest));
    am_inittable(&solver->rows, sizeof(am_Row));
    am_initpool(&solver->conspool, sizeof(am_Constraint));
    return solver;
}

AM_API void am_delsolver(am_Solver *solver) {
    am_Iterator it = am_itertable(&solver->constraints);
    while (am_nextentry(&it))
        am_freerow(solver, &(*am_val(am_Constraint*,it))->expression);
    it = am_itertable(&solver->suggests);
    while (am_nextentry(&it))
        am_freetable(solver, &am_val(am_Suggest,it)->dirtyset);
    it = am_itertable(&solver->rows);
    while (am_nextentry(&it))
        am_freerow(solver, am_val(am_Row,it));
    am_freerow(solver, &solver->objective);
    am_freetable(solver, &solver->vars);
    am_freetable(solver, &solver->constraints);
    am_freetable(solver, &solver->suggests);
    am_freetable(solver, &solver->rows);
    am_freepool(solver, &solver->conspool);
    solver->allocf(solver->ud, solver, 0, sizeof(*solver));
}

AM_API void am_resetsolver(am_Solver *solver) {
    am_Iterator it;
    if (solver == NULL) return;
    am_clearedits(solver);
    it = am_itertable(&solver->constraints);
    while (am_nextentry(&it)) {
        am_Constraint *cons = *am_val(am_Constraint*,it);
        cons->marker = cons->other = am_null();
    }
    it = am_itertable(&solver->rows);
    while (am_nextentry(&it))
        am_freerow(solver, am_val(am_Row,it));
    am_resettable(&solver->rows);
    am_resetrow(&solver->objective);
    assert(solver->infeasible_rows.id == 0);
    solver->age = 0;
}

AM_API void am_updatevars(am_Solver *solver) {
    while (solver->dirty_vars.id != 0) {
        am_Symbol var = solver->dirty_vars;
        am_Var *ve = (am_Var*)am_gettable(&solver->vars, var.id);
        assert(ve != NULL);
        solver->dirty_vars = ve->next;
        ve->next = am_null();
        if (ve->refcount == 1) {
            am_deledit(solver, var.id);
            am_deltable(&solver->vars, ve);
        } else {
            am_Row *row = (am_Row*)am_gettable(&solver->rows, var.id);
            *ve->pvalue = row ? row->constant : 0.0f;
            ve->refcount -= 1;
        }
    }
}

AM_API int am_add(am_Constraint *cons) {
    am_Solver *solver = cons ? cons->solver : NULL;
    am_Symbol sym;
    am_Row row;
    int ret;
    if (solver == NULL || cons->marker.id != 0) return AM_FAILED;
    if (!cons->marker_id)
        cons->marker_id = ((sym = am_newsymbol(solver, 0)), sym.id);
    if (!cons->other_id && cons->strength < AM_REQUIRED)
        cons->other_id = ((sym = am_newsymbol(solver, 0)), sym.id);
    row = am_makerow(solver, cons);
    if ((ret = am_try_addrow(solver, &row, cons)) != AM_OK)
        am_remove_errors(solver, cons);
    else {
        if (am_optimize(solver, &solver->objective)) return AM_UNBOUND;
        if (solver->auto_update) am_updatevars(solver);
        solver->age += 1;
    }
    assert(solver->infeasible_rows.id == 0);
    return ret;
}

AM_API void am_remove(am_Constraint *cons) {
    am_Solver *solver;
    am_Symbol marker;
    am_Row tmp;
    int ret;
    if (cons == NULL || cons->marker.id == 0) return;
    solver = cons->solver, marker = cons->marker;
    am_remove_errors(solver, cons);
    if (am_takerow(solver, marker, &tmp) != AM_OK) {
        am_Symbol leave = am_get_leaving_row(solver, marker);
        assert(leave.id != 0);
        am_takerow(solver, leave, &tmp);
        am_solvefor(solver, &tmp, marker, leave);
        am_substitute_rows(solver, marker, &tmp);
    }
    am_freerow(solver, &tmp);
    ret = am_optimize(solver, &solver->objective);
    assert(ret == AM_OK), (void)ret;
    solver->age += 1;
    if (solver->auto_update) am_updatevars(solver);
}

AM_API int am_setstrength(am_Constraint *cons, am_Num strength) {
    if (cons == NULL) return AM_FAILED;
    strength = am_nearzero(strength) ? AM_REQUIRED : strength;
    if (cons->strength == strength) return AM_OK;
    if (cons->strength >= AM_REQUIRED || strength >= AM_REQUIRED)
        return am_remove(cons), cons->strength = strength, am_add(cons);
    if (cons->marker.id != 0) {
        am_Solver *solver = cons->solver;
        am_Num diff = strength - cons->strength;
        am_mergerow(solver, &solver->objective, cons->marker, diff);
        am_mergerow(solver, &solver->objective, cons->other,  diff);
        if (am_optimize(solver, &solver->objective)) return AM_UNBOUND;
        if (solver->auto_update) am_updatevars(solver);
        solver->age += 1;
    }
    cons->strength = strength;
    return AM_OK;
}

AM_API int am_hasedit(am_Solver *solver, am_Id var) {
    if (solver == NULL) return 0;
    return am_gettable(&solver->suggests, var) != NULL;
}

static am_Suggest *am_newedit(am_Solver *solver, am_Id var, am_Num strength) {
    am_Symbol sym = {0, AM_EXTERNAL};
    am_Suggest *s;
    int ret;
    am_Var *ve = (am_Var*)am_gettable(&solver->vars, var);
    if (ve == NULL) return NULL;
    s = (am_Suggest*)am_settable(solver, &solver->suggests, (sym.id=var, sym));
    s->constraint = am_newconstraint(solver, strength);
    am_setrelation(s->constraint, AM_EQUAL);
    am_addterm(s->constraint, var, 1.0f); /* var must have positive signture */
    am_addconstant(s->constraint, -*ve->pvalue);
    ret = am_add(s->constraint);
    assert(ret == AM_OK), (void)ret;
    s->edit_value = *ve->pvalue;
    return s;
}

AM_API int am_addedit(am_Solver *solver, am_Id var, am_Num strength) {
    am_Suggest *s;
    if (solver == NULL || var == 0) return AM_FAILED;
    if (strength >= AM_STRONG) strength = AM_STRONG;
    s = (am_Suggest*)am_gettable(&solver->suggests, var);
    if (s) return am_setstrength(s->constraint, strength);
    return am_newedit(solver, var, strength) ? AM_OK : AM_FAILED;
}

AM_API void am_deledit(am_Solver *solver, am_Id var) {
    am_Suggest *s;
    if (solver == NULL || var == 0) return;
    s = (am_Suggest*)am_gettable(&solver->suggests, var);
    if (s == NULL) return;
    am_freetable(solver, &s->dirtyset);
    am_delconstraint(s->constraint);
    am_deltable(&solver->suggests, s);
}

AM_API void am_clearedits(am_Solver *solver) {
    am_Iterator it;
    if (solver == NULL) return;
    it = am_itertable(&solver->suggests);
    if (!solver->auto_update) am_updatevars(solver);
    while (am_nextentry(&it)) {
        am_Suggest *s = am_val(am_Suggest,it);
        am_freetable(solver, &s->dirtyset);
        am_delconstraint(s->constraint);
    }
    am_resettable(&solver->suggests);
}

static void am_cached_sugggest(am_Solver *solver, am_Suggest *s, am_Num delta) {
    am_Iterator it = am_itertable(&s->dirtyset);
    am_Symbol marker = s->constraint->marker;
    int pure = 1;
    while (am_nextentry(&it)) {
        am_Row *row = (am_Row*)am_gettable(&solver->rows, it.key.id);
        am_Num *term;
        assert(row != NULL);
        term = (am_Num*)am_gettable(&row->terms, marker.id);
        assert(term != NULL);
        row->constant += *term * delta;
        if (am_isexternal(it.key))
            am_markdirty(solver, it.key);
        else if (!am_nearzero(row->constant) && row->constant < 0.0f)
            pure = 0, am_infeasible(solver, it.key, row);
    }
    if (!pure) s->age = 0;
}

static void am_delta_edit_constant(am_Solver *solver, am_Suggest *s, am_Num delta) {
    am_Iterator it = am_itertable(&solver->rows);
    am_Constraint *cons = s->constraint;
    am_Row *row;
    int pure = 1;
    if ((row = (am_Row*)am_gettable(&solver->rows, cons->marker.id)) != NULL)
    { row->constant -= delta, am_infeasible(solver, cons->marker, row); return; }
    if ((row = (am_Row*)am_gettable(&solver->rows, cons->other.id)) != NULL)
    { row->constant += delta, am_infeasible(solver, cons->other, row); return; }
    if (s->age == solver->age) { am_cached_sugggest(solver, s, delta); return; }
    am_resettable(&s->dirtyset);
    while (am_nextentry(&it)) {
        am_Row *row = am_val(am_Row,it);
        am_Num *term = (am_Num*)am_gettable(&row->terms, cons->marker.id);
        if (term == NULL) continue;
        row->constant += *term*delta;
        am_settable(solver, &s->dirtyset, it.key);
        if (am_isexternal(it.key))
            am_markdirty(solver, it.key);
        else if (!am_nearzero(row->constant) && row->constant < 0.0f)
            pure = 0, am_infeasible(solver, it.key, row);
    }
    if (pure) s->age = solver->age;
}

static void am_dual_optimize(am_Solver *solver) {
    while (solver->infeasible_rows.id != 0) {
        am_Symbol enter = am_null(), leave;
        am_Num r, min_ratio = AM_NUM_MAX;
        am_Iterator it;
        am_Row tmp, *row =
            (am_Row*)am_gettable(&solver->rows, solver->infeasible_rows.id);
        assert(row != NULL);
        leave = solver->infeasible_rows;
        solver->infeasible_rows = row->infeasible_next;
        row->infeasible_next = am_null();
        if (am_nearzero(row->constant) || row->constant >= 0.0f) continue;
        it = am_itertable(&row->terms);
        while (am_nextentry(&it)) {
            am_Num term = *am_val(am_Num,it);
            am_Num *objterm;
            if (am_isdummy(it.key) || term <= 0.0f) continue;
            objterm = (am_Num*)am_gettable(&solver->objective.terms, it.key.id);
            r = objterm ? *objterm / term : 0.0f;
            if (min_ratio > r) min_ratio = r, enter = it.key;
        }
        assert(enter.id != 0);
        am_takerow(solver, leave, &tmp);
        am_solvefor(solver, &tmp, enter, leave);
        am_substitute_rows(solver, enter, &tmp);
        am_putrow(solver, enter, &tmp);
    }
}

AM_API void am_suggest(am_Solver *solver, am_Id var, am_Num value) {
    am_Suggest *s;
    am_Num delta;
    if (solver == NULL || var == 0) return;
    s = (am_Suggest*)am_gettable(&solver->suggests, var);
    if (s == NULL) s = am_newedit(solver, var, AM_MEDIUM), assert(s != NULL);
    delta = value - s->edit_value, s->edit_value = value;
    am_delta_edit_constant(solver, s, delta);
    am_dual_optimize(solver);
    if (solver->auto_update) am_updatevars(solver);
}

/* dump & load */

#ifndef AM_NAME_LEN
# define AM_NAME_LEN 256
#endif /* AM_NAME_LEN */
#ifndef AM_BUF_LEN
# define AM_BUF_LEN  4096
#endif /* AM_BUF_LEN */

#define amC(cond) do { if (!(cond)) return \
    printf(__FILE__ ":%d: " #cond "\n", __LINE__),\
    AM_FAILED; } while (0)
#define amE(cond) amC((cond) == AM_OK)

static int am_islittleendian(void) {
    union { unsigned short u16[2]; unsigned u32; } u;
    return u.u16[0] = 1, u.u32 == 1;
}

typedef struct am_DumpCtx {
    const am_Solver *solver;
    unsigned  *syms; 
    unsigned  *cons;
    am_Table   symmap;
    am_Dumper *dumper;
    char      *p;
    int        ret;
    int        endian;
    char       buf[AM_BUF_LEN];
} am_DumpCtx;

static int am_intcmp(const void *lhs, const void *rhs)
{ return *(const int*)lhs - *(const int*)rhs; }

static int am_writechar(am_DumpCtx *ctx, int ch) {
    if ((ctx->p - ctx->buf) == AM_BUF_LEN) {
        amE(ctx->ret = ctx->dumper->writer(ctx->dumper, ctx->buf, AM_BUF_LEN));
        ctx->p = ctx->buf;
    }
    return (*ctx->p++ = ch & 0xFF), AM_OK;
}

static int am_writeraw(am_DumpCtx *ctx, am_Size n, int width) {
    switch (width) {
    default: return AM_FAILED; /* LCOV_EXCL_LINE */
    case 32: amE(am_writechar(ctx, n >> 24));
             amE(am_writechar(ctx, n >> 16)); /* FALLTHROUGH */
    case 16: amE(am_writechar(ctx, n >> 8));
             amE(am_writechar(ctx, n & 0xFF));
    }
    return AM_OK;
}

static int am_writeuint32(am_DumpCtx *ctx, am_Size n) {
    if (n <= 0x7F) amE(am_writechar(ctx, n));
    else if (n <= 0xFF) {
        amE(am_writechar(ctx, 0xCC));
        amE(am_writechar(ctx, n));
    } else if (n <= 0xFFFF) {
        amE(am_writechar(ctx, 0xCD));
        amE(am_writeraw(ctx, n, 16));
    } else {
        amE(am_writechar(ctx, 0xCE));
        amE(am_writeraw(ctx, n, 32));
    }
    return AM_OK;
}

static int am_writefloat32(am_DumpCtx *ctx, am_Num n) {
    if (sizeof(am_Num) == sizeof(float)) {
        union { float flt; unsigned u32; } u;
        u.flt = n;
        amE(am_writechar(ctx, 0xCA));
        amE(am_writeraw(ctx, u.u32, 32));
    } else {
        union { double flt; unsigned u32[2]; } u;
        u.flt = n;
        amE(am_writechar(ctx, 0xCB));
        amE(am_writeraw(ctx, u.u32[ctx->endian], 32));
        amE(am_writeraw(ctx, u.u32[!ctx->endian], 32));
    }
    return AM_OK;
}

static int am_writestring(am_DumpCtx *ctx, const char *name) {
    am_Dumper *d = ctx->dumper;
    size_t namelen = (name ? strlen(name) : 0), buflen;
    amC(namelen < AM_NAME_LEN && namelen <= 0xFF);
    if (namelen < 32)
        amE(am_writechar(ctx, 0xA0 + namelen));
    else {
        amE(am_writechar(ctx, 0xD9));
        amE(am_writechar(ctx, namelen));
    }
    if ((buflen = (ctx->p - ctx->buf)) + namelen > AM_BUF_LEN) {
        ctx->p = ctx->buf;
        if (buflen) amE(ctx->ret = d->writer(d, ctx->buf, buflen));
        if (namelen > AM_BUF_LEN) return ctx->ret = d->writer(d, name, namelen);
    }
    memcpy(ctx->p, name, namelen);
    return ctx->p += namelen, AM_OK;
}

static int am_writecount(am_DumpCtx *ctx, am_Size count) {
    if (count < 16)
        amE(am_writechar(ctx, 0x90 + count));
    else if (count <= 0xFFFF) {
        amE(am_writechar(ctx, 0xDC));
        amE(am_writeraw(ctx, count, 16));
    } else {
        amE(am_writechar(ctx, 0xDD));
        amE(am_writeraw(ctx, count, 32));
    }
    return AM_OK;
}

static unsigned am_mapid(am_DumpCtx *ctx, unsigned key) {
    unsigned *id = (unsigned*)am_gettable(&ctx->symmap, key);
    return assert(id != NULL), *id;
}

static int am_writerow(am_DumpCtx *ctx, const am_Row *row) {
    am_Iterator it = am_itertable(&row->terms);
    amE(am_writecount(ctx, row->terms.count * 2 + 1));
    amE(am_writefloat32(ctx, row->constant));
    while (am_nextentry(&it)) {
        amE(am_writeuint32(ctx, am_mapid(ctx, it.key.id) << 2 | it.key.type));
        amE(am_writefloat32(ctx, *am_val(am_Num,it)));
    }
    return AM_OK;
}

static int am_writevars(am_DumpCtx *ctx) {
    const am_Solver *solver = ctx->solver;
    am_Size i;
    amE(am_writecount(ctx, solver->vars.count));
    for (i = 0; i < solver->vars.count; ++i) {
        am_Var *ve = (am_Var*)am_gettable(&solver->vars, ctx->syms[i]);
        assert(ve != NULL);
        amE(am_writestring(ctx,
                    ctx->dumper->var_name(ctx->dumper, i, ctx->syms[i], ve->pvalue)));
    }
    return AM_OK;
}

static int am_writeconstraints(am_DumpCtx *ctx) {
    const am_Solver *solver = ctx->solver;
    am_Size i;
    amE(am_writecount(ctx, solver->constraints.count));
    for (i = 0; i < solver->constraints.count; ++i) {
        am_Constraint **ce = (am_Constraint**)am_gettable(
                &solver->constraints, ctx->cons[i]);
        unsigned id;
        assert(ce != NULL);
        amE(am_writecount(ctx, 6));
        /* [name, strength, marker, other, relation, row] */
        amE(am_writestring(ctx,
                    ctx->dumper->cons_name ?
                    ctx->dumper->cons_name(ctx->dumper, i, (*ce)) : NULL));
        amE(am_writefloat32(ctx, (*ce)->strength));
        if (id = 0, (*ce)->marker.type)
            id = am_mapid(ctx, (*ce)->marker_id) << 2 | (*ce)->marker.type;
        amE(am_writeuint32(ctx, id));
        if (id = 0, (*ce)->other.type)
            id = am_mapid(ctx, (*ce)->other_id) << 2 | (*ce)->other.type;
        amE(am_writeuint32(ctx, id));
        amE(am_writeuint32(ctx, (*ce)->relation));
        amE(am_writerow(ctx, &(*ce)->expression));
    }
    return AM_OK;
}

static int am_writerows(am_DumpCtx *ctx) {
    const am_Solver *solver = ctx->solver;
    am_Iterator it = am_itertable(&solver->rows);
    amE(am_writecount(ctx, solver->rows.count*2));
    while (am_nextentry(&it)) {
        amE(am_writeuint32(ctx, am_mapid(ctx, it.key.id) << 2 | it.key.type));
        amE(am_writerow(ctx, am_val(am_Row,it)));
    }
    return AM_OK;
}

static int am_collect(am_DumpCtx *ctx) {
    const am_Solver *solver = ctx->solver;
    size_t i, vc = solver->vars.count, cc = 0, count = 0;
    am_Iterator it = am_itertable(&solver->vars);
    while (am_nextentry(&it)) ctx->syms[count++] = it.key.id;
    it = am_itertable(&solver->constraints);
    while (am_nextentry(&it)) {
        am_Constraint *cons = *am_val(am_Constraint*,it);
        if (cons->marker.type) ctx->syms[count++] = cons->marker_id;
        if (cons->other.type) ctx->syms[count++] = cons->other_id;
        ctx->cons[cc++] = it.key.id;
    }
    qsort(ctx->cons, cc, sizeof(unsigned), am_intcmp);
    qsort(ctx->syms, vc, sizeof(unsigned), am_intcmp);
    qsort(ctx->syms + vc, count - vc, sizeof(unsigned), am_intcmp);
    am_inittable(&ctx->symmap, sizeof(unsigned));
    ctx->symmap.count = count/2;
    if (!amH_alloc(solver, &ctx->symmap)) return AM_FAILED;
    for (i = 0; i < count; ++i) {
        am_Symbol sym = {0, 0};
        unsigned *val = (unsigned*)am_settable(solver, &ctx->symmap,
                (sym.id = ctx->syms[i], sym));
        assert(val != NULL), *val = i;
    }
    return assert(count == ctx->symmap.count), AM_OK;
}

static int am_dumpall(am_DumpCtx *ctx) {
    const am_Solver *solver = ctx->solver;
    am_Dumper *d = ctx->dumper;
    amE(am_writecount(ctx, 5));
    /* [total, vars, constraints, rows, objective] */
    amE(am_writeuint32(ctx, ctx->symmap.count));
    amE(am_writevars(ctx));
    amE(am_writeconstraints(ctx));
    amE(am_writerows(ctx));
    amE(am_writerow(ctx, &solver->objective));
    if (ctx->p > ctx->buf)
        amE(ctx->ret = d->writer(d, ctx->buf, (ctx->p - ctx->buf)));
    return ctx->ret;
}

AM_API int am_dump(am_Solver *solver, am_Dumper *dumper) {
    size_t cons_alloc, sym_alloc;
    am_DumpCtx ctx;
    ctx.ret = AM_FAILED;
    if (solver == NULL || dumper == NULL || dumper->writer == NULL
            || dumper->var_name == NULL) return AM_FAILED;
    am_clearedits(solver);
    cons_alloc = sizeof(unsigned) * solver->constraints.count;
    sym_alloc = sizeof(unsigned) * (cons_alloc*2 + solver->vars.count);
    memset(&ctx, 0, sizeof(ctx));
    ctx.syms = (unsigned*)solver->allocf(solver->ud, NULL, sym_alloc, 0);
    ctx.cons = (unsigned*)solver->allocf(solver->ud, NULL, cons_alloc, 0);
    ctx.solver = solver;
    if (ctx.syms && ctx.cons && (ctx.ret = am_collect(&ctx)) == AM_OK) {
        ctx.endian = am_islittleendian();
        ctx.dumper = dumper;
        ctx.p = ctx.buf;
        ctx.ret = am_dumpall(&ctx);
    }
    if (ctx.syms) solver->allocf(solver->ud, ctx.syms, 0, sym_alloc);
    if (ctx.cons) solver->allocf(solver->ud, ctx.cons, 0, cons_alloc);
    am_freetable(solver, &ctx.symmap);
    return ctx.ret;
}

typedef struct am_LoadCtx {
    am_Solver  *solver;
    am_Loader  *loader;
    size_t      n;
    const char *p, *s;
    am_Size     offset;
    int         endian;
    char        buf[AM_NAME_LEN];
} am_LoadCtx;

#define am_getchar(ctx) ((ctx)->n-- > 0 ? *(ctx)->p++ & 0xFF : am_fill(ctx))

static int am_fill(am_LoadCtx *ctx) {
    ctx->p = ctx->loader->reader(ctx->loader, &ctx->n);
    if (ctx->p == NULL || ctx->n == 0) return AM_FAILED;
    return ctx->n -= 1, *ctx->p++ & 0xFF;
}

static int am_readraw(am_LoadCtx *ctx, unsigned *pv, int width) {
    int c1 = 0, c2 = 0, c3, c4;
    switch (width) {
    default: return AM_FAILED; /* LCOV_EXCL_LINE */
    case 32: amC((c1 = am_getchar(ctx)) != AM_FAILED);
             amC((c2 = am_getchar(ctx)) != AM_FAILED); /* FALLTHROUGH */
    case 16: amC((c3 = am_getchar(ctx)) != AM_FAILED);
             amC((c4 = am_getchar(ctx)) != AM_FAILED);
    }
    return (*pv = c1 << 24 | c2 << 16 | c3 << 8 | c4), AM_OK;
}

static int am_readuint32(am_LoadCtx *ctx, am_Size *pv) {
    int ty, c;
    switch (ty = am_getchar(ctx)) {
    default: amC(ty <= 0x7F); *pv = ty; break;
    case 0xCC: amC((c = am_getchar(ctx)) != AM_FAILED); *pv = c; break;
    case 0xCD: amE(am_readraw(ctx, pv, 16)); break;
    case 0xCE: amE(am_readraw(ctx, pv, 32)); break;
    }
    return AM_OK;
}

static int am_readfloat32(am_LoadCtx *ctx, am_Num *pv) {
    int ty = am_getchar(ctx);
    if (ty == 0xCA) {
        union { float flt; unsigned u32; } u;
        amE(am_readraw(ctx, &u.u32, 32)); return u.flt;
    } else if (ty == 0xCB) {
        union { double flt; unsigned u32[2]; } u;
        amE(am_readraw(ctx, &u.u32[ctx->endian], 32));
        amE(am_readraw(ctx, &u.u32[!ctx->endian], 32));
        return (*pv = u.flt), AM_OK;
    }
    return AM_FAILED; /* LCOV_EXCL_LINE */
}

static int am_readstring(am_LoadCtx *ctx) {
    char *buf = ctx->buf;
    am_Size size;
    int ty, c;
    switch (ty = am_getchar(ctx)) {
    default:   amC(ty >= 0xA0 && ty <= 0xBF); size = ty - 0xA0;   break;
    case 0xD9: amC((c = am_getchar(ctx)) != AM_FAILED); size = c; break;
    }
    ctx->s = size ? ctx->buf : NULL;
    if (ctx->n >= size)
        return memcpy(ctx->buf, ctx->p, size), ctx->buf[size] = 0,
               ctx->p += size, ctx->n -= size, AM_OK;
    for (;;) {
        memcpy(buf, ctx->p, ctx->n), buf += ctx->n, size -= ctx->n;
        amC((c = am_fill(ctx)) != AM_FAILED);
        *buf++ = c, size -= 1;
        if (size <= ctx->n) {
            memcpy(buf, ctx->p, size), buf[size] = 0;
            return (ctx->n -= size, ctx->p += size), AM_OK;
        }
    }
}

static int am_readcount(am_LoadCtx *ctx, am_Size *pcount) {
    int ty;
    switch (ty = am_getchar(ctx)) {
    default: amC(ty >= 0x90 && ty <= 0x9F); *pcount = ty - 0x90; break;
    case 0xDC: amE(am_readraw(ctx, pcount, 16)); break;
    case 0xDD: amE(am_readraw(ctx, pcount, 32)); break; 
    }
    return AM_OK;
}

static int am_readrow(am_LoadCtx *ctx, am_Row *row) {
    am_Size i, count, value;
    amE(am_readcount(ctx, &count));
    amC(count >= 1 && (count & 1) == 1);
    amE(am_readfloat32(ctx, &row->constant));
    for (i = 1; i < count; i += 2) {
        am_Symbol sym = {0, 0};
        am_Num *term;
        amE(am_readuint32(ctx, &value));
        sym.id = ctx->offset+(value>>2), sym.type = value & 3;
        amC(term = (am_Num*)am_settable(ctx->solver, &row->terms, sym));
        amE(am_readfloat32(ctx, term));
    }
    return AM_OK;
}

static int am_readvars(am_LoadCtx *ctx) {
    am_Solver *solver = ctx->solver;
    am_Size i, count;
    am_Symbol sym = {0, AM_EXTERNAL};
    amE(am_readcount(ctx, &count));
    for (i = 0; i < count; ++i) {
        am_Var *ve;
        am_Num *pvalue;
        amE(am_readstring(ctx));
        pvalue = ctx->loader->load_var(ctx->loader, ctx->s, i, ctx->offset+i);
        amC(pvalue != NULL);
        sym.id = ctx->offset+i;
        amC(ve = (am_Var*)am_settable(solver, &solver->vars, sym));
        memset(ve, 0, sizeof(*ve));
        ve->pvalue   = pvalue;
        ve->refcount = 1;
    }
    return AM_OK;
}

static int am_readmarker(am_LoadCtx *ctx, am_Symbol *sym, unsigned *id) {
    am_Size value;
    amE(am_readuint32(ctx, &value));
    *id = ctx->offset + (value >> 2);
    sym->type = value & 3;
    if (sym->type) sym->id = *id;
    return AM_OK;
}

static int am_readconstraints(am_LoadCtx *ctx) {
    am_Size i, count, value;
    am_Solver *solver = ctx->solver;
    amE(am_readcount(ctx, &count));
    for (i = 0; i < count; ++i) {
        am_Constraint *cons;
        am_Num strength;
        amE(am_readcount(ctx, &value));
        amC(value == 6); /* [name, strength, marker, other, relation, row] */
        amE(am_readstring(ctx));
        amE(am_readfloat32(ctx, &strength));
        amC(cons = am_newconstraint(solver, strength));
        if (ctx->loader->load_cons)
            ctx->loader->load_cons(ctx->loader, ctx->s, i, cons);
        amE(am_readmarker(ctx, &cons->marker, &cons->marker_id));
        amE(am_readmarker(ctx, &cons->other, &value));
        cons->other_id = value;
        amE(am_readuint32(ctx, &value));
        amC(value >= AM_LESSEQUAL && value <= AM_GREATEQUAL);
        cons->relation = value;
        amE(am_readrow(ctx, &cons->expression));
    }
    return AM_OK;
}

static int am_readrows(am_LoadCtx *ctx) {
    am_Solver *solver = ctx->solver;
    am_Size i, count, value;
    amE(am_readcount(ctx, &count));
    amC((count & 1) == 0);
    for (i = 0; i < count; i += 2) {
        am_Symbol sym;
        am_Row *row;
        amE(am_readuint32(ctx, &value));
        sym.id = ctx->offset + (value >> 2), sym.type = value & 3;
        amC(row = (am_Row*)am_settable(solver, &solver->rows, sym));
        am_initrow(row);
        amE(am_readrow(ctx, row));
    }
    return AM_OK;
}

AM_API int am_load(am_Solver *solver, am_Loader *loader) {
    am_LoadCtx ctx;
    am_Size count, total;
    if (solver == NULL || loader == NULL || loader->reader == NULL
            || loader->load_var == NULL) return AM_FAILED;
    memset(&ctx, 0, sizeof(ctx));
    ctx.solver = solver;
    ctx.offset = solver->id_count + 1;
    ctx.endian = am_islittleendian();
    ctx.loader = loader;
    amE(am_readcount(&ctx, &count));
    amC(count == 5); /* [total, vars, constraints, rows, objective] */
    amE(am_readuint32(&ctx, &total));
    amC(ctx.offset + total <= 0x3FFFFFFF);
    am_resetsolver(solver);
    amE(am_readvars(&ctx));
    amE(am_readconstraints(&ctx));
    amE(am_readrows(&ctx));
    amE(am_readrow(&ctx, &solver->objective));
    return solver->id_count = ctx.offset + total, AM_OK;
}


AM_NS_END

#endif /* AM_IMPLEMENTATION */

/* cc: flags+='-shared -O2 -DAM_IMPLEMENTATION -xc'
   unixcc: output='amoeba.so'
   win32cc: output='amoeba.dll' */

