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

#define AM_POOLSIZE      4096
#define AM_MAX_SWAPSIZE  sizeof(am_Row)
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

typedef struct am_Header {
    am_Size    count;
    am_Symbol *keys;
    void      *values;
} am_Header;

typedef struct am_Table {
    am_Size   size;
    am_Size   value_size;
    am_Size   growth_left;
    am_Size   deleted;
    am_Ctrl  *ctrl; /* layout: [Header][^ctrl][keys][values] */
} am_Table;

typedef struct am_Iterator {
    const am_Table *t;
    am_Symbol       key;
    am_Size         offset;
} am_Iterator;

typedef struct am_Row {
    am_Num    constant;
    am_Symbol infeasible_next;
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
    am_Symbol  sym;
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
        if (newpage == NULL) return NULL;
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

static void am_swap(void *a, void *b, size_t len) {
    char buf[AM_MAX_SWAPSIZE];
    assert(len <= AM_MAX_SWAPSIZE);
    memcpy(buf, a, len), memcpy(a, b, len), memcpy(b, buf, len);
}

/* hash table */

#define AM_EMPTY    ((am_Ctrl)0x00)
#define AM_DELETED  ((am_Ctrl)0x01)
#define AM_USED     ((am_Ctrl)0x80)
#define AM_SENTINEL ((am_Ctrl)0x81)

#define AM_MIN_SIZE      sizeof(void*)

#define amH(t)             ((am_Header*)((t)->ctrl - sizeof(am_Header)))
#define amH_val(t,i)       ((char*)amH(t)->values + (i)*(t)->value_size)
#define amH_fingerprint(h) ((h) >> (AM_UNSIGNED_BITS - CHAR_BIT + 1)|AM_USED)
#define amH_setval(t,i,v)  memcpy(amH_val(t,i),(v),(t)->value_size)
#define am_ivalue(T,it)    ((T*)amH_val((it).t,(it).offset-1))

static am_Iterator am_itertable(const am_Table *t)
{ am_Iterator it; memset(&it, 0, sizeof(it)); it.t = t; return it; }

static void am_inittable(am_Table *t, size_t value_size)
{ memset(t, 0, sizeof(am_Table)); t->value_size = (am_Size)value_size; }

static void amH_resetgrowth(am_Table *t)
{ t->growth_left = ((t->size)/8)*7 - amH(t)->count, t->deleted = 0; }

static void am_resettable(am_Table *t) {
    if (!t->ctrl) return;
    memset(t->ctrl, AM_EMPTY, t->size);
    amH(t)->count = 0, amH_resetgrowth(t);
}

static int am_nextentry(am_Iterator *it) {
    const am_Table *t = it->t;
    am_Size cur, size = t->size;
    while ((cur = it->offset++) < size)
        if ((t->ctrl[cur] & AM_USED))
            return (it->key = amH(t)->keys[cur]), 1;
    return (it->offset = 0), 0;
}

static void am_freetable(am_Solver *solver, am_Table *t) {
    size_t hsize;
    if (t->ctrl == NULL) return;
    hsize = sizeof(am_Header) + t->size * (sizeof(am_Symbol)+t->value_size+1);
    solver->allocf(solver->ud, amH(t), 0, hsize);
    am_inittable(t, t->value_size);
}

static am_Hash amH_hash(am_Symbol key) {
    am_Hash h = key.id;
    h ^= h >> 16; h *= 0x85ebca6b;
    h ^= h >> 13; h *= 0xc2b2ae35;
    return h ^ (h >> 16);
}

static am_Header *amH_alloc(am_Solver *solver, const am_Table *t) {
    size_t count = ((t->ctrl ? amH(t)->count : 0) + 1) * 8 / 7;
    size_t capacity, hsize;
    am_Header *h;
    if (count > (~(am_Size)0 >> 2)) return NULL;
    for (capacity = AM_MIN_SIZE; capacity < count; capacity <<= 1)
        ;
    assert(count <= capacity && (capacity & (capacity-1)) == 0);
    hsize = sizeof(am_Header) + capacity * (sizeof(am_Symbol)+t->value_size+1);
    h = (am_Header*)solver->allocf(solver->ud, NULL, hsize, 0);
    if (h == NULL) return NULL;
    h->count = capacity;
    h->keys = (am_Symbol*)((am_Ctrl*)(h + 1) + capacity);
    h->values = h->keys + capacity;
    memset(h+1, AM_EMPTY, capacity);
    return h;
}

static void *amH_rawset(am_Table *t, am_Symbol key) {
    am_Hash hash = amH_hash(key);
    am_Size mask = t->size - 1, offset = hash & mask;
    assert(t->growth_left > 0);
    while (t->ctrl[offset] >= AM_USED)
        offset = (offset + 1) & mask;
    ++amH(t)->count;
    t->growth_left -= (t->ctrl[offset] == AM_EMPTY);
    t->deleted -= (t->ctrl[offset] == AM_DELETED);
    t->ctrl[offset] = amH_fingerprint(hash);
    amH(t)->keys[offset] = key;
    return amH_val(t, offset);
}

static int amH_resizetable(am_Solver *solver, am_Table *t) {
    am_Iterator it = am_itertable(t);
    am_Table nt;
    am_Header *h;
    if ((h = amH_alloc(solver, t)) == NULL) return 0;
    nt.value_size = t->value_size;
    nt.size = h->count, h->count = 0;
    nt.ctrl = (am_Ctrl*)(h+1);
    amH_resetgrowth(&nt);
    while (am_nextentry(&it)) {
        void *value = amH_rawset(&nt, it.key);
        memcpy(value, am_ivalue(void,it), t->value_size);
    }
    am_freetable(solver, t);
    return *t = nt, 1;
}

static void amH_tidytable(am_Table *t) {
    am_Header *h = amH(t);
    am_Size cur, idx, mask = t->size - 1;
    am_Hash hash;
    for (cur = 0; cur < t->size; ++cur)
        t->ctrl[cur] &= AM_USED;
    for (cur = 0; cur < t->size; ++cur) {
        if (t->ctrl[cur] == AM_EMPTY) continue;
        hash = amH_hash(h->keys[cur]), idx = hash & mask;
        while ((idx < cur && t->ctrl[idx] != AM_EMPTY) ||
                (idx > cur && t->ctrl[idx] == AM_SENTINEL))
            idx = (idx + 1) & mask;
        if (idx > cur) {
            if (t->ctrl[idx] == AM_USED) {
                am_swap(&h->keys[idx], &h->keys[cur], sizeof(am_Symbol));
                am_swap(amH_val(t,idx), amH_val(t,cur), t->value_size);
                cur -= 1; /* retry current */
            } else {
                h->keys[idx] = h->keys[cur];
                amH_setval(t, idx, amH_val(t, cur));
                t->ctrl[cur] = AM_EMPTY;
            }
            t->ctrl[idx] = AM_SENTINEL;
        } else {
            if (idx < cur) {
                assert(t->ctrl[idx] == AM_EMPTY);
                h->keys[idx] = h->keys[cur];
                amH_setval(t, idx, amH_val(t, cur));
                t->ctrl[cur] = AM_EMPTY;
            }
            t->ctrl[idx] = amH_fingerprint(hash);
        }
    }
    amH_resetgrowth(t);
}

static void am_deltable(am_Table *t, const void *value) {
    am_Header *h = (assert(t->ctrl), amH(t));
    am_Size offset = ((char*)value - (char*)h->values)/t->value_size;
    t->ctrl[offset] = AM_DELETED;
    t->deleted += 1, --h->count;
    if (t->deleted*4 > t->size) amH_tidytable(t);
}

static void *am_settable(am_Solver *solver, am_Table *t, am_Symbol key) {
    if (t->growth_left == 0 && !amH_resizetable(solver, t)) return NULL;
    if (t->deleted*4 > t->size) amH_tidytable(t);
    return amH_rawset(t, key);
}

static const void *am_gettable(const am_Table *t, am_Symbol key) {
    const am_Symbol *keys;
    am_Size remain, offset, mask = t->size - 1;
    am_Hash h = amH_hash(key), h2 = amH_fingerprint(h);
    if (!t->ctrl) return NULL;
    keys = amH(t)->keys;
    remain = t->size, offset = h & mask;
    while (t->ctrl[offset] != AM_EMPTY && remain > 0) {
        if (t->ctrl[offset] == h2 && keys[offset].id == key.id)
            return amH_val(t, offset);
        remain -= 1, offset = (offset + 1) & mask;
    }
    return NULL;
}


/* expression (row) */

static int am_isconstant(am_Row *row)
{ return row->terms.ctrl && amH(&row->terms)->count == 0; }

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
        *am_ivalue(am_Num,it) *= multiplier;
}

