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
typedef struct am_Var        am_Var;
typedef struct am_Constraint am_Constraint;

typedef void *am_Allocf (void *ud, void *ptr, size_t nsize, size_t osize);

AM_API am_Solver *am_newsolver   (am_Allocf *allocf, void *ud);
AM_API void       am_resetsolver (am_Solver *solver, int clear_constraints);
AM_API void       am_delsolver   (am_Solver *solver);

AM_API void am_updatevars (am_Solver *solver);
AM_API void am_autoupdate (am_Solver *solver, int auto_update);

AM_API int am_hasedit       (am_Var *var);
AM_API int am_hasconstraint (am_Constraint *cons);

AM_API int  am_add    (am_Constraint *cons);
AM_API void am_remove (am_Constraint *cons);

AM_API int  am_addedit (am_Var *var, am_Num strength);
AM_API void am_suggest (am_Var *var, am_Num value);
AM_API void am_deledit (am_Var *var);

AM_API am_Var *am_newvariable (am_Solver *solver);
AM_API void    am_usevariable (am_Var *var);
AM_API void    am_delvariable (am_Var *var);
AM_API int     am_variableid  (am_Var *var);
AM_API am_Num  am_value       (am_Var *var);

AM_API am_Constraint *am_newconstraint   (am_Solver *solver, am_Num strength);
AM_API am_Constraint *am_cloneconstraint (am_Constraint *other, am_Num strength);

AM_API void am_resetconstraint (am_Constraint *cons);
AM_API void am_delconstraint   (am_Constraint *cons);

AM_API int am_addterm     (am_Constraint *cons, am_Var *var, am_Num multiplier);
AM_API int am_setrelation (am_Constraint *cons, int relation);
AM_API int am_addconstant (am_Constraint *cons, am_Num constant);
AM_API int am_setstrength (am_Constraint *cons, am_Num strength);

AM_API int am_mergeconstraint (am_Constraint *cons, const am_Constraint *other, am_Num multiplier);


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
#define AM_MIN_HASHSIZE 4
#define AM_MAX_SIZET    ((~(size_t)0)-100)

#define AM_UNSIGNED_BITS (sizeof(unsigned)*CHAR_BIT)

#ifdef AM_USE_FLOAT
# define AM_NUM_MAX FLT_MAX
# define AM_NUM_EPS 1e-4f
#else
# define AM_NUM_MAX DBL_MAX
# define AM_NUM_EPS 1e-6
#endif

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wlong-long"
#endif

AM_NS_BEGIN

#if defined(__SSE2__) || (!defined(_M_ARM64EC) && \
    (defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)))
# include <emmintrin.h>
# define AM_USE_SSE2 1
//# ifdef __SSSE3__
#   include <tmmintrin.h>
#   define AM_HAVE_SSSE3 1
//# endif /* __SSSE3__ */
#elif defined(__ARM_NEON) && !(defined(__NVCC__) && defined(__CUDACC__))
# include <arm_neon.h>
# define AM_USE_NEON 1
#endif

#if AM_USE_SSE2
typedef __m128i            am_Group;
typedef unsigned __int16   am_BitMask;
#elif AM_USE_NEON
typedef uint8x8_t          am_Group;
typedef uint64_t           am_BitMask;
#else
typedef unsigned long long am_Group; /* at least 64bit */
typedef unsigned long long am_BitMask;
#endif
typedef unsigned           am_Size;  /* size should less than symbol count */
typedef unsigned           am_Hash;
typedef unsigned char      am_Ctrl;

typedef struct am_MemPool {
    size_t size;
    void  *freed;
    void  *pages;
} am_MemPool;

typedef struct am_Symbol {
    unsigned id   : AM_UNSIGNED_BITS - 2;
    unsigned type : 2;
} am_Symbol;

typedef struct am_Table {
    am_Size   mask;
    am_Size   count;
    am_Size   entry_size;
    am_Size   growth_left : AM_UNSIGNED_BITS - 1;
    am_Size   deleted     : 1;
    am_Ctrl  *ctrl;
} am_Table;

typedef struct am_Entry {
    am_Hash   hash;
    am_Symbol key;
} am_Entry;

typedef struct am_Iterator {
    const am_Table *t;
    const am_Entry *entry;
    am_BitMask      group;
    am_Size         offset;
} am_Iterator;

typedef struct am_VarEntry {
    am_Entry entry;
    am_Var  *var;
} am_VarEntry;

typedef struct am_ConsEntry {
    am_Entry       entry;
    am_Constraint *constraint;
} am_ConsEntry;

typedef struct am_Term {
    am_Entry entry;
    am_Num   multiplier;
} am_Term;

typedef struct am_Row {
    am_Entry  entry;
    am_Symbol infeasible_next;
    am_Num    constant;
    am_Table  terms;
} am_Row;

struct am_Var {
    am_Symbol      sym;
    unsigned       refcount : AM_UNSIGNED_BITS - 1;
    unsigned       dirty    : 1;
    am_Var        *next;
    am_Solver     *solver;
    am_Constraint *constraint;
    am_Num         edit_value;
    am_Num         value;
};

struct am_Constraint {
    am_Row     expression;
    am_Symbol  marker;
    am_Symbol  other;
    int        relation;
    am_Solver *solver;
    am_Num     strength;
};

struct am_Solver {
    am_Allocf *allocf;
    void      *ud;
    am_Row     objective;
    am_Table   vars;            /* symbol -> VarEntry */
    am_Table   constraints;     /* symbol -> ConsEntry */
    am_Table   rows;            /* symbol -> Row */
    am_MemPool varpool;
    am_MemPool conspool;
    unsigned   symbol_count;
    unsigned   constraint_count;
    unsigned   auto_update;
    am_Symbol  infeasible_rows;
    am_Var    *dirty_vars;
};

/* utils */

static am_Symbol am_newsymbol(am_Solver *solver, int type);

static int am_approx(am_Num a, am_Num b)
{ return a > b ? a - b < AM_NUM_EPS : b - a < AM_NUM_EPS; }

static int am_nearzero(am_Num a)
{ return am_approx(a, 0.0f); }

static am_Symbol am_null(void)
{ am_Symbol null = { 0, 0 }; return null; }

