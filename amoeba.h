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

#define AM_REQUIRED     (1000.0*1000.0*1000.0)
#define AM_STRONG       (1000.0*1000.0)
#define AM_MEDIUM       (1000.0)
#define AM_WEAK         (1.0)

#include <stddef.h>


AM_NS_BEGIN

typedef struct am_Solver     am_Solver;
typedef struct am_Variable   am_Variable;
typedef struct am_Constraint am_Constraint;

typedef void *am_Allocf (void *ud, void *ptr, size_t nsize, size_t osize);

AM_API am_Solver *am_newsolver   (am_Allocf *allocf, void *ud);
AM_API void       am_resetsolver (am_Solver *solver, int clear_constrants);
AM_API void       am_delsolver   (am_Solver *solver);

AM_API int am_hasvariable   (am_Variable *var);
AM_API int am_hasedit       (am_Variable *var);
AM_API int am_hasconstraint (am_Constraint *cons);

AM_API int  am_add    (am_Constraint *cons);
AM_API void am_remove (am_Constraint *cons);

AM_API void am_addedit (am_Variable *var, double strength);
AM_API void am_suggest (am_Variable *var, double value);
AM_API void am_deledit (am_Variable *var);

AM_API am_Variable *am_newvariable (am_Solver *solver);
AM_API void         am_usevariable (am_Variable *var);
AM_API void         am_delvariable (am_Variable *var);
AM_API int          am_variableid  (am_Variable *var);
AM_API double       am_value       (am_Variable *var);

AM_API am_Constraint *am_newconstraint   (am_Solver *solver, double strength);
AM_API am_Constraint *am_cloneconstraint (am_Constraint *other, double strength);

AM_API void am_resetconstraint (am_Constraint *cons);
AM_API void am_delconstraint   (am_Constraint *cons);

AM_API int am_addterm     (am_Constraint *cons, am_Variable *var, double multiplier);
AM_API int am_setrelation (am_Constraint *cons, int relation);
AM_API int am_addconstant (am_Constraint *cons, double constant);
AM_API int am_setstrength (am_Constraint *cons, double strength);

AM_API int am_mergeconstraint (am_Constraint *cons, am_Constraint *other, double multiplier);

AM_NS_END


#endif /* amoeba_h */


#if defined(AM_IMPLEMENTATION) && !defined(am_implemented)
#define am_implemented


#include <assert.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

#define AM_EXTERNAL     (0)
#define AM_SLACK        (1)
#define AM_ERROR        (2)
#define AM_DUMMY        (3)

#define am_isexternal(key)   (am_key(key).type == AM_EXTERNAL)
#define am_isslack(key)      (am_key(key).type == AM_SLACK)
#define am_iserror(key)      (am_key(key).type == AM_ERROR)
#define am_isdummy(key)      (am_key(key).type == AM_DUMMY)
#define am_isrestricted(key) (!am_isexternal(key))
#define am_ispivotable(key)  (am_isslack(key) || am_iserror(key))

#define AM_POOLSIZE     4096
#define AM_MIN_HASHSIZE 4
#define AM_MAX_SIZET    ((~(size_t)0)-100)


AM_NS_BEGIN

typedef struct am_Symbol {
    unsigned id   : 30;
    unsigned type : 2;
} am_Symbol;

typedef struct am_MemPool {
    size_t size;
    void  *freed;
    void  *pages;
} am_MemPool;

typedef struct am_Entry {
    am_Symbol key;
    ptrdiff_t next;
} am_Entry;

typedef struct am_Table {
    size_t    size;
    size_t    count;
    size_t    entry_size;
    size_t    lastfree;
    am_Entry *hash;
} am_Table;

typedef struct am_VarEntry {
    am_Entry     entry;
    am_Variable *variable;
} am_VarEntry;

typedef struct am_ConsEntry {
    am_Entry       entry;
    am_Constraint *constraint;
} am_ConsEntry;

typedef struct am_Term {
    am_Entry entry;
    double   multiplier;
} am_Term;

typedef struct am_Row {
    am_Entry entry;
    am_Table terms;
    double   constant;
} am_Row;

struct am_Variable {
    am_Symbol      sym;
    unsigned       refcount;
    am_Solver     *solver;
    am_Constraint *constraint;
    double         edit_value;
    double         value;
};

struct am_Constraint {
    am_Row     expression;
    am_Table   vars;
    am_Symbol  marker;
    am_Symbol  other;
    int        relation;
    am_Solver *solver;
    double     strength;
};