static void am_addvar(am_Solver *solver, am_Row *row, am_Symbol sym, am_Num value) {
    am_Num *term;
    if (sym.id == 0 || am_nearzero(value)) return;
    // TODO: make a find_or_insert routine
    term = (am_Num*)am_gettable(&row->terms, sym);
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
        am_addvar(solver, row, it.key, *am_ivalue(am_Num,it)*multiplier);
}

static void am_solvefor(am_Solver *solver, am_Row *row, am_Symbol enter, am_Symbol leave) {
    am_Num *term = (am_Num*)am_gettable(&row->terms, enter);
    am_Num reciprocal = 1.0f / *term;
    assert(enter.id != leave.id && !am_nearzero(*term));
    am_deltable(&row->terms, term);
    if (!am_approx(reciprocal, -1.0f)) am_multiply(row, -reciprocal);
    if (leave.id != 0) am_addvar(solver, row, leave, reciprocal);
}

static void am_substitute(am_Solver *solver, am_Row *row, am_Symbol enter, const am_Row *other) {
    am_Num *term = (am_Num*)am_gettable(&row->terms, enter);
    if (!term) return;
    am_deltable(&row->terms, term);
    am_addrow(solver, row, other, *term);
}

/* variables & constraints */

AM_API int am_variableid(am_Var *var) { return var ? var->sym.id : -1; }
AM_API am_Num am_value(am_Var *var) { return var ? var->value : 0.0f; }
AM_API void am_usevariable(am_Var *var) { if (var) ++var->refcount; }