static void am_initsymbol(am_Solver *solver, am_Symbol *sym, int type)
{ if (sym->id == 0) *sym = am_newsymbol(solver, type); }

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
        void *end, *newpage = solver->allocf(solver->ud, NULL, AM_POOLSIZE, 0);
        assert(newpage != NULL);
        *(void**)((char*)newpage + offset) = pool->pages;
        pool->pages = newpage;
        end = (char*)newpage + (offset/pool->size-1)*pool->size;
        while (end != newpage) {
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
    unsigned id = ++solver->symbol_count;
    if (id > 0x3FFFFFFF) id = solver->symbol_count = 1;
    assert(type >= AM_EXTERNAL && type <= AM_DUMMY);
    sym.id   = id;
    sym.type = type;
    return sym;
}

/* hash table */

#define AM_EMPTY   ((am_Ctrl)0x80)
#define AM_DELETED ((am_Ctrl)0xFF)

#if AM_USE_SSE2
# define AM_WIDTH 16
#else
# define AM_WIDTH 8
#endif
typedef char AM_STATIC_ASSERT0[sizeof(am_Group) >= AM_WIDTH ? 1 : -1];

#define AM_MAX_ENTRYSIZE sizeof(am_Row)

#define am_key(e)         (((am_Entry*)(e))->key)
#define am_offset(t,e)    (((t)->ctrl - (am_Ctrl*)(e))/(t)->entry_size - 1)
#define am_fullmark(h)    ((h) >> (AM_UNSIGNED_BITS - CHAR_BIT + 1))
#define am_alignsize(c,s) (((c)*(s) + AM_WIDTH-1) & ~(AM_WIDTH-1))

static am_Iterator am_itertable(const am_Table *t)
{ am_Iterator it = {NULL, NULL, 0, (am_Size)-AM_WIDTH}; it.t = t; return it; }

static void am_inittable(am_Table *t, size_t entry_size)
{ memset(t, 0, sizeof(am_Table)); t->entry_size = (am_Size)entry_size; }

static const am_Entry *am_entry(const am_Table *t, am_Size idx)
{ return (const am_Entry*)(t->ctrl - ((idx & t->mask)+1)*t->entry_size); }

static am_Ctrl *am_ctrl(am_Table *t, const am_Entry *e)
{ return (assert(t->ctrl), &t->ctrl[am_offset(t, e)]); }

static void am_writectrl(am_Table *t, am_Ctrl *c, am_Ctrl v)
{ t->ctrl[(((c - t->ctrl) - AM_WIDTH) & t->mask) + AM_WIDTH] = *c = v; }

#if AM_USE_SSE2
static am_Group am_group(const am_Ctrl* c)
{ return _mm_loadu_si128((const am_Group*)(c)); }

static am_BitMask am_group_load(const am_Ctrl *c)
{ return _mm_movemask_epi8(_mm_loadu_si128((const am_Group*)c)); }

static am_BitMask am_group_empty(am_Group g) {
#if AM_HAVE_SSSE3
    return _mm_movemask_epi8(_mm_sign_epi8(g, g));
#else
    return _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_set1_epi8(AM_EMPTY), g));
#endif
}

static am_BitMask am_group_match(am_Group g, am_Hash h)
{ return _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_set1_epi8((char)h), g)); }

static am_BitMask am_group_deleted(const am_Ctrl *c)
{ return _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_set1_epi8(AM_DELETED), am_group(c))); }

static am_BitMask am_group_full(const am_Ctrl *c)
{ return _mm_movemask_epi8(am_group(c)) ^ 0xffff; }

static am_BitMask am_group_nonfull(am_Ctrl *c)
{ return _mm_movemask_epi8(am_group(c)); }

static am_Size am_ctz(am_BitMask g) {
#if _MSC_VER
    unsigned long n = 0;
    unsigned char r = _BitScanForward(&n, g);
    assert(r);
    return (am_Size)n;
#elif defined(__has_builtin) && __has_builtin(__builtin_ctz)
    return __builtin_ctz(g);
#else
    am_Size n = 31;
    am_BitMask og = g;
    g &= ~g + 1;
    n -= (am_Size)!!(g & 0x0000FFFFULL) << 4;
    n -= (am_Size)!!(g & 0x00FF00FFULL) << 3;
    n -= (am_Size)!!(g & 0x0F0F0F0FULL) << 2;
    n -= (am_Size)!!(g & 0x33333333ULL) << 1;
    n -= (am_Size)!!(g & 0x55555555ULL);
    return n;
#endif
}

static am_Size am_clz(am_BitMask g) {
#if _MSC_VER
    unsigned long n = 0;
    return _BitScanReverse(&n, g) ? n : 32;
#elif defined(__has_builtin) && __has_builtin(__builtin_clzll)
    return __builtin_clzll(g);
#else
    am_Size n = 28;
    if (g >> 16) n -= 16, g >>= 16;
    if (g >>  8) n -=  8, g >>=  8;
    if (g >>  4) n -=  4, g >>=  4;
    return "\4\3\2\2\1\1\1\1\0\0\0\0\0\0\0"[g] + n;
#endif
}

static am_BitMask am_group_next(am_BitMask g, am_Size *pidx) {
    if (g == 0) return 0;
    *pidx = am_ctz(g);
    return g & (g - 1);
}

static __m128i _mm_cmpgt_epi8_fixed(__m128i a, __m128i b) {
#if defined(__GNUC__) && !defined(__clang__)
  if (CHAR_MIN == 0) {
    const __m128i mask = _mm_set1_epi8(0x80);
    const __m128i diff = _mm_subs_epi8(b, a);
    return _mm_cmpeq_epi8(_mm_and_si128(diff, mask), mask);
  }
#endif
  return _mm_cmpgt_epi8(a, b);
}

static void am_group_tidy(am_Ctrl *c) {
    am_Group ctrl = am_group(c);
    am_Group msbs = _mm_set1_epi8(AM_EMPTY);
    am_Group dels = _mm_set1_epi8(AM_DELETED);
#ifdef AM_HAVE_SSSE3
    auto res = _mm_or_si128(_mm_shuffle_epi8(dels, ctrl), msbs);
#else
    auto zero = _mm_setzero_si128();
    auto special_mask = _mm_cmpgt_epi8_fixed(zero, ctrl);
    auto res = _mm_or_si128(msbs, _mm_andnot_si128(special_mask, dels));
#endif
    _mm_storeu_si128((am_Group*)c, res);
}