struct am_Solver {
    am_Allocf *allocf;
    void      *ud;
    unsigned   symbol_count;
    unsigned   constraint_count;
    am_Row     objective;
    am_Table   vars;            /* symbol -> VarEntry */
    am_Table   constraints;     /* symbol -> ConsEntry */
    am_Table   rows;            /* symbol -> Row */
    am_Table   infeasible_rows; /* symbol set */
    am_MemPool varpool;
    am_MemPool conspool;
};


/* utils */

static am_Symbol am_newsymbol(am_Solver *solver, int type);

static int am_approx(double a, double b)
{ return a > b ? a - b < 1e-6 : b - a < 1e-6; }

static int am_nearzero(double a)
{ return am_approx(a, 0.0); }

static am_Symbol am_null()
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

#define am_key(entry) (((am_Entry*)(entry))->key)

#define am_offset(lhs, rhs) ((char*)(lhs) - (char*)(rhs))
#define am_index(h, i)      ((am_Entry*)((char*)(h) + (i)))

static am_Entry *am_newkey(am_Solver *solver, am_Table *t, am_Symbol key);

static void am_delkey(am_Table *t, am_Entry *entry)
{ entry->key = am_null(), --t->count; }

static void am_inittable(am_Table *t, size_t entry_size)
{ memset(t, 0, sizeof(*t)), t->entry_size = entry_size; }

static am_Entry *am_mainposition(const am_Table *t, am_Symbol key) {
    assert((t->size & (t->size - 1)) == 0);
    return am_index(t->hash, (key.id & (t->size - 1))*t->entry_size);
}

static size_t am_hashsize(am_Table *t, size_t len) {
    size_t newsize = AM_MIN_HASHSIZE;
    const size_t max_size = (AM_MAX_SIZET / 2) / t->entry_size;
    while (newsize < max_size && newsize < len)
        newsize <<= 1;
    return newsize < len ? 0 : newsize;
}

static void am_freetable(am_Solver *solver, am_Table *t) {
    size_t size = t->size*t->entry_size;
    if (size) solver->allocf(solver->ud, t->hash, 0, size);
    am_inittable(t, t->entry_size);
}

static void am_resettable(am_Table *t) {
    size_t i, size = t->size * t->entry_size;
    t->count = 0;
    t->lastfree = size;
    for (i = 0; i < size; i += t->entry_size) {
        am_Entry *e = am_index(t->hash, i);
        e->key  = am_null();
        e->next = 0;
    }
}

static size_t am_resizetable(am_Solver *solver, am_Table *t, size_t len) {
    size_t i, oldsize = t->size * t->entry_size;
    am_Table nt = *t;
    nt.size = am_hashsize(t, len);
    nt.lastfree = nt.size*nt.entry_size;
    nt.hash = (am_Entry*)solver->allocf(solver->ud, NULL, nt.lastfree, 0);
    memset(nt.hash, 0, nt.size*nt.entry_size);
    for (i = 0; i < oldsize; i += nt.entry_size) {
        am_Entry *e = am_index(t->hash, i);
        if (e->key.id != 0) {
            am_Entry *ne = am_newkey(solver, &nt, e->key);
            if (t->entry_size > sizeof(am_Entry))
                memcpy(ne + 1, e + 1, t->entry_size-sizeof(am_Entry));
        }
    }
    if (oldsize) solver->allocf(solver->ud, t->hash, 0, oldsize);
    *t = nt;
    return t->size;
}

static am_Entry *am_newkey(am_Solver *solver, am_Table *t, am_Symbol key) {
    if (t->size == 0) am_resizetable(solver, t, AM_MIN_HASHSIZE);
    for (;;) {
        am_Entry *mp = am_mainposition(t, key);
        if (mp->key.id != 0) {
            am_Entry *f = NULL, *othern;
            while (t->lastfree > 0) {
                am_Entry *e = am_index(t->hash, t->lastfree -= t->entry_size);
                if (e->key.id == 0 && e->next == 0)  { f = e; break; }
            }
            if (f == NULL) { am_resizetable(solver, t, t->count*2); continue; }
            assert(f->key.id == 0);
            othern = am_mainposition(t, mp->key);
            if (othern != mp) {
                am_Entry *next;
                while ((next = am_index(othern, othern->next)) != mp)
                    othern = next;
                othern->next = am_offset(f, othern);
                memcpy(f, mp, t->entry_size);
                if (mp->next != 0) { f->next += am_offset(mp, f); mp->next = 0; }
            }
            else {
                if (mp->next != 0) f->next = am_offset(mp, f) + mp->next;
                else assert(f->next == 0);
                mp->next = am_offset(f, mp); mp = f;
            }
        }
        mp->key = key;
        return mp;
    }
}

