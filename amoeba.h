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
#endif /* AM_STATIC */

#ifdef AM_STATIC_API
# ifndef AM_IMPLEMENTATION
#  define AM_IMPLEMENTATION
# endif
# define AM_API AM_STATIC
#endif /* AM_STATIC_API */

#if !defined(AM_API) && defined(_WIN32)
# ifdef AM_IMPLEMENTATION
#  define AM_API __declspec(dllexport)
# else
#  define AM_API __declspec(dllimport)
# endif
#endif /* AM_API */

#ifndef AM_API
# define AM_API extern
#endif /* AM_API */

#define AM_OK          (0)  /* No error */
#define AM_PARAM       (-1) /* Invalid parameter */
#define AM_FAILED      (-2) /* Operation failed */
#define AM_UNSATISFIED (-3) /* Constraint cannot be satisfied */
#define AM_UNBOUND     (-4) /* Required constraints conflict */

#define AM_LESSEQUAL    (1) /* x <= k */
#define AM_EQUAL        (2) /* x == k */
#define AM_GREATEREQUAL (3) /* x >= k */

#define AM_REQUIRED ((am_Num)HUGE_VAL) /* required; HUGE_VAL is C89's +inf */
#define AM_STRONG   ((am_Num)1000000)  /* strong soft constraint */
#define AM_MEDIUM   ((am_Num)1000)     /* medium, the default edit strength */
#define AM_WEAK     ((am_Num)1)        /* weak soft constraint */

#include <math.h>
#include <stddef.h>

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

typedef enum am_Type { /* clang-format off */
    am_TSolver = 1, am_TConstraint, am_TScratch,
    am_TPool, am_TArena, am_THash, am_TVec
} am_Type; /* clang-format on */

typedef void *am_Allocf(void **pud, void *p, size_t ns, size_t os, am_Type t);

AM_API am_Solver *am_newsolver(am_Allocf *allocf, void *ud);
AM_API void       am_resetsolver(am_Solver *S);
AM_API void       am_delsolver(am_Solver *S);

AM_API void am_updatevars(am_Solver *S);
AM_API void am_autoupdate(am_Solver *S, int auto_update);

AM_API am_Id   am_newvariable(am_Solver *S, am_Num *pvalue);
AM_API void    am_delvariable(am_Solver *S, am_Id var);
AM_API am_Num *am_varvalue(am_Solver *S, am_Id var, am_Num *newvalue);
AM_API int     am_refcount(const am_Solver *S, am_Id var);
AM_API am_Id   am_vars(const am_Solver *S, am_Id var);

AM_API int  am_addedit(am_Solver *S, am_Id var, am_Num strength);
AM_API void am_deledit(am_Solver *S, am_Id var);
AM_API void am_suggest(am_Solver *S, am_Id var, am_Num value);
AM_API int  am_hasedit(const am_Solver *S, am_Id var);
AM_API void am_clearedits(am_Solver *S);

AM_API am_Constraint *am_newconstraint(am_Solver *S, am_Num strength);
AM_API void           am_resetconstraint(am_Constraint *c);
AM_API void           am_delconstraint(am_Constraint *c);
AM_API am_Constraint *am_cloneconstraint(am_Constraint *other, am_Num strength);
AM_API am_Constraint *am_constraints(const am_Solver *S, am_Constraint *c);

AM_API int am_addterm(am_Constraint *c, am_Id var, am_Num f);
AM_API int am_setrelation(am_Constraint *c, int relation);
AM_API int am_addconstant(am_Constraint *c, am_Num constant);
AM_API int am_setstrength(am_Constraint *c, am_Num strength);
AM_API int am_merge(am_Constraint *c, const am_Constraint *o, am_Num coef);

AM_API int  am_add(am_Constraint *c);
AM_API void am_remove(am_Constraint *c);

AM_API int    am_hasconstraint(const am_Constraint *c);
AM_API am_Id  am_terms(const am_Constraint *c, am_Id var, am_Num *coef);
AM_API int    am_relation(const am_Constraint *c);
AM_API am_Num am_constant(const am_Constraint *c);
AM_API am_Num am_strength(const am_Constraint *c);
AM_API am_Num am_error(const am_Constraint *c);

/* dump & load */

typedef struct am_Dumper am_Dumper;
typedef struct am_Loader am_Loader;

AM_API int        am_dump(am_Solver *S, am_Dumper *dumper);
AM_API am_Solver *am_load(am_Allocf *a, void *ud, am_Loader *l, size_t maxmem);

typedef const char *am_Name;

struct am_Dumper {
    am_Name (*var_name)(am_Dumper *D, unsigned i, am_Id var, am_Num *value);
    am_Name (*cons_name)(am_Dumper *D, unsigned i, am_Constraint *c);

    int (*writer)(am_Dumper *D, const void *buf, size_t len);
};

