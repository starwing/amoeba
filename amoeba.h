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

#define AM_REQUIRED     ((am_Num)INFINITY)
#define AM_STRONG       ((am_Num)1000000)
#define AM_MEDIUM       ((am_Num)1000)
#define AM_WEAK         ((am_Num)1)

#include <stddef.h>
#include <math.h>

AM_NS_BEGIN


#ifdef AM_NUM
typedef AM_NUM am_Num;
#elif defined(AM_USE_DOUBLE)
typedef double am_Num;
#else
typedef float  am_Num;
#endif

typedef struct am_Solver     am_Solver;
typedef struct am_Constraint am_Constraint;

typedef unsigned am_Id;

typedef enum am_AllocType {
    am_AllocSolver = 1,
    am_AllocConstraint,
    am_AllocSuggest,
    am_AllocHash,
    am_AllocDump,
    am_AllocPool
} am_AllocType;

typedef void *am_Allocf (void **pud, void *ptr,
        size_t nsize, size_t osize, am_AllocType ty);

AM_API am_Solver *am_newsolver   (am_Allocf *allocf, void *ud);
AM_API void       am_resetsolver (am_Solver *S);
AM_API void       am_delsolver   (am_Solver *S);

AM_API void am_updatevars (am_Solver *S);
AM_API void am_autoupdate (am_Solver *S, int auto_update);

AM_API int  am_add    (am_Constraint *cons);
AM_API void am_remove (am_Constraint *cons);

AM_API void am_clearedits (am_Solver *S);
AM_API int  am_hasedit (am_Solver *S, am_Id var);
AM_API int  am_addedit (am_Solver *S, am_Id var, am_Num strength);
AM_API void am_suggest (am_Solver *S, am_Id var, am_Num value);
AM_API void am_deledit (am_Solver *S, am_Id var);

AM_API am_Id am_newvariable (am_Solver *S, am_Num *pvalue);
AM_API void  am_delvariable (am_Solver *S, am_Id var);

AM_API am_Num *am_varvalue (am_Solver *S, am_Id var, am_Num *newvalue);

AM_API int am_refcount      (am_Solver *S, am_Id var);
AM_API int am_hasconstraint (am_Constraint *cons);

AM_API am_Constraint *am_newconstraint   (am_Solver *S, am_Num strength);
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

AM_API int am_dump (am_Solver *S, am_Dumper *dumper);
AM_API am_Solver *am_load (am_Allocf *allocf, void *ud,
        am_Loader *loader, size_t maxmem);

struct am_Dumper {
    const char *(*var_name)  (am_Dumper *D, unsigned idx, am_Id var, am_Num *value);
    const char *(*cons_name) (am_Dumper *D, unsigned idx, am_Constraint *cons);

    int (*writer) (am_Dumper *D, const void *buf, size_t len);
};

struct am_Loader {
    am_Num *(*load_var)  (am_Loader *L, const char *name, unsigned idx, am_Id var);
    void    (*load_cons) (am_Loader *L, const char *name, unsigned idx, am_Constraint *cons);