#define am_checkvalue(t,p) if (!(p)) { am_deltable((t),(p)); return NULL; }

static am_Var *am_sym2var(am_Solver *solver, am_Symbol sym) {
    am_Var **ve = (am_Var**)am_gettable(&solver->vars, sym);
    assert(ve != NULL);
    return *ve;
}

AM_API am_Var *am_newvariable(am_Solver *solver) {
    am_Symbol sym = am_newsymbol(solver, AM_EXTERNAL);
    am_Var **ve = (am_Var**)am_settable(solver, &solver->vars, sym);
    if (ve == NULL) return NULL;
    *ve = (am_Var*)am_alloc(solver, &solver->varpool);
    am_checkvalue(&solver->vars, ve);
    memset(*ve, 0, sizeof(am_Var));
    (*ve)->sym      = sym;
    (*ve)->refcount = 1;
    (*ve)->solver   = solver;
    return *ve;
}

AM_API void am_delvariable(am_Var *var) {
    if (var && --var->refcount == 0) {
        am_Solver *solver = var->solver;
        am_Var **ve = (am_Var**)am_gettable(&solver->vars, var->sym);
        assert(!var->dirty && ve != NULL);
        am_deltable(&solver->vars, ve);
        am_remove(var->constraint);
        am_free(&solver->varpool, var);
    }
}