static const am_Entry *am_gettable(const am_Table *t, am_Symbol key) {
    const am_Entry *e;
    if (t->size == 0 || key.id == 0) return NULL;
    assert((t->size & (t->size - 1)) == 0);
    e = am_mainposition(t, key);
    while (1) {
        ptrdiff_t next = e->next;
        if (e->key.id == key.id) return e;
        if (next == 0) return NULL;
        e = am_index(e, next);
    }
    return NULL;
}

static am_Entry *am_settable(am_Solver *solver, am_Table *t, am_Symbol key) {
    am_Entry *e;
    assert(key.id != 0);
    if ((e = (am_Entry*)am_gettable(t, key)) != NULL) return e;
    e = am_newkey(solver, t, key);
    if (t->entry_size > sizeof(am_Entry))
        memset(e + 1, 0, t->entry_size-sizeof(am_Entry));
    ++t->count;
    return e;
}

static int am_nextentry(const am_Table *t, am_Entry **pentry) {
    size_t i = *pentry ? am_offset(*pentry, t->hash) + t->entry_size : 0;
    size_t size = t->size*t->entry_size;
    for (; i < size; i += t->entry_size) {
        am_Entry *e = am_index(t->hash, i);
        if (e->key.id != 0) { *pentry = e; return 1; }
    }
    *pentry = NULL;
    return 0;
}


/* expression (row) */

static int am_isconstant(am_Row *row)
{ return row->terms.count == 0; }

static void am_freerow(am_Solver *solver, am_Row *row)
{ am_freetable(solver, &row->terms); }

static void am_initrow(am_Row *row) {
    am_key(row) = am_null();
    row->constant = 0.0;
    am_inittable(&row->terms, sizeof(am_Term));
}

static void am_resetrow(am_Row *row) {
    am_Term *term = NULL;
    row->constant = 0.0;
    while (am_nextentry(&row->terms, (am_Entry**)&term))
        am_delkey(&row->terms, &term->entry);
}

static void am_multiply(am_Row *row, double multiplier) {
    am_Term *term = NULL;
    row->constant *= multiplier;
    while (am_nextentry(&row->terms, (am_Entry**)&term))
        term->multiplier *= multiplier;
}

static void am_addvar(am_Solver *solver, am_Row *row, am_Symbol sym, double value) {
    am_Term *term;
    if (sym.id == 0) return;
    if ((term = (am_Term*)am_gettable(&row->terms, sym)) == NULL)
        term = (am_Term*)am_settable(solver, &row->terms, sym);
    if (am_nearzero(term->multiplier += value))
        am_delkey(&row->terms, &term->entry);
}

static void am_addrow(am_Solver *solver, am_Row *row, const am_Row *other, double multiplier) {
    am_Term *term = NULL;
    row->constant += other->constant*multiplier;
    while (am_nextentry(&other->terms, (am_Entry**)&term))
        am_addvar(solver, row, am_key(term), term->multiplier*multiplier);
}

static void am_solvefor(am_Solver *solver, am_Row *row, am_Symbol entry, am_Symbol exit) {
    am_Term *term = (am_Term*)am_gettable(&row->terms, entry);
    double reciprocal = 1.0 / term->multiplier;
    assert(entry.id != exit.id && !am_nearzero(term->multiplier));
    am_delkey(&row->terms, &term->entry);
    am_multiply(row, -reciprocal);
    if (exit.id != 0) am_addvar(solver, row, exit, reciprocal);
}

static void am_substitute(am_Solver *solver, am_Row *row, am_Symbol entry, const am_Row *other) {
    am_Term *term = (am_Term*)am_gettable(&row->terms, entry);
    if (!term) return;
    am_delkey(&row->terms, &term->entry);
    am_addrow(solver, row, other, term->multiplier);
}


/* variables & constraints */

AM_API int am_variableid(am_Variable *var) { return var ? am_key(var).id : -1; }
AM_API double am_value(am_Variable *var) { return var ? var->value : 0.0; }
AM_API void am_usevariable(am_Variable *var) { if (var) ++var->refcount; }