static int am_neverfull(am_Table *t, const am_Ctrl *c) {
    am_BitMask before, after;
    am_Size  before_offset;
    if (t->mask+1 == AM_WIDTH) return 1;
    before_offset = (((c-t->ctrl) & t->mask) - AM_WIDTH) & t->mask;
    after  = am_group_empty(am_group(c));
    before = am_group_empty(am_group(&t->ctrl[before_offset]));
    return (before ? am_clz(before) : AM_WIDTH)
        + (after ? am_ctz(after) : AM_WIDTH) < AM_WIDTH;
}

#else
# define AM_LSBS  0x0101010101010101ULL
# define AM_MSBS  0x8080808080808080ULL

# if AM_USE_NEON
static am_Group am_group(const am_Ctrl *c)
{ return vld1_u8(c); }

static am_BitMask am_group_load(const am_Ctrl *c)
{ return vget_lane_u64(vld1_u8(c), 0); }

static am_BitMask am_group_empty(am_Group g) {
    return vget_lane_u64(vreinterpret_u64_u8(
                vshr_n_u8(vceq_u8(vdup_n_u8(AM_EMPTY), g), 7)), 0);
}

static am_BitMask am_group_match(am_Group g, am_Hash h) {
    return vget_lane_u64(vreinterpret_u64_u8(
                vshr_n_u8(vceq_u8(vdup_n_u8(h), g), 7)), 0);
}

static am_BitMask am_group_deleted(const am_Ctrl *c) {
    return vget_lane_u64(vreinterpret_u64_u8(
                vshr_n_u8(vceq_u8(vdup_n_u8(AM_DELETED), vld1_u8(c)), 7)), 0);
}
# else
static am_Group am_group(const am_Ctrl *c)
{ am_Group g; memcpy(&g, c, AM_WIDTH); return g; }

static am_BitMask am_group_load(const am_Ctrl *c)
{ return am_group(c); }

static am_BitMask am_group_empty(am_Group g)
{ return g & ~(g << 1) & AM_MSBS; }

static am_BitMask am_group_match(am_Group g, am_Hash h) {
    am_BitMask x = g ^ (AM_LSBS * h);
    return (x - AM_LSBS) & ~x & AM_MSBS;
}

static am_BitMask am_group_deleted(const am_Ctrl *c) {
    am_BitMask g = am_group_load(c);
    return g & (g << 1) & AM_MSBS;
}
# endif

static am_Size am_ctz64(am_BitMask g) {
#if _MSC_VER
    unsigned long n = 0;
    unsigned char r = _BitScanForward64(&n, g);
    assert(r);
    return (am_Size)n;
#elif defined(__has_builtin) && __has_builtin(__builtin_ctzll)
    return __builtin_ctzll(g);
#else
    am_Size n = 63;
    am_BitMask og = g;
    g &= ~g + 1;
    n -= (am_Size)!!(g & 0x00000000FFFFFFFFULL) << 5;
    n -= (am_Size)!!(g & 0x0000FFFF0000FFFFULL) << 4;
    n -= (am_Size)!!(g & 0x00FF00FF00FF00FFULL) << 3;
    n -= (am_Size)!!(g & 0x0F0F0F0F0F0F0F0FULL) << 2;
    n -= (am_Size)!!(g & 0x3333333333333333ULL) << 1;
    n -= (am_Size)!!(g & 0x5555555555555555ULL);
    return n;
#endif
}

static am_Size am_clz64(am_BitMask g) {
#if _MSC_VER
    unsigned long n = 0;
    return _BitScanReverse64(&n, g) ? n : 64;
#elif defined(__has_builtin) && __has_builtin(__builtin_clzll)
    return __builtin_clzll(g);
#else
    am_Size n = 60;
    if (g >> 32) n -= 32, g >>= 32;
    if (g >> 16) n -= 16, g >>= 16;
    if (g >>  8) n -=  8, g >>=  8;
    if (g >>  4) n -=  4, g >>=  4;
    return "\4\3\2\2\1\1\1\1\0\0\0\0\0\0\0"[g] + n;
#endif
}

static am_BitMask am_group_next(am_BitMask g, am_Size *pidx) {
    if (g == 0) return 0;
    *pidx = am_ctz64(g) / AM_WIDTH;
    return g & (g - 1);
}

static am_BitMask am_group_full(const am_Ctrl *c)
{ return ~am_group_load(c) & AM_MSBS; }

static am_BitMask am_group_nonfull(am_Ctrl *c)
{ return am_group_load(c) & AM_MSBS; }

static void am_group_tidy(am_Ctrl *c) {
    am_BitMask x = am_group_load(c) & AM_MSBS;
    am_BitMask r = ~x + (x >> 7);
    memcpy(c, &r, AM_WIDTH);
}

static int am_neverfull(am_Table *t, const am_Ctrl *c) {
    am_BitMask before, after;
    am_Size  before_offset;
    if (t->mask+1 == AM_WIDTH) return 1;
    before_offset = (((c-t->ctrl) & t->mask) - AM_WIDTH) & t->mask;
    after  = am_group_empty(am_group(c));
    before = am_group_empty(am_group(&t->ctrl[before_offset]));
    return (am_clz64(before)/AM_WIDTH
            + (after ? am_ctz64(after)/AM_WIDTH : AM_WIDTH)) < AM_WIDTH;
}

#endif

static void am_resettable(am_Table *t) {
    if (!t->ctrl) return;
    memset(t->ctrl, AM_EMPTY, t->mask+AM_WIDTH);
    t->count       = 0;
    t->deleted     = 0;
    t->growth_left = ((t->mask+1) / 8) * 7;
}

static const am_Entry *am_nextentry(am_Iterator *it) {
    const am_Table *t = it->t;
    am_Size idx;
    while (it->group == 0) {
        if ((it->offset += AM_WIDTH) >= t->mask + (t->mask != 0))
            return (*it = am_itertable(t)), (const am_Entry*)NULL;
        it->group = am_group_full(&t->ctrl[it->offset]);
    }
    it->group = am_group_next(it->group, &idx);
    return (it->entry = am_entry(t, it->offset + idx));
}