AM_API am_Constraint *am_newconstraint(am_Solver *solver, am_Num strength) {
    am_Symbol sym = { 0, AM_EXTERNAL };
    am_Constraint **ce = ((sym.id = ++solver->constraint_count),
            (am_Constraint**)am_settable(solver, &solver->constraints, sym));
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
    if (cons == NULL) return;
    am_remove(cons);
    ce = (am_Constraint**)am_gettable(&solver->constraints, cons->sym);
    assert(ce != NULL);
    am_deltable(&solver->constraints, ce);
    it = am_itertable(&cons->expression.terms);
    while (am_nextentry(&it))
        am_delvariable(am_sym2var(solver, it.key));
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
        am_usevariable(am_sym2var(cons->solver, it.key));
        am_addvar(cons->solver, &cons->expression, it.key,
                *am_ivalue(am_Num,it)*multiplier);
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
        am_delvariable(am_sym2var(cons->solver, it.key));
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

static void am_infeasible(am_Solver *solver, am_Symbol sym, am_Row *row) {
    if (row->constant < 0.0f && !am_isdummy(row->infeasible_next)) {
        row->infeasible_next.id = solver->infeasible_rows.id;
        row->infeasible_next.type = AM_DUMMY;
        solver->infeasible_rows = sym;
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
        am_Row *row = am_ivalue(am_Row,it);
        am_substitute(solver, row, var, expr);
        if (am_isexternal(it.key))
            am_markdirty(solver, am_sym2var(solver, it.key));
        else
            am_infeasible(solver, it.key, row);
    }
    am_substitute(solver, &solver->objective, var, expr);
}

static int am_takerow(am_Solver *solver, am_Symbol sym, am_Row *dst) {
    am_Row *row = (am_Row*)am_gettable(&solver->rows, sym);
    if (row == NULL) return AM_FAILED;
    am_deltable(&solver->rows, row);
    dst->constant   = row->constant;
    dst->terms      = row->terms;
    return AM_OK;
}

static int am_putrow(am_Solver *solver, am_Symbol sym, const am_Row *src) {
    am_Row *row;
    assert(am_gettable(&solver->rows, sym) == NULL);
    row = (am_Row*)am_settable(solver, &solver->rows, sym);
    assert(row != NULL);
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
        am_Num r, min_ratio = AM_NUM_MAX, *term;
        am_Iterator it = am_itertable(&objective->terms);
        am_Row tmp;

        assert(solver->infeasible_rows.id == 0);
        while (am_nextentry(&it))
            if (!am_isdummy(it.key) && *am_ivalue(am_Num,it) < 0.0f)
            { enter = it.key; break; }
        if (enter.id == 0) return AM_OK;

        it = am_itertable(&solver->rows);
        while (am_nextentry(&it)) {
            am_Row *row = am_ivalue(am_Row,it);
            if (am_isexternal(it.key)) continue;
            term = (am_Num*)am_gettable(&row->terms, enter);
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
        am_markdirty(solver, am_sym2var(solver, it.key));
        am_mergerow(solver, &row, it.key, *am_ivalue(am_Num,it));
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
    am_Num *term;
    int ret;
    --solver->symbol_count; /* artificial variable will be removed */
    am_initrow(&tmp);
    am_addrow(solver, &tmp, row, 1.0f);
    am_putrow(solver, a, row);
    if (am_optimize(solver, &tmp)) return AM_UNBOUND;
    ret = am_nearzero(tmp.constant) ? AM_OK : AM_UNBOUND;
    am_freerow(solver, &tmp);
    if (am_takerow(solver, a, &tmp) == AM_OK) {
        am_Symbol enter = am_null();
        if (am_isconstant(&tmp)) { am_freerow(solver, &tmp); return ret; }
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
        am_Row *row = am_ivalue(am_Row,it);
        am_Num *term = (am_Num*)am_gettable(&row->terms, a);
        if (term) am_deltable(&row->terms, term);
    }
    term = (am_Num*)am_gettable(&solver->objective.terms, a);
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
        am_Num *term = (am_Num*)am_gettable(&row->terms, cons->marker);
        if (*term < 0.0f) subject = cons->marker;
    }
    if (subject.id == 0 && am_ispivotable(cons->other)) {
        am_Num *term = (am_Num*)am_gettable(&row->terms, cons->other);
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
        am_Row *row = am_ivalue(am_Row,it);
        am_Num *term = (am_Num*)am_gettable(&row->terms, marker);
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

static void am_delta_edit_constant(am_Solver *solver, am_Num delta, am_Constraint *cons) {
    am_Iterator it;
    am_Row *row;
    if ((row = (am_Row*)am_gettable(&solver->rows, cons->marker)) != NULL)
    { row->constant -= delta; am_infeasible(solver, cons->marker, row); return; }
    if ((row = (am_Row*)am_gettable(&solver->rows, cons->other)) != NULL)
    { row->constant += delta; am_infeasible(solver, cons->other, row); return; }
    it = am_itertable(&solver->rows);
    while (am_nextentry(&it)) {
        am_Row *row = am_ivalue(am_Row,it);
        am_Num *term = (am_Num*)am_gettable(&row->terms, cons->marker);
        if (term == NULL) continue;
        row->constant += *term*delta;
        if (am_isexternal(it.key))
            am_markdirty(solver, am_sym2var(solver, it.key));
        else
            am_infeasible(solver, it.key, row);
    }
}

static void am_dual_optimize(am_Solver *solver) {
    while (solver->infeasible_rows.id != 0) {
        am_Symbol cur, enter = am_null(), leave;
        am_Num *objterm;
        am_Num r, min_ratio = AM_NUM_MAX;
        am_Iterator it;
        am_Row tmp, *row =
            (am_Row*)am_gettable(&solver->rows, solver->infeasible_rows);
        assert(row != NULL);
        leave = solver->infeasible_rows;
        solver->infeasible_rows = row->infeasible_next;
        row->infeasible_next = am_null();
        if (am_nearzero(row->constant) || row->constant >= 0.0f) continue;
        it = am_itertable(&row->terms);
        while (am_nextentry(&it)) {
            am_Num term = *am_ivalue(am_Num,it);
            if (am_isdummy(cur = it.key) || term <= 0.0f)
                continue;
            objterm = (am_Num*)am_gettable(&solver->objective.terms, cur);
            r = objterm ? *objterm / term : 0.0f;
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
    am_inittable(&solver->vars, sizeof(am_Var*));
    am_inittable(&solver->constraints, sizeof(am_Constraint*));
    am_inittable(&solver->rows, sizeof(am_Row));
    am_initpool(&solver->varpool, sizeof(am_Var));
    am_initpool(&solver->conspool, sizeof(am_Constraint));
    return solver;
}

AM_API void am_delsolver(am_Solver *solver) {
    am_Iterator it = am_itertable(&solver->constraints);
    while (am_nextentry(&it)) {
        am_Constraint **ce = am_ivalue(am_Constraint*,it);
        am_freerow(solver, &(*ce)->expression);
    }
    it = am_itertable(&solver->rows);
    while (am_nextentry(&it))
        am_freerow(solver, am_ivalue(am_Row,it));
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
        am_Var *var = *am_ivalue(am_Var*,it);
        am_Constraint **cons = &var->constraint;
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
        am_Constraint *cons = *am_ivalue(am_Constraint*,it);
        if (cons->marker.id != 0)
            cons->marker = cons->other = am_null();
    }
    it = am_itertable(&solver->rows);
    while (am_nextentry(&it)) {
        am_freerow(solver, am_ivalue(am_Row,it));
        am_deltable(&solver->rows, am_ivalue(void,it));
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
        if (am_optimize(solver, &solver->objective)) return AM_UNBOUND;
        if (solver->auto_update) am_updatevars(solver);
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
    assert(ret == AM_OK);
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
        if (am_optimize(solver, &solver->objective)) return AM_UNBOUND;
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