AM_API am_Variable *am_newvariable(am_Solver *solver) {
    am_Variable *var = (am_Variable*)am_alloc(solver, &solver->varpool);
    memset(var, 0, sizeof(*var));
    am_key(var)   = am_newsymbol(solver, AM_EXTERNAL);
    var->refcount = 1;
    var->solver   = solver;
    return var;
}

AM_API void am_delvariable(am_Variable *var) {
    if (var && --var->refcount <= 0) {
        am_Solver *solver = var->solver;
        am_VarEntry *e = (am_VarEntry*)am_gettable(&solver->vars, var->sym);
        if (e != NULL) am_delkey(&solver->vars, &e->entry);
        am_remove(var->constraint);
        am_free(&solver->varpool, var);
    }
}

AM_API am_Constraint *am_newconstraint(am_Solver *solver, double strength) {
    am_ConsEntry *ce;
    am_Constraint *cons = (am_Constraint*)am_alloc(solver, &solver->conspool);
    memset(cons, 0, sizeof(*cons));
    cons->solver   = solver;
    cons->strength = am_nearzero(strength) ? AM_REQUIRED : strength;
    am_initrow(&cons->expression);
    am_inittable(&cons->vars, sizeof(am_VarEntry));
    am_key(cons).id = ++solver->constraint_count;
    am_key(cons).type = AM_EXTERNAL;
    ce = (am_ConsEntry*)am_settable(solver, &solver->constraints, am_key(cons));
    ce->constraint = cons;
    return cons;
}

AM_API void am_delconstraint(am_Constraint *cons) {
    am_Solver *solver = cons ? cons->solver : NULL;
    am_ConsEntry *ce;
    am_VarEntry *ve = NULL;
    if (cons == NULL) return;
    am_remove(cons);
    ce = (am_ConsEntry*)am_gettable(&solver->constraints, am_key(cons));
    assert(ce != NULL);
    am_delkey(&solver->constraints, &ce->entry);
    while (am_nextentry(&cons->vars, (am_Entry**)&ve))
        am_delvariable(ve->variable);
    am_freetable(solver, &cons->vars);
    am_freerow(solver, &cons->expression);
    am_free(&solver->conspool, cons);
}

AM_API am_Constraint *am_cloneconstraint(am_Constraint *other, double strength) {
    am_Constraint *cons;
    if (other == NULL) return NULL;
    cons = am_newconstraint(other->solver,
            am_nearzero(strength) ? other->strength : strength);
    am_mergeconstraint(cons, other, 1.0);
    cons->relation = other->relation;
    return cons;
}

AM_API int am_mergeconstraint(am_Constraint *cons, am_Constraint *other, double multiplier) {
    am_VarEntry *ve = NULL, *nve;
    if (cons == NULL || other == NULL || cons->marker.id != 0
            || cons->solver != other->solver)
        return AM_FAILED;
    if (cons->relation == AM_GREATEQUAL) multiplier = -multiplier;
    am_addrow(cons->solver, &cons->expression, &other->expression, multiplier);
    while (am_nextentry(&other->vars, (am_Entry**)&ve)) {
        nve = (am_VarEntry*)am_settable(cons->solver, &cons->vars, am_key(ve));
        am_usevariable(ve->variable);
        nve->variable = ve->variable;
    }
    return AM_OK;
}

AM_API void am_resetconstraint(am_Constraint *cons) {
    am_VarEntry *ve = NULL;
    if (cons == NULL) return;
    am_remove(cons);
    cons->relation = 0;
    while (am_nextentry(&cons->vars, (am_Entry**)&ve)) {
        am_delvariable(ve->variable);
        am_delkey(&cons->vars, &ve->entry);
    }
    am_resetrow(&cons->expression);
}

AM_API int am_addterm(am_Constraint *cons, am_Variable *var, double multiplier) {
    am_VarEntry *ve;
    if (cons == NULL || cons->marker.id != 0 || cons->solver != var->solver)
        return AM_FAILED;
    assert(am_key(var).id != 0);
    assert(var->solver == cons->solver);
    if (cons->relation == AM_GREATEQUAL) multiplier = -multiplier;
    am_addvar(cons->solver, &cons->expression, var->sym, multiplier);
    ve = (am_VarEntry*)am_settable(cons->solver, &cons->vars, var->sym);
    am_usevariable(var);
    ve->variable = var;
    return AM_OK;
}