static void am_freetable(am_Solver *solver, am_Table *t) {
    if (t->ctrl) {
        size_t size = am_alignsize(t->mask + 1, t->entry_size);
        void* mem = t->ctrl - size;
        solver->allocf(solver->ud, mem, 0, size + t->mask+1 + AM_WIDTH);
        am_inittable(t, t->entry_size);
    }
}

static void am_delkey(am_Table *t, const am_Entry *e) {
    am_Ctrl *c = am_ctrl(t, e);
    am_writectrl(t, c, am_neverfull(t, c) ? AM_EMPTY : AM_DELETED);
    t->deleted = 1, --t->count, t->growth_left += (*c == AM_EMPTY);
}

static am_Hash am_hash(am_Symbol key) {
    am_Hash h = key.id;
    h ^= h >> 16; h *= 0x85ebca6b;
    h ^= h >> 13; h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

static int am_find(am_Iterator *it, am_Symbol key, am_Hash h) {
    const am_Table *t = it->t;
    am_Size stride = 0;
    if (!t->ctrl) return 0;
    it->offset = h & t->mask;
    for (;;) {
        am_Group g = am_group(&t->ctrl[it->offset]);
        it->group = am_group_match(g, am_fullmark(h));
        while (it->group != 0)
            if (am_nextentry(it)->key.id == key.id) return 1;
        if (am_group_empty(g)) return 0;
        stride += AM_WIDTH, it->offset = (it->offset + stride) & t->mask;
    }
}

static const am_Entry *am_gettable(const am_Table *t, am_Symbol key) {
    am_Iterator it = am_itertable((am_Table*)t);
    return am_find(&it, key, am_hash(key)) ? it.entry : NULL;
}

static int am_alloctable(am_Table *t, am_Solver *solver, am_Size count, am_Size entry_size) {
    size_t size = ((count + 1) * 8) / 7; /* 1/8 empty required */
    size_t capacity;
    void  *mem;
    if (size > (~(am_Size)0 >> 2)/entry_size) return 0;
    for (capacity = AM_WIDTH; capacity < size; capacity <<= 1)
        ;
    assert(count < capacity-1);
    size = am_alignsize(capacity, entry_size);
    mem = solver->allocf(solver->ud, NULL, size + capacity + AM_WIDTH, 0);
    if (mem == NULL) return 0;
    memset(t, 0, sizeof(am_Table));
    t->count       = count;
    t->mask        = (am_Size)capacity - 1;
    t->ctrl        = (am_Ctrl*)mem + size;
    t->growth_left = (capacity / 8) * 7 - count;
    t->entry_size  = entry_size;
    t->deleted     = 0;
    memset(t->ctrl, AM_EMPTY, capacity+AM_WIDTH);
    return 1;
}

static int am_resizetable(am_Solver *solver, am_Table *t) {
    am_Iterator it = am_itertable(t);
    const am_Entry *e;
    am_Table nt;
    if (!am_alloctable(&nt, solver, t->count, t->entry_size)) return 0;
    while ((e = am_nextentry(&it))) {
        am_Size offset = e->hash & nt.mask, stride = 0, idx;
        am_BitMask m;
        while ((m = am_group_empty(am_group(&nt.ctrl[offset]))) == 0)
            stride += AM_WIDTH, offset = (offset + stride) & nt.mask;
        am_group_next(m, &idx), offset = (offset + idx) & nt.mask;
        am_writectrl(&nt, &nt.ctrl[offset], am_fullmark(e->hash));
        memcpy((void*)am_entry(&nt, offset), e, nt.entry_size);
    }
    am_freetable(solver, t);
    return *t = nt, 1;
}

static am_Entry *am_findinsert(am_Iterator *it, am_Hash h) {
    const am_Table *t = it->t;
    am_Size stride = 0;
    it->offset = h & t->mask;
    while (!(it->group = am_group_nonfull(&t->ctrl[it->offset])))
        stride += AM_WIDTH, it->offset = (it->offset + stride) & t->mask;
    return (am_Entry*)am_nextentry(it);
}

static void am_relocentry(am_Iterator *it, char *tmp) {
    am_Table *t = (am_Table*)it->t;
    am_Iterator newit = am_itertable(t);
    am_Entry *ne, *e = (am_Entry*)it->entry;
    am_Ctrl  *nc, *c = am_ctrl(t, e);
    if (*c != AM_DELETED) return;
    for (;;) {
        am_Size offset = e->hash & t->mask;
        am_Size mask = t->mask & ~(AM_WIDTH-1);
        ne = am_findinsert(&newit, e->hash), nc = am_ctrl(t, ne);
        if ((((am_Size)(c-t->ctrl) - offset) & mask) ==
                (((am_Size)(nc-t->ctrl) - offset) & mask)) {
            am_writectrl(t, c, am_fullmark(e->hash));
            break;
        }
        if (*nc == AM_EMPTY) {
            memcpy(ne, e, t->entry_size);
            am_writectrl(t, c, AM_EMPTY);
            am_writectrl(t, nc, am_fullmark(e->hash));
            break;
        }
        assert(*nc == AM_DELETED);
        memcpy(tmp, ne,  t->entry_size);
        memcpy(ne,  e,   t->entry_size);
        memcpy(e,   tmp, t->entry_size);
        am_writectrl(t, nc, am_fullmark(ne->hash));
    }
}

static void am_tidytable(am_Table *t) {
    am_Iterator it = am_itertable(t);
    am_Ctrl *ctrl, *end;
    char buff[AM_MAX_ENTRYSIZE];
    assert(t->deleted && t->entry_size <= AM_MAX_ENTRYSIZE);
    for (ctrl = t->ctrl, end = ctrl + t->mask + 1; ctrl < end; ctrl += AM_WIDTH)
        am_group_tidy(ctrl);
    for (it.offset = 0; it.offset < t->mask; it.offset += AM_WIDTH) {
        it.group = am_group_deleted(&t->ctrl[it.offset]);
        while (it.group != 0) {
            am_nextentry(&it);
            am_relocentry(&it, buff);
        }
    }
    t->growth_left = ((t->mask+1) / 8) * 7 - t->count;
    t->deleted = 0;
}

static am_Entry *am_settable(am_Solver *solver, am_Table *t, am_Symbol key) {
    am_Iterator it = am_itertable((am_Table*)t);
    am_Hash h = am_hash(key);
    am_Entry *e;
    am_Ctrl *c;
    if (t->growth_left == 0 || (t->deleted && t->count*32 >= 24*(t->mask+1))) {
        /*if (t->growth_left != 0)
            am_tidytable(t);
        else */if (!am_resizetable(solver, t))
            return NULL;
    }
    assert(t->growth_left > 0);
    e = am_findinsert(&it, h), c = am_ctrl(t, e);
    {
        am_Ctrl *mem = t->ctrl - am_alignsize(t->mask + 1, t->entry_size);
        assert((am_Ctrl*)e >= mem && (am_Ctrl*)e < t->ctrl);
    }
    ++t->count, t->growth_left -= (*c == AM_EMPTY);
    am_writectrl(t, c, am_fullmark(h));
    e->key = key, e->hash = h;
    return e;
}

/* expression (row) */

static int am_isconstant(am_Row *row)
{ return row->terms.count == 0; }

static void am_freerow(am_Solver *solver, am_Row *row)
{ am_freetable(solver, &row->terms); }

static void am_resetrow(am_Row *row)
{ row->constant = 0.0f; am_resettable(&row->terms); }

static void am_initrow(am_Row *row) {
    am_key(row) = am_null();
    row->infeasible_next = am_null();
    row->constant = 0.0f;
    am_inittable(&row->terms, sizeof(am_Term));
}

static void am_multiply(am_Row *row, am_Num multiplier) {
    am_Iterator it = am_itertable(&row->terms);
    row->constant *= multiplier;
    while (am_nextentry(&it))
        ((am_Term*)it.entry)->multiplier *= multiplier;
}

static void am_addvar(am_Solver *solver, am_Row *row, am_Symbol sym, am_Num value) {
    am_Term *term;
    if (sym.id == 0 || am_nearzero(value)) return;
    term = (am_Term*)am_gettable(&row->terms, sym);
    if (term == NULL) {
        term = (am_Term*)am_settable(solver, &row->terms, sym);
        assert(term != NULL);
        term->multiplier = value;
    } else if (am_nearzero(term->multiplier += value))
        am_delkey(&row->terms, &term->entry);
}

static void am_addrow(am_Solver *solver, am_Row *row, const am_Row *other, am_Num multiplier) {
    am_Iterator it = am_itertable(&other->terms);
    am_Term *term;
    row->constant += other->constant*multiplier;
    while ((term = (am_Term*)am_nextentry(&it)))
        am_addvar(solver, row, am_key(term), term->multiplier*multiplier);
}

static void am_solvefor(am_Solver *solver, am_Row *row, am_Symbol enter, am_Symbol leave) {
    am_Term *term = (am_Term*)am_gettable(&row->terms, enter);
    am_Num reciprocal = 1.0f / term->multiplier;
    assert(enter.id != leave.id && !am_nearzero(term->multiplier));
    am_delkey(&row->terms, &term->entry);
    am_multiply(row, -reciprocal);
    if (leave.id != 0) am_addvar(solver, row, leave, reciprocal);
}

static void am_substitute(am_Solver *solver, am_Row *row, am_Symbol enter, const am_Row *other) {
    am_Term *term = (am_Term*)am_gettable(&row->terms, enter);
    if (!term) return;
    am_delkey(&row->terms, &term->entry);
    am_addrow(solver, row, other, term->multiplier);
}

/* variables & constraints */

AM_API int am_variableid(am_Var *var) { return var ? var->sym.id : -1; }
AM_API am_Num am_value(am_Var *var) { return var ? var->value : 0.0f; }
AM_API void am_usevariable(am_Var *var) { if (var) ++var->refcount; }

static am_Var *am_sym2var(am_Solver *solver, am_Symbol sym) {
    am_VarEntry *ve = (am_VarEntry*)am_gettable(&solver->vars, sym);
    assert(ve != NULL);
    return ve->var;
}

AM_API am_Var *am_newvariable(am_Solver *solver) {
    am_Var *var = (am_Var*)am_alloc(solver, &solver->varpool);
    am_Symbol sym = am_newsymbol(solver, AM_EXTERNAL);
    am_VarEntry *ve = (am_VarEntry*)am_settable(solver, &solver->vars, sym);
    assert(ve != NULL);
    memset(var, 0, sizeof(am_Var));
    var->sym      = sym;
    var->refcount = 1;
    var->solver   = solver;
    ve->var       = var;
    return var;
}

AM_API void am_delvariable(am_Var *var) {
    if (var && --var->refcount == 0) {
        am_Solver *solver = var->solver;
        am_VarEntry *e = (am_VarEntry*)am_gettable(&solver->vars, var->sym);
        assert(!var->dirty && e != NULL);
        am_delkey(&solver->vars, &e->entry);
        am_remove(var->constraint);
        am_free(&solver->varpool, var);
    }
}

AM_API am_Constraint *am_newconstraint(am_Solver *solver, am_Num strength) {
    am_Constraint *cons = (am_Constraint*)am_alloc(solver, &solver->conspool);
    memset(cons, 0, sizeof(*cons));
    cons->solver   = solver;
    cons->strength = am_nearzero(strength) ? AM_REQUIRED : strength;
    am_initrow(&cons->expression);
    am_key(cons).id = ++solver->constraint_count;
    am_key(cons).type = AM_EXTERNAL;
    ((am_ConsEntry*)am_settable(solver, &solver->constraints,
        am_key(cons)))->constraint = cons;
    return cons;
}

AM_API void am_delconstraint(am_Constraint *cons) {
    am_Solver *solver = cons ? cons->solver : NULL;
    am_Iterator it;
    am_ConsEntry *ce;
    if (cons == NULL) return;
    am_remove(cons);
    ce = (am_ConsEntry*)am_gettable(&solver->constraints, am_key(cons));
    assert(ce != NULL);
    am_delkey(&solver->constraints, &ce->entry);
    it = am_itertable(&cons->expression.terms);
    while (am_nextentry(&it))
        am_delvariable(am_sym2var(solver, it.entry->key));
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
        am_Term *term = (am_Term*)it.entry;
        am_usevariable(am_sym2var(cons->solver, am_key(term)));
        am_addvar(cons->solver, &cons->expression, am_key(term),
                term->multiplier*multiplier);
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
        am_delvariable(am_sym2var(cons->solver, it.entry->key));
    am_resetrow(&cons->expression);
}

AM_API int am_addterm(am_Constraint *cons, am_Var *var, am_Num multiplier) {
    if (cons == NULL || var == NULL || cons->marker.id != 0 ||
            cons->solver != var->solver) return AM_FAILED;
    assert(var->sym.id != 0);
    assert(var->solver == cons->solver);
    if (cons->relation == AM_GREATEQUAL) multiplier = -multiplier;
    am_addvar(cons->solver, &cons->expression, var->sym, multiplier);
    am_usevariable(var);
    return AM_OK;
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

AM_API int am_hasedit(am_Var *var)
{ return var != NULL && var->constraint != NULL; }

AM_API int am_hasconstraint(am_Constraint *cons)
{ return cons != NULL && cons->marker.id != 0; }

AM_API void am_autoupdate(am_Solver *solver, int auto_update)
{ solver->auto_update = auto_update; }

static void am_infeasible(am_Solver *solver, am_Row *row) {
    if (row->constant < 0.0f && !am_isdummy(row->infeasible_next)) {
        row->infeasible_next.id = solver->infeasible_rows.id;
        row->infeasible_next.type = AM_DUMMY;
        solver->infeasible_rows = am_key(row);
    }
}

static void am_markdirty(am_Solver *solver, am_Var *var) {
    if (var->dirty) return;
    var->next = solver->dirty_vars;
    solver->dirty_vars = var;
    var->dirty = 1;
    ++var->refcount;
}

static void am_substitute_rows(am_Solver *solver, am_Symbol var, am_Row *expr) {
    am_Iterator it = am_itertable(&solver->rows);
    while (am_nextentry(&it)) {
        am_Row *row = (am_Row*)it.entry;
        am_substitute(solver, row, var, expr);
        if (am_isexternal(am_key(row)))
            am_markdirty(solver, am_sym2var(solver, am_key(row)));
        else
            am_infeasible(solver, row);
    }
    am_substitute(solver, &solver->objective, var, expr);
}

static int am_takerow(am_Solver *solver, am_Symbol sym, am_Row *dst) {
    am_Row *row = (am_Row*)am_gettable(&solver->rows, sym);
    am_key(dst) = am_null();
    if (row == NULL) return AM_FAILED;
    am_delkey(&solver->rows, &row->entry);
    dst->constant   = row->constant;
    dst->terms      = row->terms;
    return AM_OK;
}

static int am_putrow(am_Solver *solver, am_Symbol sym, const am_Row *src) {
    am_Row *row;
    assert(am_gettable(&solver->rows, sym) == NULL);
    row = (am_Row*)am_settable(solver, &solver->rows, sym);
    row->infeasible_next = am_null();
    row->constant = src->constant;
    row->terms    = src->terms;
    return AM_OK;
}

static void am_mergerow(am_Solver *solver, am_Row *row, am_Symbol var, am_Num multiplier) {
    am_Row *oldrow = (am_Row*)am_gettable(&solver->rows, var);
    if (oldrow)
        am_addrow(solver, row, oldrow, multiplier);
    else
        am_addvar(solver, row, var, multiplier);
}

static int am_optimize(am_Solver *solver, am_Row *objective) {
    for (;;) {
        am_Symbol enter = am_null(), leave = am_null();
        am_Num r, min_ratio = AM_NUM_MAX;
        am_Iterator it = am_itertable(&objective->terms);
        am_Row tmp, *row;
        am_Term *term;

        assert(solver->infeasible_rows.id == 0);
        while ((term = (am_Term*)am_nextentry(&it)))
            if (!am_isdummy(am_key(term)) && term->multiplier < 0.0f)
            { enter = am_key(term); break; }
        if (enter.id == 0) return AM_OK;

        it = am_itertable(&solver->rows);
        while ((row = (am_Row*)am_nextentry(&it))) {
            if (am_isexternal(am_key(row))) continue;
            term = (am_Term*)am_gettable(&row->terms, enter);
            if (term == NULL || term->multiplier > 0.0f) continue;
            r = -row->constant / term->multiplier;
            if (r < min_ratio || (am_approx(r, min_ratio)
                        && am_key(row).id < leave.id))
                min_ratio = r, leave = am_key(row);
        }
        assert(leave.id != 0);
        if (leave.id == 0) return AM_FAILED;

        am_takerow(solver, leave, &tmp);
        am_solvefor(solver, &tmp, enter, leave);
        am_substitute_rows(solver, enter, &tmp);
        if (objective != &solver->objective)
            am_substitute(solver, objective, enter, &tmp);
        am_putrow(solver, enter, &tmp);
    }
}

static am_Row am_makerow(am_Solver *solver, am_Constraint *cons) {
    am_Iterator it = am_itertable(&cons->expression.terms);
    am_Row row;
    am_initrow(&row);
    row.constant = cons->expression.constant;
    while (am_nextentry(&it)) {
        am_Term *term = (am_Term*)it.entry;
        am_markdirty(solver, am_sym2var(solver, am_key(term)));
        am_mergerow(solver, &row, am_key(term), term->multiplier);
    }
    if (cons->relation != AM_EQUAL) {
        am_initsymbol(solver, &cons->marker, AM_SLACK);
        am_addvar(solver, &row, cons->marker, -1.0f);
        if (cons->strength < AM_REQUIRED) {
            am_initsymbol(solver, &cons->other, AM_ERROR);
            am_addvar(solver, &row, cons->other, 1.0f);
            am_addvar(solver, &solver->objective, cons->other, cons->strength);
        }
    } else if (cons->strength >= AM_REQUIRED) {
        am_initsymbol(solver, &cons->marker, AM_DUMMY);
        am_addvar(solver, &row, cons->marker, 1.0f);
    } else {
        am_initsymbol(solver, &cons->marker, AM_ERROR);
        am_initsymbol(solver, &cons->other,  AM_ERROR);
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
    if (am_isconstant(&solver->objective))
        solver->objective.constant = 0.0f;
    cons->marker = cons->other = am_null();
}

static int am_add_with_artificial(am_Solver *solver, am_Row *row, am_Constraint *cons) {
    am_Symbol a = am_newsymbol(solver, AM_SLACK);
    am_Iterator it;
    am_Row tmp;
    am_Term *term;
    int ret;
    --solver->symbol_count; /* artificial variable will be removed */
    am_initrow(&tmp);
    am_addrow(solver, &tmp, row, 1.0f);
    am_putrow(solver, a, row);
    am_initrow(row); /* row is useless */
    am_optimize(solver, &tmp);
    ret = am_nearzero(tmp.constant) ? AM_OK : AM_UNBOUND;
    am_freerow(solver, &tmp);
    if (am_takerow(solver, a, &tmp) == AM_OK) {
        am_Symbol enter = am_null();
        if (am_isconstant(&tmp)) { am_freerow(solver, &tmp); return ret; }
        it = am_itertable(&tmp.terms);
        while ((term = (am_Term*)am_nextentry(&it)))
            if (am_ispivotable(am_key(term))) { enter = am_key(term); break; }
        if (enter.id == 0) { am_freerow(solver, &tmp); return AM_UNBOUND; }
        am_solvefor(solver, &tmp, enter, a);
        am_substitute_rows(solver, enter, &tmp);
        am_putrow(solver, enter, &tmp);
    }
    it = am_itertable(&solver->rows);
    while ((row = (am_Row*)am_nextentry(&it))) {
        term = (am_Term*)am_gettable(&row->terms, a);
        if (term) am_delkey(&row->terms, &term->entry);
    }
    term = (am_Term*)am_gettable(&solver->objective.terms, a);
    if (term) am_delkey(&solver->objective.terms, &term->entry);
    if (ret != AM_OK) am_remove(cons);
    return ret;
}

static int am_try_addrow(am_Solver *solver, am_Row *row, am_Constraint *cons) {
    am_Symbol subject = am_null();
    am_Term *term;
    am_Iterator it = am_itertable(&row->terms);
    while ((term = (am_Term*)am_nextentry(&it)))
        if (am_isexternal(am_key(term))) { subject = am_key(term); break; }
    if (subject.id == 0 && am_ispivotable(cons->marker)) {
        am_Term *mterm = (am_Term*)am_gettable(&row->terms, cons->marker);
        if (mterm->multiplier < 0.0f) subject = cons->marker;
    }
    if (subject.id == 0 && am_ispivotable(cons->other)) {
        am_Term *oterm = (am_Term*)am_gettable(&row->terms, cons->other);
        if (oterm->multiplier < 0.0f) subject = cons->other;
    }
    if (subject.id == 0) {
        it = am_itertable(&row->terms);
        while ((term = (am_Term*)am_nextentry(&it)))
            if (!am_isdummy(am_key(term))) break;
        if (term == NULL) {
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
        am_Row *row = (am_Row*)it.entry;
        am_Term *term = (am_Term*)am_gettable(&row->terms, marker);
        if (term == NULL) continue;
        if (am_isexternal(am_key(row)))
            third = am_key(row);
        else if (term->multiplier < 0.0f) {
            am_Num r = -row->constant / term->multiplier;
            if (r < r1) r1 = r, first = am_key(row);
        } else {
            am_Num r = row->constant / term->multiplier;
            if (r < r2) r2 = r, second = am_key(row);
        }
    }
    return first.id ? first : second.id ? second : third;
}

static void am_delta_edit_constant(am_Solver *solver, am_Num delta, am_Constraint *cons) {
    am_Iterator it;
    am_Row *row;
    if ((row = (am_Row*)am_gettable(&solver->rows, cons->marker)) != NULL)
    { row->constant -= delta; am_infeasible(solver, row); return; }
    if ((row = (am_Row*)am_gettable(&solver->rows, cons->other)) != NULL)
    { row->constant += delta; am_infeasible(solver, row); return; }
    it = am_itertable(&solver->rows);
    while ((row = (am_Row*)am_nextentry(&it))) {
        am_Term *term = (am_Term*)am_gettable(&row->terms, cons->marker);
        if (term == NULL) continue;
        row->constant += term->multiplier*delta;
        if (am_isexternal(am_key(row)))
            am_markdirty(solver, am_sym2var(solver, am_key(row)));
        else
            am_infeasible(solver, row);
    }
}

static void am_dual_optimize(am_Solver *solver) {
    while (solver->infeasible_rows.id != 0) {
        am_Symbol cur, enter = am_null(), leave;
        am_Term *objterm, *term;
        am_Num r, min_ratio = AM_NUM_MAX;
        am_Iterator it;
        am_Row tmp, *row =
            (am_Row*)am_gettable(&solver->rows, solver->infeasible_rows);
        assert(row != NULL);
        leave = am_key(row);
        solver->infeasible_rows = row->infeasible_next;
        row->infeasible_next = am_null();
        if (am_nearzero(row->constant) || row->constant >= 0.0f) continue;
        it = am_itertable(&row->terms);
        while ((term = (am_Term*)am_nextentry(&it))) {
            if (am_isdummy(cur = am_key(term)) || term->multiplier <= 0.0f)
                continue;
            objterm = (am_Term*)am_gettable(&solver->objective.terms, cur);
            r = objterm ? objterm->multiplier / term->multiplier : 0.0f;
            if (min_ratio > r) min_ratio = r, enter = cur;
        }
        assert(enter.id != 0);
        am_takerow(solver, leave, &tmp);
        am_solvefor(solver, &tmp, enter, leave);
        am_substitute_rows(solver, enter, &tmp);
        am_putrow(solver, enter, &tmp);
    }
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
    am_inittable(&solver->vars, sizeof(am_VarEntry));
    am_inittable(&solver->constraints, sizeof(am_ConsEntry));
    am_inittable(&solver->rows, sizeof(am_Row));
    am_initpool(&solver->varpool, sizeof(am_Var));
    am_initpool(&solver->conspool, sizeof(am_Constraint));
    return solver;
}

AM_API void am_delsolver(am_Solver *solver) {
    am_Iterator it = am_itertable(&solver->constraints);
    am_ConsEntry *ce;
    am_Row *row;
    while ((ce = (am_ConsEntry*)am_nextentry(&it)))
        am_freerow(solver, &ce->constraint->expression);
    it = am_itertable(&solver->rows);
    while ((row = (am_Row*)am_nextentry(&it)))
        am_freerow(solver, row);
    am_freerow(solver, &solver->objective);
    am_freetable(solver, &solver->vars);
    am_freetable(solver, &solver->constraints);
    am_freetable(solver, &solver->rows);
    am_freepool(solver, &solver->varpool);
    am_freepool(solver, &solver->conspool);
    solver->allocf(solver->ud, solver, 0, sizeof(*solver));
}

AM_API void am_resetsolver(am_Solver *solver, int clear_constraints) {
    am_Iterator it = am_itertable(&solver->vars);
    if (!solver->auto_update) am_updatevars(solver);
    while (am_nextentry(&it)) {
        am_VarEntry *ve = (am_VarEntry*)it.entry;
        am_Constraint **cons = &ve->var->constraint;
        am_remove(*cons);
        *cons = NULL;
    }
    assert(am_nearzero(solver->objective.constant));
    assert(solver->infeasible_rows.id == 0);
    assert(solver->dirty_vars == NULL);
    if (!clear_constraints) return;
    am_resetrow(&solver->objective);
    it = am_itertable(&solver->constraints);
    while (am_nextentry(&it)) {
        am_Constraint *cons = ((am_ConsEntry*)it.entry)->constraint;
        if (cons->marker.id != 0)
            cons->marker = cons->other = am_null();
    }
    it = am_itertable(&solver->rows);
    while (am_nextentry(&it)) {
        am_delkey(&solver->rows, (am_Entry*)it.entry);
        am_freerow(solver, (am_Row*)it.entry);
    }
}

AM_API void am_updatevars(am_Solver *solver) {
    am_Var *var, *dead_vars = NULL;
    while (solver->dirty_vars != NULL) {
        var = solver->dirty_vars;
        solver->dirty_vars = var->next;
        var->dirty = 0;
        if (var->refcount == 1)
            var->next = dead_vars, dead_vars = var;
        else {
            am_Row *row = (am_Row*)am_gettable(&solver->rows, var->sym);
            var->value = row ? row->constant : 0.0f;
            --var->refcount;
        }
    }
    while (dead_vars != NULL) {
        var = dead_vars, dead_vars = var->next;
        am_delvariable(var);
    }
}

AM_API int am_add(am_Constraint *cons) {
    am_Solver *solver = cons ? cons->solver : NULL;
    int ret, oldsym = solver ? solver->symbol_count : 0;
    am_Row row;
    if (solver == NULL || cons->marker.id != 0) return AM_FAILED;
    row = am_makerow(solver, cons);
    if ((ret = am_try_addrow(solver, &row, cons)) != AM_OK) {
        am_remove_errors(solver, cons);
        solver->symbol_count = oldsym;
    } else {
        am_optimize(solver, &solver->objective);
        if (solver->auto_update) am_updatevars(solver);
    }
    assert(solver->infeasible_rows.id == 0);
    return ret;
}

AM_API void am_remove(am_Constraint *cons) {
    am_Solver *solver;
    am_Symbol marker;
    am_Row tmp;
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
    am_optimize(solver, &solver->objective);
    if (solver->auto_update) am_updatevars(solver);
}

AM_API int am_setstrength(am_Constraint *cons, am_Num strength) {
    if (cons == NULL) return AM_FAILED;
    strength = am_nearzero(strength) ? AM_REQUIRED : strength;
    if (cons->strength == strength) return AM_OK;
    if (cons->strength >= AM_REQUIRED || strength >= AM_REQUIRED)
    { am_remove(cons), cons->strength = strength; return am_add(cons); }
    if (cons->marker.id != 0) {
        am_Solver *solver = cons->solver;
        am_Num diff = strength - cons->strength;
        am_mergerow(solver, &solver->objective, cons->marker, diff);
        am_mergerow(solver, &solver->objective, cons->other,  diff);
        am_optimize(solver, &solver->objective);
        if (solver->auto_update) am_updatevars(solver);
    }
    cons->strength = strength;
    return AM_OK;
}

AM_API int am_addedit(am_Var *var, am_Num strength) {
    am_Solver *solver = var ? var->solver : NULL;
    am_Constraint *cons;
    if (var == NULL) return AM_FAILED;
    if (strength >= AM_STRONG) strength = AM_STRONG;
    if (var->constraint) return am_setstrength(var->constraint, strength);
    assert(var->sym.id != 0);
    cons = am_newconstraint(solver, strength);
    am_setrelation(cons, AM_EQUAL);
    am_addterm(cons, var, 1.0f); /* var must have positive signture */
    am_addconstant(cons, -var->value);
    if (am_add(cons) != AM_OK) assert(0);
    var->constraint = cons;
    var->edit_value = var->value;
    return AM_OK;
}

AM_API void am_deledit(am_Var *var) {
    if (var == NULL || var->constraint == NULL) return;
    am_delconstraint(var->constraint);
    var->constraint = NULL;
    var->edit_value = 0.0f;
}

AM_API void am_suggest(am_Var *var, am_Num value) {
    am_Solver *solver = var ? var->solver : NULL;
    am_Num delta;
    if (var == NULL) return;
    if (var->constraint == NULL) {
        am_addedit(var, AM_MEDIUM);
        assert(var->constraint != NULL);
    }
    delta = value - var->edit_value;
    var->edit_value = value;
    am_delta_edit_constant(solver, delta, var->constraint);
    am_dual_optimize(solver);
    if (solver->auto_update) am_updatevars(solver);
}


AM_NS_END

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#endif /* AM_IMPLEMENTATION */

/* cc: flags+='-shared -O2 -DAM_IMPLEMENTATION -xc'
   unixcc: output='amoeba.so'
   win32cc: output='amoeba.dll' */