struct am_Loader {
    void (*solver)(am_Loader *L, const am_Solver *S);
    am_Num *(*load_var)(am_Loader *L, am_Name name, unsigned i, am_Id var);
    void (*load_cons)(am_Loader *L, am_Name name, unsigned i, am_Constraint *c);

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
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define AM_ID_MAX (~0u >> 2) /* largest id value, reserved for artificials */

#define AM_EXTERNAL (0)
#define AM_SLACK    (1)
#define AM_ERROR    (2)
#define AM_DUMMY    (3)

#define amV_isexternal(key)  ((key).u.type == AM_EXTERNAL)
#define amV_isslack(key)     ((key).u.type == AM_SLACK)
#define amV_iserror(key)     ((key).u.type == AM_ERROR)
#define amV_isdummy(key)     ((key).u.type == AM_DUMMY)
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

#define AM_STATIC_ASSERT(cond)      AM_SA_0(cond, am_SA_, __LINE__)
#define AM_SA_0(cond, prefix, line) AM_SA_1(cond, prefix, line)
#define AM_SA_1(cond, prefix, line) typedef char prefix##line[(cond) ? 1 : -1]

/* clang-format off */
#define amC(cond) do { if (!(cond)) return AM_DEBUG( \
    "%s:%d: %s\n", __FILE__, __LINE__, #cond), AM_FAILED; } while (0)
#define amE(expr) do { int r_; if ((r_ = (expr)) != AM_OK) return \
    AM_DEBUG("%s:%d: %s err %d\n", __FILE__, __LINE__, #expr, r_), \
    r_; } while (0)
/* clang-format on */

#if defined(_MSC_VER)
# define AM_INLINE static __forceinline
#elif defined(__GNUC__)
# define AM_INLINE static __inline__ __attribute__((always_inline))
#else
# define AM_INLINE static
#endif

AM_NS_BEGIN

typedef unsigned am_Size;

typedef union am_Symbol {
    unsigned bits; /* whole symbol, 0 = none */
    struct {
        unsigned id : AM_UNSIGNED_BITS - 2; /* symbol id, 0 = none */
        unsigned type : 2; /* AM_EXTERNAL, AM_SLACK, AM_ERROR or AM_DUMMY */
    } u;
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
    am_Size refs; /* tables still pointing into this block */
    size_t  size; /* buffer size in bytes */
    size_t  used; /* bytes taken out of it by amH_take */
    void   *buf;  /* storage, NULL when unused */
} am_Arena;

typedef struct am_Table {
    am_Size size;                              /* slot count, a power of two */
    am_Size count;                             /* live entry count */
    am_Size value_size : AM_UNSIGNED_BITS - 2; /* value size in bytes */
    am_Size arena_id : 2; /* 0 = heap, 1..2 = S->arena[arena_id - 1] */
    am_Size last_free;    /* first slot to scan for a free one */
    am_Key *keys;         /* slot keys, parallel to vals */
    void   *vals;         /* value slots */
} am_Table;

typedef struct am_Iterator {
    const am_Table *t;      /* table being walked */
    am_Symbol       key;    /* current key, id 0 = done */
    am_Size         offset; /* next slot index */
} am_Iterator;

typedef struct am_Row {
    am_Symbol infeasible_next; /* next queued symbol, {0,AM_DUMMY} ends it */
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
    am_Symbol  marker;     /* slack/error symbol, AM_EXTERNAL = not in solver */
    am_Symbol  other;      /* second error symbol, AM_EXTERNAL = none */
    unsigned   relation;   /* AM_LESSEQUAL, AM_EQUAL or AM_GREATEREQUAL */
    am_Num     strength;   /* AM_REQUIRED or a soft strength */
    am_Solver *S;          /* owning solver */
};

typedef struct am_Suggest {
    am_Size       age;        /* solver age when dirtyset was filled */
    am_Num        edit_value; /* current suggested value */
    am_Constraint constraint; /* the edit's hidden equality constraint */
    am_Symbol    *dirtyset;   /* rows touched by this edit, a vec */
} am_Suggest;

typedef struct am_Scratch {
    char           *bin;  /* load's copy of the scratch bytes */
    am_Constraint **cons; /* stream order -> public constraint */
} am_Scratch;

struct am_Solver {
    void       *ud;              /* allocator userdata */
    am_Allocf  *allocf;          /* allocator callback */
    am_Row      objective;       /* row being minimized */
    am_Table    vars;            /* symbol -> am_Var */
    am_Table    constraints;     /* symbol -> am_Constraint* */
    am_Table    suggests;        /* symbol -> am_Suggest* */
    am_Table    rows;            /* symbol -> am_Row */
    am_Arena    arena[2];        /* rows and expressions */
    am_Scratch *scratch;         /* non-NULL = tableau not materialized yet */
    am_Size     auto_update : 1; /* 1 = update bound values after each change */
    am_Size     age;             /* tableau generation, bumps on every change */
    am_Size     current;         /* last name handed out (one per owner) */
    am_Symbol   infeasible_rows; /* rows with a negative constant */
    am_Symbol   dirty_vars;      /* vars whose value changed, to be written */
    am_MemPool  conspool;        /* pool of am_Constraint */
    am_MemPool  editpool;        /* pool of am_Suggest */
    size_t      maxmem;          /* remaining load budget, 0 = unlimited */
};

/* number & symbol primitives */

/* clang-format off */
typedef union am_Bits { double f64; float f32; unsigned u32[2]; } am_Bits;

static int amN_approx(am_Num a, am_Num b)
{ return a > b ? a - b < AM_NUM_EPS : b - a < AM_NUM_EPS; }

static am_Symbol amS_var(am_Id id)
{ am_Symbol sym = {0}; if (id <= AM_ID_MAX) sym.u.id = id; return sym; }

static unsigned amS_new(am_Solver *S)
{ unsigned  id = ++S->current; if (id >= AM_ID_MAX) abort(); return id; }

static am_Symbol amS_null(void) { am_Symbol null = {0}; return null; }

static int amN_bigendian(void)
{ static const unsigned probe = 1; return *(const unsigned char *)&probe == 0; }

static unsigned amS_bswap(unsigned v)
{ return (v>>24) | ((v>>8) & 0xFF00u) | ((v<<8) & 0xFF0000u) | (v << 24); }
/* clang-format on */

static int amN_nearzero(am_Num a) { return amN_approx(a, 0.f); }
static int amN_negative(am_Num a) { return !amN_nearzero(a) && a < 0.f; }

/* memory */

/* clang-format off */
typedef struct am_Vec { am_Size cap, len; } am_Vec;

AM_INLINE void amP_put(am_MemPool *pool, void *obj)
{ *(void **)obj = pool->freed, pool->freed = obj; }

AM_INLINE am_Iterator amH_iter(const am_Table *t)
{ am_Iterator it; it.t = t, it.key = amS_null(), it.offset = 0; return it; }

AM_INLINE void amH_init(am_Table *t, size_t value_size)
{ memset(t, 0, sizeof(*t)), t->value_size = (am_Size)value_size; }

AM_INLINE am_Size amH_calcsize(am_Size request)
{ am_Size s = AM_MIN_HASHSIZE; while (s < request) s <<= 1; return s; }
/* clang-format on */

static void *amM_realloc(am_Solver *S, void *p, size_t n, size_t o, am_Type t) {
    size_t grow = n > o ? n - o : 0;
    void  *np;
    if (S->maxmem != 0 && grow >= S->maxmem) return NULL; /* fail on 0 */
    np = S->allocf(&S->ud, p, n, o, t);
    if (n != 0 && np == NULL) return NULL;
    if (S->maxmem != 0) S->maxmem += o - n;
    return np;
}

AM_INLINE void amP_init(am_MemPool *pool, size_t size) {
    memset(pool, 0, sizeof(*pool)), pool->size = size;
    assert(size >= sizeof(void *) && size % sizeof(void *) == 0);
    assert(AM_POOL_SIZE / size > 2);
}

static void amP_free(am_Solver *S, am_MemPool *pool) {
    const size_t offset = AM_POOL_SIZE - sizeof(void *);
    while (pool->pages != NULL) {
        void *next = *(void **)((char *)pool->pages + offset);
        amM_realloc(S, pool->pages, 0, AM_POOL_SIZE, am_TPool);
        pool->pages = next;
    }
    amP_init(pool, pool->size);
}

AM_INLINE void *amP_get(am_Solver *S, am_MemPool *pool) {
    const size_t size = pool->size;
    const size_t offset = AM_POOL_SIZE - sizeof(void *);
    char        *page, *end, *obj = (char *)pool->freed;
    if (obj != NULL) return (pool->freed = *(void **)obj), obj;
    page = (char *)amM_realloc(S, NULL, AM_POOL_SIZE, 0, am_TPool);
    if (page == NULL) return NULL;
    end = &page[offset], *(void **)end = pool->pages;
    pool->pages = page, page += size, end -= size;
    while ((page += size) <= end) *(void **)(page - size) = page;
    *(void **)(page - size) = pool->freed;
    return (pool->freed = (void *)((char *)pool->pages + size)), pool->pages;
}

#define amX_call_(S, a, n)  ((S), (void **)&(a), (am_Size)(n), sizeof(*(a)))
#define amX_grow(S, a, n)   amX_grow_ amX_call_(S, a, n)
#define amX_resize(S, a, n) amX_resize_ amX_call_(S, a, n)
#define amX_free(S, a)      amX_resize((S), (a), 0)
#define amX_hdr(a)          (assert(a), (am_Vec *)(a) - 1)
#define amX_cap(a)          ((a) ? amX_hdr(a)->cap : 0u)
#define amX_len(a)          ((a) ? amX_hdr(a)->len : 0u)
#define amX_setlen(a, n)    (amX_hdr(a)->len = (am_Size)(n))
#define amX_clear(a, n)     (memset((a), 0, amX_setlen(a, n) * sizeof(*(a))))

static int amX_resize_(am_Solver *S, void **pA, am_Size cap, size_t esz) {
    am_Vec *h = *pA ? amX_hdr(*pA) : NULL;
    size_t  bytes, old = h ? sizeof(am_Vec) + (size_t)h->cap * esz : 0;
    char   *np;
    if (cap == 0) return amM_realloc(S, h, 0, old, am_TVec), *pA = NULL, AM_OK;
    if ((size_t)cap > (~(size_t)0 - sizeof(am_Vec)) / esz) return AM_FAILED;
    bytes = sizeof(am_Vec) + (size_t)cap * esz;
    if ((np = (char *)amM_realloc(S, h, bytes, old, am_TVec)) == NULL)
        return AM_FAILED;
    h = (am_Vec *)np, h->cap = cap;
    if (old == 0) h->len = 0;
    return *pA = (void *)(h + 1), AM_OK;
}

AM_INLINE int amX_grow_(am_Solver *S, void **pA, am_Size n, size_t esz) {
    am_Size len = amX_len(*pA), cap = amX_cap(*pA), nc = cap < 4 ? 4 : cap;
    if (n <= cap - len) return AM_OK;
    if (n > ~0u - len) return AM_FAILED;
    n += len;
    while (nc < n) nc = nc > ~0u / 2 ? n : nc * 2;
    return amX_resize_(S, pA, nc, esz);
}

#define amH_slot(t, i) ((char *)(t)->vals + (i) * (t)->value_size)
#define amH_val(T, it) ((T *)amH_slot((it).t, (it).offset - 1))

AM_INLINE int amH_initarena(am_Solver *S, am_Arena *a, size_t size) {
    assert(a->buf == NULL), a->buf = amM_realloc(S, NULL, size, 0, am_TArena);
    return a->buf ? (a->size = size, a->used = 0, AM_OK) : AM_FAILED;
}

AM_INLINE void amH_ref(am_Arena *a, am_Table *t, am_Size n, size_t vs) {
    size_t bytes = (n = amH_calcsize(n)) * (sizeof(am_Key) + vs);
    assert(a->buf && a->used <= a->size && bytes <= a->size - a->used);
    t->size = n, t->count = 0, t->value_size = (am_Size)vs, t->last_free = 0;
    t->keys = (am_Key *)((char *)a->buf + a->used), t->vals = t->keys + n;
    a->refs += 1, a->used += bytes;
}

static void amH_unref(am_Solver *S, int id, int force) {
    am_Arena *a = &S->arena[id];
    if (!a->buf) return;
    if (!force && (assert(a->refs > 0), --a->refs > 0)) return;
    assert(a->refs == 0), amM_realloc(S, a->buf, 0, a->size, am_TArena);
    a->buf = NULL, a->size = a->used = 0;
}

AM_INLINE void amH_reset(am_Table *t) {
    t->last_free = t->count = 0;
    if (t->keys) memset(t->keys, 0, t->size * sizeof(am_Key));
}

AM_INLINE void amH_free(am_Solver *S, am_Table *t) {
    size_t hsize = t->size * (sizeof(am_Key) + t->value_size);
    if (hsize == 0) return;
    t->arena_id ? amH_unref(S, t->arena_id - 1, 0)
                : (void)amM_realloc(S, t->keys, 0, hsize, am_THash);
    amH_init(t, t->value_size);
}

AM_INLINE am_Size amH_next(am_Iterator *it) {
    am_Size cur = it->offset, size = it->t->size;
    while (cur < size && (it->key = it->t->keys[cur].sym).u.id == 0) cur++;
    if (cur >= size) return it->key = amS_null(), it->offset = 0;
    return it->offset = cur + 1;
}

AM_INLINE am_Size amH_find(const am_Table *t, am_Symbol sym) {
    am_Size cur;
    if (t->size == 0 || sym.u.id == 0) return 0;
    cur = sym.u.id & (t->size - 1);
    for (; t->keys[cur].sym.bits != sym.bits; cur += t->keys[cur].next)
        if (t->keys[cur].next == 0) return 0;
    return cur + 1;
}

AM_INLINE const void *amH_get(const am_Table *t, am_Symbol key) {
    am_Size o = amH_find(t, key);
    return o ? amH_slot(t, o - 1) : NULL;
}

AM_INLINE int amH_alloc(am_Table *t, am_Solver *S, size_t req, size_t vsz) {
    size_t  size, objs;
    am_Key *keys;
    amC(req <= (~(am_Size)0 >> 1));
    size = amH_calcsize((am_Size)req), objs = size * (sizeof(am_Key) + vsz);
    amC((keys = (am_Key *)amM_realloc(S, NULL, objs, 0, am_THash)) != NULL);
    t->size = (am_Size)size, t->value_size = (am_Size)vsz;
    t->arena_id = 0, t->keys = keys, t->vals = keys + size;
    return amH_reset(t), AM_OK;
}

AM_INLINE am_Size amH_avail(am_Table *t) {
    am_Size f = t->last_free;
    while (f < t->size && (t->keys[f].sym.bits || t->keys[f].next)) f += 1;
    return t->last_free = f + (f < t->size), f;
}

AM_INLINE am_Size amH_chain(am_Table *t, am_Size h, am_Size f) {
    int x = (int)h + t->keys[h].next;
    assert(t->keys[h].sym.bits && (t->keys[h].sym.u.id & (t->size - 1)) == h);
    t->keys[f].next = x == (int)h ? 0 : x - (int)f;
    return t->keys[h].next = (int)(f - h), f;
}

AM_INLINE void *amH_rawset(am_Table *t, am_Symbol key) {
    am_Key *ks = t->keys;
    int     mp = (int)(key.u.id & (t->size - 1)), othern;
    am_Size f;
    if (!ks[mp].sym.bits) return ks[mp].sym = key, amH_slot(t, mp);
    if ((f = amH_avail(t)) == t->size) return NULL;
    if ((othern = (int)(ks[mp].sym.u.id & (t->size - 1))) == mp)
        mp = (int)amH_chain(t, (am_Size)mp, f);
    else {
        assert(ks[othern].next != 0);
        while (othern + ks[othern].next != mp) othern += ks[othern].next;
        ks[othern].next = ((int)f - othern), ks[f] = ks[mp];
        memcpy(amH_slot(t, f), amH_slot(t, mp), t->value_size);
        if (ks[mp].next) ks[f].next += (mp - (int)f), ks[mp].next = 0;
    }
    return ks[mp].sym = key, amH_slot(t, mp);
}

AM_INLINE int amH_resize(am_Solver *S, am_Table *t, size_t request) {
    am_Table nt;
    am_Size  i, arena_id = t->arena_id;
    amE(amH_alloc(&nt, S, request, t->value_size));
    for (i = 0; i < t->size; ++i) {
        void *value;
        if (!t->keys[i].sym.u.id) continue;
        value = amH_rawset(&nt, t->keys[i].sym), nt.count += 1;
        assert(value != NULL), memcpy(value, amH_slot(t, i), t->value_size);
    }
    arena_id ? amH_unref(S, arena_id - 1, 0) : amH_free(S, t);
    return (*t = nt), AM_OK;
}

AM_INLINE void *amH_set(am_Solver *S, am_Table *t, am_Symbol key) {
    void *value;
    assert(key.u.id != 0 && amH_get(t, key) == NULL);
    if (!t->size && amH_resize(S, t, 0) != AM_OK) return NULL;
    if ((value = amH_rawset(t, key)) == NULL) {
        if (amH_resize(S, t, t->count * 2) != AM_OK) return NULL;
        value = amH_rawset(t, key); /* retry and must success */
    }
    return t->count += 1, assert(value != NULL), value;
}

AM_INLINE void amH_del(am_Table *t, const void *value) {
    size_t vsz = t->value_size;
    size_t offset = ((char *)value - (char *)t->vals) / (assert(vsz), vsz);
    t->count -= 1, t->keys[offset].sym = amS_null();
}

/* expression (row) */

#define amR_get(S, sym) (am_Row *)amH_get(&(S)->rows, sym)

/* clang-format off */
static void amR_init(am_Row *r)
{ memset(r, 0, sizeof(*r)), amH_init(&r->terms, sizeof(am_Num)); }

AM_INLINE void amR_add(am_Row *r, am_Num c)
{ r->constant += c; if (amN_nearzero(r->constant)) r->constant = 0.f; }
/* clang-format on */

AM_INLINE void amR_reset(am_Row *r) { r->constant = 0.f, amH_reset(&r->terms); }
AM_INLINE void amR_free(am_Solver *S, am_Row *r) { amH_free(S, &r->terms); }

static void amR_mul(am_Row *r, am_Num f) {
    am_Iterator it = amH_iter(&r->terms);
    if (r->constant *= f, amN_nearzero(r->constant)) r->constant = 0.f;
    while (amH_next(&it)) *amH_val(am_Num, it) *= f;
}

AM_INLINE void amR_div(am_Row *r, am_Num f) {
    am_Iterator it = amH_iter(&r->terms);
    if (r->constant /= f, amN_nearzero(r->constant)) r->constant = 0.f;
    while (amH_next(&it)) *amH_val(am_Num, it) /= f;
}

AM_INLINE int amR_addvar(am_Solver *S, am_Row *r, am_Symbol sym, am_Num value) {
    am_Num *t;
    if (sym.u.id == 0 || amN_nearzero(value)) return 0;
    if ((t = (am_Num *)amH_get(&r->terms, sym)))
        return amN_nearzero(*t += value) ? (amH_del(&r->terms, t), -1) : 0;
    return t = (am_Num *)amH_set(S, &r->terms, sym), assert(t), (*t = value), 1;
}

static int amR_delvar(am_Row *r, am_Symbol sym) {
    am_Num *t = (am_Num *)amH_get(&r->terms, sym);
    return t ? (amH_del(&r->terms, t), 1) : 0;
}

static void amR_addrow(am_Solver *S, am_Row *r, const am_Row *o, am_Num f) {
    am_Iterator it = amH_iter(&o->terms);
    amR_add(r, o->constant * f);
    while (amH_next(&it)) amR_addvar(S, r, it.key, *amH_val(am_Num, it) * f);
}

AM_INLINE void amR_elim(am_Solver *S, am_Row *d, am_Symbol o, const am_Row *r) {
    am_Num f, *t = (am_Num *)amH_get(&d->terms, o);
    if (t) f = *t, amH_del(&d->terms, t), amR_addrow(S, d, r, f);
}

/* variables & constraints */

#define amV_get(S, v) ((S) ? (am_Var *)amH_get(&(S)->vars, amS_var(v)) : NULL)

AM_INLINE int amC_init(am_Solver *S, am_Symbol sym, am_Constraint *c) {
    am_Constraint **ce = (am_Constraint **)amH_set(S, &S->constraints, sym);
    amC(ce != NULL);
    *ce = c, memset(c, 0, sizeof(*c)), c->S = S;
    c->sym = sym, c->marker.u.id = c->other.u.id = sym.u.id; /* owner's name */
    return amR_init(&(*ce)->expression), AM_OK;
}

AM_INLINE void amC_setup(am_Solver *S, am_Constraint *c) {
    int soft = (c->strength < AM_REQUIRED), eq = (c->relation == AM_EQUAL);
    c->marker.u.type = !eq ? AM_SLACK : (soft ? AM_ERROR : AM_DUMMY);
    c->other.u.type = soft ? AM_ERROR : AM_EXTERNAL;
    if (soft && eq && c->other.u.id == c->sym.u.id) c->other.u.id = amS_new(S);
}

AM_INLINE int amC_addterm(am_Constraint *c, am_Id var, am_Num value) {
    am_Symbol sym = amS_var(var);
    am_Var   *ve;
    amC((ve = (am_Var *)amH_get(&c->S->vars, sym)) != NULL);
    return ve->refcount += amR_addvar(c->S, &c->expression, sym, value), AM_OK;
}

static am_Suggest *amC_newedit(am_Solver *S, am_Id var, am_Num strength) {
    am_Symbol    sym = amS_var(var);
    int          ret;
    am_Var      *ve;
    am_Suggest **s;
    am_Row      *r;
    assert(!amN_nearzero(strength) && strength <= AM_STRONG);
    if ((ve = (am_Var *)amH_get(&S->vars, sym)) == NULL) return NULL;
    if (!(s = (am_Suggest **)amH_set(S, &S->suggests, sym))) return NULL;
    *s = (am_Suggest *)amP_get(S, &S->editpool);
    if (*s == NULL) return amH_del(&S->suggests, s), (am_Suggest *)0;
    memset(*s, 0, sizeof(**s)), sym.u.type = AM_DUMMY;
    (*s)->edit_value = (r = amR_get(S, amS_var(var))) ? r->constant : 0.f;
    if (!amV_isdummy(ve->next)) (*s)->edit_value = *ve->pvalue;
    amC_init(S, sym, &(*s)->constraint), (*s)->constraint.strength = strength;
    am_setrelation(&(*s)->constraint, AM_EQUAL);
    am_addterm(&(*s)->constraint, var, 1.0f);
    am_addconstant(&(*s)->constraint, -(*s)->edit_value);
    return ret = am_add(&(*s)->constraint), assert(ret == AM_OK), (void)ret, *s;
}

static void amC_freesuggest(am_Solver *S, am_Suggest *s) {
    amX_free(S, s->dirtyset), am_delconstraint(&s->constraint);
    amP_put(&S->editpool, s);
}

/* Cassowary algorithm */

static int amA_takerow(am_Solver *S, am_Symbol sym, am_Row *dst) {
    am_Row *r = amR_get(S, sym);
    if (r == NULL) return 0;
    amH_del(&S->rows, r);
    return (dst->constant = r->constant, dst->terms = r->terms), 1;
}

AM_INLINE void amA_solvefor(am_Solver *S, am_Row *r, am_Symbol i, am_Symbol o) {
    am_Num *t = (am_Num *)amH_get(&r->terms, i), f = *t;
    assert(i.bits != o.bits && !amN_nearzero(f)), amH_del(&r->terms, t);
    if (f != -1.0f) amR_div(r, -f); /* a/a is exact; a * (1/a) is not */
    if (o.u.id != 0) amR_addvar(S, r, o, 1.0f / f);
}

AM_INLINE void amA_markdirty(am_Solver *S, am_Symbol var) {
    am_Var *ve = (am_Var *)amH_get(&S->vars, var);
    if (assert(ve), amV_isdummy(ve->next)) return;
    ve->next.u.id = S->dirty_vars.u.id, ve->next.u.type = AM_DUMMY;
    S->dirty_vars = var, ve->refcount += 1;
}

AM_INLINE int amA_infeasible(am_Solver *S, am_Symbol sym, am_Row *r) {
    if (amV_isexternal(sym)) return amA_markdirty(S, sym), 0;
    if (!amV_isexternal(r->infeasible_next)) return 1; /* already queued */
    if (!amN_negative(r->constant)) return 0;
    r->infeasible_next = S->infeasible_rows;
    if (r->infeasible_next.u.id == 0) r->infeasible_next.u.type = AM_DUMMY;
    return (S->infeasible_rows = sym), 1;
}

AM_INLINE void amA_substitute(am_Solver *S, am_Symbol var, am_Row *expr) {
    am_Iterator it = amH_iter(&S->rows);
    while (amH_next(&it)) {
        am_Row *r = amH_val(am_Row, it);
        amR_elim(S, r, var, expr), amA_infeasible(S, it.key, r);
    }
    amR_elim(S, &S->objective, var, expr);
}

AM_INLINE int amA_putrow(am_Solver *S, am_Symbol sym, const am_Row *src) {
    am_Row *r = (am_Row *)amH_set(S, &S->rows, sym);
    assert(r != NULL), r->infeasible_next = amS_null();
    if (amV_isexternal(sym)) amA_markdirty(S, sym);
    return (r->constant = src->constant, r->terms = src->terms), AM_OK;
}

static void amA_flushinfeasible(am_Solver *S) {
    am_Symbol next, cur = S->infeasible_rows;
    for (; cur.u.id != 0; cur = next) {
        am_Row *r = amR_get(S, cur);
        assert(r != NULL && !amN_negative(r->constant)); /* must be stale */
        next = r ? r->infeasible_next : amS_null();
        if (r) r->infeasible_next = amS_null(); /* unmark for a later push */
    }
    S->infeasible_rows = amS_null();
}

static am_Symbol amA_findleave(am_Solver *S, am_Symbol enter) {
    am_Num     *t, ratio, min = AM_NUM_MAX;
    am_Symbol   leave = amS_null();
    am_Iterator it = amH_iter(&S->rows);
    while (amH_next(&it)) {
        am_Row *r = amH_val(am_Row, it);
        if (amV_isexternal(it.key)) continue;
        if (!(t = (am_Num *)amH_get(&r->terms, enter)) || *t > 0.f) continue;
        ratio = -r->constant / *t;
        if (ratio < min || (amN_approx(ratio, min) && it.key.u.id < leave.u.id))
            min = ratio, leave = it.key;
    }
    return leave;
}

AM_INLINE void amA_optimize(am_Solver *S, am_Row *obj) {
    amA_flushinfeasible(S);
    for (;;) {
        am_Symbol   enter = amS_null(), leave = amS_null();
        am_Iterator it = amH_iter(&obj->terms);
        am_Row      tmp;
        assert(S->infeasible_rows.u.id == 0);
        while (amH_next(&it))
            if (!amV_isdummy(it.key) && amN_negative(*amH_val(am_Num, it)))
                break;
        if (it.key.u.id == 0) return;
        enter = it.key, leave = amA_findleave(S, enter);
        if (leave.u.id == 0) { /* feasible means bounded, so this is noise */
            amR_delvar(obj, enter);
            continue;
        }
        amA_takerow(S, leave, &tmp);
        amA_solvefor(S, &tmp, enter, leave);
        amA_substitute(S, enter, &tmp);
        if (obj != &S->objective) amR_elim(S, obj, enter, &tmp);
        amA_putrow(S, enter, &tmp);
    }
}

AM_INLINE void amA_mergerow(am_Solver *S, am_Row *r, am_Symbol var, am_Num f) {
    am_Row *oldrow = amR_get(S, var);
    oldrow ? amR_addrow(S, r, oldrow, f) : (void)amR_addvar(S, r, var, f);
}

static am_Row amA_makerow(am_Solver *S, am_Constraint *c) {
    am_Iterator it = amH_iter(&c->expression.terms);
    am_Row      r;
    amR_init(&r), r.constant = c->expression.constant;
    while (amH_next(&it)) {
        amA_markdirty(S, it.key);
        amA_mergerow(S, &r, it.key, *amH_val(am_Num, it));
    }
    amR_addvar(S, &r, c->marker, -1.0f);
    if (c->strength < AM_REQUIRED) {
        if (c->relation == AM_EQUAL)
            amR_addvar(S, &S->objective, c->marker, c->strength);
        amR_addvar(S, &r, c->other, 1.0f);
        amR_addvar(S, &S->objective, c->other, c->strength);
    }
    if (r.constant < 0.f) amR_mul(&r, -1.0f);
    return r;
}

static int amA_addartificial(am_Solver *S, am_Row *r, am_Constraint *c) {
    am_Symbol   a, enter = amS_null();
    int         ret = AM_OK;
    am_Row      tmp;
    am_Iterator it;
    a.u.id = AM_ID_MAX, a.u.type = AM_SLACK; /* reserved, not an owner's name */
    amR_init(&tmp), amR_addrow(S, &tmp, r, 1.0f);
    amA_putrow(S, a, r), amA_optimize(S, &tmp);
    if (!amN_nearzero(tmp.constant)) ret = AM_UNBOUND, am_remove(c);
    if (amR_free(S, &tmp), amA_takerow(S, a, &tmp)) {
        if (tmp.terms.count == 0) return amR_free(S, &tmp), ret;
        it = amH_iter(&tmp.terms);
        while (amH_next(&it) && !amV_ispivotable(it.key)) continue;
        if (it.offset) enter = it.key;
        if (enter.u.id == 0) return amR_free(S, &tmp), AM_UNBOUND;
        amA_solvefor(S, &tmp, enter, a);
        amA_substitute(S, enter, &tmp);
        amA_putrow(S, enter, &tmp);
    }
    it = amH_iter(&S->rows);
    while (amH_next(&it)) r = amH_val(am_Row, it), amR_delvar(r, a);
    return amR_delvar(&S->objective, a), ret;
}

static int amA_tryaddrow(am_Solver *S, am_Row *r, am_Constraint *c) {
    am_Symbol   subject = amS_null();
    am_Iterator it = amH_iter(&r->terms);
    while (amH_next(&it) && !amV_isexternal(it.key)) continue;
    if (it.offset) subject = it.key;
    if (subject.u.id == 0 && amV_ispivotable(c->marker))
        if (*(am_Num *)amH_get(&r->terms, c->marker) < 0.f) subject = c->marker;
    if (subject.u.id == 0 && amV_ispivotable(c->other))
        if (*(am_Num *)amH_get(&r->terms, c->other) < 0.f) subject = c->other;
    if (subject.u.id == 0) {
        it = amH_iter(&r->terms);
        while (amH_next(&it) && amV_isdummy(it.key)) continue;
        if (!it.offset) subject = c->marker;
        if (!it.offset && !amN_nearzero(r->constant))
            return amR_free(S, r), AM_UNSATISFIED;
    }
    if (subject.u.id == 0) return amA_addartificial(S, r, c);
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
    c->marker.u.type = c->other.u.type = AM_EXTERNAL;
}

static am_Symbol amA_leavingrow(am_Solver *S, am_Symbol marker) {
    am_Symbol   first = amS_null(), second = amS_null(), third = amS_null();
    am_Num     *t, ratio, r1 = AM_NUM_MAX, r2 = AM_NUM_MAX;
    am_Iterator it = amH_iter(&S->rows);
    while (amH_next(&it)) {
        am_Row *r = amH_val(am_Row, it);
        if ((t = (am_Num *)amH_get(&r->terms, marker)) == NULL) continue;
        if (amV_isexternal(it.key))
            third = it.key;
        else if (*t < 0.f) {
            if ((ratio = -r->constant / *t) < r1) r1 = ratio, first = it.key;
        } else {
            if ((ratio = r->constant / *t) < r2) r2 = ratio, second = it.key;
        }
    }
    return first.u.id ? first : second.u.id ? second : third;
}

static void amA_cachedsuggest(am_Solver *S, am_Suggest *s, am_Num delta) {
    am_Symbol marker = s->constraint.marker;
    am_Size   i, len;
    for (i = 0, len = amX_len(s->dirtyset); i < len; ++i) {
        am_Row *r = amR_get(S, s->dirtyset[i]);
        am_Num *t = (am_Num *)amH_get(&(assert(r), r)->terms, marker);
        assert(t != NULL), amR_add(r, *t * delta);
        if (amA_infeasible(S, s->dirtyset[i], r)) s->age = 0;
    }
}

static int amA_editdelta(am_Solver *S, am_Suggest *s, am_Num delta) {
    am_Iterator    it = amH_iter(&S->rows);
    am_Constraint *c = &s->constraint;
    am_Row        *r;
    am_Num        *t;
    if ((r = amR_get(S, c->marker)) != NULL)
        return amR_add(r, -delta), amA_infeasible(S, c->marker, r), 0;
    if ((r = amR_get(S, c->other)) != NULL)
        return amR_add(r, delta), amA_infeasible(S, c->other, r), 0;
    if (s->age == S->age) return amA_cachedsuggest(S, s, delta), 0;
    if (s->age = S->age, s->dirtyset) amX_setlen(s->dirtyset, 0);
    while (amH_next(&it)) {
        r = amH_val(am_Row, it);
        if (!(t = (am_Num *)amH_get(&r->terms, c->marker))) continue;
        amR_add(r, *t * delta);
        if (amA_infeasible(S, it.key, r)) s->age = 0;
        if (s->age && amX_grow(S, s->dirtyset, 1) != AM_OK) s->age = 0;
        if (s->age) s->dirtyset[amX_hdr(s->dirtyset)->len++] = it.key;
    }
    return 1;
}

static void amA_invalidateedits(am_Solver *S, const am_Row *r) {
    am_Iterator  it = amH_iter(&r->terms);
    am_Suggest **se;
    while (amH_next(&it)) {
        if (!amV_iserror(it.key)) continue;
        se = (am_Suggest **)amH_get(&S->suggests, amS_var(it.key.u.id));
        if (!se) continue;
        assert((*se)->constraint.marker.u.id == it.key.u.id), (*se)->age = 0;
    }
}

static void amA_dualoptimize(am_Solver *S) {
    am_Symbol   leave, enter = amS_null();
    am_Num      ratio, min_ratio = AM_NUM_MAX;
    am_Row      tmp, *r = amR_get(S, S->infeasible_rows);
    am_Iterator it = amH_iter(&(assert(r), r)->terms);
    leave = S->infeasible_rows, S->infeasible_rows = r->infeasible_next;
    r->infeasible_next = amS_null();
    if (!amN_negative(r->constant)) return;
    while (amH_next(&it)) {
        am_Num *objt, t = *amH_val(am_Num, it);
        if (amV_isdummy(it.key) || t <= 0.f) continue;
        objt = (am_Num *)amH_get(&S->objective.terms, it.key);
        ratio = objt ? *objt / t : 0.f;
        if (min_ratio > ratio) min_ratio = ratio, enter = it.key;
    }
    assert(enter.u.id != 0);
    amA_takerow(S, leave, &tmp);
    amA_solvefor(S, &tmp, enter, leave), amA_invalidateedits(S, &tmp);
    amA_substitute(S, enter, &tmp);
    amA_putrow(S, enter, &tmp);
}

/* dump */

#define AM_TERM_SIZE ((am_Size)(sizeof(am_Size) + sizeof(am_Num)))
#define AM_NAME_LEN  255

#ifndef AM_BUF_LEN
# define AM_BUF_LEN 4096
#endif /* AM_BUF_LEN */

AM_STATIC_ASSERT(AM_BUF_LEN >= 16);

typedef struct am_Dump {
    am_Solver *S;                  /* solver being dumped */
    am_Size   *map;                /* (id << 2 | type) -> stream symbol bits */
    am_Size    total, seq, nc, ns; /* owners and cursors */
    int        idx;                /* index of each element in the dump */
    am_Dumper *dumper;             /* caller callbacks */
    char      *p, buf[AM_BUF_LEN]; /* staging buffer and next free pointer */
} am_Dump;

typedef struct am_DCache {
    am_Size *keys; /* row key bits in stream order */
    am_Size  var;  /* variable seq */
} am_DCache;

typedef struct am_DScratch {
    am_DCache *cache; /* one entry per written edit */
    am_Size   *mark;  /* source variable id -> cache index + 1 */
    char      *bin;   /* scratch bytes being built */
    size_t     rec;   /* next record header offset */
} am_DScratch;

/* clang-format off */
AM_INLINE int amD_flush(am_Dump *X, am_Size len)
{ return X->dumper->writer(X->dumper, X->p = X->buf, len); }

AM_INLINE char *amD_u32le(char *p, am_Size n)
{ if (amN_bigendian()) n = amS_bswap(n); return memcpy(p, &n, 4), p + 4; }

AM_INLINE char *amD_u32be(char *p, am_Size n)
{ if (!amN_bigendian()) n = amS_bswap(n); return memcpy(p, &n, 4), p + 4; }

AM_INLINE am_Size *amD_slot(am_Dump *X, am_Symbol sym)
{ return &X->map[((unsigned)sym.u.id << 2) | sym.u.type]; }

AM_INLINE am_Size amD_sym(am_Size id, unsigned ty)
{ return id | (ty << (AM_UNSIGNED_BITS - 2)); }
/* clang-format on */

AM_INLINE int amD_reserve(am_Dump *X, am_Size n) {
    if ((am_Size)(X->p - X->buf) + n <= AM_BUF_LEN) return AM_OK;
    return X->p != X->buf ? amD_flush(X, (am_Size)(X->p - X->buf)) : AM_OK;
}

AM_INLINE int amD_head(am_Dump *X, unsigned tag, am_Size n, unsigned len) {
    amE(amD_reserve(X, 1 + len));
    if (!amN_bigendian()) n = amS_bswap(n);
    *X->p = (char)tag, memcpy(X->p + 1, (char *)&n + 4 - len, len);
    return (X->p += 1 + len), AM_OK;
}

AM_INLINE int amD_u32(am_Dump *X, am_Size n) {
    if (n <= 0x7F) return amD_head(X, (unsigned)n, 0, 0);
    if (n <= 0xFF) return amD_head(X, 0xCC, n, 1);
    return n <= 0xFFFF ? amD_head(X, 0xCD, n, 2) : amD_head(X, 0xCE, n, 4);
}

AM_INLINE int amD_count(am_Dump *X, am_Size n) {
    if (n < 16) return amD_head(X, 0x90 + (unsigned)n, 0, 0);
    return n <= 0xFFFF ? amD_head(X, 0xDC, n, 2) : amD_head(X, 0xDD, n, 4);
}

AM_INLINE int amD_num(am_Dump *X, am_Num n) {
    const int hi = !amN_bigendian(), is32 = sizeof(am_Num) < sizeof(double);
    am_Bits   u;
    amE(amD_reserve(X, 1 + (am_Size)sizeof(am_Num)));
    *X->p++ = (unsigned char)(sizeof(am_Num) < sizeof(double) ? 0xCA : 0xCB);
    is32 ? (u.f32 = (float)n) : (u.f64 = (double)n);
    if (is32) return X->p = amD_u32be(X->p, u.u32[0]), AM_OK;
    return X->p = amD_u32be(amD_u32be(X->p, u.u32[hi]), u.u32[!hi]), AM_OK;
}

AM_INLINE int amD_string(am_Dump *X, am_Name name) {
    am_Dumper *d = X->dumper;
    size_t     namelen = name ? strlen(name) : ((name = ""), 0);
    amC(namelen <= AM_NAME_LEN && namelen <= 0xFF);
    amE(namelen < 32 ? amD_head(X, 0xA0 + (unsigned)namelen, 0, 0)
                     : amD_head(X, 0xD9, (am_Size)namelen, 1));
    if ((X->p - X->buf) + namelen > AM_BUF_LEN)
        amE(amD_flush(X, (am_Size)(X->p - X->buf)));
    if (namelen > AM_BUF_LEN) return d->writer(d, name, namelen);
    return memcpy(X->p, name, namelen), (X->p += namelen), AM_OK;
}

AM_INLINE int amD_var(am_Dump *X, am_Iterator *it) {
    const am_Row *r = (const am_Row *)amH_get(&X->S->rows, it->key);
    am_Name (*f)(am_Dumper *, unsigned, am_Id, am_Num *) = X->dumper->var_name;
    am_Num *pv = amH_val(am_Var, *it)->pvalue;
    *amD_slot(X, it->key) = amD_sym(++X->seq, AM_EXTERNAL);
    amE(amD_count(X, 2)); /* [value, name] */
    amE(amD_num(X, r ? r->constant : 0.f));
    return amD_string(X, f ? f(X->dumper, X->idx++, it->key.u.id, pv) : NULL);
}

AM_INLINE int amD_cons(am_Dump *X, am_Iterator *it) {
    am_Constraint *c = *amH_val(am_Constraint *, *it);
    am_Name (*f)(am_Dumper *, unsigned, am_Constraint *) = X->dumper->cons_name;
    if (amV_isdummy(c->sym)) return AM_OK;
    amE(amD_count(X, 5)); /* [strength, relation, in_solver, constant, name] */
    amE(amD_num(X, c->strength));
    amE(amD_u32(X, c->relation));
    amE(amD_u32(X, amV_isexternal(c->marker) ? 0 : 1));
    amE(amD_num(X, c->expression.constant));
    amE(amD_string(X, f ? f(X->dumper, X->idx++, c) : NULL));
    *amD_slot(X, c->sym) = amD_sym(++X->seq, AM_EXTERNAL);
    if (amV_isexternal(c->marker)) return AM_OK;
    *amD_slot(X, c->marker) = amD_sym(X->seq, c->marker.u.type);
    if (amV_isexternal(c->other)) return AM_OK;
    if (c->relation == AM_EQUAL && c->strength < AM_REQUIRED) ++X->seq;
    return *amD_slot(X, c->other) = amD_sym(X->seq, c->other.u.type), AM_OK;
}

AM_INLINE int amD_edit(am_Dump *X, am_Iterator *it) {
    am_Suggest *s = *amH_val(am_Suggest *, *it);
    am_Symbol   sym = it->key, other = s->constraint.other;
    am_Size     ref = *amD_slot(X, sym), var = ref & AM_ID_MAX;
    if (amV_isexternal(s->constraint.marker)) return AM_OK; /* not in solver */
    amE(amD_count(X, 3)); /* [var_seq, strength, value] */
    amE(amD_u32(X, var));
    amE(amD_num(X, s->constraint.strength));
    amE(amD_num(X, s->edit_value));
    sym.u.type = AM_ERROR, *amD_slot(X, sym) = amD_sym(var, AM_ERROR);
    return *amD_slot(X, other) = amD_sym(++X->seq, other.u.type), AM_OK;
}

AM_INLINE char *amD_numle(char *p, am_Num n) {
    const int hi = !amN_bigendian(), is32 = sizeof(am_Num) < sizeof(double);
    am_Bits   u;
    if (is32) return u.f32 = (float)n, amD_u32le(p, u.u32[0]);
    return u.f64 = (double)n, amD_u32le(amD_u32le(p, u.u32[!hi]), u.u32[hi]);
}

AM_INLINE int amD_initcache(am_Dump *X, am_DScratch *D) {
    am_Iterator it = amH_iter(&X->S->suggests);
    am_Size     bits, i = 0;
    amE(amX_resize(X->S, D->cache, X->ns));
    while (amH_next(&it)) {
        am_Suggest *s = *amH_val(am_Suggest *, it);
        if (amV_isexternal(s->constraint.marker)) continue;
        bits = *amD_slot(X, it.key), assert(bits != 0 && i < X->ns);
        D->cache[i].keys = NULL, D->cache[i].var = bits & AM_ID_MAX;
        D->mark[it.key.u.id] = ++i;
    }
    return assert(i == X->ns), amX_setlen(D->cache, i), AM_OK;
}

AM_INLINE int amD_rec(am_Dump *X, am_DScratch *D, const am_Row *r, am_Size b) {
    am_Iterator it = amH_iter(&r->terms);
    size_t      rec = D->rec, unit = AM_TERM_SIZE;
    char       *p;
    amE(amX_grow(X->S, D->bin, ((size_t)r->terms.count + 1) * unit));
    D->rec += sizeof(am_Size), p = D->bin + amX_len(D->bin);
    if (b != amD_sym(0, AM_DUMMY)) p = amD_numle(amD_u32le(p, b), r->constant);
    while (amH_next(&it)) {
        am_Size k, ref = *amD_slot(X, it.key);
        if (!ref) continue;
        if (b && amV_iserror(it.key) && (k = D->mark[it.key.u.id]) != 0) {
            am_DCache *dc = &D->cache[k - 1];
            amE(amX_grow(X->S, dc->keys, 1));
            dc->keys[amX_hdr(dc->keys)->len++] = b;
        }
        p = amD_numle(amD_u32le(p, ref), *amH_val(am_Num, it));
    }
    amD_u32le(D->bin + rec, (am_Size)(p - D->bin) - amX_len(D->bin));
    return amX_setlen(D->bin, (am_Size)(p - D->bin)), AM_OK;
}

static int amD_terms(am_Dump *X, am_DScratch *D) {
    am_Solver     *S = X->S;
    am_Iterator    it = amH_iter(&S->rows);
    am_Size        bits, nrows = 0;
    size_t         lens;
    am_Constraint *c;
    amE(amX_resize(S, D->mark, (am_Size)S->current + 1));
    amX_clear(D->mark, (am_Size)S->current + 1);
    if (X->ns) amE(amD_initcache(X, D));
    while (amH_next(&it)) nrows += *amD_slot(X, it.key) != 0;
    lens = sizeof(am_Size) * ((size_t)nrows + 1 + X->nc + X->ns);
    amE(amX_grow(S, D->bin, sizeof(am_Size) + lens));
    D->rec = sizeof(am_Size), amX_setlen(D->bin, D->rec + lens);
    amE(amD_rec(X, D, &S->objective, 0));
    while (amH_next(&it))
        if ((bits = *amD_slot(X, it.key)))
            amE(amD_rec(X, D, amH_val(am_Row, it), bits));
    it = amH_iter(&S->constraints);
    while (amH_next(&it))
        if (!amV_isdummy((c = *amH_val(am_Constraint *, it))->sym))
            amE(amD_rec(X, D, &c->expression, amD_sym(0, AM_DUMMY)));
    return amD_u32le(D->bin, nrows + 1), AM_OK;
}

static int amD_scratch(am_Dump *X, am_DScratch *D) {
    size_t  cs = sizeof(am_Size) * X->ns;
    am_Size i, j, len;
    char   *p;
    amE(amD_terms(X, D));
    for (i = 0; i < X->ns; ++i)
        cs += sizeof(am_Size) * amX_len(D->cache[i].keys);
    amE(amX_grow(X->S, D->bin, cs));
    p = D->bin + amX_len(D->bin);
    for (i = 0; i < X->ns; ++i) {
        am_DCache *c = &D->cache[i];
        amD_u32le(D->bin + D->rec + 4 * i, 4 + 4 * amX_len(c->keys));
        p = amD_u32le(p, c->var);
        for (j = 0; j < amX_len(c->keys); ++j) p = amD_u32le(p, c->keys[j]);
    }
    len = amX_setlen(D->bin, (am_Size)(p - D->bin));
    amE(len <= 0xFF     ? amD_head(X, 0xC4, len, 1)
        : len <= 0xFFFF ? amD_head(X, 0xC5, len, 2)
                        : amD_head(X, 0xC6, len, 4));
    if (X->p > X->buf) amE(amD_flush(X, (am_Size)(X->p - X->buf)));
    return X->dumper->writer(X->dumper, D->bin, len);
}

AM_INLINE int amD_body(am_Dump *X) {
    am_Solver  *S = X->S;
    am_Iterator it;
    am_DScratch D;
    am_Size     i;
    int         ret;
    amE(amD_count(X, 6)); /* [meta, total, vars, cons, edits, scratch] */
    amE(amD_count(X, 3)); /* ["amoeba", 5, num_size] */
    amE(amD_string(X, "amoeba"));
    amE(amD_u32(X, 5));
    amE(amD_u32(X, (am_Size)sizeof(am_Num)));
    amE(amD_u32(X, X->total));
    amE((X->idx = 0, it = amH_iter(&S->vars), amD_count(X, S->vars.count)));
    while (amH_next(&it)) amE(amD_var(X, &it));
    amE((X->idx = 0, it = amH_iter(&S->constraints), amD_count(X, X->nc)));
    while (amH_next(&it)) amE(amD_cons(X, &it));
    amE((it = amH_iter(&S->suggests), amD_count(X, X->ns)));
    while (amH_next(&it)) amE(amD_edit(X, &it));
    memset(&D, 0, sizeof(D)), ret = amD_scratch(X, &D);
    for (i = 0; D.cache && i < X->ns; ++i) amX_free(S, D.cache[i].keys);
    return amX_free(S, D.cache), amX_free(S, D.mark), amX_free(S, D.bin), ret;
}

/* load */

typedef struct am_Load {
    am_Solver  *S;                    /* solver being filled */
    am_Loader  *loader;               /* caller callbacks */
    const char *p, *e;                /* chunk cursor, chunk end */
    char        buf[AM_NAME_LEN + 1]; /* buffer for integer and string */
} am_Load;

#define amL_octet(x)          ((unsigned)(x) & 0xFF)
#define amL_rm(X)             ((X)->e != (X)->p ? (X)->e - (X)->p : 0)
#define amL_vint(X, pv, cond) ((cond) ? amL_u16be(X, (pv)) : amL_u32be(X, (pv)))
#define amL_EC(e, c)          amC((e) == AM_OK && (c))

AM_INLINE int amL_window(am_Load *X) {
    size_t n;
    if (amL_rm(X)) return AM_OK;
    X->p = X->loader->reader(X->loader, &n);
    return X->p && n ? (X->e = X->p + n, AM_OK) : AM_FAILED;
}

/* clang-format off */
AM_INLINE int amL_u8(am_Load *X, unsigned *pv)
{ amE(amL_window(X)); return (*pv = amL_octet(*X->p++)), AM_OK; }

AM_INLINE unsigned amL_u32le(const char *p)
{ unsigned v; return memcpy(&v,p,sizeof(v)), amN_bigendian()?amS_bswap(v):v; }

static am_Symbol amL_sym(const char *p)
{ am_Symbol sym; sym.bits = amL_u32le(p); return sym; }
/* clang-format on */

AM_INLINE int amL_read(am_Load *X, char *dst, size_t n) {
    size_t k, left;
    for (; n; X->p += k, dst += k, n -= k) {
        amE(amL_window(X));
        left = amL_rm(X), memcpy(dst, X->p, k = left < n ? left : n);
    }
    return AM_OK;
}

AM_INLINE int amL_u16be(am_Load *X, unsigned *pv) {
    const char *p = X->buf;
    amE(amL_rm(X) < 2 ? amL_read(X, X->buf, 2) : (p = X->p, X->p += 2, AM_OK));
    return (*pv = amL_octet(*p) << 8 | amL_octet(p[1])), AM_OK;
}

AM_INLINE int amL_u32be(am_Load *X, unsigned *pv) {
    const char *b = X->buf;
    amE(amL_rm(X) < 4 ? amL_read(X, X->buf, 4) : (b = X->p, X->p += 4, AM_OK));
    if (memcpy(pv, b, sizeof(*pv)), !amN_bigendian()) *pv = amS_bswap(*pv);
    return AM_OK;
}

AM_INLINE int amL_u32(am_Load *X, am_Size *pv) {
    unsigned v;
    amL_EC(amL_u8(X, &v), v <= 0x7F || (v >= 0xCC && v <= 0xCE));
    if (v <= 0x7F) return (*pv = (am_Size)v), AM_OK;
    return v == 0xCC ? amL_u8(X, pv) : amL_vint(X, pv, v == 0xCD);
}

AM_INLINE int amL_count(am_Load *X, am_Size *pv) {
    unsigned v;
    amL_EC(amL_u8(X, &v), (v >= 0x90 && v <= 0x9F) || v == 0xDC || v == 0xDD);
    if (v >= 0x90 && v <= 0x9F) return (*pv = (am_Size)(v - 0x90)), AM_OK;
    return amL_vint(X, pv, v == 0xDC);
}

AM_INLINE int amL_num(am_Load *X, am_Num *pv) {
    const int hi = !amN_bigendian();
    am_Bits   u;
    unsigned  v;
    amL_EC(amL_u8(X, &v), v == 0xCA || v == 0xCB);
    amE(amL_u32be(X, &u.u32[0]));
    if (v == 0xCA) return *pv = (am_Num)u.f32, AM_OK;
    amE(amL_u32be(X, &u.u32[hi ? (u.u32[1] = u.u32[0], 0) : 1]));
    return *pv = (am_Num)u.f64, AM_OK;
}

AM_INLINE int amL_string(am_Load *X, const char **ps) {
    unsigned v, l;
    amL_EC(amL_u8(X, &v), (v & 0xE0) == 0xA0 || v == 0xD9); /* str8 only */
    if ((l = v - 0xA0) > 31) amL_EC(amL_u8(X, &v), (l = v) <= AM_NAME_LEN);
    X->buf[l] = 0, ps && (*ps = l ? X->buf : NULL);
    return amL_read(X, X->buf, l);
}

AM_INLINE int amL_var(am_Load *X, am_Size i) {
    am_Symbol   sym = amS_var(amS_new(X->S));
    am_Size     count;
    am_Num     *pv, v;
    const char *name;
    am_Var     *ve;
    amL_EC(amL_count(X, &count), count == 2); /* [value, name] */
    amE(amL_num(X, &v));
    amE(amL_string(X, &name));
    amC((pv = X->loader->load_var(X->loader, name, i, sym.u.id)));
    amC((*pv = v, ve = (am_Var *)amH_set(X->S, &X->S->vars, sym)));
    return (ve->next = amS_null(), ve->pvalue = pv, ve->refcount = 1), AM_OK;
}

AM_INLINE int amL_cons(am_Load *X, am_Scratch *sc, am_Size i) {
    am_Constraint *c;
    am_Num         strength, constant;
    am_Size        count, relation, insolver;
    const char    *name;
    amL_EC(amL_count(X, &count), count == 5);
    amL_EC(amL_num(X, &strength), strength > 0 && strength <= AM_REQUIRED);
    amL_EC(amL_u32(X, &relation), relation <= AM_GREATEREQUAL);
    amL_EC(amL_u32(X, &insolver), insolver <= 1);
    amE(amL_num(X, &constant));
    amE(amL_string(X, &name));
    amC((c = sc->cons[i] = am_newconstraint(X->S, strength)));
    if (X->loader->load_cons) X->loader->load_cons(X->loader, name, i, c);
    c->relation = relation, c->expression.constant = constant;
    if (insolver) amC_setup(X->S, c);
    return AM_OK;
}

static int amL_initcons(am_Load *X, am_Scratch *sc, am_Size n, am_Size total) {
    amC(n <= total - X->S->vars.count);
    if (n == 0) return AM_OK;
    amC(n <= (AM_ID_MAX - 1 - X->S->vars.count) / 2);
    amE(amX_resize(X->S, sc->cons, n));
    return (amX_setlen(sc->cons, n), amH_resize(X->S, &X->S->constraints, n));
}

AM_INLINE int amL_edit(am_Load *X) {
    am_Solver  *S = X->S;
    am_Suggest *s, **sv;
    am_Symbol   sym, csym;
    am_Num      strength, value;
    am_Size     var, count;
    amL_EC(amL_count(X, &count), count == 3); /* [var_seq, strength, value] */
    amL_EC(amL_u32(X, &var), var >= 1 && var <= S->vars.count);
    amL_EC(amL_num(X, &strength), strength > 0 && strength <= AM_STRONG);
    amL_EC(amL_num(X, &value), !amH_get(&S->suggests, sym = amS_var(var)));
    amC((s = (am_Suggest *)amP_get(S, &S->editpool)));
    memset(s, 0, sizeof(*s)), s->age = 0, s->edit_value = value;
    sv = (am_Suggest **)amH_set(S, &S->suggests, sym);
    assert(sv), *sv = s, csym.u.id = var, csym.u.type = AM_DUMMY;
    amC(amC_init(S, csym, &s->constraint) == AM_OK);
    s->constraint.strength = strength, s->constraint.relation = AM_EQUAL;
    amE(amH_resize(S, &s->constraint.expression.terms, 1));
    amE(amC_addterm(&s->constraint, var, 1.0f));
    amE(am_addconstant(&s->constraint, -value));
    return amC_setup(S, &s->constraint), AM_OK;
}

AM_INLINE int amL_initedits(am_Load *X, am_Size n, size_t total) {
    am_Solver *S = X->S;
    amC(S->current <= total && n <= total - S->current);
    if (n == 0) return AM_OK;
    if (S->constraints.count + n > S->constraints.size)
        amE(amH_resize(S, &S->constraints, S->constraints.count + n));
    return amH_resize(S, &S->suggests, n);
}

AM_INLINE am_Num amL_numle(const char *p) {
    const int hi = !amN_bigendian(), is32 = sizeof(am_Num) < sizeof(double);
    am_Bits   u;
    u.u32[0] = amL_u32le(p);
    if (!is32) u.u32[hi || (u.u32[1] = u.u32[0], !hi)] = amL_u32le(p + 4);
    return is32 ? (am_Num)u.f32 : (am_Num)u.f64;
}

AM_INLINE int amL_addslots(size_t *psum, am_Size m, size_t vsz) {
    const size_t unit = sizeof(am_Key) + vsz;
    am_Size      s = m ? amH_calcsize(m) : 0;
    size_t       add = 0;
    if (s > (~(size_t)0 - 7) / unit) return AM_FAILED; /* align8 */
    if (s) add = ((size_t)s * unit + 7) & ~(size_t)7;
    amC(add <= ~(size_t)0 - *psum);
    return (*psum += add), AM_OK;
}

static int amL_terms(am_Solver *S, am_Scratch *sc, am_Size nrec, size_t sz[2]) {
    am_Size i, len, ns = S->suggests.count, unit = AM_TERM_SIZE;
    size_t  lens = sizeof(am_Size) * ((size_t)nrec + amX_len(sc->cons) + ns);
    size_t  at = sizeof(am_Size) + lens, end = amX_len(sc->bin);
    const char *b = sc->bin, *p = b + 4;
    for (i = 0; i < nrec; ++i, p += 4, at += len) {
        amC((len = amL_u32le(p), len && !(len % unit) && len <= end - at));
        amE(amL_addslots(&sz[0], len / unit - 1, sizeof(am_Num)));
    }
    amE(amL_addslots(&sz[0], nrec - 1, sizeof(am_Row)));
    for (i = 0; i < amX_len(sc->cons); ++i, p += 4, at += len) {
        amC((len = amL_u32le(p), len <= end - at && !(len % unit)));
        amE(amL_addslots(&sz[1], len / unit, sizeof(am_Num)));
    }
    for (i = 0; i < ns; ++i, p += 4, at += len) {
        am_Suggest **se;
        amC((len = amL_u32le(p), len && !(len % 4) && len <= end - at));
        amC(len / 4 <= nrec);
        amC((se = (am_Suggest **)amH_get(&S->suggests, amL_sym(b + at))));
        if (len / 4 > amX_cap((*se)->dirtyset))
            amE(amX_resize(S, (*se)->dirtyset, len / 4));
    }
    amC(p == b + 4 + lens && at == end && !amL_u32le(b + 4 + lens));
    return AM_OK;
}

static int amL_scratch(am_Load *X, am_Scratch *sc) {
    am_Size    i, v, size, nrec;
    size_t     slots, need[2] = {0, 0};
    am_Solver *S = X->S;
    amL_EC(amL_u8(X, &v), v >= 0xC4 && v <= 0xC6);
    amE(v == 0xC4 ? amL_u8(X, &size) : amL_vint(X, &size, v == 0xC5));
    amC(size >= 4 && amX_resize(X->S, sc->bin, size) == AM_OK);
    amE((amX_setlen(sc->bin, size), amL_read(X, sc->bin, size)));
    amC((nrec = amL_u32le(sc->bin)) && nrec <= (slots = (size - 4) / 4));
    amC(amX_len(sc->cons) + S->suggests.count <= slots - nrec);
    amE(amL_terms(S, sc, nrec, need));
    for (i = 0; i < 2; ++i)
        if (need[i]) amE(amH_initarena(S, &S->arena[i], need[i]));
    return AM_OK;
}

static int amL_body(am_Load *X) {
    am_Size     i, n, count, total;
    am_Solver  *S = X->S;
    am_Scratch *sc;
    /* [meta, total, vars, constraints, edits, bin] */
    amL_EC(amL_count(X, &count), count == 6);
    amL_EC(amL_count(X, &count), count == 3); /* ["amoeba", ver, num_size] */
    amL_EC(amL_string(X, NULL), !strcmp(X->buf, "amoeba"));
    amL_EC(amL_u32(X, &count), count == 5);              /* stream version */
    amL_EC(amL_u32(X, &count), count == sizeof(am_Num)); /* num_size */
    amL_EC(amL_u32(X, &total), total <= AM_ID_MAX - 1);
    if (X->loader->solver) X->loader->solver(X->loader, S);
    amC((sc = (am_Scratch *)amM_realloc(S, NULL, sizeof(*sc), 0, am_TScratch)));
    memset(sc, 0, sizeof(*sc)), S->scratch = sc;
    amL_EC(amL_count(X, &n), n <= total && amH_resize(S, &S->vars, n) == AM_OK);
    for (i = 0; i < n; ++i) amE(amL_var(X, i));
    amL_EC(amL_count(X, &n), amL_initcons(X, sc, n, total) == AM_OK);
    for (i = 0; i < n; ++i) amE(amL_cons(X, sc, i));
    amL_EC(amL_count(X, &n), amL_initedits(X, n, total) == AM_OK);
    for (i = 0; i < n; ++i) amE(amL_edit(X));
    amL_EC(amL_scratch(X, sc), S->current == total);
    return AM_OK;
}

static void amL_freescratch(am_Solver *S, am_Scratch *sc) {
    amX_free(S, sc->bin), amX_free(S, sc->cons);
    amM_realloc(S, sc, 0, sizeof(*sc), am_TScratch);
}

AM_INLINE void amL_fill(am_Table *t, const char *p, am_Size len) {
    am_Size     h, f, mask = t->size - 1, stride = AM_TERM_SIZE;
    const char *tp, *end = p + len;
    am_Symbol   sym;
    assert(t->value_size == sizeof(am_Num));
    for (tp = p; tp < end; tp += stride) { /* pass 1: home slots first */
        if (sym = amL_sym(tp), sym.u.id == 0) continue;
        if (t->keys[h = sym.u.id & mask].sym.bits) continue;
        t->keys[h].sym = sym, *(am_Num *)amH_slot(t, h) = amL_numle(tp + 4);
    }
    for (tp = p; tp < end; tp += stride) { /* pass 2: chain rest after home */
        if (sym = amL_sym(tp), sym.u.id == 0) continue;
        if (t->keys[h = sym.u.id & mask].sym.bits == sym.bits) continue;
        f = amH_avail(t), assert(f < t->size), amH_chain(t, h, f);
        t->keys[f].sym = sym, *(am_Num *)amH_slot(t, f) = amL_numle(tp + 4);
    }
    t->count = len / stride;
}

AM_INLINE am_Size amL_row(am_Solver *S, const char *p, const char *terms) {
    am_Symbol key = amL_sym(terms);
    am_Size   len = amL_u32le(p), unit = AM_TERM_SIZE;
    am_Row *r = key.u.id ? (am_Row *)amH_rawset(&S->rows, key) : &S->objective;
    if (assert(r), !key.u.id) amH_free(S, &S->objective.terms);
    r->constant = amL_numle(terms + 4), r->infeasible_next = amS_null();
    if (len == unit)
        amH_init(&r->terms, sizeof(am_Num));
    else { /* a reused rows slot is not zeroed for us */
        r->terms.arena_id = 1;
        amH_ref(&S->arena[0], &r->terms, len / unit - 1, sizeof(am_Num));
        amL_fill(&r->terms, terms + unit, len - unit);
    }
    return (S->rows.count += key.u.id != 0), len;
}

AM_INLINE am_Size amL_expr(am_Solver *S, int i, const char *p, const char *t) {
    am_Constraint *c = S->scratch->cons[i];
    am_Size        j, len = amL_u32le(p), unit = AM_TERM_SIZE, m = len / unit;
    am_Var        *ve;
    if (!len) return 0;
    c->expression.terms.arena_id = 2;
    amH_ref(&S->arena[1], &c->expression.terms, m, sizeof(am_Num));
    amL_fill(&c->expression.terms, t, len);
    for (j = 0; j < m; ++j, t += unit)
        if ((ve = amV_get(S, amL_u32le(t)))) ve->refcount += 1;
    return len;
}

AM_INLINE am_Size amL_suggest(am_Solver *S, const char *p, const char *t) {
    am_Size     n, j, len = amL_u32le(p), var = amL_u32le(t);
    am_Suggest *s, **se = (am_Suggest **)amH_get(&S->suggests, amS_var(var));
    if (assert(se), s = *se, len <= 4 || amX_len(s->dirtyset)) return len;
    for (j = 0, n = len / 4 - 1; j < n; ++j) {
        am_Symbol sym = amL_sym(t + 4 + j * 4);
        am_Row   *r = amR_get(S, sym);
        if (!r || !amH_get(&r->terms, s->constraint.marker)) break;
        s->dirtyset[j] = sym;
    }
    if (j == n) amX_setlen(s->dirtyset, n), s->age = S->age;
    return len;
}

AM_INLINE void amL_wake(am_Solver *S) {
    am_Scratch *sc = S->scratch;
    am_Arena   *a = S->arena;
    am_Size     i, len, nc, ns, nrec;
    const char *p, *rend, *t;
    if (sc == NULL) return;
    for (i = 0; i < 2; ++i)
        if (a[i].buf) memset(a[i].buf, 0, a[i].size);
    nrec = amL_u32le(sc->bin), nc = amX_len(sc->cons), ns = S->suggests.count;
    p = sc->bin + 4, t = rend = p + 4 * ((size_t)nrec + nc + ns);
    if (nrec > 1)
        S->rows.arena_id = 1, amH_ref(a, &S->rows, nrec - 1, sizeof(am_Row));
    for (i = 0; i < nrec; ++i, p += 4, t += len) len = amL_row(S, p, t);
    for (i = 0; i < nc; ++i, p += 4, t += len) len = amL_expr(S, i, p, t);
    for (i = 0; i < ns; ++i, p += 4, t += len) len = amL_suggest(S, p, t);
    assert(p == rend && t == sc->bin + amX_len(sc->bin));
    for (i = 0; i < 2; ++i) assert(a[i].used == a[i].size);
    amL_freescratch(S, sc), S->scratch = NULL;
}

/* APIs */

/* clang-format off */
AM_API int am_hasedit(const am_Solver *S, am_Id var)
{ return S ? amH_get(&S->suggests, amS_var(var)) != NULL : 0; }

AM_API int am_hasconstraint(const am_Constraint *c)
{ return c != NULL && !amV_isexternal(c->marker); }

AM_API am_Num am_constant(const am_Constraint *c)
{ return c ? c->expression.constant : 0; }

AM_API am_Num am_strength(const am_Constraint *c)
{ return c ? c->strength : 0; }
/* clang-format on */

AM_API void am_autoupdate(am_Solver *S, int v) { S->auto_update = v; }
AM_API int  am_relation(const am_Constraint *c) { return c ? c->relation : 0; }

static void *amS_defallocf(void **pud, void *p, size_t n, size_t o, am_Type t) {
    void *np;
    if (n == 0) return free(p), (void *)0;
    if ((np = realloc(p, n)) == NULL) abort();
    return (void)pud, (void)o, (void)t, np;
}

static void *amS_getcons(am_Solver *S) {
    if (S->allocf == amS_defallocf) return amP_get(S, &S->conspool);
    return amM_realloc(S, NULL, sizeof(am_Constraint), 0, am_TConstraint);
}

AM_INLINE int amS_putcons(am_Solver *S, am_Constraint *c) {
    if (S->allocf == amS_defallocf) return amP_put(&S->conspool, c), 1;
    return amM_realloc(S, c, 0, sizeof(*c), am_TConstraint), 0;
}

AM_API am_Solver *am_newsolver(am_Allocf *allocf, void *ud) {
    am_Solver *S;
    if (allocf == NULL) allocf = amS_defallocf;
    S = (am_Solver *)allocf(&ud, NULL, sizeof(am_Solver), 0, am_TSolver);
    if (S == NULL) return NULL;
    memset(S, 0, sizeof(*S)), S->ud = ud, S->allocf = allocf;
    amR_init(&S->objective);
    amH_init(&S->vars, sizeof(am_Var));
    amH_init(&S->constraints, sizeof(am_Constraint *));
    amH_init(&S->suggests, sizeof(am_Suggest *));
    amH_init(&S->rows, sizeof(am_Row));
    amP_init(&S->conspool, sizeof(am_Constraint));
    amP_init(&S->editpool, sizeof(am_Suggest));
    return S;
}

AM_API void am_resetsolver(am_Solver *S) {
    am_Iterator    it;
    am_Constraint *c;
    if (S == NULL) return;
    amL_wake(S), am_clearedits(S);
    it = amH_iter(&S->constraints);
    while (amH_next(&it)) {
        c = *amH_val(am_Constraint *, it);
        c->marker.u.type = c->other.u.type = AM_EXTERNAL;
    }
    it = amH_iter(&S->rows);
    while (amH_next(&it)) amR_free(S, amH_val(am_Row, it));
    amH_free(S, &S->rows), amH_free(S, &S->objective.terms);
    S->objective.constant = 0, S->infeasible_rows = amS_null(), S->age = 0;
}

AM_API void am_delsolver(am_Solver *S) {
    am_Iterator it = amH_iter(S ? &S->constraints : NULL);
    am_Size     i;
    if (it.t == NULL) return;
    if (S->scratch) amL_freescratch(S, S->scratch), S->scratch = NULL;
    while (amH_next(&it)) {
        am_Constraint *c = *amH_val(am_Constraint *, it);
        amR_free(S, &c->expression);
        if (amV_isexternal(c->sym)) amS_putcons(S, c);
    }
    it = amH_iter(&S->suggests);
    while (amH_next(&it)) {
        am_Suggest *s = *amH_val(am_Suggest *, it);
        amX_free(S, s->dirtyset), amH_free(S, &s->constraint.expression.terms);
        amP_put(&S->editpool, s);
    }
    it = amH_iter(&S->rows);
    while (amH_next(&it)) amR_free(S, amH_val(am_Row, it));
    amR_free(S, &S->objective), amH_free(S, &S->vars);
    amH_free(S, &S->constraints), amH_free(S, &S->suggests);
    amH_free(S, &S->rows), amP_free(S, &S->conspool), amP_free(S, &S->editpool);
    for (i = 0; i < 2; ++i) amH_unref(S, i, 1);
    S->allocf(&S->ud, S, 0, sizeof(am_Solver), am_TSolver);
}

AM_API void am_updatevars(am_Solver *S) {
    if (S == NULL) return;
    while (S->dirty_vars.u.id != 0) {
        am_Symbol var = S->dirty_vars; /* link type is the queued sentinel */
        am_Var   *ve = (am_Var *)amH_get(&S->vars, amS_var(var.u.id));
        assert(ve != NULL), S->dirty_vars = ve->next, ve->next = amS_null();
        if (ve->refcount == 1)
            am_deledit(S, var.u.id), amH_del(&S->vars, ve);
        else {
            am_Row *r = amR_get(S, amS_var(var.u.id));
            ve->refcount -= 1, *ve->pvalue = r ? r->constant : 0.f;
        }
    }
}

AM_API am_Id am_newvariable(am_Solver *S, am_Num *pvalue) {
    am_Symbol sym;
    am_Var   *ve;
    if (S == NULL || pvalue == NULL) return 0;
    sym = amS_var(amS_new(S));
    if ((ve = (am_Var *)amH_set(S, &S->vars, sym)) == NULL) return 0;
    memset(ve, 0, sizeof(*ve)), ve->pvalue = pvalue, ve->refcount = 1;
    return sym.u.id;
}

AM_API void am_delvariable(am_Solver *S, am_Id var) {
    am_Var *ve;
    if (S == NULL) return;
    amL_wake(S), ve = amV_get(S, var);
    if (!ve || --ve->refcount) return;
    assert(!amV_isdummy(ve->next)), am_deledit(S, var), amH_del(&S->vars, ve);
}

AM_API am_Num *am_varvalue(am_Solver *S, am_Id var, am_Num *newvalue) {
    am_Var *ve = amV_get(S, var);
    am_Num *pv = ve ? ve->pvalue : NULL;
    if (ve && newvalue) ve->pvalue = newvalue;
    return pv;
}

AM_API int am_refcount(const am_Solver *S, am_Id var) {
    am_Var *ve;
    if (S) amL_wake((am_Solver *)S);
    return (ve = amV_get(S, var)) ? ve->refcount : 0;
}

AM_API am_Id am_vars(const am_Solver *S, am_Id var) {
    am_Iterator it;
    if (S == NULL) return 0;
    it = amH_iter(&S->vars);
    if (var && !(it.offset = amH_find(&S->vars, amS_var(var)))) return 0;
    return amH_next(&it) ? it.key.u.id : 0;
}

AM_API int am_addedit(am_Solver *S, am_Id var, am_Num strength) {
    am_Suggest **s;
    if (S == NULL || var == 0 || amN_negative(strength)) return AM_PARAM;
    amL_wake(S);
    if (amN_nearzero(strength) || strength >= AM_STRONG) strength = AM_STRONG;
    s = (am_Suggest **)amH_get(&S->suggests, amS_var(var));
    if (s) return assert(*s), am_setstrength(&(*s)->constraint, strength);
    return amC_newedit(S, var, strength) ? AM_OK : AM_FAILED;
}

AM_API void am_deledit(am_Solver *S, am_Id var) {
    am_Suggest **ps, *s;
    if (S == NULL || var == 0) return;
    amL_wake(S), ps = (am_Suggest **)amH_get(&S->suggests, amS_var(var));
    if (ps) s = *ps, amH_del(&S->suggests, ps), amC_freesuggest(S, s);
}

AM_API void am_suggest(am_Solver *S, am_Id var, am_Num value) {
    am_Suggest **ps, *s;
    am_Num       delta;
    if (S == NULL || var == 0) return;
    amL_wake(S), ps = (am_Suggest **)amH_get(&S->suggests, amS_var(var));
    if (!(s = ps ? *ps : amC_newedit(S, var, AM_MEDIUM))) return;
    delta = value - s->edit_value, s->edit_value = value;
    amA_editdelta(S, s, delta);
    while (S->infeasible_rows.u.id != 0) amA_dualoptimize(S);
    if (S->auto_update) am_updatevars(S);
}

AM_API void am_clearedits(am_Solver *S) {
    am_Iterator it;
    if (S == NULL) return;
    if (amL_wake(S), !S->auto_update) am_updatevars(S);
    it = amH_iter(&S->suggests);
    while (amH_next(&it)) amC_freesuggest(S, *amH_val(am_Suggest *, it));
    amH_reset(&S->suggests);
}

AM_API am_Constraint *am_newconstraint(am_Solver *S, am_Num strength) {
    am_Constraint *c;
    int            ret;
    if (S == NULL || amN_negative(strength)) return NULL;
    if (!(c = (am_Constraint *)amS_getcons(S))) return NULL;
    ret = amC_init(S, amS_var(amS_new(S)), c);
    c->strength = amN_nearzero(strength) ? AM_REQUIRED : strength;
    if (ret != AM_OK) amS_putcons(S, c), c = NULL;
    return c;
}

AM_API void am_resetconstraint(am_Constraint *c) {
    am_Iterator it;
    if (c == NULL) return;
    amL_wake(c->S), am_remove(c), c->relation = 0;
    it = amH_iter(&c->expression.terms);
    while (amH_next(&it)) am_delvariable(c->S, it.key.u.id);
    amR_reset(&c->expression);
}

AM_API void am_delconstraint(am_Constraint *c) {
    am_Solver      *S = c ? c->S : NULL;
    am_Constraint **ce;
    am_Iterator     it;
    if (S == NULL) return;
    amL_wake(S);
    am_remove(c), ce = (am_Constraint **)amH_get(&S->constraints, c->sym);
    assert(ce && *ce == c), amH_del(&S->constraints, ce);
    it = amH_iter(&c->expression.terms);
    while (amH_next(&it)) am_delvariable(c->S, it.key.u.id);
    amR_free(S, &c->expression);
    if (amV_isexternal(c->sym)) amS_putcons(S, c);
}

AM_API am_Constraint *am_cloneconstraint(am_Constraint *o, am_Num strength) {
    am_Constraint *c;
    if (o == NULL || amN_negative(strength)) return NULL;
    if (amN_nearzero(strength)) strength = o->strength;
    if ((c = am_newconstraint(o->S, strength)) == NULL) return NULL;
    return am_merge(c, o, 1.0f), (c->relation = o->relation), c;
}

AM_API am_Constraint *am_constraints(const am_Solver *S, am_Constraint *c) {
    am_Iterator it;
    if (S == NULL) return 0;
    it = amH_iter(&S->constraints);
    if (c && !(it.offset = amH_find(&S->constraints, c->sym))) return 0;
    while (amH_next(&it) && !amV_isexternal(it.key)) continue;
    return it.offset ? *amH_val(am_Constraint *, it) : 0;
}

AM_API int am_addterm(am_Constraint *c, am_Id var, am_Num f) {
    if (c == NULL || var == 0) return AM_PARAM;
    amC((amL_wake(c->S), amV_isexternal(c->marker)));
    return amC_addterm(c, var, c->relation == AM_GREATEREQUAL ? -f : f);
}

AM_API int am_setrelation(am_Constraint *c, int relation) {
    if (c == NULL) return AM_PARAM;
    if (relation < AM_LESSEQUAL || relation > AM_GREATEREQUAL) return AM_PARAM;
    amC((amL_wake(c->S), amV_isexternal(c->marker) && c->relation == 0));
    if (relation != AM_GREATEREQUAL) amR_mul(&c->expression, -1.0f);
    return (c->relation = relation), AM_OK;
}

AM_API int am_addconstant(am_Constraint *c, am_Num constant) {
    if (c == NULL) return AM_PARAM;
    amC(amV_isexternal(c->marker));
    if (c->relation == AM_GREATEREQUAL) constant = -constant;
    return amR_add(&c->expression, constant), AM_OK;
}

AM_API int am_setstrength(am_Constraint *c, am_Num strength) {
    am_Num     old, diff;
    am_Solver *S;
    if (c == NULL || amN_negative(strength)) return AM_PARAM;
    amL_wake(c->S), strength = amN_nearzero(strength) ? AM_REQUIRED : strength;
    if (c->strength == strength) return AM_OK;
    if (!am_hasconstraint(c)) return (c->strength = strength), AM_OK;
    if (c->strength >= AM_REQUIRED || strength >= AM_REQUIRED) {
        int ret, ra = AM_OK;
        old = c->strength, am_remove(c), c->strength = strength;
        if ((ret = am_add(c)) != AM_OK) c->strength = old, ra = am_add(c);
        return assert(ra == AM_OK), (void)ra, ret;
    }
    S = c->S, diff = strength - c->strength;
    if (amV_iserror(c->marker)) amA_mergerow(S, &S->objective, c->marker, diff);
    if (amV_iserror(c->other)) amA_mergerow(S, &S->objective, c->other, diff);
    amA_optimize(S, &S->objective);
    if (S->auto_update) am_updatevars(S);
    return (S->age += 1, c->strength = strength), AM_OK;
}

AM_API int am_merge(am_Constraint *c, const am_Constraint *o, am_Num coef) {
    am_Iterator it;
    if (c == NULL || o == NULL || c->S != o->S) return AM_PARAM;
    amC((amL_wake(c->S), amV_isexternal(c->marker)));
    if (c->relation == AM_GREATEREQUAL) coef = -coef;
    amR_add(&c->expression, o->expression.constant * coef);
    it = amH_iter(&o->expression.terms);
    while (amH_next(&it))
        amE(amC_addterm(c, it.key.u.id, *amH_val(am_Num, it) * coef));
    return AM_OK;
}

AM_API int am_add(am_Constraint *c) {
    am_Solver *S = c ? c->S : NULL;
    int        ret;
    am_Row     r;
    if (S == NULL) return AM_PARAM;
    amC((amL_wake(S), amV_isexternal(c->marker)));
    amC_setup(S, c), r = amA_makerow(S, c);
    if ((ret = amA_tryaddrow(S, &r, c)) != AM_OK)
        return amA_removeerrors(S, c), ret;
    amA_optimize(S, &S->objective);
    if (S->auto_update) am_updatevars(S);
    return S->age += 1, assert(S->infeasible_rows.u.id == 0), AM_OK;
}

AM_API void am_remove(am_Constraint *c) {
    am_Symbol  marker, other, leave;
    am_Row     tmp;
    am_Solver *S;
    if (!c || (amL_wake(S = c->S), amV_isexternal(c->marker))) return;
    marker = c->marker, other = c->other;
    amA_removeerrors(S, c);
    if (!amA_takerow(S, marker, &tmp)) {
        am_Var *ve;
        leave = amA_leavingrow(S, marker), assert(leave.u.id != 0);
        if (amV_isexternal(leave)) amR_delvar(&S->objective, marker);
        amA_takerow(S, leave, &tmp);
        amA_solvefor(S, &tmp, marker, leave);
        amA_substitute(S, marker, &tmp);
        if (amV_isexternal(leave) && (ve = amV_get(S, leave.u.id)))
            *ve->pvalue = 0.f;
    }
    amR_free(S, &tmp);
    if (amV_iserror(other)) amR_delvar(&S->objective, other);
    amA_optimize(S, &S->objective), S->age += 1;
    if (S->auto_update) am_updatevars(S);
}

AM_API am_Id am_terms(const am_Constraint *c, am_Id var, am_Num *coef) {
    am_Iterator it;
    if (c == NULL || coef == NULL) return 0;
    amL_wake(c->S);
    it = amH_iter(&c->expression.terms);
    if (var && !(it.offset = amH_find(&c->expression.terms, amS_var(var))))
        return 0;
    return amH_next(&it) ? ((*coef = *amH_val(am_Num, it)), it.key.u.id) : 0;
}

AM_API am_Num am_error(const am_Constraint *c) {
    am_Solver *S = c ? c->S : NULL;
    am_Num     e = 0;
    am_Row    *r;
    if (c == NULL) return 0;
    if (amL_wake(S), amV_isexternal(c->marker)) return 0;
    if (amV_iserror(c->marker) && (r = amR_get(S, c->marker))) e += r->constant;
    if (amV_iserror(c->other) && (r = amR_get(S, c->other))) e += r->constant;
    return e;
}

AM_API int am_dump(am_Solver *S, am_Dumper *dumper) {
    am_Size     nc = 0, keq = 0, ns = 0;
    int         ret;
    am_Iterator it;
    am_Dump     X;
    if (!S || !dumper || !dumper->writer || !dumper->var_name) return AM_PARAM;
    if (amL_wake(S), !S->auto_update) am_updatevars(S);
    memset(&X, 0, sizeof(X)), X.S = S, X.p = X.buf, X.dumper = dumper;
    for (it = amH_iter(&S->constraints); amH_next(&it);) {
        am_Constraint *c = *amH_val(am_Constraint *, it);
        if (amV_isdummy(c->sym)) continue;
        if (nc += 1, !amV_isexternal(c->marker) && c->strength < AM_REQUIRED)
            keq += (c->relation == AM_EQUAL);
    }
    for (it = amH_iter(&S->suggests); amH_next(&it);)
        ns += !amV_isexternal((*amH_val(am_Suggest *, it))->constraint.marker);
    X.nc = nc, X.ns = ns, X.total = S->vars.count + nc + keq + ns;
    amE(amX_resize(S, X.map, 4 * (S->current + 1)));
    amX_clear(X.map, 4 * (S->current + 1));
    return (ret = amD_body(&X)), amX_free(S, X.map), ret;
}

AM_API am_Solver *am_load(am_Allocf *a, void *ud, am_Loader *l, size_t maxmem) {
    am_Solver *S;
    am_Load    X;
    if (!l || !l->reader || !l->load_var) return NULL;
    if ((S = am_newsolver(a, ud)) == NULL) return NULL;
    memset(&X, 0, sizeof(X)), X.S = S, X.loader = l;
    S->maxmem = maxmem ? maxmem : AM_MAX_LOADMEM;
    if (amL_body(&X) != AM_OK) return am_delsolver(X.S), (am_Solver *)0;
    return (S->maxmem = 0, S->age = 1), S;
}

AM_NS_END

#endif /* AM_IMPLEMENTATION */

/* cc: flags+='-shared -O2 -pedantic -std=c89 -DAM_IMPLEMENTATION -xc'
   unixcc: output='amoeba.so'
   win32cc: output='amoeba.dll' */