AM_API int am_addconstant(am_Constraint *cons, double constant) {
    if (cons == NULL || cons->marker.id != 0) return AM_FAILED;
    if (cons->relation == AM_GREATEQUAL)
        cons->expression.constant -= constant;
    else
        cons->expression.constant += constant;
    return AM_OK;
}

AM_API int am_setrelation(am_Constraint *cons, int relation) {
    assert(relation >= AM_LESSEQUAL && relation <= AM_GREATEQUAL);
    if (cons == NULL || cons->marker.id != 0 || cons->relation != 0)
        return AM_FAILED;
    if (relation != AM_GREATEQUAL) am_multiply(&cons->expression, -1.0);
    cons->relation = relation;
    return AM_OK;
}


/* Cassowary algorithm */

AM_API int am_hasvariable(am_Variable *var)
{ return var != NULL && am_gettable(&var->solver->vars,am_key(var)) != NULL; }

AM_API int am_hasedit(am_Variable *var)
{ return var != NULL && var->constraint != NULL; }

AM_API int am_hasconstraint(am_Constraint *cons)
{ return cons != NULL && cons->marker.id != 0; }

static void am_updatevars(am_Solver *solver) {
    am_VarEntry *ve = NULL;
    while (am_nextentry(&solver->vars, (am_Entry**)&ve)) {
        am_Row *row = (am_Row*)am_gettable(&solver->rows, am_key(ve));
        ve->variable->value = row ? row->constant : 0.0;
    }
}

static void am_substitute_rows(am_Solver *solver, am_Symbol var, am_Row *expr) {
    am_Row *row = NULL;
    while (am_nextentry(&solver->rows, (am_Entry**)&row)) {
        am_substitute(solver, row, var, expr);
        if (am_isrestricted(row) && row->constant < 0.0)
            am_settable(solver, &solver->infeasible_rows, am_key(row));
    }
    am_substitute(solver, &solver->objective, var, expr);
}

static int am_getrow(am_Solver *solver, am_Symbol sym, am_Row *dst) {
    am_Row *row = (am_Row*)am_gettable(&solver->rows, sym);
    am_key(dst) = am_null();
    if (row == NULL) return AM_FAILED;
    am_delkey(&solver->rows, &row->entry);
    dst->constant   = row->constant;
    dst->terms      = row->terms;
    return AM_OK;
}

static int am_putrow(am_Solver *solver, am_Symbol sym, const am_Row *src) {
    am_Row *row = (am_Row*)am_settable(solver, &solver->rows, sym);
    row->constant = src->constant;
    row->terms    = src->terms;
    return AM_OK;
}

static void am_mergerow(am_Solver *solver, am_Row *row, am_Symbol var, double multiplier) {
    am_Row *oldrow = (am_Row*)am_gettable(&solver->rows, var);
    if (oldrow) am_addrow(solver, row, oldrow, multiplier);
    else am_addvar(solver, row, var, multiplier);
}

static int am_optimize(am_Solver *solver, am_Row *objective) {
    for (;;) {
        am_Symbol enter = am_null(), exit = am_null();
        double min_ratio = DBL_MAX;
        am_Row tmp, *row = NULL;
        am_Term *term = NULL;

        while (am_nextentry(&objective->terms, (am_Entry**)&term)) {
            if (!am_isdummy(term) && term->multiplier < 0.0)
            { enter = am_key(term); break; }
        }
        if (enter.id == 0) return AM_OK;

        while (am_nextentry(&solver->rows, (am_Entry**)&row)) {
            term = (am_Term*)am_gettable(&row->terms, enter);
            if (term && am_ispivotable(row) && term->multiplier < 0.0) {
                double r = -row->constant / term->multiplier;
                if (r < min_ratio || (am_approx(r, min_ratio)
                            && am_key(row).id < exit.id))
                    min_ratio = r, exit = am_key(row);
            }
        }
        assert(exit.id != 0);
        if (exit.id == 0) return AM_FAILED;

        am_getrow(solver, exit, &tmp);
        am_solvefor(solver, &tmp, enter, exit);
        am_substitute_rows(solver, enter, &tmp);
        if (objective != &solver->objective)
            am_substitute(solver, objective, enter, &tmp);
        am_putrow(solver, enter, &tmp);
    }
}