    const char *(*reader) (am_Loader *L, size_t *plen);
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

#define amV_isexternal(key)   ((key).type == AM_EXTERNAL)
#define amV_isslack(key)      ((key).type == AM_SLACK)
#define amV_iserror(key)      ((key).type == AM_ERROR)
#define amV_isdummy(key)      ((key).type == AM_DUMMY)
#define amV_ispivotable(key)  (amV_isslack(key) || amV_iserror(key))

#define amM_alloc(T,S,ty) ((T*)amM_realloc((S),NULL,sizeof(T),0,(ty)))
#define amM_free(S,p,ty)  (amM_realloc((S),(p),0,sizeof(*(p)),(ty)), (void)0)

#define AM_MIN_HASHSIZE  8
#define AM_POOL_SIZE     4096
#define AM_UNSIGNED_BITS (sizeof(unsigned)*CHAR_BIT)

#ifdef AM_USE_DOUBLE
# define AM_NUM_MAX DBL_MAX
# define AM_NUM_EPS 1e-6
#else
# define AM_NUM_MAX FLT_MAX
# define AM_NUM_EPS 1e-4
#endif

#ifndef AM_DEBUG
# ifdef __GNUC__
#   pragma GCC diagnostic ignored "-Wvariadic-macros"
# endif
# define AM_DEBUG(...) ((void)0)
#endif /* AM_DEBUG */

#define amC(cond) do { if (!(cond)) return \
    AM_DEBUG(__FILE__ ":%d: " #cond "\n", __LINE__), AM_FAILED; } while (0)
#define amE(expr) do { int r_; if ((r_ = (expr)) != AM_OK) return \
    AM_DEBUG(__FILE__ ":%d: " #expr " err %d\n", __LINE__, r_), r_; } while (0)

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

typedef struct am_Arena {
    size_t size;
    void  *buf;
} am_Arena;

typedef struct am_Table {
    am_Size size;
    am_Size count;
    am_Size value_size : AM_UNSIGNED_BITS - 1;
    am_Size arena      : 1;
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

struct am_Constraint {
    void      *ud;
    am_Row     expression;
    am_Symbol  sym; /* AM_EXTERNAL for pool cons, AM_DUMMY for suggest */
    am_Symbol  marker;
    am_Symbol  other;
    unsigned   marker_id;
    unsigned   other_id : AM_UNSIGNED_BITS - 2;
    unsigned   relation : 2;
    am_Num     strength;
    am_Solver *S;
};

typedef struct am_Suggest {
    am_Size       age;
    am_Num        edit_value;
    am_Constraint constraint;
    am_Table      dirtyset;
} am_Suggest;

struct am_Solver {
    void      *ud;
    am_Allocf *allocf;
    am_Row     objective;
    am_Table   vars;            /* symbol -> am_Var */
    am_Table   constraints;     /* symbol -> am_Constraint* */
    am_Table   suggests;        /* symbol -> am_Suggest* */
    am_Table   rows;            /* symbol -> am_Row */
    am_Arena   arena;
    am_Size    auto_update;
    am_Size    age; /* counts of rows changed */
    am_Size    current_id;
    am_Size    current_cons;
    am_Symbol  infeasible_rows;
    am_Symbol  dirty_vars;
    am_MemPool conspool;
    size_t     maxmem; /* remaining load budget, 0 = unlimited */
};

static void *amM_realloc(am_Solver *S, void *ptr,
        size_t nsize, size_t osize, am_AllocType ty) {
    size_t grow = nsize > osize ? nsize - osize : 0;
    void *ret;
    if (ty == am_AllocSolver) /* allocated before the budget */
        return S->allocf(&S->ud, ptr, nsize, osize, ty);
    /* equality fails so 0 can keep meaning "unlimited" */
    if (S->maxmem != 0 && grow >= S->maxmem) return NULL;
    ret = S->allocf(&S->ud, ptr, nsize, osize, ty);
    if (nsize != 0 && ret == NULL) return NULL;
    if (S->maxmem != 0) {
        if (nsize > osize)      S->maxmem -= nsize - osize;
        else if (osize > nsize) S->maxmem += osize - nsize;
    }
    return ret;
}

/* number & symbol primitives */

static int amN_approx(am_Num a, am_Num b)
{ return a > b ? a - b < AM_NUM_EPS : b - a < AM_NUM_EPS; }

static int amN_nearzero(am_Num a) { return amN_approx(a, 0.f); }
static int amN_negative(am_Num a) { return !amN_nearzero(a) && a < 0.f; }
static am_Symbol amS_null(void) { am_Symbol null = { 0, 0 }; return null; }

static void amM_poolfree(am_MemPool *pool, void *obj)
{ *(void**)obj = pool->freed, pool->freed = obj; }

static void amM_freearena(am_Solver *S, am_Arena *a) {
    if (!a->buf) return;
    amM_realloc(S, a->buf, 0, a->size, am_AllocDump);
    a->buf = NULL, a->size = 0;
}

static void amM_allocarena(am_Solver *S, am_Arena *a, size_t size) {
    assert(a->buf == NULL);
    a->buf = amM_realloc(S, NULL, size, 0, am_AllocDump);
    if (a->buf) a->size = size;
}

static void amM_initpool(am_MemPool *pool, size_t size) {
    pool->size  = size;
    pool->freed = pool->pages = NULL;
    assert(size > sizeof(void*) && size < AM_POOL_SIZE/4);
}

static void amM_freepool(am_Solver *S) {
    const size_t offset = AM_POOL_SIZE - sizeof(void*);
    while (S->conspool.pages != NULL) {
        void *next = *(void**)((char*)S->conspool.pages + offset);
        amM_realloc(S, S->conspool.pages, 0, AM_POOL_SIZE, am_AllocPool);
        S->conspool.pages = next;
    }
}

static void *amM_poolalloc(am_Solver *S) {
    am_MemPool *pool = &S->conspool;
    void *obj = pool->freed;
    if (obj == NULL) {
        const size_t offset = AM_POOL_SIZE - sizeof(void*);
        void *end, *ptr = amM_realloc(S, NULL, AM_POOL_SIZE, 0, am_AllocPool);
        if (ptr == NULL) return NULL;
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

/* The default allocator am_newsolver installs when allocf is NULL.  It is the
 * only S here that means system rather than symbol: it is a public-API
 * callback, not an internal helper. */
static void *amS_defallocf(void **pud, void *ptr, size_t nsize, size_t osize, am_AllocType ty) {
    void *newptr;
    (void)pud, (void)osize, (void)ty;
    if (nsize == 0) return free(ptr), (void*)NULL;
    if ((newptr = realloc(ptr, nsize)) == NULL) abort();
    return newptr;
}

static void *amM_alloccons(am_Solver *S) {
    if (S->allocf == amS_defallocf) /* pooled; its page paid the budget */
        return amM_poolalloc(S);
    return amM_realloc(S, NULL, sizeof(am_Constraint), 0, am_AllocConstraint);
}

static void amM_freecons(am_Solver *S, am_Constraint *cons) {
    if (S->allocf == amS_defallocf) /* back to the pool, nothing to refund */
        amM_poolfree(&S->conspool, cons);
    else
        amM_realloc(S, cons, 0, sizeof(*cons), am_AllocConstraint);
}

static am_Symbol amS_new(am_Solver *S, int type) {
    am_Symbol sym;
    unsigned id = ++S->current_id;
    if (id > 0x3FFFFFFF) abort(); /* id runs out */
    assert(type >= AM_EXTERNAL && type <= AM_DUMMY);
    sym.id   = id;
    sym.type = type;
    return sym;
}

/* hash table */

#define amH_slot(t,i)  ((char*)(t)->vals+(i)*(t)->value_size)
#define amH_val(T,it)  ((T*)amH_slot((it).t,(it).offset-1))

static am_Iterator amH_iter(const am_Table *t)
{ am_Iterator it; it.t = t, it.key = amS_null(), it.offset = 0; return it; }

static void amH_init(am_Table *t, size_t value_size)
{ memset(t, 0, sizeof(*t)), t->value_size = (am_Size)value_size; }

static void amH_reset(am_Table *t)
{ t->last_free = t->count = 0; memset(t->keys, 0, t->size * sizeof(am_Key)); }

static am_Size amH_calcsize(am_Size request)
{ am_Size s = AM_MIN_HASHSIZE; while (s < request) s <<= 1; return s; }

static int amH_next(am_Iterator *it) {
    am_Size cur = it->offset, size = it->t->size;
    am_Symbol key;
    while (cur < size && (key = it->t->keys[cur].sym).id == 0) cur++;
    if (cur == size) return it->key = amS_null(), it->offset = 0;
    return it->key = key, it->offset = cur + 1;
}

static void amH_free(am_Solver *S, am_Table *t) {
    size_t hsize = t->size * (sizeof(am_Key) + t->value_size);
    if (hsize == 0) return;
    if (!t->arena) amM_realloc(S, t->keys, 0, hsize, am_AllocHash);
    amH_init(t, t->value_size);
}

static const void *amH_get(const am_Table *t, unsigned key) {
    am_Size cur;
    if (t->size == 0) return NULL;
    cur = key & (t->size - 1);
    for (; t->keys[cur].sym.id != key; cur += t->keys[cur].next)
        if (t->keys[cur].next == 0) return NULL;
    return amH_slot(t, cur);
}

static void amH_attach(am_Table *t, am_Key *keys, size_t size, size_t vsize) {
    t->size = (am_Size)size;
    t->value_size = vsize;
    t->arena = 0;
    t->keys = keys;
    t->vals = keys + size;
    amH_reset(t);
}

static int amH_alloc(am_Table *t, am_Solver *S, size_t req, size_t vsz) {
    size_t size;
    am_Key *keys;
    amC(req <= (~(am_Size)0 >> 1));
    size = amH_calcsize((am_Size)req);
    keys = (am_Key*)amM_realloc(S, NULL,
            size * (sizeof(am_Key) + vsz), 0, am_AllocHash);
    amC(keys != NULL);
    return amH_attach(t, keys, size, vsz), AM_OK;
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
            assert(ks[othern].next != 0);
            while (othern + ks[othern].next != mp)
                othern += ks[othern].next;
            ks[othern].next = (f - othern);
            ks[f] = ks[mp], memcpy(amH_slot(t,f), amH_slot(t,mp), t->value_size);
            if (ks[mp].next) ks[f].next += (mp - f), ks[mp].next = 0;
        }
    }
    return ks[mp].sym = key, amH_slot(t, mp);
}

static int amH_resize(am_Solver *S, am_Table *t, size_t request) {
    am_Table nt;
    am_Size i;
    amE(amH_alloc(&nt, S, request, t->value_size));
    for (i = 0; i < t->size; ++i) {
        void *value;
        if (!t->keys[i].sym.id) continue;
        value = amH_rawset(&nt, t->keys[i].sym);
        assert(value != NULL), memcpy(value, amH_slot(t, i), t->value_size);
    }
    nt.count = t->count;
    if (!t->arena) amH_free(S, t);
    return (*t = nt), AM_OK;
}

static int amH_grow(am_Solver *S, am_Table *t, size_t grow)
{ return (grow += t->count)*2 < t->size ? AM_OK : amH_resize(S, t, grow); }

static void *amH_set(am_Solver *S, am_Table *t, am_Symbol key) {
    void *value;
    assert(key.id != 0 && amH_get(t, key.id) == NULL);
    if (!t->size && amH_resize(S, t, 0) != AM_OK) return NULL;
    if ((value = amH_rawset(t, key)) == NULL) {
        if (amH_resize(S, t, t->count*2) != AM_OK) return NULL;
        value = amH_rawset(t, key); /* retry and must success */
    }
    return t->count += 1, assert(value != NULL), value;
}

static void amH_del(am_Table *t, const void *value) {
    size_t offset = ((char*)value - (char*)t->vals)/t->value_size;
    t->count -= 1, t->keys[offset].sym = amS_null();
}

/* expression (row) */

static void amR_free(am_Solver *S, am_Row *row)
{ amH_free(S, &row->terms); }

static void amR_reset(am_Row *row)
{ row->constant = 0.f; amH_reset(&row->terms); }

static void amR_init(am_Row *row) {
    row->infeasible_next = amS_null(), row->constant = 0.f;
    amH_init(&row->terms, sizeof(am_Num));
}

static void amR_mul(am_Row *row, am_Num multiplier) {
    am_Iterator it = amH_iter(&row->terms);
    row->constant *= multiplier;
    while (amH_next(&it))
        *amH_val(am_Num,it) *= multiplier;
}

static void amR_addvar(am_Solver *S, am_Row *row, am_Symbol sym, am_Num value) {
    am_Num *term;
    if (sym.id == 0 || amN_nearzero(value)) return;
    term = (am_Num*)amH_get(&row->terms, sym.id);
    if (term == NULL) {
        term = (am_Num*)amH_set(S, &row->terms, sym);
        assert(term != NULL);
        *term = value;
    } else if (amN_nearzero(*term += value))
        amH_del(&row->terms, term);
}

static void amR_addrow(am_Solver *S, am_Row *row, const am_Row *other, am_Num multiplier) {
    am_Iterator it = amH_iter(&other->terms);
    row->constant += other->constant*multiplier;
    while (amH_next(&it))
        amR_addvar(S, row, it.key, *amH_val(am_Num,it)*multiplier);
}

/* variables & constraints */

AM_API int am_hasconstraint(am_Constraint *cons)
{ return cons != NULL && cons->marker.id != 0; }

AM_API void am_autoupdate(am_Solver *S, int auto_update)
{ S->auto_update = auto_update; }

static am_Var *amV_get(am_Solver *S, am_Id var)
{ return S ? (am_Var*)amH_get(&S->vars, var) : NULL; }

AM_API am_Id am_newvariable(am_Solver *S, am_Num *pvalue) {
    am_Symbol sym;
    am_Var *ve;
    if (S == NULL || pvalue == NULL) return 0;
    sym = amS_new(S, AM_EXTERNAL);
    ve = (am_Var*)amH_set(S, &S->vars, sym);
    if (ve == NULL) return 0;
    memset(ve, 0, sizeof(*ve));
    ve->pvalue   = pvalue;
    ve->refcount = 1;
    return sym.id;
}

AM_API void am_delvariable(am_Solver *S, am_Id var) {
    am_Var *ve = amV_get(S, var);
    if (ve && --ve->refcount == 0) {
        assert(!amV_isdummy(ve->next));
        am_deledit(S, var);
        amH_del(&S->vars, ve);
    }
}

AM_API int am_refcount(am_Solver *S, am_Id var) {
    am_Var *ve = amV_get(S, var);
    return ve ? ve->refcount : 0;
}

AM_API am_Num *am_varvalue(am_Solver *S, am_Id var, am_Num *newvalue) {
    am_Var *ve = amV_get(S, var);
    am_Num *pvalue = ve ? ve->pvalue : NULL;
    if (ve && newvalue) ve->pvalue = newvalue;
    return pvalue;
}

AM_API void am_updatevars(am_Solver *S) {
    while (S->dirty_vars.id != 0) {
        am_Symbol var = S->dirty_vars;
        am_Var *ve = (am_Var*)amH_get(&S->vars, var.id);
        assert(ve != NULL);
        S->dirty_vars = ve->next;
        ve->next = amS_null();
        if (ve->refcount == 1) {
            am_deledit(S, var.id);
            amH_del(&S->vars, ve);
        } else {
            am_Row *row = (am_Row*)amH_get(&S->rows, var.id);
            *ve->pvalue = row ? row->constant : 0.f;
            ve->refcount -= 1;
        }
    }
}

static int amC_init(am_Solver *S, am_Symbol sym, am_Num strength, am_Constraint *cons) {
    am_Constraint **ce = (am_Constraint**)amH_set(S, &S->constraints, sym);
    amC(ce != NULL);
    *ce = cons;
    memset(cons, 0, sizeof(*cons));
    cons->S        = S;
    cons->sym      = sym;
    cons->strength = amN_nearzero(strength) ? AM_REQUIRED : strength;
    amR_init(&(*ce)->expression);
    return AM_OK;
}

AM_API am_Constraint *am_newconstraint(am_Solver *S, am_Num strength) {
    am_Symbol sym = {0, AM_EXTERNAL}; /* AM_EXTERNAL for pool cons */
    am_Constraint *cons;
    int ret;
    if (S == NULL || amN_negative(strength)) return NULL;
    cons = (am_Constraint*)amM_alloccons(S);
    if (cons == NULL) return NULL;
    sym.id = ++S->current_cons;
    ret = amC_init(S, sym, strength, cons);
    if (ret != AM_OK) amM_freecons(S, cons), cons = NULL;
    return cons;
}

AM_API void am_delconstraint(am_Constraint *cons) {
    am_Solver *S = cons ? cons->S : NULL;
    am_Iterator it;
    am_Constraint **ce;
    if (S == NULL) return;
    am_remove(cons);
    ce = (am_Constraint**)amH_get(&S->constraints, cons->sym.id);
    assert(ce != NULL && *ce == cons);
    amH_del(&S->constraints, ce);
    it = amH_iter(&cons->expression.terms);
    while (amH_next(&it))
        am_delvariable(cons->S, it.key.id);
    amR_free(S, &cons->expression);
    if (amV_isexternal(cons->sym)) amM_freecons(S, cons);
}

AM_API am_Constraint *am_cloneconstraint(am_Constraint *other, am_Num strength) {
    am_Constraint *cons;
    if (other == NULL) return NULL;
    cons = am_newconstraint(other->S,
            amN_nearzero(strength) ? other->strength : strength);
    am_mergeconstraint(cons, other, 1.0f);
    cons->relation = other->relation;
    return cons;
}

AM_API int am_mergeconstraint(am_Constraint *cons, const am_Constraint *other, am_Num multiplier) {
    am_Iterator it;
    amC(cons && other && cons->marker.id == 0 && cons->S == other->S);
    if (cons->relation == AM_GREATEQUAL) multiplier = -multiplier;
    cons->expression.constant += other->expression.constant*multiplier;
    it = amH_iter(&other->expression.terms);
    while (amH_next(&it)) {
        am_Var *ve = (am_Var*)amH_get(&cons->S->vars, it.key.id);
        assert(ve != NULL);
        ve->refcount += 1;
        amR_addvar(cons->S, &cons->expression, it.key,
                *amH_val(am_Num,it)*multiplier);
    }
    return AM_OK;
}

AM_API void am_resetconstraint(am_Constraint *cons) {
    am_Iterator it;
    if (cons == NULL) return;
    am_remove(cons);
    cons->relation = 0;
    it = amH_iter(&cons->expression.terms);
    while (amH_next(&it))
        am_delvariable(cons->S, it.key.id);
    amR_reset(&cons->expression);
}

AM_API int am_addterm(am_Constraint *cons, am_Id var, am_Num multiplier) {
    am_Symbol sym = {0, AM_EXTERNAL};
    am_Var *ve;
    amC(cons && var && cons->marker.id == 0);
    if (cons->relation == AM_GREATEQUAL) multiplier = -multiplier;
    amC(ve = (am_Var*)amH_get(&cons->S->vars, var));
    amR_addvar(cons->S, &cons->expression, (sym.id=var, sym), multiplier);
    return ve->refcount += 1, AM_OK;
}

AM_API int am_addconstant(am_Constraint *cons, am_Num constant) {
    amC(cons && cons->marker.id == 0);
    cons->expression.constant +=
        cons->relation == AM_GREATEQUAL ? -constant : constant;
    return AM_OK;
}

AM_API int am_setrelation(am_Constraint *cons, int relation) {
    assert(relation >= AM_LESSEQUAL && relation <= AM_GREATEQUAL);
    amC(cons && cons->marker.id == 0 && cons->relation == 0);
    if (relation != AM_GREATEQUAL) amR_mul(&cons->expression, -1.0f);
    cons->relation = relation;
    return AM_OK;
}

AM_API int am_hasedit(am_Solver *S, am_Id var) {
    if (S == NULL) return 0;
    return amH_get(&S->suggests, var) != NULL;
}

static am_Suggest *amC_newedit(am_Solver *S, am_Id var, am_Num strength) {
    am_Symbol sym = {0, AM_EXTERNAL};
    am_Suggest **s;
    am_Row *row;
    am_Var *ve;
    int ret;
    if (S == NULL || var == 0 || amN_negative(strength)) return NULL;
    if ((ve = (am_Var*)amH_get(&S->vars, var)) == NULL) return NULL;
    s = (am_Suggest**)amH_set(S, &S->suggests, (sym.id=var, sym));
    if (s == NULL) return NULL;
    *s = amM_alloc(am_Suggest, S, am_AllocSuggest);
    if (*s == NULL) return amH_del(&S->suggests, s), (am_Suggest*)NULL;
    memset(*s, 0, sizeof(**s));
    sym.id = ++S->current_cons, sym.type = AM_DUMMY; /* local cons */
    (*s)->edit_value = !amV_isdummy(ve->next) ? *ve->pvalue :
        (row = (am_Row*)amH_get(&S->rows, var)) ? row->constant : 0.f;
    amC_init(S, sym, strength, &(*s)->constraint);
    am_setrelation(&(*s)->constraint, AM_EQUAL);
    am_addterm(&(*s)->constraint, var, 1.0f); /* var must have positive signture */
    am_addconstant(&(*s)->constraint, -(*s)->edit_value);
    ret = am_add(&(*s)->constraint);
    assert(ret == AM_OK), (void)ret;
    return *s;
}

AM_API int am_addedit(am_Solver *S, am_Id var, am_Num strength) {
    am_Suggest **s;
    amC(S && var);
    if (strength >= AM_STRONG) strength = AM_STRONG;
    s = (am_Suggest**)amH_get(&S->suggests, var);
    if (s) return assert(*s), am_setstrength(&(*s)->constraint, strength);
    amC(amC_newedit(S, var, strength));
    return AM_OK;
}

AM_API void am_deledit(am_Solver *S, am_Id var) {
    am_Suggest **ps, *s;
    if (S == NULL || var == 0) return;
    ps = (am_Suggest**)amH_get(&S->suggests, var);
    if (ps == NULL) return;
    s = *ps, amH_del(&S->suggests, ps);
    amH_free(S, &s->dirtyset);
    am_delconstraint(&(s->constraint));
    amM_free(S, s, am_AllocSuggest);
}

AM_API void am_clearedits(am_Solver *S) {
    am_Iterator it;
    if (S == NULL) return;
    it = amH_iter(&S->suggests);
    if (!S->auto_update) am_updatevars(S);
    while (amH_next(&it)) {
        am_Suggest *s = *amH_val(am_Suggest*,it);
        amH_free(S, &s->dirtyset);
        am_delconstraint(&s->constraint);
        amM_free(S, s, am_AllocSuggest);
    }
    amH_reset(&S->suggests);
}

/* Cassowary algorithm */

static int amA_takerow(am_Solver *S, am_Symbol sym, am_Row *dst) {
    am_Row *row = (am_Row*)amH_get(&S->rows, sym.id);
    if (row == NULL) return AM_FAILED;
    amH_del(&S->rows, row);
    dst->constant = row->constant;
    dst->terms    = row->terms;
    return AM_OK;
}

static void amA_solvefor(am_Solver *S, am_Row *row, am_Symbol enter, am_Symbol leave) {
    am_Num *term = (am_Num*)amH_get(&row->terms, enter.id);
    am_Num reciprocal = 1.0f / *term;
    assert(enter.id != leave.id && !amN_nearzero(*term));
    amH_del(&row->terms, term);
    if (!amN_approx(reciprocal, -1.0f)) amR_mul(row, -reciprocal);
    if (leave.id != 0) amR_addvar(S, row, leave, reciprocal);
}

static void amA_eliminate(am_Solver *S, am_Row *dst, am_Symbol out, const am_Row *row) {
    am_Num *term = (am_Num*)amH_get(&dst->terms, out.id);
    if (!term) return;
    amH_del(&dst->terms, term);
    amR_addrow(S, dst, row, *term);
}

static void amA_markdirty(am_Solver *S, am_Symbol var) {
    am_Var *ve = (am_Var*)amH_get(&S->vars, var.id);
    assert(ve != NULL);
    if (amV_isdummy(ve->next)) return;
    ve->next.id = S->dirty_vars.id;
    ve->next.type = AM_DUMMY;
    S->dirty_vars = var;
    ve->refcount += 1;
}

static int amA_infeasible(am_Solver *S, am_Symbol sym, am_Row *row) {
    if (amV_isdummy(row->infeasible_next)) return 1;
    if (amN_negative(row->constant)) {
        row->infeasible_next.id = S->infeasible_rows.id;
        row->infeasible_next.type = AM_DUMMY;
        return (S->infeasible_rows = sym), 1;
    }
    return 0;
}

static void amA_substitute(am_Solver *S, am_Symbol var, am_Row *expr) {
    am_Iterator it = amH_iter(&S->rows);
    while (amH_next(&it)) {
        am_Row *row = amH_val(am_Row,it);
        amA_eliminate(S, row, var, expr);
        if (amV_isexternal(it.key))
            amA_markdirty(S, it.key);
        else
            amA_infeasible(S, it.key, row);
    }
    amA_eliminate(S, &S->objective, var, expr);
}

static int amA_putrow(am_Solver *S, am_Symbol sym, const am_Row *src) {
    am_Row *row;
    assert(amH_get(&S->rows, sym.id) == NULL);
    row = (am_Row*)amH_set(S, &S->rows, sym);
    assert(row != NULL);
    row->infeasible_next = amS_null();
    row->constant = src->constant;
    row->terms    = src->terms;
    return AM_OK;
}

static void amA_optimize(am_Solver *S, am_Row *objective) {
    for (;;) {
        am_Symbol enter = amS_null(), leave = amS_null();
        am_Num r, min_ratio = AM_NUM_MAX, *term;
        am_Iterator it = amH_iter(&objective->terms);
        am_Row tmp;

        assert(S->infeasible_rows.id == 0);
        while (amH_next(&it))
            if (!amV_isdummy(it.key) && amN_negative(*amH_val(am_Num, it))) break;
        if (it.key.id == 0) return;
        enter = it.key;

        it = amH_iter(&S->rows);
        while (amH_next(&it)) {
            am_Row *row = amH_val(am_Row,it);
            if (amV_isexternal(it.key)) continue;
            term = (am_Num*)amH_get(&row->terms, enter.id);
            if (term == NULL || *term > 0.f) continue;
            r = -row->constant / *term;
            if (r < min_ratio || (amN_approx(r, min_ratio)
                        && it.key.id < leave.id))
                min_ratio = r, leave = it.key;
        }
        assert(leave.id != 0);
        amA_takerow(S, leave, &tmp);
        amA_solvefor(S, &tmp, enter, leave);
        amA_substitute(S, enter, &tmp);
        if (objective != &S->objective)
            amA_eliminate(S, objective, enter, &tmp);
        amA_putrow(S, enter, &tmp);
    }
}

static void amA_mergerow(am_Solver *S, am_Row *row, am_Symbol var, am_Num multiplier) {
    am_Row *oldrow = (am_Row*)amH_get(&S->rows, var.id);
    if (oldrow) amR_addrow(S, row, oldrow, multiplier);
    else        amR_addvar(S, row, var, multiplier);
}

static am_Row amA_makerow(am_Solver *S, am_Constraint *cons) {
    am_Iterator it = amH_iter(&cons->expression.terms);
    am_Row row;
    amR_init(&row);
    row.constant = cons->expression.constant;
    while (amH_next(&it)) {
        amA_markdirty(S, it.key);
        amA_mergerow(S, &row, it.key, *amH_val(am_Num,it));
    }
    if (cons->relation != AM_EQUAL) {
        cons->marker.id = cons->marker_id, cons->marker.type = AM_SLACK;
        amR_addvar(S, &row, cons->marker, -1.0f);
        if (cons->strength < AM_REQUIRED) {
            cons->other.id = cons->other_id, cons->other.type = AM_ERROR;
            amR_addvar(S, &row, cons->other, 1.0f);
            amR_addvar(S, &S->objective, cons->other, cons->strength);
        }
    } else if (cons->strength >= AM_REQUIRED) {
        cons->marker.id = cons->marker_id, cons->marker.type = AM_DUMMY;
        amR_addvar(S, &row, cons->marker, 1.0f);
    } else {
        cons->marker.id = cons->marker_id, cons->marker.type = AM_ERROR;
        cons->other.id = cons->other_id, cons->other.type = AM_ERROR;
        amR_addvar(S, &row, cons->marker, -1.0f);
        amR_addvar(S, &row, cons->other,   1.0f);
        amR_addvar(S, &S->objective, cons->marker, cons->strength);
        amR_addvar(S, &S->objective, cons->other,  cons->strength);
    }
    if (row.constant < 0.f) amR_mul(&row, -1.0f);
    return row;
}

static int amA_addartificial(am_Solver *S, am_Row *row, am_Constraint *cons) {
    am_Symbol a = amS_new(S, AM_SLACK);
    am_Iterator it;
    am_Row tmp;
    am_Num *term;
    int ret;
    --S->current_id; /* artificial variable will be removed */
    amR_init(&tmp);
    amR_addrow(S, &tmp, row, 1.0f);
    amA_putrow(S, a, row);
    amA_optimize(S, &tmp);
    ret = amN_nearzero(tmp.constant) ? AM_OK : AM_UNBOUND;
    amR_free(S, &tmp);
    if (amA_takerow(S, a, &tmp) == AM_OK) {
        am_Symbol enter = amS_null();
        if (tmp.terms.count == 0) return amR_free(S, &tmp), ret;
        it = amH_iter(&tmp.terms);
        while (amH_next(&it))
            if (amV_ispivotable(it.key)) { enter = it.key; break; }
        if (enter.id == 0) return amR_free(S, &tmp), AM_UNBOUND;
        amA_solvefor(S, &tmp, enter, a);
        amA_substitute(S, enter, &tmp);
        amA_putrow(S, enter, &tmp);
    }
    it = amH_iter(&S->rows);
    while (amH_next(&it)) {
        am_Row *row = amH_val(am_Row,it);
        am_Num *term = (am_Num*)amH_get(&row->terms, a.id);
        if (term) amH_del(&row->terms, term);
    }
    term = (am_Num*)amH_get(&S->objective.terms, a.id);
    if (term) amH_del(&S->objective.terms, term);
    if (ret != AM_OK) am_remove(cons);
    return ret;
}

static int amA_tryaddrow(am_Solver *S, am_Row *row, am_Constraint *cons) {
    am_Symbol subject = amS_null();
    am_Iterator it = amH_iter(&row->terms);
    while (amH_next(&it))
        if (amV_isexternal(it.key)) { subject = it.key; break; }
    if (subject.id == 0 && amV_ispivotable(cons->marker)) {
        am_Num *term = (am_Num*)amH_get(&row->terms, cons->marker.id);
        if (*term < 0.f) subject = cons->marker;
    }
    if (subject.id == 0 && amV_ispivotable(cons->other)) {
        am_Num *term = (am_Num*)amH_get(&row->terms, cons->other.id);
        if (*term < 0.f) subject = cons->other;
    }
    if (subject.id == 0) {
        it = amH_iter(&row->terms);
        while (amH_next(&it))
            if (!amV_isdummy(it.key)) break;
        if (it.offset == 0) {
            if (!amN_nearzero(row->constant))
                return amR_free(S, row), AM_UNSATISFIED;
            subject = cons->marker;
        }
    }
    if (subject.id == 0) return amA_addartificial(S, row, cons);
    amA_solvefor(S, row, subject, amS_null());
    amA_substitute(S, subject, row);
    amA_putrow(S, subject, row);
    return AM_OK;
}

static void amA_removeerrors(am_Solver *S, am_Constraint *cons) {
    if (amV_iserror(cons->marker))
        amA_mergerow(S, &S->objective, cons->marker, -cons->strength);
    if (amV_iserror(cons->other))
        amA_mergerow(S, &S->objective, cons->other, -cons->strength);
    if (S->objective.terms.count == 0)
        S->objective.constant = 0.f;
    cons->marker = cons->other = amS_null();
}

AM_API int am_add(am_Constraint *cons) {
    am_Solver *S = cons ? cons->S : NULL;
    am_Symbol sym;
    am_Row row;
    int ret;
    amC(S && cons->marker.id == 0);
    if (cons->marker_id == 0)
        cons->marker_id = ((sym = amS_new(S, 0)), sym.id);
    if (cons->other_id == 0 && cons->strength < AM_REQUIRED)
        cons->other_id = ((sym = amS_new(S, 0)), sym.id);
    row = amA_makerow(S, cons);
    if ((ret = amA_tryaddrow(S, &row, cons)) != AM_OK)
        amA_removeerrors(S, cons);
    else {
        amA_optimize(S, &S->objective);
        if (S->auto_update) am_updatevars(S);
        S->age += 1;
    }
    assert(S->infeasible_rows.id == 0);
    return ret;
}

static am_Symbol amA_leavingrow(am_Solver *S, am_Symbol marker) {
    am_Symbol first = amS_null(), second = amS_null(), third = amS_null();
    am_Num r1 = AM_NUM_MAX, r2 = AM_NUM_MAX;
    am_Iterator it = amH_iter(&S->rows);
    while (amH_next(&it)) {
        am_Row *row = amH_val(am_Row,it);
        am_Num *term = (am_Num*)amH_get(&row->terms, marker.id);
        if (term == NULL) continue;
        if (amV_isexternal(it.key))
            third = it.key;
        else if (*term < 0.f) {
            am_Num r = -row->constant / *term;
            if (r < r1) r1 = r, first = it.key;
        } else {
            am_Num r = row->constant / *term;
            if (r < r2) r2 = r, second = it.key;
        }
    }
    return first.id ? first : second.id ? second : third;
}

AM_API void am_remove(am_Constraint *cons) {
    am_Solver *S;
    am_Symbol marker;
    am_Row tmp;
    if (cons == NULL || cons->marker.id == 0) return;
    S = cons->S, marker = cons->marker;
    amA_removeerrors(S, cons);
    if (amA_takerow(S, marker, &tmp) != AM_OK) {
        am_Symbol leave = amA_leavingrow(S, marker);
        assert(leave.id != 0);
        amA_takerow(S, leave, &tmp);
        amA_solvefor(S, &tmp, marker, leave);
        amA_substitute(S, marker, &tmp);
    }
    amR_free(S, &tmp);
    amA_optimize(S, &S->objective);
    S->age += 1;
    if (S->auto_update) am_updatevars(S);
}

AM_API int am_setstrength(am_Constraint *cons, am_Num strength) {
    am_Solver *S;
    am_Num old, diff;
    amC(cons);
    strength = amN_nearzero(strength) ? AM_REQUIRED : strength;
    if (cons->strength == strength) return AM_OK;
    if (!am_hasconstraint(cons)) return (cons->strength = strength), AM_OK;
    if (cons->strength >= AM_REQUIRED || strength >= AM_REQUIRED) {
        int r, ra = AM_OK;
        old = cons->strength, am_remove(cons), cons->strength = strength;
        if ((r = am_add(cons)) != AM_OK)
            cons->strength = old, ra = am_add(cons);
        return assert(ra == AM_OK), (void)ra, r;
    }
    S = cons->S, diff = strength - cons->strength;
    amA_mergerow(S, &S->objective, cons->marker, diff);
    amA_mergerow(S, &S->objective, cons->other,  diff);
    amA_optimize(S, &S->objective);
    if (S->auto_update) am_updatevars(S);
    return (S->age += 1, cons->strength = strength), AM_OK;
}

static void amA_cachedsuggest(am_Solver *S, am_Suggest *s, am_Num delta) {
    am_Iterator it = amH_iter(&s->dirtyset);
    am_Symbol marker = s->constraint.marker;
    int pure = 1;
    while (amH_next(&it)) {
        am_Row *row = (am_Row*)amH_get(&S->rows, it.key.id);
        am_Num *term;
        assert(row != NULL);
        term = (am_Num*)amH_get(&row->terms, marker.id);
        assert(term != NULL);
        row->constant += *term * delta;
        if (amV_isexternal(it.key))
            amA_markdirty(S, it.key);
        else if (amA_infeasible(S, it.key, row))
            pure = 0;
    }
    if (!pure) s->age = 0;
}

static void amA_editdelta(am_Solver *S, am_Suggest *s, am_Num delta) {
    am_Iterator it = amH_iter(&S->rows);
    am_Constraint *cons = &s->constraint;
    am_Row *row;
    int pure = 1;
    if ((row = (am_Row*)amH_get(&S->rows, cons->marker.id)) != NULL)
    { row->constant -= delta, amA_infeasible(S, cons->marker, row); return; }
    if ((row = (am_Row*)amH_get(&S->rows, cons->other.id)) != NULL)
    { row->constant += delta, amA_infeasible(S, cons->other, row); return; }
    if (s->age == S->age) { amA_cachedsuggest(S, s, delta); return; }
    amH_reset(&s->dirtyset);
    while (amH_next(&it)) {
        am_Row *row = amH_val(am_Row,it);
        am_Num *term = (am_Num*)amH_get(&row->terms, cons->marker.id);
        if (term == NULL) continue;
        row->constant += *term*delta;
        amH_set(S, &s->dirtyset, it.key);
        if (amV_isexternal(it.key))
            amA_markdirty(S, it.key);
        else if (amA_infeasible(S, it.key, row))
            pure = 0;
    }
    if (pure) s->age = S->age;
}

static void amA_dualoptimize(am_Solver *S) {
    while (S->infeasible_rows.id != 0) {
        am_Symbol enter = amS_null(), leave;
        am_Num r, min_ratio = AM_NUM_MAX;
        am_Iterator it;
        am_Row tmp, *row =
            (am_Row*)amH_get(&S->rows, S->infeasible_rows.id);
        assert(row != NULL);
        leave = S->infeasible_rows;
        S->infeasible_rows = row->infeasible_next;
        row->infeasible_next = amS_null();
        if (!amN_negative(row->constant)) continue;
        it = amH_iter(&row->terms);
        while (amH_next(&it)) {
            am_Num term = *amH_val(am_Num,it);
            am_Num *objterm;
            if (amV_isdummy(it.key) || term <= 0.f) continue;
            objterm = (am_Num*)amH_get(&S->objective.terms, it.key.id);
            r = objterm ? *objterm / term : 0.f;
            if (min_ratio > r) min_ratio = r, enter = it.key;
        }
        assert(enter.id != 0);
        amA_takerow(S, leave, &tmp);
        amA_solvefor(S, &tmp, enter, leave);
        amA_substitute(S, enter, &tmp);
        amA_putrow(S, enter, &tmp);
    }
}

AM_API void am_suggest(am_Solver *S, am_Id var, am_Num value) {
    am_Suggest **ps, *s;
    am_Num delta;
    if (S == NULL || var == 0) return;
    ps = (am_Suggest**)amH_get(&S->suggests, var);
    s = ps ? *ps : amC_newedit(S, var, AM_MEDIUM);
    assert(s != NULL);
    delta = value - s->edit_value, s->edit_value = value;
    amA_editdelta(S, s, delta);
    amA_dualoptimize(S);
    if (S->auto_update) am_updatevars(S);
}

AM_API am_Solver *am_newsolver(am_Allocf *allocf, void *ud) {
    am_Solver *S;
    if (allocf == NULL) allocf = amS_defallocf;
    S = (am_Solver*)allocf(&ud, NULL, sizeof(am_Solver), 0, am_AllocSolver);
    if (S == NULL) return NULL;
    memset(S, 0, sizeof(*S));
    S->ud     = ud;
    S->allocf = allocf;
    amR_init(&S->objective);
    amH_init(&S->vars, sizeof(am_Var));
    amH_init(&S->constraints, sizeof(am_Constraint*));
    amH_init(&S->suggests, sizeof(am_Suggest*));
    amH_init(&S->rows, sizeof(am_Row));
    amM_initpool(&S->conspool, sizeof(am_Constraint));
    return S;
}

AM_API void am_delsolver(am_Solver *S) {
    am_Iterator it = amH_iter(&S->constraints);
    while (amH_next(&it)) {
        am_Constraint *cons = *amH_val(am_Constraint*,it);
        amR_free(S, &cons->expression);
        if (amV_isexternal(cons->sym)) amM_freecons(S, cons);
    }
    it = amH_iter(&S->suggests);
    while (amH_next(&it)) {
        am_Suggest *s = *amH_val(am_Suggest*,it);
        amH_free(S, &s->dirtyset);
        amH_free(S, &s->constraint.expression.terms);
        amM_free(S, s, am_AllocSuggest);
    }
    it = amH_iter(&S->rows);
    while (amH_next(&it))
        amR_free(S, amH_val(am_Row,it));
    amR_free(S, &S->objective);
    amH_free(S, &S->vars);
    amH_free(S, &S->constraints);
    amH_free(S, &S->suggests);
    amH_free(S, &S->rows);
    amM_freepool(S);
    amM_freearena(S, &S->arena);
    amM_free(S, S, am_AllocSolver);
}

AM_API void am_resetsolver(am_Solver *S) {
    am_Iterator it;
    if (S == NULL) return;
    am_clearedits(S);
    it = amH_iter(&S->constraints);
    while (amH_next(&it)) {
        am_Constraint *cons = *amH_val(am_Constraint*,it);
        cons->marker = cons->other = amS_null();
    }
    it = amH_iter(&S->rows);
    while (amH_next(&it))
        amR_free(S, amH_val(am_Row,it));
    amH_reset(&S->rows);
    amR_reset(&S->objective);
    amM_freearena(S, &S->arena);
    assert(S->infeasible_rows.id == 0);
    S->age = 0;
}

/* dump & load */

#ifndef AM_NAME_LEN
# define AM_NAME_LEN 256
#endif /* AM_NAME_LEN */
#ifndef AM_BUF_LEN
# define AM_BUF_LEN  4096
#endif /* AM_BUF_LEN */

typedef struct am_DumpCtx {
    am_Solver *S;
    unsigned  *syms; 
    unsigned  *cons;
    am_Table   symmap;
    am_Dumper *dumper;
    char      *p;
    int        ret;
    char       buf[AM_BUF_LEN];
} am_DumpCtx;

static int amD_intcmp(const void *lhs, const void *rhs)
{ return *(const int*)lhs - *(const int*)rhs; }

static int amD_char(am_DumpCtx *ctx, int ch) {
    if ((ctx->p - ctx->buf) >= AM_BUF_LEN) {
        amE(ctx->ret = ctx->dumper->writer(ctx->dumper, ctx->buf, AM_BUF_LEN));
        ctx->p = ctx->buf;
    }
    return (*ctx->p++ = ch & 0xFF), AM_OK;
}

static int amD_raw(am_DumpCtx *ctx, am_Size n, int width) {
    switch (width) {
    default: return AM_FAILED;
    case 32: amE(amD_char(ctx, n >> 24));
             amE(amD_char(ctx, n >> 16)); /* FALLTHROUGH */
    case 16: amE(amD_char(ctx, n >> 8));
             amE(amD_char(ctx, n & 0xFF));
    }
    return AM_OK;
}

static int amD_uint32(am_DumpCtx *ctx, am_Size n) {
    if (n <= 0x7F) amE(amD_char(ctx, n));
    else if (n <= 0xFF) {
        amE(amD_char(ctx, 0xCC));
        amE(amD_char(ctx, n));
    } else if (n <= 0xFFFF) {
        amE(amD_char(ctx, 0xCD));
        amE(amD_raw(ctx, n, 16));
    } else {
        amE(amD_char(ctx, 0xCE));
        amE(amD_raw(ctx, n, 32));
    }
    return AM_OK;
}

static int amD_float(am_DumpCtx *ctx, am_Num n) {
    union { double f64; float f32; unsigned u32; } u;
    if (sizeof(am_Num) == sizeof(float))
        u.f32 = n;
    else
        u.f64 = n, u.f32 = (float)u.f64;
    amE(amD_char(ctx, 0xCA));
    amE(amD_raw(ctx, u.u32, 32));
    return AM_OK;
}

static int amD_string(am_DumpCtx *ctx, const char *name) {
    am_Dumper *d = ctx->dumper;
    size_t namelen = (name ? strlen(name) : 0), buflen;
    amC(namelen < AM_NAME_LEN && namelen <= 0xFF);
    if (namelen < 32)
        amE(amD_char(ctx, 0xA0 + (int)namelen));
    else {
        amE(amD_char(ctx, 0xD9));
        amE(amD_char(ctx, (int)namelen));
    }
    if ((buflen = (ctx->p - ctx->buf)) + namelen > AM_BUF_LEN) {
        ctx->p = ctx->buf;
        if (buflen) amE(ctx->ret = d->writer(d, ctx->buf, buflen));
        if (namelen > AM_BUF_LEN) return ctx->ret = d->writer(d, name, namelen);
    }
    memcpy(ctx->p, name, namelen);
    return ctx->p += namelen, AM_OK;
}

static int amD_count(am_DumpCtx *ctx, am_Size count) {
    if (count < 16)
        amE(amD_char(ctx, 0x90 + count));
    else if (count <= 0xFFFF) {
        amE(amD_char(ctx, 0xDC));
        amE(amD_raw(ctx, count, 16));
    } else {
        amE(amD_char(ctx, 0xDD));
        amE(amD_raw(ctx, count, 32));
    }
    return AM_OK;
}

static unsigned amD_mapid(am_DumpCtx *ctx, unsigned key) {
    unsigned *id = (unsigned*)amH_get(&ctx->symmap, key);
    return assert(id != NULL), *id;
}

static int amD_row(am_DumpCtx *ctx, const am_Row *row) {
    am_Iterator it = amH_iter(&row->terms);
    amE(amD_count(ctx, row->terms.count * 2 + 1));
    amE(amD_float(ctx, row->constant));
    while (amH_next(&it)) {
        amE(amD_uint32(ctx, amD_mapid(ctx, it.key.id) << 2 | it.key.type));
        amE(amD_float(ctx, *amH_val(am_Num,it)));
    }
    return AM_OK;
}

static int amD_vars(am_DumpCtx *ctx) {
    const am_Solver *S = ctx->S;
    am_Size i;
    amE(amD_count(ctx, S->vars.count));
    for (i = 0; i < S->vars.count; ++i) {
        am_Var *ve = (am_Var*)amH_get(&S->vars, ctx->syms[i]);
        const char *vn;
        assert(ve != NULL);
        vn = ctx->dumper->var_name(ctx->dumper, i, ctx->syms[i], ve->pvalue);
        amE(amD_string(ctx, vn));
    }
    return AM_OK;
}

static int amD_constraints(am_DumpCtx *ctx) {
    const am_Solver *S = ctx->S;
    am_Size i;
    amE(amD_count(ctx, S->constraints.count));
    for (i = 0; i < S->constraints.count; ++i) {
        am_Constraint **ce = (am_Constraint**)amH_get(
                &S->constraints, ctx->cons[i]);
        unsigned id;
        assert(ce != NULL);
        amE(amD_count(ctx, 6));
        /* [name, strength, marker, other, relation, row] */
        amE(amD_string(ctx, ctx->dumper->cons_name ?
                    ctx->dumper->cons_name(ctx->dumper, i, (*ce)) : NULL));
        amE(amD_float(ctx, (*ce)->strength));
        if (id = 0, (*ce)->marker.type)
            id = amD_mapid(ctx, (*ce)->marker_id) << 2 | (*ce)->marker.type;
        amE(amD_uint32(ctx, id));
        if (id = 0, (*ce)->other.type)
            id = amD_mapid(ctx, (*ce)->other_id) << 2 | (*ce)->other.type;
        amE(amD_uint32(ctx, id));
        amE(amD_uint32(ctx, (*ce)->relation));
        amE(amD_row(ctx, &(*ce)->expression));
    }
    return AM_OK;
}

static int amD_rows(am_DumpCtx *ctx) {
    const am_Solver *S = ctx->S;
    size_t arena = 0;
    am_Iterator it = amH_iter(&S->rows);
    while (amH_next(&it))
        arena += amH_calcsize(amH_val(am_Row,it)->terms.count);
    if (arena > ~(am_Size)0) arena = 0;
    amE(amD_count(ctx, S->rows.count*2 + 1));
    amE(amD_uint32(ctx, (am_Size)arena));
    while (amH_next(&it)) {
        amE(amD_uint32(ctx, amD_mapid(ctx, it.key.id) << 2 | it.key.type));
        amE(amD_row(ctx, amH_val(am_Row,it)));
    }
    return AM_OK;
}

static int amD_collect(am_DumpCtx *ctx) {
    am_Solver *S = ctx->S;
    am_Symbol sym = {0, 0};
    size_t i, vc = S->vars.count, cc = 0, count = 0;
    am_Iterator it = amH_iter(&S->vars);
    while (amH_next(&it)) ctx->syms[count++] = it.key.id;
    it = amH_iter(&S->constraints);
    while (amH_next(&it)) {
        am_Constraint *cons = *amH_val(am_Constraint*,it);
        if (cons->marker.type) ctx->syms[count++] = cons->marker_id;
        if (cons->other.type) ctx->syms[count++] = cons->other_id;
        ctx->cons[cc++] = it.key.id;
    }
    qsort(ctx->cons, cc, sizeof(unsigned), amD_intcmp);
    qsort(ctx->syms, vc, sizeof(unsigned), amD_intcmp);
    qsort(ctx->syms + vc, count - vc, sizeof(unsigned), amD_intcmp);
    amH_init(&ctx->symmap, sizeof(unsigned));
    amE(amH_grow(S, &ctx->symmap, count));
    for (i = 0; i < count; ++i) {
        unsigned *val = (unsigned*)amH_set(S, &ctx->symmap,
                (sym.id = ctx->syms[i], sym));
        assert(val != NULL), *val = (unsigned)i;
    }
    return assert(count == ctx->symmap.count), AM_OK;
}

static int amD_all(am_DumpCtx *ctx) {
    const am_Solver *S = ctx->S;
    am_Dumper *d = ctx->dumper;
    amE(amD_count(ctx, 5));
    /* [total, vars, constraints, rows, objective] */
    amE(amD_uint32(ctx, ctx->symmap.count));
    amE(amD_vars(ctx));
    amE(amD_constraints(ctx));
    amE(amD_rows(ctx));
    amE(amD_row(ctx, &S->objective));
    if (ctx->p > ctx->buf)
        amE(ctx->ret = d->writer(d, ctx->buf, (ctx->p - ctx->buf)));
    return ctx->ret;
}

AM_API int am_dump(am_Solver *S, am_Dumper *dumper) {
    size_t cons_alloc, sym_alloc;
    am_DumpCtx ctx;
    ctx.ret = AM_FAILED;
    amC(S && dumper && dumper->writer && dumper->var_name);
    am_clearedits(S);
    if (!S->auto_update) am_updatevars(S);
    cons_alloc = sizeof(unsigned) * S->constraints.count;
    sym_alloc = sizeof(unsigned) * (cons_alloc*2 + S->vars.count);
    memset(&ctx, 0, sizeof(ctx));
    ctx.syms = (unsigned*)amM_realloc(S, NULL, sym_alloc, 0, am_AllocDump);
    ctx.cons = (unsigned*)amM_realloc(S, NULL, cons_alloc, 0, am_AllocDump);
    ctx.S = S;
    if (ctx.syms && ctx.cons && (ctx.ret = amD_collect(&ctx)) == AM_OK) {
        ctx.dumper = dumper, ctx.p = ctx.buf;
        ctx.ret = amD_all(&ctx);
    }
    if (ctx.syms) amM_realloc(S, ctx.syms, 0, sym_alloc, am_AllocDump);
    if (ctx.cons) amM_realloc(S, ctx.cons, 0, cons_alloc, am_AllocDump);
    amH_free(S, &ctx.symmap);
    return ctx.ret;
}

#ifndef AM_MAX_LOADMEM
# define AM_MAX_LOADMEM (16u*1024u*1024u) /* default load budget */
#endif /* AM_MAX_LOADMEM */

typedef struct am_LoadCtx {
    am_Solver  *S;
    am_Loader  *loader;
    size_t      n;
    const char *p, *s;
    am_Size     offset;
    am_Size     total;      /* symbol count declared by the stream */
    size_t      arena_max;  /* budget when the load started, for the slot guard */
    char       *arena_p;    /* row-table arena cursor, NULL outside rows */
    size_t      arena_left; /* bytes left in that arena */
    char        buf[AM_NAME_LEN];
} am_LoadCtx;

#define amL_getchar(ctx) ((ctx)->n-- > 0 ? *(ctx)->p++ & 0xFF : amL_fill(ctx))

static int amL_fill(am_LoadCtx *ctx) {
    ctx->p = ctx->loader->reader(ctx->loader, &ctx->n);
    amC(ctx->p && ctx->n);
    return ctx->n -= 1, *ctx->p++ & 0xFF;
}

static int amL_rawslow16(am_LoadCtx *ctx, unsigned *pv) {
    unsigned r = 0;
    int c;
    amC((c = amL_getchar(ctx)) != AM_FAILED);
    r |= (unsigned)c << 8;
    amC((c = amL_getchar(ctx)) != AM_FAILED);
    return (*pv = (r | (unsigned)c)), AM_OK;
}

static int amL_rawslow32(am_LoadCtx *ctx, unsigned *pv) {
    unsigned r = 0;
    int c;
    amC((c = amL_getchar(ctx)) != AM_FAILED);
    r |= (unsigned)c << 24;
    amC((c = amL_getchar(ctx)) != AM_FAILED);
    r |= (unsigned)c << 16;
    amE(amL_rawslow16(ctx, pv));
    return (*pv |= r), AM_OK;
}

static int amL_raw32(am_LoadCtx *ctx, unsigned *pv) {
    if (ctx->n >= 4) {
        unsigned n = *ctx->p++ & 0xFF;
        n <<= 8; n |= *ctx->p++ & 0xFF;
        n <<= 8; n |= *ctx->p++ & 0xFF;
        n <<= 8; n |= *ctx->p++ & 0xFF;
        return *pv = n, ctx->n -= 4, AM_OK;
    }
    return amL_rawslow32(ctx, pv);
}

static int amL_raw16(am_LoadCtx *ctx, unsigned *pv) {
    if (ctx->n >= 2) {
        unsigned n = *ctx->p++ & 0xFF;
        n <<= 8; n |= *ctx->p++ & 0xFF;
        return *pv = n, ctx->n -= 2, AM_OK;
    }
    return amL_rawslow16(ctx, pv);
}

static int amL_uint32(am_LoadCtx *ctx, am_Size *pv) {
    int ty, c;
    switch (ty = amL_getchar(ctx)) {
    default: amC(ty >= 0 && ty <= 0x7F); *pv = ty; break;
    case 0xCC: amC((c = amL_getchar(ctx)) != AM_FAILED); *pv = c; break;
    case 0xCD: amE(amL_raw16(ctx, pv)); break;
    case 0xCE: amE(amL_raw32(ctx, pv)); break;
    }
    return AM_OK;
}

static int amL_float(am_LoadCtx *ctx, am_Num *pv) {
    union { float f32; unsigned u32[2]; } u;
    amC(amL_getchar(ctx) == 0xCA);
    amE(amL_raw32(ctx, &u.u32[0]));
    return *pv = (am_Num)u.f32, AM_OK;
}

static int amL_string(am_LoadCtx *ctx) {
    char *buf = ctx->buf;
    am_Size size;
    int ty, c;
    switch (ty = amL_getchar(ctx)) {
    default:   amC(ty >= 0xA0 && ty <= 0xBF); size = ty - 0xA0;   break;
    case 0xD9: amC((c = amL_getchar(ctx)) != AM_FAILED); size = c; break;
    }
    amC(size < AM_NAME_LEN);
    ctx->s = size ? ctx->buf : NULL;
    if (ctx->n >= size)
        return memcpy(ctx->buf, ctx->p, size), ctx->buf[size] = 0,
               ctx->p += size, ctx->n -= size, AM_OK;
    for (;;) {
        memcpy(buf, ctx->p, ctx->n), buf += ctx->n, size -= (am_Size)ctx->n;
        amC((c = amL_fill(ctx)) != AM_FAILED);
        *buf++ = c, size -= 1;
        if (size <= ctx->n) {
            memcpy(buf, ctx->p, size), buf[size] = 0;
            return (ctx->n -= size, ctx->p += size), AM_OK;
        }
    }
}

static int amL_count(am_LoadCtx *ctx, am_Size *pcount) {
    int ty;
    switch (ty = amL_getchar(ctx)) {
    default: amC(ty >= 0x90 && ty <= 0x9F); *pcount = ty - 0x90; break;
    case 0xDC: amE(amL_raw16(ctx, pcount)); break;
    case 0xDD: amE(amL_raw32(ctx, pcount)); break;
    }
    return AM_OK;
}

static char *amL_takemem(am_LoadCtx *ctx, am_Size size, size_t unit) {
    size_t bytes = size * unit;
    char *p;
    if (bytes / unit != size || bytes > ctx->arena_left) return NULL;
    ctx->arena_left -= bytes;
    return (p = ctx->arena_p, ctx->arena_p += bytes), p;
}

static int amL_row(am_LoadCtx *ctx, am_Row *row) {
    am_Size i, count, value;
    amE(amL_count(ctx, &count));
    amC(count >= 1 && (count & 1) == 1);
    if (ctx->arena_p) {
        am_Size size = amH_calcsize(count/2);
        size_t unit = sizeof(am_Key) + sizeof(am_Num);
        am_Key *keys = (am_Key*)amL_takemem(ctx, size, unit);
        amC(keys != NULL);
        amH_attach(&row->terms, keys, size, sizeof(am_Num));
        row->terms.arena = 1;
    }
    amE(amL_float(ctx, &row->constant));
    for (i = 1; i < count; i += 2) {
        am_Symbol sym = {0, 0};
        am_Num *term;
        amE(amL_uint32(ctx, &value));
        amC((value >> 2) < ctx->total); /* 30-bit id, never 0 */
        sym.id = ctx->offset + (value >> 2), sym.type = value & 3;
        amC(amH_get(&row->terms, sym.id) == NULL); /* amH_set's contract */
        amC(term = (am_Num*)amH_set(ctx->S, &row->terms, sym));
        amE(amL_float(ctx, term));
    }
    return AM_OK;
}

static int amL_vars(am_LoadCtx *ctx) {
    am_Solver *S = ctx->S;
    am_Size i, count;
    am_Symbol sym = {0, AM_EXTERNAL};
    amE(amL_count(ctx, &count));
    amC(count <= ctx->total); /* ids are offset+i, keep them in total */
    for (i = 0; i < count; ++i) {
        am_Var *ve;
        am_Num *pvalue;
        amE(amL_string(ctx));
        pvalue = ctx->loader->load_var(ctx->loader, ctx->s, i, ctx->offset+i);
        amC(pvalue != NULL);
        sym.id = ctx->offset+i;
        amC(ve = (am_Var*)amH_set(S, &S->vars, sym));
        memset(ve, 0, sizeof(*ve));
        ve->pvalue   = pvalue;
        ve->refcount = 1;
    }
    return AM_OK;
}

static int amL_marker(am_LoadCtx *ctx, am_Symbol *sym, unsigned *id) {
    am_Size value;
    amE(amL_uint32(ctx, &value));
    amC(value == 0 || (value >> 2) < ctx->total); /* 0 = absent */
    sym->type = value & 3;
    if (sym->type == 0) {
        *id = 0, sym->id = 0;
        return AM_OK;
    }
    *id = ctx->offset + (value >> 2);
    sym->id = *id;
    return AM_OK;
}

static int amL_constraints(am_LoadCtx *ctx) {
    am_Size i, count, value;
    am_Solver *S = ctx->S;
    amE(amL_count(ctx, &count));
    for (i = 0; i < count; ++i) {
        am_Constraint *cons;
        am_Num strength;
        amE(amL_count(ctx, &value));
        amC(value == 6); /* [name, strength, marker, other, relation, row] */
        amE(amL_string(ctx));
        amE(amL_float(ctx, &strength));
        amC(cons = am_newconstraint(S, strength));
        if (ctx->loader->load_cons)
            ctx->loader->load_cons(ctx->loader, ctx->s, i, cons);
        amE(amL_marker(ctx, &cons->marker, &cons->marker_id));
        amE(amL_marker(ctx, &cons->other, &value));
        cons->other_id = value;
        amE(amL_uint32(ctx, &value));
        amC(value <= AM_GREATEQUAL); /* 0 = never configured */
        cons->relation = value;
        amE(amL_row(ctx, &cons->expression));
    }
    return AM_OK;
}

static int amL_rows(am_LoadCtx *ctx) {
    am_Solver *S = ctx->S;
    am_Size i, count, value, slots;
    amE(amL_count(ctx, &count));
    amC((count & 1) == 1);
    amE(amL_uint32(ctx, &slots));
    ctx->arena_p = NULL, ctx->arena_left = 0;
    if (slots) {
        size_t unit = sizeof(am_Key) + sizeof(am_Num);
        amC((size_t)slots <= ctx->arena_max / unit); /* cannot overflow */
        amM_allocarena(S, &S->arena, (size_t)slots * unit);
        amC(S->arena.buf != NULL);
        ctx->arena_p = (char*)S->arena.buf;
        ctx->arena_left = S->arena.size;
    }
    for (i = 1; i < count; i += 2) {
        am_Symbol sym;
        am_Row *row;
        amE(amL_uint32(ctx, &value));
        amC((value >> 2) < ctx->total);
        sym.id = ctx->offset + (value >> 2), sym.type = value & 3;
        amC(amH_get(&S->rows, sym.id) == NULL);
        amC(row = (am_Row*)amH_set(S, &S->rows, sym));
        amR_init(row);
        amE(amL_row(ctx, row));
    }
    amC(ctx->arena_left == 0);
    ctx->arena_p = NULL;
    return AM_OK;
}

static int amL_body(am_LoadCtx *ctx) {
    am_Solver *S = ctx->S;
    amE(amL_vars(ctx));
    amE(amL_constraints(ctx));
    amE(amL_rows(ctx));
    amE(amL_row(ctx, &S->objective));
    return S->current_id = ctx->offset + ctx->total, AM_OK;
}

AM_API am_Solver *am_load(am_Allocf *allocf, void *ud,
                          am_Loader *loader, size_t maxmem) {
    am_LoadCtx ctx;
    am_Solver *S;
    am_Size count, total;
    if (loader == NULL || loader->reader == NULL || loader->load_var == NULL)
        return NULL;
    S = am_newsolver(allocf, ud);
    if (S == NULL) return NULL;
    memset(&ctx, 0, sizeof(ctx));
    ctx.S = S;
    ctx.loader = loader;
    ctx.offset = S->current_id + 1;
    if (amL_count(&ctx, &count) != AM_OK || count != 5 ||
            amL_uint32(&ctx, &total) != AM_OK ||
            ctx.offset > 0x3FFFFFFF ||
            total > 0x3FFFFFFF - ctx.offset) {
        am_delsolver(S);
        return NULL;
    }
    ctx.total = total;
    S->maxmem = maxmem ? maxmem : AM_MAX_LOADMEM;
    ctx.arena_max = S->maxmem ? S->maxmem : (size_t)-1;
    if (amL_body(&ctx) != AM_OK) {
        am_delsolver(S);
        return NULL;
    }
    S->maxmem = 0;
    return S;
}


AM_NS_END

#endif /* AM_IMPLEMENTATION */

/* cc: flags+='-shared -O2 -pedantic -std=c89 -DAM_IMPLEMENTATION -xc'
   unixcc: output='amoeba.so'
   win32cc: output='amoeba.dll' */

