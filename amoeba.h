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

#define AM_OK          (0)  /* No error */
#define AM_PARAM       (-1) /* Invalid parameter */
#define AM_FAILED      (-2) /* Operation failed */
#define AM_UNSATISFIED (-3) /* Constraint cannot be satisfied */
#define AM_UNBOUND     (-4) /* Required constraints conflict */

#define AM_LESSEQUAL  (1) /* x <= k */
#define AM_EQUAL      (2) /* x == k */
#define AM_GREATEQUAL (3) /* x >= k */

#define AM_REQUIRED ((am_Num)HUGE_VAL) /* required; HUGE_VAL is C89's +inf */
#define AM_STRONG   ((am_Num)1000000)  /* strong soft constraint */
#define AM_MEDIUM   ((am_Num)1000)     /* medium, the default edit strength */
#define AM_WEAK     ((am_Num)1)        /* weak soft constraint */

#include <stddef.h>
#include <math.h>

AM_NS_BEGIN

#ifdef AM_NUM
typedef AM_NUM am_Num;
#elif defined(AM_USE_DOUBLE)
typedef double am_Num;
#else
typedef float am_Num;
#endif

typedef struct am_Solver     am_Solver;
typedef struct am_Constraint am_Constraint;

typedef unsigned am_Id;

typedef enum am_Type {
    am_AllocSolver = 1,
    am_AllocConstraint,
    am_AllocSuggest,
    am_AllocHash,
    am_AllocDump,
    am_AllocPool
} am_Type;

typedef void *am_Allocf(void **pud, void *p, size_t ns, size_t os, am_Type t);

AM_API am_Solver *am_newsolver(am_Allocf *allocf, void *ud);
AM_API void       am_resetsolver(am_Solver *S);
AM_API void       am_delsolver(am_Solver *S);

AM_API am_Id am_newvariable(am_Solver *S, am_Num *pvalue);
AM_API void  am_delvariable(am_Solver *S, am_Id var);

AM_API am_Num *am_varvalue(am_Solver *S, am_Id var, am_Num *newvalue);
AM_API am_Id   am_vars(const am_Solver *S, am_Id var);

AM_API int  am_refcount(const am_Solver *S, am_Id var);
AM_API void am_updatevars(am_Solver *S);
AM_API void am_autoupdate(am_Solver *S, int auto_update);

AM_API void am_clearedits(am_Solver *S);
AM_API int  am_hasedit(const am_Solver *S, am_Id var);
AM_API int  am_addedit(am_Solver *S, am_Id var, am_Num strength);
AM_API void am_suggest(am_Solver *S, am_Id var, am_Num value);
AM_API void am_deledit(am_Solver *S, am_Id var);

AM_API am_Constraint *am_newconstraint(am_Solver *S, am_Num strength);
AM_API am_Constraint *am_cloneconstraint(am_Constraint *other, am_Num strength);
AM_API am_Constraint *am_constraints(const am_Solver *S, am_Constraint *c);

AM_API int  am_add(am_Constraint *c);
AM_API void am_remove(am_Constraint *c);

AM_API void  am_resetconstraint(am_Constraint *c);
AM_API void  am_delconstraint(am_Constraint *c);
AM_API int   am_hasconstraint(const am_Constraint *c);
AM_API int   am_relation(const am_Constraint *c);
AM_API am_Id am_terms(const am_Constraint *c, am_Id var, am_Num *coef);

AM_API am_Num am_strength(const am_Constraint *c);
AM_API am_Num am_constant(const am_Constraint *c);
AM_API am_Num am_error(const am_Constraint *c);

AM_API int am_addterm(am_Constraint *c, am_Id var, am_Num f);
AM_API int am_setrelation(am_Constraint *c, int relation);
AM_API int am_addconstant(am_Constraint *c, am_Num constant);
AM_API int am_setstrength(am_Constraint *c, am_Num strength);

AM_API int am_merge(am_Constraint *c, const am_Constraint *o, am_Num coef);

/* dump & load */

typedef struct am_Dumper am_Dumper;
typedef struct am_Loader am_Loader;

AM_API int        am_dump(am_Solver *S, am_Dumper *dumper);
AM_API am_Solver *am_load(am_Allocf *a, void *ud, am_Loader *l, size_t maxmem);

struct am_Dumper {
    const char *(*var_name)(am_Dumper *D, unsigned i, am_Id var, am_Num *value);
    const char *(*cons_name)(am_Dumper *D, unsigned i, am_Constraint *c);

    int (*writer)(am_Dumper *D, const void *buf, size_t len);
};

struct am_Loader {
    am_Num *(*load_var)(am_Loader *L, const char *name, unsigned i, am_Id var);
    void (*load_cons)(am_Loader *L, const char *name, unsigned i,
                      am_Constraint *c);

    const char *(*reader)(am_Loader *L, size_t *plen);
};

AM_NS_END

#endif /* amoeba_h */

#if defined(AM_IMPLEMENTATION) && !defined(am_implemented)
#define am_implemented

#ifndef AM_MAX_LOADMEM
# define AM_MAX_LOADMEM (16u * 1024u * 1024u) /* default load budget */
#endif

#include <assert.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define AM_EXTERNAL (0)
#define AM_SLACK    (1)
#define AM_ERROR    (2)
#define AM_DUMMY    (3)

#define amV_isexternal(key)  ((key).type == AM_EXTERNAL)
#define amV_isslack(key)     ((key).type == AM_SLACK)
#define amV_iserror(key)     ((key).type == AM_ERROR)
#define amV_isdummy(key)     ((key).type == AM_DUMMY)
#define amV_ispivotable(key) (amV_isslack(key) || amV_iserror(key))

#define AM_MIN_HASHSIZE  8
#define AM_POOL_SIZE     4096
#define AM_UNSIGNED_BITS (sizeof(unsigned) * CHAR_BIT)

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