static am_Row am_makerow(am_Solver *solver, am_Constraint *cons) {
    am_VarEntry *ve = NULL, *nve;
    am_Term *term = NULL;
    am_Row row;
    while (am_nextentry(&cons->vars, (am_Entry**)&ve)) {
        nve = (am_VarEntry*)am_settable(solver, &solver->vars, am_key(ve));
        nve->variable = ve->variable;
    }
    am_initrow(&row);
    row.constant = cons->expression.constant;
    while (am_nextentry(&cons->expression.terms, (am_Entry**)&term))
        am_mergerow(solver, &row, am_key(term), term->multiplier);
    if (cons->relation != AM_EQUAL) {
        am_initsymbol(solver, &cons->marker, AM_SLACK);
        am_addvar(solver, &row, cons->marker, -1.0);
        if (cons->strength < AM_REQUIRED) {
            am_initsymbol(solver, &cons->other, AM_ERROR);
            am_addvar(solver, &row, cons->other, 1.0);
            am_addvar(solver, &solver->objective, cons->other, cons->strength);
        }
    }
    else if (cons->strength >= AM_REQUIRED) {
        am_initsymbol(solver, &cons->marker, AM_DUMMY);
        am_addvar(solver, &row, cons->marker, 1.0);
    }
    else {
        am_initsymbol(solver, &cons->marker, AM_ERROR);
        am_initsymbol(solver, &cons->other,  AM_ERROR);
        am_addvar(solver, &row, cons->marker, -1.0);
        am_addvar(solver, &row, cons->other,   1.0);
        am_addvar(solver, &solver->objective, cons->marker, cons->strength);
        am_addvar(solver, &solver->objective, cons->other,  cons->strength);
    }
    if (row.constant < 0.0) am_multiply(&row, -1.0);
    return row;
}

static void am_remove_errors(am_Solver *solver, am_Constraint *cons) {
    if (am_iserror(&cons->marker))
        am_mergerow(solver, &solver->objective, cons->marker, -cons->strength);
    if (am_iserror(&cons->other))
        am_mergerow(solver, &solver->objective, cons->other, -cons->strength);
    if (am_isconstant(&solver->objective))
        solver->objective.constant = 0.0;
    cons->marker = cons->other = am_null();
}

static int am_add_with_artificial(am_Solver *solver, am_Row *row, am_Constraint *cons) {
    am_Symbol a = am_newsymbol(solver, AM_SLACK);
    am_Term *term = NULL;
    am_Row tmp;
    int ret;
    --solver->symbol_count; /* artificial variable will be removed */
    am_initrow(&tmp);
    am_addrow(solver, &tmp, row, 1.0);
    am_putrow(solver, a, row);
    am_initrow(row); row = NULL; /* row is useless */
    am_optimize(solver, &tmp);
    ret = am_nearzero(tmp.constant) ? AM_OK : AM_UNBOUND;
    am_freerow(solver, &tmp);
    if (am_getrow(solver, a, &tmp) == AM_OK) {
        am_Symbol entry = am_null();
        if (am_isconstant(&tmp)) { am_freerow(solver, &tmp); return ret; }
        while (am_nextentry(&tmp.terms, (am_Entry**)&term))
            if (am_ispivotable(term)) { entry = am_key(term); break; }
        if (entry.id == 0) { am_freerow(solver, &tmp); return AM_UNBOUND; }
        am_solvefor(solver, &tmp, entry, a);
        am_substitute_rows(solver, entry, &tmp);
        am_putrow(solver, entry, &tmp);
    }
    while (am_nextentry(&solver->rows, (am_Entry**)&row)) {
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
    am_Term *term = NULL;
    while (am_nextentry(&row->terms, (am_Entry**)&term)) {
        if (am_isexternal(term))
        { subject = am_key(term); break; }
    }
    if (subject.id == 0 && am_ispivotable(&cons->marker)) {
        am_Term *term = (am_Term*)am_gettable(&row->terms, cons->marker);
        if (term->multiplier < 0.0) subject = cons->marker;
    }
    if (subject.id == 0 && am_ispivotable(&cons->other)) {
        am_Term *term = (am_Term*)am_gettable(&row->terms, cons->other);
        if (term->multiplier < 0.0) subject = cons->other;
    }
    if (subject.id == 0) {
        while (am_nextentry(&row->terms, (am_Entry**)&term))
            if (!am_isdummy(term)) break;
        if (term == NULL && !am_nearzero(row->constant)) {
            am_freerow(solver, row);
            return AM_UNSATISFIED;
        }
        return am_add_with_artificial(solver, row, cons);
    }
    am_solvefor(solver, row, subject, am_null());
    am_substitute_rows(solver, subject, row);
    am_putrow(solver, subject, row);
    return AM_OK;
}

static am_Symbol am_get_leaving_row(am_Solver *solver, am_Symbol marker) {
    am_Symbol first = am_null(), second = am_null(), third = am_null();
    double r1 = DBL_MAX, r2 = DBL_MAX;
    am_Row *row = NULL;
    while (am_nextentry(&solver->rows, (am_Entry**)&row)) {
        am_Term *term = (am_Term*)am_gettable(&row->terms, marker);
        if (!term) continue;
        if (am_isexternal(row))
            third = am_key(row);
        else if (term->multiplier < 0.0) {
            double r = -row->constant / term->multiplier;
            if (r < r1) r1 = r, first = am_key(row);
        }
        else {
            double r = row->constant / term->multiplier;
            if (r < r2) r2 = r, second = am_key(row);
        }
    }
    return first.id ? first : second.id ? second : third;
}

static void am_delta_edit_constant(am_Solver *solver, double delta, am_Constraint *cons) {
    am_Row *row;
    if ((row = (am_Row*)am_gettable(&solver->rows, cons->marker)) != NULL) {
        if ((row->constant -= delta) < 0.0)
            am_settable(solver, &solver->infeasible_rows, cons->marker);
        return;
    }
    if ((row = (am_Row*)am_gettable(&solver->rows, cons->other)) != NULL) {
        if ((row->constant += delta) < 0.0)
            am_settable(solver, &solver->infeasible_rows, cons->other);
        return;
    }
    while (am_nextentry(&solver->rows, (am_Entry**)&row)) {
        am_Term *term = (am_Term*)am_gettable(&row->terms, cons->marker);
        if (term == NULL) continue;
        if ((row->constant += term->multiplier*delta) < 0.0)
            am_settable(solver, &solver->infeasible_rows, am_key(row));
    }
}

static void am_dual_optimize(am_Solver *solver) {
    for (;;) {
        double min_ratio = DBL_MAX, r;
        am_Term *term = NULL, *oterm;
        am_Entry *e = NULL;
        am_Symbol enter = am_null(), exit;
        am_Row *row, tmp;
        if (!am_nextentry(&solver->infeasible_rows, &e)) return;
        exit = am_key(e);
        am_delkey(&solver->infeasible_rows, e);
        row = (am_Row*)am_gettable(&solver->rows, exit);
        if (row == NULL || row->constant >= 0.0) continue;
        while (am_nextentry(&row->terms, (am_Entry**)&term)) {
            if (am_isdummy(term) || term->multiplier <= 0.0) continue;
            oterm = (am_Term*)am_gettable(&solver->objective.terms, am_key(term));
            r = oterm ? oterm->multiplier / term->multiplier : 0.0;
            if (min_ratio > r) min_ratio = r, enter = am_key(term);
        }
        assert(enter.id != 0);
        am_getrow(solver, exit, &tmp);
        am_solvefor(solver, &tmp, enter, exit);
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
    am_inittable(&solver->infeasible_rows, sizeof(am_Entry));
    am_initpool(&solver->varpool, sizeof(am_Variable));
    am_initpool(&solver->conspool, sizeof(am_Constraint));
    return solver;
}

AM_API void am_delsolver(am_Solver *solver) {
    am_ConsEntry *ce = NULL;
    am_Row *row = NULL;
    while (am_nextentry(&solver->constraints, (am_Entry**)&ce)) {
        am_Constraint *cons = ce->constraint;
        am_freetable(solver, &cons->vars);
        am_freerow(solver, &cons->expression);
    }
    while (am_nextentry(&solver->rows, (am_Entry**)&row))
        am_freerow(solver, row);
    am_freerow(solver, &solver->objective);
    am_freetable(solver, &solver->vars);
    am_freetable(solver, &solver->constraints);
    am_freetable(solver, &solver->rows);
    am_freetable(solver, &solver->infeasible_rows);
    am_freepool(solver, &solver->varpool);
    am_freepool(solver, &solver->conspool);
    solver->allocf(solver->ud, solver, 0, sizeof(*solver));
}

AM_API void am_resetsolver(am_Solver *solver, int clear_constrants) {
    am_Entry *entry = NULL;
    if (!clear_constrants) {
        while (am_nextentry(&solver->vars, &entry)) {
            am_Constraint **cons = &((am_VarEntry*)entry)->variable->constraint;
            am_remove(*cons);
            *cons = NULL;
        }
        assert(am_nearzero(solver->objective.constant));
        return;
    }
    solver->objective.constant = 0.0;
    am_resettable(&solver->objective.terms);
    am_resettable(&solver->vars);
    am_resettable(&solver->infeasible_rows);
    while (am_nextentry(&solver->constraints, &entry)) {
        am_Constraint *cons = ((am_ConsEntry*)entry)->constraint;
        if (cons->marker.id == 0) continue;
        cons->marker = cons->other = am_null();
    }
    while (am_nextentry(&solver->rows, &entry)) {
        am_delkey(&solver->rows, entry);
        am_freerow(solver, (am_Row*)entry);
    }
}

AM_API int am_add(am_Constraint *cons) {
    am_Solver *solver = cons ? cons->solver : NULL;
    am_Row row;
    int ret, oldsym = solver ? solver->symbol_count : 0;
    if (cons == NULL || cons->marker.id != 0) return AM_FAILED;
    row = am_makerow(solver, cons);
    if ((ret = am_try_addrow(solver, &row, cons)) != AM_OK) {
        am_remove_errors(solver, cons);
        solver->symbol_count = oldsym;
    }
    else {
        am_optimize(solver, &solver->objective);
        am_updatevars(solver);
    }
    return ret;
}

AM_API void am_remove(am_Constraint *cons) {
    am_Solver *solver;
    am_Symbol marker;
    am_Row tmp;
    if (cons == NULL || cons->marker.id == 0) return;
    solver = cons->solver, marker = cons->marker;
    am_remove_errors(solver, cons);
    if (am_getrow(solver, marker, &tmp) != AM_OK) {
        am_Symbol exit = am_get_leaving_row(solver, marker);
        assert(exit.id != 0);
        am_getrow(solver, exit, &tmp);
        am_solvefor(solver, &tmp, marker, exit);
        am_substitute_rows(solver, marker, &tmp);
    }
    am_freerow(solver, &tmp);
    am_optimize(solver, &solver->objective);
    am_updatevars(solver);
}

AM_API int am_setstrength(am_Constraint *cons, double strength) {
    strength = am_nearzero(strength) ? AM_REQUIRED : strength;
    if (cons->strength >= AM_REQUIRED) return AM_FAILED;
    if (cons->marker.id != 0) {
        am_Solver *solver = cons->solver;
        double diff = strength - cons->strength;
        am_mergerow(solver, &solver->objective, cons->marker, diff);
        am_mergerow(solver, &solver->objective, cons->other,  diff);
    }
    cons->strength = strength;
    return AM_OK;
}

AM_API void am_addedit(am_Variable *var, double strength) {
    am_Solver *solver = var ? var->solver : NULL;
    am_Constraint *cons;
    int ret;
    if (var == NULL) return;
    assert(am_key(var).id != 0);
    if (var->constraint != NULL) return;
    if (strength >= AM_STRONG) strength = AM_STRONG;
    cons = am_newconstraint(solver, strength);
    am_setrelation(cons, AM_EQUAL);
    am_addterm(cons, var, 1.0); /* var must have positive signture */
    am_addconstant(cons, -var->value);
    ret = am_add(cons);
    assert(ret == AM_OK);
    var->constraint = cons;
    var->edit_value = var->value;
}

AM_API void am_deledit(am_Variable *var) {
    if (var == NULL || var->constraint == NULL) return;
    am_delconstraint(var->constraint);
    var->constraint = NULL;
    var->edit_value = 0.0;
}

AM_API void am_suggest(am_Variable *var, double value) {
    am_Solver *solver = var ? var->solver : NULL;
    double delta;
    if (var == NULL) return;
    if (var->constraint == NULL) {
        am_addedit(var, AM_MEDIUM);
        assert(var->constraint != NULL);
    }
    delta = value - var->edit_value;
    var->edit_value = value;
    am_delta_edit_constant(solver, delta, var->constraint);
    am_dual_optimize(solver);
    am_updatevars(solver);
}

AM_NS_END


#endif /* AM_IMPLEMENTATION */

/* cc: flags+='-shared -O2 -DAM_IMPLEMENTATION -xc'
   unixcc: output='amoeba.so'
   win32cc: output='amoeba.dll' */