#define amC(cond)                                                              \
    do {                                                                       \
        if (!(cond))                                                           \
            return AM_DEBUG(__FILE__ ":%d: " #cond "\n", __LINE__), AM_FAILED; \
    } while (0)
#define amE(expr)                                                              \
    do {                                                                       \
        int r_;                                                                \
        if ((r_ = (expr)) != AM_OK)                                            \
            return AM_DEBUG(__FILE__ ":%d: " #expr " err %d\n", __LINE__, r_), \
                   r_;                                                         \
    } while (0)

AM_NS_BEGIN

typedef unsigned am_Size;

typedef struct am_Symbol {
    unsigned id : AM_UNSIGNED_BITS - 2; /* symbol id, 0 = none */
    unsigned type : 2; /* AM_EXTERNAL, AM_SLACK, AM_ERROR or AM_DUMMY */
} am_Symbol;

typedef struct am_MemPool {
    void  *pages; /* page chain, link at tail */
    void  *freed; /* free slot list */
    size_t size;  /* slot size, pointer multiple */
} am_MemPool;

typedef struct am_Key {
    int       next; /* probe offset to the next key, 0 = chain end */
    am_Symbol sym;  /* key symbol, id 0 = empty slot */
} am_Key;

typedef struct am_Arena {
    size_t size; /* buffer size in bytes */
    void  *buf;  /* row storage, NULL when unused */
} am_Arena;

typedef struct am_Table {
    am_Size size;                              /* slot count, a power of two */
    am_Size count;                             /* live entry count */
    am_Size value_size : AM_UNSIGNED_BITS - 1; /* value size in bytes */
    am_Size arena : 1; /* 1 = keys live in the load arena */
    am_Size last_free; /* first slot to scan for a free one */
    am_Key *keys;      /* slot keys, parallel to vals */
    void   *vals;      /* value slots */
} am_Table;

typedef struct am_Iterator {
    const am_Table *t;      /* table being walked */
    am_Symbol       key;    /* current key, id 0 = done */
    am_Size         offset; /* next slot index */
} am_Iterator;

typedef struct am_Row {
    am_Symbol infeasible_next; /* link in the infeasible row list */
    am_Num    constant;        /* constant term */
    am_Table  terms;           /* symbol -> coefficient */
} am_Row;

typedef struct am_Var {
    am_Symbol next;     /* dirty-list link, type AM_DUMMY = queued */
    unsigned  refcount; /* 1 for the variable, +1 per term */
    am_Num   *pvalue;   /* caller's bound value */
} am_Var;

struct am_Constraint {
    void      *ud;         /* caller data, never touched by the library */
    am_Row     expression; /* terms and constant as configured */
    am_Symbol  sym;        /* AM_EXTERNAL for pool cons, AM_DUMMY for suggest */
    am_Symbol  marker;     /* slack/error symbol, id 0 = not in solver */
    am_Symbol  other;      /* second error symbol, id 0 = none */
    unsigned   marker_id;  /* symbol id reserved for marker */
    unsigned   other_id : AM_UNSIGNED_BITS - 2; /* id reserved for other */
    unsigned   relation : 2; /* AM_LESSEQUAL, AM_EQUAL or AM_GREATEQUAL */
    am_Num     strength;     /* AM_REQUIRED or a soft strength */
    am_Solver *S;            /* owning solver */
};

typedef struct am_Suggest {
    am_Size       age;        /* solver age when dirtyset was filled */
    am_Num        edit_value; /* current suggested value */
    am_Constraint constraint; /* the edit's hidden equality constraint */
    am_Table      dirtyset;   /* rows touched by this edit */
} am_Suggest;

struct am_Solver {
    void      *ud;              /* allocator userdata */
    am_Allocf *allocf;          /* allocator callback */
    am_Row     objective;       /* row being minimized */
    am_Table   vars;            /* symbol -> am_Var */
    am_Table   constraints;     /* symbol -> am_Constraint* */
    am_Table   suggests;        /* symbol -> am_Suggest* */
    am_Table   rows;            /* symbol -> am_Row */
    am_Arena   arena;           /* storage for loaded row tables */
    am_Size    auto_update;     /* 1 = update bound values after each change */
    am_Size    age;             /* tableau generation, bumps on every change */
    am_Size    current;         /* last symbol id handed out */
    am_Size    current_cons;    /* last constraint symbol id handed out */
    am_Symbol  infeasible_rows; /* rows with a negative constant */
    am_Symbol  dirty_vars;      /* vars whose value changed, to be written */
    am_MemPool conspool;        /* pool of am_Constraint */
    size_t     maxmem;          /* remaining load budget, 0 = unlimited */
};

/* memory */

/* clang-format off */
static void amM_put(am_MemPool *pool, void *obj)
{ *(void **)obj = pool->freed, pool->freed = obj; }
/* clang-format on */

static void *amM_realloc(am_Solver *S, void *p, size_t n, size_t o, am_Type t) {
    size_t grow = n > o ? n - o : 0;
    void  *np;
    if (t == am_AllocSolver) return S->allocf(&S->ud, p, n, o, t);
    /* equality fails so 0 can keep meaning "unlimited" */
    if (S->maxmem != 0 && grow >= S->maxmem) return NULL;
    np = S->allocf(&S->ud, p, n, o, t);
    if (n != 0 && np == NULL) return NULL;
    if (S->maxmem == 0) return np;
    return (S->maxmem += o - n), np;
}

static void amM_freearena(am_Solver *S, am_Arena *a) {
    if (!a->buf) return;
    amM_realloc(S, a->buf, 0, a->size, am_AllocDump);
    a->buf = NULL, a->size = 0;
}

static void *amM_allocarena(am_Solver *S, am_Arena *a, size_t size) {
    assert(a->buf == NULL);
    a->buf = amM_realloc(S, NULL, size, 0, am_AllocDump);
    if (a->buf) a->size = size;
    return a->buf;
}

static void amM_init(am_MemPool *pool, size_t size) {
    memset(pool, 0, sizeof(*pool)), pool->size = size;
    assert(size >= sizeof(void *) && size % sizeof(void *) == 0);
    assert(AM_POOL_SIZE / size > 2);
}

static void amM_free(am_Solver *S, am_MemPool *pool) {
    const size_t offset = AM_POOL_SIZE - sizeof(void *);
    while (pool->pages != NULL) {
        void *next = *(void **)((char *)pool->pages + offset);
        amM_realloc(S, pool->pages, 0, AM_POOL_SIZE, am_AllocPool);
        pool->pages = next;
    }
    amM_init(pool, pool->size);
}

static void *amM_get(am_Solver *S, am_MemPool *pool) {
    const size_t size = pool->size;
    const size_t offset = AM_POOL_SIZE - sizeof(void *);
    char        *page, *end, *obj = (char *)pool->freed;
    if (obj != NULL) return (pool->freed = *(void **)obj), obj;
    page = (char *)amM_realloc(S, NULL, AM_POOL_SIZE, 0, am_AllocPool);
    if (page == NULL) return NULL;
    end = &page[offset], *(void **)end = pool->pages;
    pool->pages = page, page += size, end -= size;
    while ((page += size) <= end) *(void **)(page - size) = page;
    *(void **)(page - size) = pool->freed;
    return (pool->freed = (void *)((char *)pool->pages + size)), pool->pages;
}

/* number & symbol primitives */

/* clang-format off */
static int amN_approx(am_Num a, am_Num b)
{ return a > b ? a - b < AM_NUM_EPS : b - a < AM_NUM_EPS; }

static am_Symbol amS_null(void) { am_Symbol null = {0, 0}; return null; }
/* clang-format on */

static int amN_nearzero(am_Num a) { return amN_approx(a, 0.f); }
static int amN_negative(am_Num a) { return !amN_nearzero(a) && a < 0.f; }

static am_Symbol amS_new(am_Solver *S, int type) {
    am_Symbol sym;
    unsigned  id = ++S->current;
    if (id > 0x3FFFFFFF) abort(); /* id runs out */
    assert(type >= AM_EXTERNAL && type <= AM_DUMMY);
    return (sym.id = id, sym.type = type), sym;
}

/* hash table */

#define amH_slot(t, i) ((char *)(t)->vals + (i) * (t)->value_size)
#define amH_val(T, it) ((T *)amH_slot((it).t, (it).offset - 1))

/* clang-format off */
static am_Iterator amH_iter(const am_Table *t)
{ am_Iterator it; it.t = t, it.key = amS_null(), it.offset = 0; return it; }

static void amH_init(am_Table *t, size_t value_size)
{ memset(t, 0, sizeof(*t)), t->value_size = (am_Size)value_size; }

static am_Size amH_calcsize(am_Size request)
{ am_Size s = AM_MIN_HASHSIZE; while (s < request) s <<= 1; return s; }
/* clang-format on */

static void amH_reset(am_Table *t) {
    t->last_free = t->count = 0;
    if (t->keys) memset(t->keys, 0, t->size * sizeof(am_Key));
}

static int amH_next(am_Iterator *it) {
    am_Size   cur = it->offset, size = it->t->size;
    am_Symbol key;
    while (cur < size && (key = it->t->keys[cur].sym).id == 0) cur++;
    if (cur >= size) return it->key = amS_null(), it->offset = 0;
    return it->key = key, it->offset = cur + 1;
}

static void amH_free(am_Solver *S, am_Table *t) {
    size_t hsize = t->size * (sizeof(am_Key) + t->value_size);
    if (hsize == 0) return;
    if (!t->arena) amM_realloc(S, t->keys, 0, hsize, am_AllocHash);
    amH_init(t, t->value_size);
}

static am_Size amH_find(const am_Table *t, am_Id key) {
    am_Size cur;
    if (t->size == 0) return 0;
    cur = key & (t->size - 1);
    for (; t->keys[cur].sym.id != key; cur += t->keys[cur].next)
        if (t->keys[cur].next == 0) return 0;
    return cur + 1;
}

/* clang-format off */
static const void *amH_get(const am_Table *t, am_Id key)
{ am_Size o = amH_find(t, key); return o ? amH_slot(t, o - 1) : NULL; }
/* clang-format on */

static void amH_attach(am_Table *t, am_Key *keys, size_t size, size_t vsize) {
    t->size = (am_Size)size, t->value_size = vsize;
    t->arena = 0, t->keys = keys, t->vals = keys + size;
    amH_reset(t);
}

static int amH_alloc(am_Table *t, am_Solver *S, size_t req, size_t vsz) {
    size_t  size, objs;
    am_Key *keys;
    amC(req <= (~(am_Size)0 >> 1));
    size = amH_calcsize((am_Size)req), objs = size * (sizeof(am_Key) + vsz);
    keys = (am_Key *)amM_realloc(S, NULL, objs, 0, am_AllocHash);
    amC(keys != NULL);
    return amH_attach(t, keys, size, vsz), AM_OK;
}

static void *amH_rawset(am_Table *t, am_Symbol key) {
    int     mp = key.id & (t->size - 1);
    am_Key *ks = t->keys;
    if (ks[mp].sym.id) {
        int f = t->last_free, othern;
        while (f < (int)t->size && (ks[f].sym.id || ks[f].next)) f += 1;
        if (f == (int)t->size) return NULL;
        t->last_free = f + 1;
        if ((othern = ks[mp].sym.id & (t->size - 1)) == mp) {
            if (ks[mp].next) ks[f].next = (mp + ks[mp].next) - f;
            ks[mp].next = (f - mp), mp = f;
        } else {
            assert(ks[othern].next != 0);
            while (othern + ks[othern].next != mp) othern += ks[othern].next;
            ks[othern].next = (f - othern), ks[f] = ks[mp];
            memcpy(amH_slot(t, f), amH_slot(t, mp), t->value_size);
            if (ks[mp].next) ks[f].next += (mp - f), ks[mp].next = 0;
        }
    }
    return ks[mp].sym = key, amH_slot(t, mp);
}

static int amH_resize(am_Solver *S, am_Table *t, size_t request) {
    am_Table nt;
    am_Size  i;
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

static void *amH_set(am_Solver *S, am_Table *t, am_Symbol key) {
    void *value;
    assert(key.id != 0 && amH_get(t, key.id) == NULL);
    if (!t->size && amH_resize(S, t, 0) != AM_OK) return NULL;
    if ((value = amH_rawset(t, key)) == NULL) {
        if (amH_resize(S, t, t->count * 2) != AM_OK) return NULL;
        value = amH_rawset(t, key); /* retry and must success */
    }
    return t->count += 1, assert(value != NULL), value;
}

static void amH_del(am_Table *t, const void *value) {
    size_t offset = ((char *)value - (char *)t->vals) / t->value_size;
    t->count -= 1, t->keys[offset].sym = amS_null();
}

/* expression (row) */

static void amR_reset(am_Row *r) { r->constant = 0.f, amH_reset(&r->terms); }
static void amR_free(am_Solver *S, am_Row *r) { amH_free(S, &r->terms); }

static void amR_init(am_Row *r) {
    r->infeasible_next = amS_null(), r->constant = 0.f;
    amH_init(&r->terms, sizeof(am_Num));
}

static void amR_mul(am_Row *r, am_Num f) {
    am_Iterator it = amH_iter(&r->terms);
    r->constant *= f;
    while (amH_next(&it)) *amH_val(am_Num, it) *= f;
}

static int amR_addvar(am_Solver *S, am_Row *r, am_Symbol sym, am_Num value) {
    am_Num *term;
    if (sym.id == 0 || amN_nearzero(value)) return 0;
    term = (am_Num *)amH_get(&r->terms, sym.id);
    if (term == NULL) {
        term = (am_Num *)amH_set(S, &r->terms, sym);
        return assert(term != NULL), (*term = value), 1;
    }
    if (!amN_nearzero(*term += value)) return 0;
    return amH_del(&r->terms, term), -1;
}

static void amR_addrow(am_Solver *S, am_Row *r, const am_Row *o, am_Num f) {
    am_Iterator it = amH_iter(&o->terms);
    r->constant += o->constant * f;
    while (amH_next(&it)) amR_addvar(S, r, it.key, *amH_val(am_Num, it) * f);
}

/* variables & constraints */

#define amR_get(S, id) (am_Row *)amH_get(&(S)->rows, id)

/* clang-format off */
static am_Var *amV_get(const am_Solver *S, am_Id var)
{ return S ? (am_Var *)amH_get(&S->vars, var) : NULL; }
/* clang-format on */

static int amC_init(am_Solver *S, am_Symbol sym, am_Constraint *c) {
    am_Constraint **ce = (am_Constraint **)amH_set(S, &S->constraints, sym);
    amC(ce != NULL);
    *ce = c, memset(c, 0, sizeof(*c)), c->S = S;
    return (c->sym = sym), amR_init(&(*ce)->expression), AM_OK;
}

static int amC_addterm(am_Constraint *c, am_Id var, am_Num value) {
    am_Symbol sym = {0, AM_EXTERNAL};
    am_Var   *ve;
    amC((ve = (am_Var *)amH_get(&c->S->vars, var)) != NULL);
    sym.id = var, ve->refcount += amR_addvar(c->S, &c->expression, sym, value);
    return AM_OK;
}

static am_Suggest *amC_newedit(am_Solver *S, am_Id var, am_Num strength) {
    am_Symbol    sym = {0, AM_EXTERNAL};
    am_Suggest **s;
    am_Row      *r;
    am_Var      *ve;
    int          ret;
    if (S == NULL || var == 0 || amN_negative(strength)) return NULL;
    if ((ve = (am_Var *)amH_get(&S->vars, var)) == NULL) return NULL;
    s = (am_Suggest **)amH_set(S, &S->suggests, (sym.id = var, sym));
    if (s == NULL) return NULL;
    *s = (am_Suggest *)amM_realloc(S, NULL, sizeof(am_Suggest), 0,
                                   am_AllocSuggest);
    if (*s == NULL) return amH_del(&S->suggests, s), (am_Suggest *)NULL;
    memset(*s, 0, sizeof(**s));
    sym.id = ++S->current_cons, sym.type = AM_DUMMY; /* local cons */
    (*s)->edit_value = (r = amR_get(S, var)) ? r->constant : 0.f;
    if (!amV_isdummy(ve->next)) (*s)->edit_value = *ve->pvalue;
    amC_init(S, sym, &(*s)->constraint);
    (*s)->constraint.strength = amN_nearzero(strength) ? AM_REQUIRED : strength;
    am_setrelation(&(*s)->constraint, AM_EQUAL);
    am_addterm(&(*s)->constraint, var, 1.0f);
    am_addconstant(&(*s)->constraint, -(*s)->edit_value);
    ret = am_add(&(*s)->constraint), assert(ret == AM_OK), (void)ret;
    return *s;
}

static void amC_freesuggest(am_Solver *S, am_Suggest *s) {
    amH_free(S, &s->dirtyset), am_delconstraint(&s->constraint);
    amM_realloc(S, s, 0, sizeof(am_Suggest), am_AllocSuggest);
}

/* Cassowary algorithm */

static int amA_takerow(am_Solver *S, am_Symbol sym, am_Row *dst) {
    am_Row *r = amR_get(S, sym.id);
    if (r == NULL) return AM_FAILED;
    amH_del(&S->rows, r);
    return (dst->constant = r->constant, dst->terms = r->terms), AM_OK;
}

static void amA_solvefor(am_Solver *S, am_Row *r, am_Symbol en, am_Symbol lv) {
    am_Num *term = (am_Num *)amH_get(&r->terms, en.id);
    am_Num  reciprocal = 1.0f / *term;
    assert(en.id != lv.id && !amN_nearzero(*term));
    amH_del(&r->terms, term);
    if (!amN_approx(reciprocal, -1.0f)) amR_mul(r, -reciprocal);
    if (lv.id != 0) amR_addvar(S, r, lv, reciprocal);
}

static void amA_elim(am_Solver *S, am_Row *d, am_Symbol out, const am_Row *r) {
    am_Num *term = (am_Num *)amH_get(&d->terms, out.id);
    if (term) amH_del(&d->terms, term), amR_addrow(S, d, r, *term);
}

static void amA_markdirty(am_Solver *S, am_Symbol var) {
    am_Var *ve = (am_Var *)amH_get(&S->vars, var.id);
    assert(ve != NULL);
    if (amV_isdummy(ve->next)) return;
    ve->next.id = S->dirty_vars.id, ve->next.type = AM_DUMMY;
    S->dirty_vars = var, ve->refcount += 1;
}

static int amA_infeasible(am_Solver *S, am_Symbol sym, am_Row *r) {
    if (amV_isdummy(r->infeasible_next)) return 1;
    if (!amN_negative(r->constant)) return 0;
    r->infeasible_next.id = S->infeasible_rows.id;
    r->infeasible_next.type = AM_DUMMY;
    return (S->infeasible_rows = sym), 1;
}

static void amA_substitute(am_Solver *S, am_Symbol var, am_Row *expr) {
    am_Iterator it = amH_iter(&S->rows);
    while (amH_next(&it)) {
        am_Row *r = amH_val(am_Row, it);
        amA_elim(S, r, var, expr);
        if (amV_isexternal(it.key))
            amA_markdirty(S, it.key);
        else
            amA_infeasible(S, it.key, r);
    }
    amA_elim(S, &S->objective, var, expr);
}

static int amA_putrow(am_Solver *S, am_Symbol sym, const am_Row *src) {
    am_Row *r;
    assert(amR_get(S, sym.id) == NULL);
    r = (am_Row *)amH_set(S, &S->rows, sym);
    assert(r != NULL), r->infeasible_next = amS_null();
    return (r->constant = src->constant, r->terms = src->terms), AM_OK;
}

static void amA_optimize(am_Solver *S, am_Row *obj) {
    for (;;) {
        am_Symbol   enter = amS_null(), leave = amS_null();
        am_Num      ratio, min = AM_NUM_MAX, *term;
        am_Iterator it = amH_iter(&obj->terms);
        am_Row      tmp;
        assert(S->infeasible_rows.id == 0);
        while (amH_next(&it))
            if (!amV_isdummy(it.key) && amN_negative(*amH_val(am_Num, it)))
                break;
        if (it.key.id == 0) return;
        enter = it.key, it = amH_iter(&S->rows);
        while (amH_next(&it)) {
            am_Row *r = amH_val(am_Row, it);
            if (amV_isexternal(it.key)) continue;
            term = (am_Num *)amH_get(&r->terms, enter.id);
            if (term == NULL || *term > 0.f) continue;
            ratio = -r->constant / *term;
            if (ratio < min || (amN_approx(ratio, min) && it.key.id < leave.id))
                min = ratio, leave = it.key;
        }
        assert(leave.id != 0);
        amA_takerow(S, leave, &tmp);
        amA_solvefor(S, &tmp, enter, leave);
        amA_substitute(S, enter, &tmp);
        if (obj != &S->objective) amA_elim(S, obj, enter, &tmp);
        amA_putrow(S, enter, &tmp);
    }
}

static int amA_mergerow(am_Solver *S, am_Row *r, am_Symbol var, am_Num f) {
    am_Row *oldrow = amR_get(S, var.id);
    if (oldrow) return amR_addrow(S, r, oldrow, f), 1;
    return amR_addvar(S, r, var, f), 0;
}

static am_Row amA_makerow(am_Solver *S, am_Constraint *c) {
    am_Iterator it = amH_iter(&c->expression.terms);
    am_Row      r;
    amR_init(&r), r.constant = c->expression.constant;
    while (amH_next(&it)) {
        amA_markdirty(S, it.key);
        amA_mergerow(S, &r, it.key, *amH_val(am_Num, it));
    }
    if (c->relation != AM_EQUAL) {
        c->marker.id = c->marker_id, c->marker.type = AM_SLACK;
        amR_addvar(S, &r, c->marker, -1.0f);
        if (c->strength < AM_REQUIRED) {
            c->other.id = c->other_id, c->other.type = AM_ERROR;
            amR_addvar(S, &r, c->other, 1.0f);
            amR_addvar(S, &S->objective, c->other, c->strength);
        }
    } else if (c->strength >= AM_REQUIRED) {
        c->marker.id = c->marker_id, c->marker.type = AM_DUMMY;
        amR_addvar(S, &r, c->marker, 1.0f);
    } else {
        c->marker.id = c->marker_id, c->marker.type = AM_ERROR;
        c->other.id = c->other_id, c->other.type = AM_ERROR;
        amR_addvar(S, &r, c->marker, -1.0f);
        amR_addvar(S, &r, c->other, 1.0f);
        amR_addvar(S, &S->objective, c->marker, c->strength);
        amR_addvar(S, &S->objective, c->other, c->strength);
    }
    if (r.constant < 0.f) amR_mul(&r, -1.0f);
    return r;
}

static int amA_addartificial(am_Solver *S, am_Row *r, am_Constraint *c) {
    am_Symbol   a = amS_new(S, AM_SLACK);
    am_Iterator it;
    am_Row      tmp;
    am_Num     *term;
    int         ret;
    --S->current; /* artificial variable will be removed */
    amR_init(&tmp), amR_addrow(S, &tmp, r, 1.0f);
    amA_putrow(S, a, r), amA_optimize(S, &tmp);
    ret = amN_nearzero(tmp.constant) ? AM_OK : AM_UNBOUND;
    amR_free(S, &tmp);
    if (amA_takerow(S, a, &tmp) == AM_OK) {
        am_Symbol enter = amS_null();
        if (tmp.terms.count == 0) return amR_free(S, &tmp), ret;
        it = amH_iter(&tmp.terms);
        while (amH_next(&it) && !amV_ispivotable(it.key)) continue;
        if (it.offset) enter = it.key;
        if (enter.id == 0) return amR_free(S, &tmp), AM_UNBOUND;
        amA_solvefor(S, &tmp, enter, a);
        amA_substitute(S, enter, &tmp);
        amA_putrow(S, enter, &tmp);
    }
    it = amH_iter(&S->rows);
    while (amH_next(&it)) {
        am_Row *r = amH_val(am_Row, it);
        am_Num *term = (am_Num *)amH_get(&r->terms, a.id);
        if (term) amH_del(&r->terms, term);
    }
    term = (am_Num *)amH_get(&S->objective.terms, a.id);
    if (term) amH_del(&S->objective.terms, term);
    if (ret != AM_OK) am_remove(c);
    return ret;
}

static int amA_tryaddrow(am_Solver *S, am_Row *r, am_Constraint *c) {
    am_Symbol   subject = amS_null();
    am_Iterator it = amH_iter(&r->terms);
    while (amH_next(&it) && !amV_isexternal(it.key)) continue;
    if (it.offset) subject = it.key;
    if (subject.id == 0 && amV_ispivotable(c->marker)) {
        am_Num *term = (am_Num *)amH_get(&r->terms, c->marker.id);
        if (*term < 0.f) subject = c->marker;
    }
    if (subject.id == 0 && amV_ispivotable(c->other)) {
        am_Num *term = (am_Num *)amH_get(&r->terms, c->other.id);
        if (*term < 0.f) subject = c->other;
    }
    if (subject.id == 0) {
        it = amH_iter(&r->terms);
        while (amH_next(&it) && amV_isdummy(it.key)) continue;
        if (it.offset == 0) {
            if (!amN_nearzero(r->constant))
                return amR_free(S, r), AM_UNSATISFIED;
            subject = c->marker;
        }
    }
    if (subject.id == 0) return amA_addartificial(S, r, c);
    amA_solvefor(S, r, subject, amS_null());
    amA_substitute(S, subject, r);
    amA_putrow(S, subject, r);
    return AM_OK;
}

static void amA_removeerrors(am_Solver *S, am_Constraint *c) {
    if (amV_iserror(c->marker))
        amA_mergerow(S, &S->objective, c->marker, -c->strength);
    if (amV_iserror(c->other))
        amA_mergerow(S, &S->objective, c->other, -c->strength);
    if (S->objective.terms.count == 0) S->objective.constant = 0.f;
    c->marker = c->other = amS_null();
}

static am_Symbol amA_leavingrow(am_Solver *S, am_Symbol marker) {
    am_Symbol   first = amS_null(), second = amS_null(), third = amS_null();
    am_Num      r1 = AM_NUM_MAX, r2 = AM_NUM_MAX;
    am_Iterator it = amH_iter(&S->rows);
    while (amH_next(&it)) {
        am_Row *r = amH_val(am_Row, it);
        am_Num *term = (am_Num *)amH_get(&r->terms, marker.id);
        if (term == NULL) continue;
        if (amV_isexternal(it.key))
            third = it.key;
        else if (*term < 0.f) {
            am_Num ratio = -r->constant / *term;
            if (ratio < r1) r1 = ratio, first = it.key;
        } else {
            am_Num ratio = r->constant / *term;
            if (ratio < r2) r2 = ratio, second = it.key;
        }
    }
    return first.id ? first : second.id ? second : third;
}

static void amA_cachedsuggest(am_Solver *S, am_Suggest *s, am_Num delta) {
    am_Iterator it = amH_iter(&s->dirtyset);
    am_Symbol   marker = s->constraint.marker;
    int         pure = 1;
    while (amH_next(&it)) {
        am_Row *r = amR_get(S, it.key.id);
        am_Num *term;
        assert(r != NULL), term = (am_Num *)amH_get(&r->terms, marker.id);
        assert(term != NULL), r->constant += *term * delta;
        if (amV_isexternal(it.key))
            amA_markdirty(S, it.key);
        else if (amA_infeasible(S, it.key, r))
            pure = 0;
    }
    if (!pure) s->age = 0;
}

static int amA_editdelta(am_Solver *S, am_Suggest *s, am_Num delta) {
    am_Iterator    it = amH_iter(&S->rows);
    am_Constraint *c = &s->constraint;
    am_Row        *r;
    int            pure = 1;
    if ((r = amR_get(S, c->marker.id)) != NULL)
        return r->constant -= delta, amA_infeasible(S, c->marker, r), 0;
    if ((r = amR_get(S, c->other.id)) != NULL)
        return r->constant += delta, amA_infeasible(S, c->other, r), 0;
    if (s->age == S->age) return amA_cachedsuggest(S, s, delta), 0;
    amH_reset(&s->dirtyset);
    while (amH_next(&it)) {
        am_Row *r = amH_val(am_Row, it);
        am_Num *term = (am_Num *)amH_get(&r->terms, c->marker.id);
        if (term == NULL) continue;
        r->constant += *term * delta;
        amH_set(S, &s->dirtyset, it.key);
        if (amV_isexternal(it.key))
            amA_markdirty(S, it.key);
        else if (amA_infeasible(S, it.key, r))
            pure = 0;
    }
    if (pure) s->age = S->age;
    return 1;
}

static void amA_dualoptimize(am_Solver *S) {
    while (S->infeasible_rows.id != 0) {
        am_Symbol   enter = amS_null(), leave;
        am_Num      ratio, min_ratio = AM_NUM_MAX;
        am_Row      tmp, *r = amR_get(S, S->infeasible_rows.id);
        am_Iterator it;
        assert(r != NULL);
        leave = S->infeasible_rows;
        S->infeasible_rows = r->infeasible_next;
        r->infeasible_next = amS_null();
        if (!amN_negative(r->constant)) continue;
        it = amH_iter(&r->terms);
        while (amH_next(&it)) {
            am_Num  term = *amH_val(am_Num, it);
            am_Num *objterm;
            if (amV_isdummy(it.key) || term <= 0.f) continue;
            objterm = (am_Num *)amH_get(&S->objective.terms, it.key.id);
            ratio = objterm ? *objterm / term : 0.f;
            if (min_ratio > ratio) min_ratio = ratio, enter = it.key;
        }
        assert(enter.id != 0);
        amA_takerow(S, leave, &tmp);
        amA_solvefor(S, &tmp, enter, leave);
        amA_substitute(S, enter, &tmp);
        amA_putrow(S, enter, &tmp);
    }
}

/* dump */

#ifndef AM_NAME_LEN
# define AM_NAME_LEN 256
#endif /* AM_NAME_LEN */
#ifndef AM_BUF_LEN
# define AM_BUF_LEN 4096
#endif /* AM_BUF_LEN */

typedef struct am_Dump {
    am_Solver *S;               /* solver being dumped */
    unsigned  *syms;            /* symbol ids, variables first */
    unsigned  *cons;            /* constraint symbol ids, sorted */
    am_Table   symmap;          /* symbol id -> stream index */
    am_Dumper *dumper;          /* caller callbacks */
    char      *p;               /* next free byte in buf */
    int        ret;             /* first writer error */
    char       buf[AM_BUF_LEN]; /* staging buffer */
} am_Dump;

/* clang-format off */
typedef union { double f64; float f32; unsigned u32[2]; } am_Bits;

static int amD_intcmp(const void *lhs, const void *rhs)
{ return *(const int *)lhs - *(const int *)rhs; }
/* clang-format on */

static int amD_char(am_Dump *X, int ch) {
    if ((X->p - X->buf) >= AM_BUF_LEN) {
        amE(X->ret = X->dumper->writer(X->dumper, X->buf, AM_BUF_LEN));
        X->p = X->buf;
    }
    return (*X->p++ = ch & 0xFF), AM_OK;
}

/* clang-format off */
static int amD_raw16(am_Dump *X, am_Size n)
{ amE(amD_char(X, n >> 8)); amE(amD_char(X, n & 0xFF)); return AM_OK; }
/* clang-format on */

static int amD_raw32(am_Dump *X, am_Size n) {
    amE(amD_char(X, n >> 24));
    amE(amD_char(X, n >> 16));
    amE(amD_raw16(X, n));
    return AM_OK;
}

static int amD_uint32(am_Dump *X, am_Size n) {
    if (n <= 0x7F)
        amE(amD_char(X, n));
    else if (n <= 0xFF) {
        amE(amD_char(X, 0xCC));
        amE(amD_char(X, n));
    } else if (n <= 0xFFFF) {
        amE(amD_char(X, 0xCD));
        amE(amD_raw16(X, n));
    } else {
        amE(amD_char(X, 0xCE));
        amE(amD_raw32(X, n));
    }
    return AM_OK;
}

static int amD_float(am_Dump *X, am_Num n) {
    am_Bits u;
    if (sizeof(am_Num) == sizeof(double)) {
        unsigned probe = 1;
        int      be = *(const unsigned char *)&probe == 0; /* big-endian host */
        u.f64 = (double)n;
        amE(amD_char(X, 0xCB));
        amE(amD_raw32(X, u.u32[!be])); /* high half first */
        amE(amD_raw32(X, u.u32[be]));
    } else {
        amE(amD_char(X, 0xCA));
        amE(amD_raw32(X, (u.f32 = (float)n, u.u32[0])));
    }
    return AM_OK;
}

static int amD_string(am_Dump *X, const char *name) {
    am_Dumper *d = X->dumper;
    size_t     buflen, namelen = name ? strlen(name) : ((name = ""), 0);
    amC(namelen < AM_NAME_LEN && namelen <= 0xFF);
    if (namelen < 32)
        amE(amD_char(X, 0xA0 + (int)namelen));
    else {
        amE(amD_char(X, 0xD9));
        amE(amD_char(X, (int)namelen));
    }
    if ((buflen = (X->p - X->buf)) + namelen > AM_BUF_LEN) {
        X->p = X->buf;
        if (buflen) amE(X->ret = d->writer(d, X->buf, buflen));
        if (namelen > AM_BUF_LEN) return X->ret = d->writer(d, name, namelen);
    }
    return memcpy(X->p, name, namelen), (X->p += namelen), AM_OK;
}

static int amD_count(am_Dump *X, am_Size count) {
    if (count < 16)
        amE(amD_char(X, 0x90 + count));
    else if (count <= 0xFFFF) {
        amE(amD_char(X, 0xDC));
        amE(amD_raw16(X, count));
    } else {
        amE(amD_char(X, 0xDD));
        amE(amD_raw32(X, count));
    }
    return AM_OK;
}

static int amD_sym(am_Dump *X, am_Symbol sym) {
    const unsigned *id;
    if (sym.id == 0) return amD_uint32(X, 0); /* id 0 = no such symbol */
    id = (const unsigned *)amH_get(&X->symmap, sym.id);
    amC(id != NULL);
    return amD_uint32(X, (*id << 2) | sym.type);
}

static int amD_row(am_Dump *X, const am_Row *r) {
    am_Iterator it = amH_iter(&r->terms);
    amE(amD_count(X, r->terms.count * 2 + 1));
    amE(amD_float(X, r->constant));
    while (amH_next(&it)) {
        amE(amD_sym(X, it.key));
        amE(amD_float(X, *amH_val(am_Num, it)));
    }
    return AM_OK;
}

static int amD_vars(am_Dump *X) {
    const am_Solver *S = X->S;
    am_Size          i;
    amE(amD_count(X, S->vars.count));
    for (i = 0; i < S->vars.count; ++i) {
        am_Var     *ve = (am_Var *)amH_get(&S->vars, X->syms[i]);
        const char *vn;
        assert(ve != NULL);
        vn = X->dumper->var_name(X->dumper, i, X->syms[i], ve->pvalue);
        amE(amD_string(X, vn));
    }
    return AM_OK;
}

static int amD_constraints(am_Dump *X) {
    const am_Solver *S = X->S;
    am_Size          i;
    amE(amD_count(X, S->constraints.count));
    for (i = 0; i < S->constraints.count; ++i) {
        const char     *name = NULL;
        am_Constraint **ce;
        ce = (am_Constraint **)amH_get(&S->constraints, X->cons[i]);
        assert(ce != NULL);
        amE(amD_count(X, 6)); /* [name, strength, marker, other, op, row] */
        if (X->dumper->cons_name)
            name = X->dumper->cons_name(X->dumper, i, (*ce));
        amE(amD_string(X, name));
        amE(amD_float(X, (*ce)->strength));
        amE(amD_sym(X, (*ce)->marker));
        amE(amD_sym(X, (*ce)->other));
        amE(amD_uint32(X, (*ce)->relation));
        amE(amD_row(X, &(*ce)->expression));
    }
    return AM_OK;
}

static int amD_rows(am_Dump *X) {
    const am_Solver *S = X->S;
    size_t           arena = 0;
    am_Iterator      it = amH_iter(&S->rows);
    while (amH_next(&it))
        arena += amH_calcsize(amH_val(am_Row, it)->terms.count);
    if (arena > ~(am_Size)0) arena = 0;
    amE(amD_count(X, S->rows.count * 2 + 1));
    amE(amD_uint32(X, (am_Size)arena));
    while (amH_next(&it)) {
        amE(amD_sym(X, it.key));
        amE(amD_row(X, amH_val(am_Row, it)));
    }
    return AM_OK;
}

static int amD_collect(am_Dump *X) {
    am_Solver  *S = X->S;
    am_Symbol   sym = {0, 0};
    size_t      i, vc = S->vars.count, cc = 0, count = 0, rest;
    am_Iterator it = amH_iter(&S->vars);
    while (amH_next(&it)) X->syms[count++] = it.key.id;
    it = amH_iter(&S->constraints);
    while (amH_next(&it)) {
        am_Constraint *c = *amH_val(am_Constraint *, it);
        if (c->marker.type) X->syms[count++] = c->marker_id;
        if (c->other.type) X->syms[count++] = c->other_id;
        X->cons[cc++] = it.key.id;
    }
    rest = count - vc;
    if (cc) qsort(X->cons, cc, sizeof(unsigned), amD_intcmp);
    if (vc) qsort(X->syms, vc, sizeof(unsigned), amD_intcmp);
    if (rest) qsort(X->syms + vc, rest, sizeof(unsigned), amD_intcmp);
    amH_init(&X->symmap, sizeof(unsigned));
    if ((count + X->symmap.count) * 2 >= X->symmap.size)
        amE(amH_resize(S, &X->symmap, count + X->symmap.count));
    for (i = 0; i < count; ++i) {
        unsigned *val;
        sym.id = X->syms[i], val = (unsigned *)amH_set(S, &X->symmap, sym);
        assert(val != NULL), *val = (unsigned)i;
    }
    return assert(count == X->symmap.count), AM_OK;
}

static int amD_all(am_Dump *X) {
    const am_Solver *S = X->S;
    am_Dumper       *d = X->dumper;
    amE(amD_count(X, 5)); /* [total, vars, constraints, rows, objective] */
    amE(amD_uint32(X, X->symmap.count));
    amE(amD_vars(X));
    amE(amD_constraints(X));
    amE(amD_rows(X));
    amE(amD_row(X, &S->objective));
    if (X->p > X->buf) amE(X->ret = d->writer(d, X->buf, (X->p - X->buf)));
    return X->ret;
}

/* load */

typedef struct am_Load {
    am_Solver  *S;      /* solver being filled */
    am_Loader  *loader; /* caller callbacks */
    size_t      n;      /* bytes left in the current chunk */
    const char *p, *s;  /* chunk cursor, current string */
    am_Size     offset; /* first symbol id of this load */
    am_Size     total;  /* symbol count declared by the stream */
    size_t arena_max;   /* budget when the load started, for the slot guard */
    char  *arena_p;     /* row-table arena cursor, NULL outside rows */
    size_t arena_left;  /* bytes left in that arena */
    char   buf[AM_NAME_LEN]; /* string staging buffer */
} am_Load;

#define amL_getchar(X) ((X)->n-- > 0 ? *(X)->p++ & 0xFF : amL_fill(X))

static int amL_fill(am_Load *X) {
    X->p = X->loader->reader(X->loader, &X->n);
    amC(X->p && X->n);
    return X->n -= 1, *X->p++ & 0xFF;
}

static int amL_rawslow16(am_Load *X, unsigned *pv) {
    unsigned r = 0;
    int      c;
    amC((c = amL_getchar(X)) != AM_FAILED);
    r |= (unsigned)c << 8;
    amC((c = amL_getchar(X)) != AM_FAILED);
    return (*pv = (r | (unsigned)c)), AM_OK;
}

static int amL_raw16(am_Load *X, unsigned *pv) {
    if (X->n >= 2) {
        unsigned n = *X->p++ & 0xFF;
        n <<= 8, n |= *X->p++ & 0xFF;
        return (*pv = n, X->n -= 2), AM_OK;
    }
    return amL_rawslow16(X, pv);
}

static int amL_raw32(am_Load *X, unsigned *pv) {
    unsigned r = 0;
    int      c;
    if (X->n >= 4) {
        r = *X->p++ & 0xFF;
        r <<= 8, r |= *X->p++ & 0xFF;
        r <<= 8, r |= *X->p++ & 0xFF;
        r <<= 8, r |= *X->p++ & 0xFF;
        return (*pv = r, X->n -= 4), AM_OK;
    }
    amC((c = amL_getchar(X)) != AM_FAILED);
    r |= (unsigned)c << 24;
    amC((c = amL_getchar(X)) != AM_FAILED);
    r |= (unsigned)c << 16;
    amE(amL_rawslow16(X, pv));
    return (*pv |= r), AM_OK;
}

static int amL_uint32(am_Load *X, am_Size *pv) {
    int ty, c;
    switch (ty = amL_getchar(X)) { /* clang-format off */
    default: amC(ty >= 0 && ty <= 0x7F); *pv = ty; break;
    case 0xCC: amC((c = amL_getchar(X)) != AM_FAILED); *pv = c; break;
    case 0xCD: amE(amL_raw16(X, pv)); break;
    case 0xCE: amE(amL_raw32(X, pv)); break;
    } /* clang-format on */
    return AM_OK;
}

static int amL_float(am_Load *X, am_Num *pv) {
    int     ty = amL_getchar(X);
    am_Bits u;
    if (ty == 0xCB) {
        unsigned probe = 1;
        int      be = *(const unsigned char *)&probe == 0; /* big-endian host */
        amE(amL_raw32(X, &u.u32[!be]));
        amE(amL_raw32(X, &u.u32[be]));
        return (*pv = (am_Num)u.f64), AM_OK;
    }
    amC(ty == 0xCA);
    amE(amL_raw32(X, u.u32));
    return (*pv = (am_Num)u.f32), AM_OK;
}

static int amL_string(am_Load *X) {
    char   *buf = X->buf;
    am_Size size;
    int     ty, c;
    switch (ty = amL_getchar(X)) { /* clang-format off */
    default: amC(ty >= 0xA0 && ty <= 0xBF); size = ty - 0xA0; break;
    case 0xD9: amC((c = amL_getchar(X)) != AM_FAILED); size = c; break;
    } /* clang-format on */
    amC(size < AM_NAME_LEN);
    X->s = size ? X->buf : NULL;
    if (X->n >= size) {
        memcpy(X->buf, X->p, size), X->buf[size] = 0;
        return X->p += size, X->n -= size, AM_OK;
    }
    for (;;) {
        memcpy(buf, X->p, X->n), buf += X->n, size -= (am_Size)X->n;
        amC((c = amL_fill(X)) != AM_FAILED);
        *buf++ = c, size -= 1;
        if (size <= X->n) {
            memcpy(buf, X->p, size), buf[size] = 0;
            return (X->n -= size, X->p += size), AM_OK;
        }
    }
}

static int amL_count(am_Load *X, am_Size *pcount) {
    int ty;
    switch (ty = amL_getchar(X)) { /* clang-format off */
    default: amC(ty >= 0x90 && ty <= 0x9F); *pcount = ty - 0x90; break;
    case 0xDC: amE(amL_raw16(X, pcount)); break;
    case 0xDD: amE(amL_raw32(X, pcount)); break;
    } /* clang-format on */
    return AM_OK;
}

static char *amL_takemem(am_Load *X, am_Size size, size_t unit) {
    size_t bytes = size * unit;
    char  *p;
    if (bytes / unit != size || bytes > X->arena_left) return NULL;
    X->arena_left -= bytes;
    return (p = X->arena_p, X->arena_p += bytes), p;
}

static int amL_row(am_Load *X, am_Row *r) {
    am_Size i, count, value, unit = sizeof(am_Key) + sizeof(am_Num);
    amE(amL_count(X, &count));
    amC(count >= 1 && (count & 1) == 1);
    if (X->arena_p) {
        am_Size size = amH_calcsize(count / 2);
        am_Key *keys = (am_Key *)amL_takemem(X, size, unit);
        amC(keys != NULL);
        amH_attach(&r->terms, keys, size, sizeof(am_Num)), r->terms.arena = 1;
    }
    amE(amL_float(X, &r->constant));
    for (i = 1; i < count; i += 2) {
        am_Symbol sym;
        am_Num   *term;
        amE(amL_uint32(X, &value));
        amC((value >> 2) < X->total); /* 30-bit id, never 0 */
        sym.id = X->offset + (value >> 2), sym.type = value & 3;
        amC(amH_get(&r->terms, sym.id) == NULL); /* amH_set's contract */
        amC(term = (am_Num *)amH_set(X->S, &r->terms, sym));
        amE(amL_float(X, term));
    }
    return AM_OK;
}

static int amL_var(am_Load *X, am_Size i) {
    am_Symbol sym = {0, AM_EXTERNAL};
    am_Var   *ve;
    am_Num   *pvalue;
    amE(amL_string(X));
    pvalue = X->loader->load_var(X->loader, X->s, i, X->offset + i);
    amC(pvalue != NULL);
    sym.id = X->offset + i;
    amC(ve = (am_Var *)amH_set(X->S, &X->S->vars, sym));
    memset(ve, 0, sizeof(*ve)), ve->pvalue = pvalue, ve->refcount = 1;
    return (*pvalue = 0.f), AM_OK;
}

static int amL_marker(am_Load *X, am_Symbol *sym, unsigned *id) {
    am_Size value;
    amE(amL_uint32(X, &value));
    amC(value == 0 || (value >> 2) < X->total); /* 0 = absent */
    sym->type = value & 3;
    if (sym->type == 0) return (*id = 0, sym->id = 0), AM_OK;
    return (*id = X->offset + (value >> 2), sym->id = *id), AM_OK;
}

static int amL_constraint(am_Load *X, am_Size i) {
    am_Iterator    it;
    am_Constraint *c;
    am_Num         strength;
    am_Size        value;
    amE(amL_count(X, &value));
    amC(value == 6); /* [name, strength, marker, other, relation, row] */
    amE(amL_string(X));
    amE(amL_float(X, &strength));
    amC(c = am_newconstraint(X->S, strength));
    if (X->loader->load_cons) X->loader->load_cons(X->loader, X->s, i, c);
    amE(amL_marker(X, &c->marker, &c->marker_id));
    amE(amL_marker(X, &c->other, &value));
    c->other_id = value;
    amE(amL_uint32(X, &value));
    amC(value <= AM_GREATEQUAL); /* 0 = never configured */
    c->relation = value;
    amE(amL_row(X, &c->expression));
    it = amH_iter(&c->expression.terms);
    while (amH_next(&it)) {
        am_Var *ve = (am_Var *)amH_get(&X->S->vars, it.key.id);
        amC(it.key.type == AM_EXTERNAL && ve && (ve->refcount += 1));
    }
    return AM_OK;
}

static int amL_rows(am_Load *X) {
    am_Size i, count, value, unit = sizeof(am_Key) + sizeof(am_Num);
    amE(amL_count(X, &count));
    amC((count & 1) == 1);
    amE(amL_uint32(X, &value));
    if (value) {
        amC((size_t)value <= X->arena_max / unit); /* cannot overflow */
        amC(amM_allocarena(X->S, &X->S->arena, (size_t)value * unit));
        X->arena_p = (char *)X->S->arena.buf, X->arena_left = X->S->arena.size;
    }
    for (i = 1; i < count; i += 2) {
        am_Symbol sym;
        am_Row   *r;
        am_Var   *ve;
        amE(amL_uint32(X, &value));
        amC((value >> 2) < X->total);
        sym.id = X->offset + (value >> 2), sym.type = value & 3;
        amC(amH_get(&X->S->rows, sym.id) == NULL);
        amC(r = (am_Row *)amH_set(X->S, &X->S->rows, sym));
        amE((amR_init(r), amL_row(X, r)));
        if (amV_isexternal(sym) && (ve = amV_get(X->S, sym.id)))
            *ve->pvalue = r->constant;
    }
    return X->arena_left ? AM_FAILED : (X->arena_p = NULL, AM_OK);
}

static int amL_body(am_Load *X) {
    am_Size i, count;
    amE(amL_count(X, &count)); /* vars */
    amC(count <= X->total);    /* ids are offset+i, keep them in total */
    for (i = 0; i < count; ++i) amE(amL_var(X, i));
    amE(amL_count(X, &count)); /* constraints */
    for (i = 0; i < count; ++i) amE(amL_constraint(X, i));
    amE(amL_rows(X));
    amE(amL_row(X, &X->S->objective));
    return X->S->current = X->offset + X->total, AM_OK;
}

/* APIs */

/* clang-format off */
AM_API int am_hasedit(const am_Solver *S, am_Id var)
{ return S ? amH_get(&S->suggests, var) != NULL : 0; }

AM_API int am_refcount(const am_Solver *S, am_Id var)
{ am_Var *ve = amV_get(S, var); return ve ? ve->refcount : 0; }

AM_API int am_hasconstraint(const am_Constraint *c)
{ return c != NULL && c->marker.id != 0; }

AM_API am_Num am_strength(const am_Constraint *c)
{ return c ? c->strength : 0; }

AM_API am_Num am_constant(const am_Constraint *c)
{ return c ? c->expression.constant : 0; }

static am_Solver *amL_fail(am_Solver *S)
{ return am_delsolver(S), (am_Solver *)0; }
/* clang-format on */

AM_API void am_autoupdate(am_Solver *S, int v) { S->auto_update = v; }
AM_API int  am_relation(const am_Constraint *c) { return c ? c->relation : 0; }

static void *amS_defallocf(void **pud, void *p, size_t n, size_t o, am_Type t) {
    void *np;
    (void)pud, (void)o, (void)t;
    if (n == 0) return free(p), (void *)0;
    if ((np = realloc(p, n)) == NULL) abort();
    return np;
}

static void *amS_getcons(am_Solver *S) {
    if (S->allocf == amS_defallocf) return amM_get(S, &S->conspool);
    return amM_realloc(S, NULL, sizeof(am_Constraint), 0, am_AllocConstraint);
}

static int amS_putcons(am_Solver *S, am_Constraint *c) {
    if (S->allocf == amS_defallocf) return amM_put(&S->conspool, c), 1;
    return amM_realloc(S, c, 0, sizeof(*c), am_AllocConstraint), 0;
}

AM_API am_Solver *am_newsolver(am_Allocf *allocf, void *ud) {
    am_Solver *S;
    if (allocf == NULL) allocf = amS_defallocf;
    S = (am_Solver *)allocf(&ud, NULL, sizeof(am_Solver), 0, am_AllocSolver);
    if (S == NULL) return NULL;
    memset(S, 0, sizeof(*S)), S->ud = ud, S->allocf = allocf;
    amR_init(&S->objective);
    amH_init(&S->vars, sizeof(am_Var));
    amH_init(&S->constraints, sizeof(am_Constraint *));
    amH_init(&S->suggests, sizeof(am_Suggest *));
    amH_init(&S->rows, sizeof(am_Row));
    amM_init(&S->conspool, sizeof(am_Constraint));
    return S;
}

AM_API void am_resetsolver(am_Solver *S) {
    am_Iterator    it;
    am_Constraint *c;
    if (S == NULL) return;
    am_clearedits(S);
    it = amH_iter(&S->constraints);
    while (amH_next(&it))
        c = *amH_val(am_Constraint *, it), c->marker = c->other = amS_null();
    it = amH_iter(&S->rows);
    while (amH_next(&it)) amR_free(S, amH_val(am_Row, it));
    amH_reset(&S->rows), amR_reset(&S->objective);
    amM_freearena(S, &S->arena), assert(S->infeasible_rows.id == 0), S->age = 0;
}

AM_API void am_delsolver(am_Solver *S) {
    am_Iterator it = amH_iter(S ? &S->constraints : NULL);
    if (it.t == NULL) return;
    while (amH_next(&it)) {
        am_Constraint *c = *amH_val(am_Constraint *, it);
        amR_free(S, &c->expression);
        if (amV_isexternal(c->sym)) amS_putcons(S, c);
    }
    it = amH_iter(&S->suggests);
    while (amH_next(&it)) {
        am_Suggest *s = *amH_val(am_Suggest *, it);
        amH_free(S, &s->dirtyset);
        amH_free(S, &s->constraint.expression.terms);
        amM_realloc(S, s, 0, sizeof(am_Suggest), am_AllocSuggest);
    }
    it = amH_iter(&S->rows);
    while (amH_next(&it)) amR_free(S, amH_val(am_Row, it));
    amR_free(S, &S->objective);
    amH_free(S, &S->vars);
    amH_free(S, &S->constraints);
    amH_free(S, &S->suggests);
    amH_free(S, &S->rows);
    amM_free(S, &S->conspool);
    amM_freearena(S, &S->arena);
    amM_realloc(S, S, 0, sizeof(am_Solver), am_AllocSolver);
}

AM_API void am_updatevars(am_Solver *S) {
    if (S == NULL) return;
    while (S->dirty_vars.id != 0) {
        am_Symbol var = S->dirty_vars;
        am_Var   *ve = (am_Var *)amH_get(&S->vars, var.id);
        am_Row   *r;
        assert(ve != NULL), S->dirty_vars = ve->next, ve->next = amS_null();
        if (ve->refcount == 1)
            am_deledit(S, var.id), amH_del(&S->vars, ve);
        else {
            *ve->pvalue = (r = amR_get(S, var.id)) ? r->constant : 0.f;
            ve->refcount -= 1;
        }
    }
}

AM_API int am_add(am_Constraint *c) {
    am_Solver *S = c ? c->S : NULL;
    int        ret;
    am_Symbol  sym;
    am_Row     r;
    if (S == NULL) return AM_PARAM;
    amC(c->marker.id == 0);
    if (c->marker_id == 0) c->marker_id = ((sym = amS_new(S, 0)), sym.id);
    if (c->other_id == 0 && c->strength < AM_REQUIRED)
        c->other_id = ((sym = amS_new(S, 0)), sym.id);
    r = amA_makerow(S, c);
    if ((ret = amA_tryaddrow(S, &r, c)) != AM_OK)
        amA_removeerrors(S, c);
    else {
        amA_optimize(S, &S->objective);
        if (S->auto_update) am_updatevars(S);
        S->age += 1;
    }
    return assert(S->infeasible_rows.id == 0), ret;
}

AM_API void am_remove(am_Constraint *c) {
    am_Solver *S;
    am_Symbol  marker;
    am_Row     tmp;
    if (c == NULL || c->marker.id == 0) return;
    S = c->S, marker = c->marker;
    amA_removeerrors(S, c);
    if (amA_takerow(S, marker, &tmp) != AM_OK) {
        am_Symbol leave = amA_leavingrow(S, marker);
        am_Var   *ve;
        assert(leave.id != 0);
        amA_takerow(S, leave, &tmp);
        amA_solvefor(S, &tmp, marker, leave);
        amA_substitute(S, marker, &tmp);
        if (amV_isexternal(leave) && (ve = amV_get(S, leave.id)))
            *ve->pvalue = 0.f;
    }
    amR_free(S, &tmp);
    amA_optimize(S, &S->objective), S->age += 1;
    if (S->auto_update) am_updatevars(S);
}

AM_API void am_clearedits(am_Solver *S) {
    am_Iterator it;
    if (S == NULL) return;
    it = amH_iter(&S->suggests);
    if (!S->auto_update) am_updatevars(S);
    while (amH_next(&it)) amC_freesuggest(S, *amH_val(am_Suggest *, it));
    amH_reset(&S->suggests);
}

AM_API int am_addedit(am_Solver *S, am_Id var, am_Num strength) {
    am_Suggest **s;
    if (S == NULL || var == 0) return AM_PARAM;
    if (strength >= AM_STRONG) strength = AM_STRONG;
    s = (am_Suggest **)amH_get(&S->suggests, var);
    if (s) return assert(*s), am_setstrength(&(*s)->constraint, strength);
    amC(amC_newedit(S, var, strength));
    return AM_OK;
}

AM_API void am_suggest(am_Solver *S, am_Id var, am_Num value) {
    am_Suggest **ps, *s;
    am_Num       delta;
    if (S == NULL || var == 0) return;
    ps = (am_Suggest **)amH_get(&S->suggests, var);
    s = ps ? *ps : amC_newedit(S, var, AM_MEDIUM);
    if (s == NULL) return; /* unknown var: nothing to suggest */
    delta = value - s->edit_value, s->edit_value = value;
    amA_editdelta(S, s, delta), amA_dualoptimize(S);
    if (S->auto_update) am_updatevars(S);
}

AM_API void am_deledit(am_Solver *S, am_Id var) {
    am_Suggest **ps, *s;
    if (S == NULL || var == 0) return;
    ps = (am_Suggest **)amH_get(&S->suggests, var);
    if (ps) s = *ps, amH_del(&S->suggests, ps), amC_freesuggest(S, s);
}

AM_API am_Id am_newvariable(am_Solver *S, am_Num *pvalue) {
    am_Symbol sym;
    am_Var   *ve;
    if (S == NULL || pvalue == NULL) return 0;
    sym = amS_new(S, AM_EXTERNAL);
    if ((ve = (am_Var *)amH_set(S, &S->vars, sym)) == NULL) return 0;
    memset(ve, 0, sizeof(*ve)), ve->pvalue = pvalue, ve->refcount = 1;
    return sym.id;
}

AM_API void am_delvariable(am_Solver *S, am_Id var) {
    am_Var *ve = amV_get(S, var);
    if (!ve || --ve->refcount) return;
    assert(!amV_isdummy(ve->next)), am_deledit(S, var), amH_del(&S->vars, ve);
}

AM_API am_Num *am_varvalue(am_Solver *S, am_Id var, am_Num *newvalue) {
    am_Var *ve = amV_get(S, var);
    am_Num *pvalue = ve ? ve->pvalue : NULL;
    if (ve && newvalue) ve->pvalue = newvalue;
    return pvalue;
}

AM_API am_Id am_vars(const am_Solver *S, am_Id var) {
    am_Iterator it;
    if (S == NULL) return 0;
    it = amH_iter(&S->vars);
    if (var && !(it.offset = amH_find(&S->vars, var))) return 0;
    return amH_next(&it) ? it.key.id : 0;
}

AM_API am_Constraint *am_constraints(const am_Solver *S, am_Constraint *c) {
    am_Iterator it;
    if (S == NULL) return 0;
    it = amH_iter(&S->constraints);
    if (c && !(it.offset = amH_find(&S->constraints, c->sym.id))) return 0;
    while (amH_next(&it))
        if (amV_isexternal(it.key)) return *amH_val(am_Constraint *, it);
    return 0;
}

AM_API am_Id am_terms(const am_Constraint *c, am_Id var, am_Num *coef) {
    am_Iterator it;
    if (c == NULL || coef == NULL) return 0;
    it = amH_iter(&c->expression.terms);
    if (var && !(it.offset = amH_find(&c->expression.terms, var))) return 0;
    return amH_next(&it) ? ((*coef = *amH_val(am_Num, it)), it.key.id) : 0;
}

AM_API am_Constraint *am_newconstraint(am_Solver *S, am_Num strength) {
    am_Symbol      sym = {0, AM_EXTERNAL}; /* AM_EXTERNAL for pool cons */
    am_Constraint *c;
    int            r;
    if (S == NULL || amN_negative(strength)) return NULL;
    if (!(c = (am_Constraint *)amS_getcons(S))) return NULL;
    sym.id = ++S->current_cons, r = amC_init(S, sym, c);
    c->strength = amN_nearzero(strength) ? AM_REQUIRED : strength;
    if (r != AM_OK) amS_putcons(S, c), c = NULL;
    return c;
}

AM_API am_Constraint *am_cloneconstraint(am_Constraint *o, am_Num strength) {
    am_Constraint *c;
    if (o == NULL) return NULL;
    if (amN_nearzero(strength)) strength = o->strength;
    if ((c = am_newconstraint(o->S, strength)) == NULL) return NULL;
    return am_merge(c, o, 1.0f), (c->relation = o->relation), c;
}

AM_API void am_resetconstraint(am_Constraint *c) {
    am_Iterator it;
    if (c == NULL) return;
    am_remove(c), c->relation = 0;
    it = amH_iter(&c->expression.terms);
    while (amH_next(&it)) am_delvariable(c->S, it.key.id);
    amR_reset(&c->expression);
}

AM_API void am_delconstraint(am_Constraint *c) {
    am_Solver      *S = c ? c->S : NULL;
    am_Iterator     it;
    am_Constraint **ce;
    if (S == NULL) return;
    am_remove(c), ce = (am_Constraint **)amH_get(&S->constraints, c->sym.id);
    assert(ce != NULL && *ce == c), amH_del(&S->constraints, ce);
    it = amH_iter(&c->expression.terms);
    while (amH_next(&it)) am_delvariable(c->S, it.key.id);
    amR_free(S, &c->expression);
    if (amV_isexternal(c->sym)) amS_putcons(S, c);
}

AM_API am_Num am_error(const am_Constraint *c) {
    am_Solver *S = c ? c->S : NULL;
    am_Row    *r;
    am_Num     err = 0;
    if (c == NULL || c->marker.id == 0) return 0;
    if (amV_iserror(c->marker) && (r = amR_get(S, c->marker.id)))
        err += r->constant;
    if (amV_iserror(c->other) && (r = amR_get(S, c->other.id)))
        err += r->constant;
    return err;
}

AM_API int am_addterm(am_Constraint *c, am_Id var, am_Num f) {
    if (c == NULL || var == 0) return AM_PARAM;
    amC(c->marker.id == 0);
    return amC_addterm(c, var, c->relation == AM_GREATEQUAL ? -f : f);
}

AM_API int am_setrelation(am_Constraint *c, int relation) {
    if (c == NULL) return AM_PARAM;
    if (relation < AM_LESSEQUAL || relation > AM_GREATEQUAL) return AM_PARAM;
    amC(c->marker.id == 0 && c->relation == 0);
    if (relation != AM_GREATEQUAL) amR_mul(&c->expression, -1.0f);
    return (c->relation = relation), AM_OK;
}

AM_API int am_addconstant(am_Constraint *c, am_Num constant) {
    if (c == NULL) return AM_PARAM;
    amC(c->marker.id == 0);
    if (c->relation == AM_GREATEQUAL) constant = -constant;
    return (c->expression.constant += constant), AM_OK;
}

AM_API int am_setstrength(am_Constraint *c, am_Num strength) {
    am_Solver *S;
    am_Num     old, diff;
    if (c == NULL) return AM_PARAM;
    strength = amN_nearzero(strength) ? AM_REQUIRED : strength;
    if (c->strength == strength) return AM_OK;
    if (!am_hasconstraint(c)) return (c->strength = strength), AM_OK;
    if (c->strength >= AM_REQUIRED || strength >= AM_REQUIRED) {
        int r, ra = AM_OK;
        old = c->strength, am_remove(c), c->strength = strength;
        if ((r = am_add(c)) != AM_OK) c->strength = old, ra = am_add(c);
        return assert(ra == AM_OK), (void)ra, r;
    }
    S = c->S, diff = strength - c->strength;
    amA_mergerow(S, &S->objective, c->marker, diff);
    amA_mergerow(S, &S->objective, c->other, diff);
    amA_optimize(S, &S->objective);
    if (S->auto_update) am_updatevars(S);
    return (S->age += 1, c->strength = strength), AM_OK;
}

AM_API int am_merge(am_Constraint *c, const am_Constraint *o, am_Num coef) {
    am_Iterator it;
    if (c == NULL || o == NULL || c->S != o->S) return AM_PARAM;
    amC(c->marker.id == 0);
    if (c->relation == AM_GREATEQUAL) coef = -coef;
    c->expression.constant += o->expression.constant * coef;
    it = amH_iter(&o->expression.terms);
    while (amH_next(&it))
        amE(amC_addterm(c, it.key.id, *amH_val(am_Num, it) * coef));
    return AM_OK;
}

AM_API int am_dump(am_Solver *S, am_Dumper *dumper) {
    size_t  cons_alloc, sym_alloc;
    int     ok;
    am_Dump X;
    if (!S || !dumper || !dumper->writer || !dumper->var_name) return AM_PARAM;
    am_clearedits(S);
    if (!S->auto_update) am_updatevars(S);
    cons_alloc = sizeof(unsigned) * S->constraints.count;
    sym_alloc = sizeof(unsigned) * (cons_alloc * 2 + S->vars.count);
    memset(&X, 0, sizeof(X)), X.S = S, X.ret = AM_FAILED;
    X.syms = (unsigned *)amM_realloc(S, NULL, sym_alloc, 0, am_AllocDump);
    X.cons = (unsigned *)amM_realloc(S, NULL, cons_alloc, 0, am_AllocDump);
    ok = (!sym_alloc || X.syms) && (!cons_alloc || X.cons);
    if (ok && (X.ret = amD_collect(&X)) == AM_OK)
        X.dumper = dumper, X.p = X.buf, X.ret = amD_all(&X);
    if (X.syms) amM_realloc(S, X.syms, 0, sym_alloc, am_AllocDump);
    if (X.cons) amM_realloc(S, X.cons, 0, cons_alloc, am_AllocDump);
    return amH_free(S, &X.symmap), X.ret;
}

AM_API am_Solver *am_load(am_Allocf *a, void *ud, am_Loader *l, size_t maxmem) {
    am_Load    X;
    am_Solver *S;
    am_Size    count, total;
    if (!l || !l->reader || !l->load_var) return NULL;
    if ((S = am_newsolver(a, ud)) == NULL) return NULL;
    memset(&X, 0, sizeof(X)), X.S = S, X.loader = l, X.offset = S->current + 1;
    if (amL_count(&X, &count) != AM_OK || count != 5) return amL_fail(S);
    if (amL_uint32(&X, &total) != AM_OK) return amL_fail(S);
    if (X.offset > 0x3FFFFFFF) return amL_fail(S);
    if (total > 0x3FFFFFFF - X.offset) return amL_fail(S);
    S->maxmem = (maxmem ? maxmem : AM_MAX_LOADMEM);
    X.total = total, X.arena_max = S->maxmem ? S->maxmem : (size_t)-1;
    if (amL_body(&X) != AM_OK) return amL_fail(S);
    return (S->maxmem = 0), S;
}

AM_NS_END

#endif /* AM_IMPLEMENTATION */

/* cc: flags+='-shared -O2 -pedantic -std=c89 -DAM_IMPLEMENTATION -xc'
   unixcc: output='amoeba.so'
   win32cc: output='amoeba.dll' */
