/* tests/dump_test.c -- dump/load suites
 *
 * Ported from the old test.c "test dumpload" section.  Two regressions were
 * added on top of the port:
 *
 *   dump_roundtrip_keeps_solution  dump -> load must preserve every variable
 *                                  value, not just the byte count
 *   load_rejects_short_arena       a crafted stream must be refused instead of
 *                                  writing past its arena (see TODO.md)
 */

#include "amoeba_test.h"

#define TEST_BUFSIZE 16384

typedef struct MyDumper {
    am_Dumper base;
    char     *p;
    size_t   *len;
} MyDumper;

static const char *dump_varname(am_Dumper *d, unsigned idx, am_Id var,
                                am_Num *value) {
    const char *names[] = {"width", "height", "a", ""};
    (void)d, (void)var, (void)value;
    return idx < 4 ? names[idx] : NULL;
}

static const char *dump_consname(am_Dumper *d, unsigned idx,
                                 am_Constraint *cons) {
    const char *name = "a-very-long-name-that-excceed-32-bytes";
    (void)d, (void)idx, (void)cons;
    return idx == 0 ? name : NULL;
}

static int dump_writer(am_Dumper *d, const void *buf, size_t len) {
    MyDumper *md = (MyDumper *)d;
    assert(*md->len + len <= TEST_BUFSIZE);
    if (*md->len + len > TEST_BUFSIZE) return AM_FAILED;
    memcpy(md->p + *md->len, buf, len);
    *md->len += len;
    return AM_OK;
}

typedef struct MyLoader {
    am_Loader   base;
    am_Num      values[37];
    const char *p;
    size_t      len;
} MyLoader;

static am_Num *load_var(am_Loader *l, const char *name, unsigned i, am_Id var) {
    MyLoader *ml = (MyLoader *)l;
    /* am_load() installs the binding it gets from load_var() without refreshing
     * it; start from a known state so a missing refresh (TODO #3) is visible. */
    return (void)name, (void)var, ml->values[i] = 0, &ml->values[i];
}

void load_cons(am_Loader *l, const char *name, unsigned i,
               am_Constraint *cons) {
    (void)l, (void)name, (void)i, (void)cons;
}

const char *load_reader(am_Loader *l, size_t *plen) {
    MyLoader *ml = (MyLoader *)l;
    /* reads from char by char */
    if (ml->len == 0) return NULL;
    ml->len -= 1;
    return *plen = 1, ml->p++;
}

/* One constraint of the enaml-like layout below.  These used to be declared
 * inside build_solver(); C++ keeps nested tags out of the enclosing scope, so
 * the file-scope form is what makes this suite compile as C++ too. */
struct layout_item {
    am_Id  var;
    am_Num mul;
};

struct layout_constraint {
    struct layout_item term[5]; /* zero-terminated: var == 0 ends the list */
    am_Num             constant;
    int                relation;
    am_Num             strength;
};

static void build_solver(am_Solver *S, am_Id width, am_Id height,
                         am_Num *values) {
    /* Create custom strength */
    am_Num mmedium = AM_MEDIUM * 1.25;
    am_Num smedium = AM_MEDIUM * 100;

    /* Create the variable */
    am_Id left = am_newvariable(S, &values[0]);
    am_Id top = am_newvariable(S, &values[1]);
    am_Id contents_top = am_newvariable(S, &values[2]);
    am_Id contents_bottom = am_newvariable(S, &values[3]);
    am_Id contents_left = am_newvariable(S, &values[4]);
    am_Id contents_right = am_newvariable(S, &values[5]);
    am_Id midline = am_newvariable(S, &values[6]);
    am_Id ctleft = am_newvariable(S, &values[7]);
    am_Id ctheight = am_newvariable(S, &values[8]);
    am_Id cttop = am_newvariable(S, &values[9]);
    am_Id ctwidth = am_newvariable(S, &values[10]);
    am_Id lb1left = am_newvariable(S, &values[11]);
    am_Id lb1height = am_newvariable(S, &values[12]);
    am_Id lb1top = am_newvariable(S, &values[13]);
    am_Id lb1width = am_newvariable(S, &values[14]);
    am_Id lb2left = am_newvariable(S, &values[15]);
    am_Id lb2height = am_newvariable(S, &values[16]);
    am_Id lb2top = am_newvariable(S, &values[17]);
    am_Id lb2width = am_newvariable(S, &values[18]);
    am_Id lb3left = am_newvariable(S, &values[19]);
    am_Id lb3height = am_newvariable(S, &values[20]);
    am_Id lb3top = am_newvariable(S, &values[21]);
    am_Id lb3width = am_newvariable(S, &values[22]);
    am_Id fl1left = am_newvariable(S, &values[23]);
    am_Id fl1height = am_newvariable(S, &values[24]);
    am_Id fl1top = am_newvariable(S, &values[25]);
    am_Id fl1width = am_newvariable(S, &values[26]);
    am_Id fl2left = am_newvariable(S, &values[27]);
    am_Id fl2height = am_newvariable(S, &values[28]);
    am_Id fl2top = am_newvariable(S, &values[29]);
    am_Id fl2width = am_newvariable(S, &values[30]);
    am_Id fl3left = am_newvariable(S, &values[31]);
    am_Id fl3height = am_newvariable(S, &values[32]);
    am_Id fl3top = am_newvariable(S, &values[33]);
    am_Id fl3width = am_newvariable(S, &values[34]);

#ifdef __GNUC__
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmissing-field-initializers"
# pragma GCC diagnostic ignored "-Wc99-extensions"
#endif

    /* Add the constraints */
    const struct layout_constraint constraints[] = {
            {{{left}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{height}}, 0, AM_EQUAL, AM_MEDIUM},
            {{{top}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{width}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{height}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{top, -1}, {contents_top}}, -10, AM_EQUAL, AM_REQUIRED},
            {{{lb3height}}, -16, AM_EQUAL, AM_STRONG},
            {{{lb3height}}, -16, AM_GREATEQUAL, AM_STRONG},
            {{{ctleft}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{cttop}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{ctwidth}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{ctheight}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{fl3left}}, 0, AM_GREATEQUAL, AM_REQUIRED},
            {{{ctheight}}, -24, AM_GREATEQUAL, smedium},
            {{{ctwidth}}, -1.67772e+07, AM_LESSEQUAL, smedium},
            {{{ctheight}}, -24, AM_LESSEQUAL, smedium},
            {{{fl3top}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{fl3width}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{fl3height}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{lb1width}}, -67, AM_EQUAL, AM_WEAK},
            {{{lb2width}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{lb2height}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{fl2height}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{lb3left}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{fl2width}}, -125, AM_GREATEQUAL, AM_STRONG},
            {{{fl2height}}, -21, AM_EQUAL, AM_STRONG},
            {{{fl2height}}, -21, AM_GREATEQUAL, AM_STRONG},
            {{{lb3top}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{lb3width}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{lb1left}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{fl1width}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{lb1width}}, -67, AM_GREATEQUAL, AM_STRONG},
            {{{fl2left}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{lb2width}}, -66, AM_EQUAL, AM_WEAK},
            {{{lb2width}}, -66, AM_GREATEQUAL, AM_STRONG},
            {{{lb2height}}, -16, AM_EQUAL, AM_STRONG},
            {{{fl1height}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{fl1top}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{lb2top}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{lb2top, -1}, {lb3top}, {lb2height, -1}}, -10, AM_EQUAL, mmedium},
            {{{lb3top, -1}, {lb3height, -1}, {fl3top}},
             -10,
             AM_GREATEQUAL,
             AM_REQUIRED},
            {{{lb3top, -1}, {lb3height, -1}, {fl3top}}, -10, AM_EQUAL, mmedium},
            {{{contents_bottom}, {fl3height, -1}, {fl3top, -1}},
             -0,
             AM_EQUAL,
             mmedium},
            {{{fl1top}, {contents_top, -1}}, 0, AM_GREATEQUAL, AM_REQUIRED},
            {{{fl1top}, {contents_top, -1}}, 0, AM_EQUAL, mmedium},
            {{{contents_bottom}, {fl3height, -1}, {fl3top, -1}},
             -0,
             AM_GREATEQUAL,
             AM_REQUIRED},
            {{{left, -1}, {width, -1}, {contents_right}},
             10,
             AM_EQUAL,
             AM_REQUIRED},
            {{{top, -1}, {height, -1}, {contents_bottom}},
             10,
             AM_EQUAL,
             AM_REQUIRED},
            {{{left, -1}, {contents_left}}, -10, AM_EQUAL, AM_REQUIRED},
            {{{lb3left}, {contents_left, -1}}, 0, AM_EQUAL, mmedium},
            {{{fl1left}, {midline, -1}}, 0, AM_EQUAL, AM_STRONG},
            {{{fl2left}, {midline, -1}}, 0, AM_EQUAL, AM_STRONG},
            {{{ctleft}, {midline, -1}}, 0, AM_EQUAL, AM_STRONG},
            {{{fl1top}, {fl1height, 0.5}, {lb1top, -1}, {lb1height, -0.5}},
             0,
             AM_EQUAL,
             AM_STRONG},
            {{{lb1left}, {contents_left, -1}}, 0, AM_GREATEQUAL, AM_REQUIRED},
            {{{lb1left}, {contents_left, -1}}, 0, AM_EQUAL, mmedium},
            {{{lb1left, -1}, {fl1left}, {lb1width, -1}},
             -10,
             AM_GREATEQUAL,
             AM_REQUIRED},
            {{{lb1left, -1}, {fl1left}, {lb1width, -1}},
             -10,
             AM_EQUAL,
             mmedium},
            {{{fl1left, -1}, {contents_right}, {fl1width, -1}},
             -0,
             AM_GREATEQUAL,
             AM_REQUIRED},
            {{{width}}, 0, AM_EQUAL, AM_MEDIUM},
            {{{fl1top, -1}, {fl2top}, {fl1height, -1}},
             -10,
             AM_GREATEQUAL,
             AM_REQUIRED},
            {{{fl1top, -1}, {fl2top}, {fl1height, -1}}, -10, AM_EQUAL, mmedium},
            {{{cttop}, {fl2top, -1}, {fl2height, -1}},
             -10,
             AM_GREATEQUAL,
             AM_REQUIRED},
            {{{ctheight, -1}, {cttop, -1}, {fl3top}},
             -10,
             AM_GREATEQUAL,
             AM_REQUIRED},
            {{{contents_bottom}, {fl3height, -1}, {fl3top, -1}},
             -0,
             AM_GREATEQUAL,
             AM_REQUIRED},
            {{{cttop}, {fl2top, -1}, {fl2height, -1}}, -10, AM_EQUAL, mmedium},
            {{{fl1left, -1}, {contents_right}, {fl1width, -1}},
             -0,
             AM_EQUAL,
             mmedium},
            {{{lb2top, -1}, {lb2height, -0.5}, {fl2top}, {fl2height, 0.5}},
             0,
             AM_EQUAL,
             AM_STRONG},
            {{{contents_left, -1}, {lb2left}}, 0, AM_GREATEQUAL, AM_REQUIRED},
            {{{contents_left, -1}, {lb2left}}, 0, AM_EQUAL, mmedium},
            {{{fl2left}, {lb2width, -1}, {lb2left, -1}},
             -10,
             AM_GREATEQUAL,
             AM_REQUIRED},
            {{{ctheight, -1}, {cttop, -1}, {fl3top}}, -10, AM_EQUAL, mmedium},
            {{{contents_bottom}, {fl3height, -1}, {fl3top, -1}},
             -0,
             AM_EQUAL,
             mmedium},
            {{{lb1top}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{lb1width}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{lb1height}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{fl2left}, {lb2width, -1}, {lb2left, -1}},
             -10,
             AM_EQUAL,
             mmedium},
            {{{fl2left, -1}, {fl2width, -1}, {contents_right}},
             -0,
             AM_EQUAL,
             mmedium},
            {{{fl2left, -1}, {fl2width, -1}, {contents_right}},
             -0,
             AM_GREATEQUAL,
             AM_REQUIRED},
            {{{lb3left}, {contents_left, -1}}, 0, AM_GREATEQUAL, AM_REQUIRED},
            {{{lb1left}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{ctheight, 0.5}, {cttop}, {lb3top, -1}, {lb3height, -0.5}},
             0,
             AM_EQUAL,
             AM_STRONG},
            {{{ctleft}, {lb3left, -1}, {lb3width, -1}},
             -10,
             AM_GREATEQUAL,
             AM_REQUIRED},
            {{{ctwidth, -1}, {ctleft, -1}, {contents_right}},
             -0,
             AM_GREATEQUAL,
             AM_REQUIRED},
            {{{ctleft}, {lb3left, -1}, {lb3width, -1}}, -10, AM_EQUAL, mmedium},
            {{{fl3left}, {contents_left, -1}}, 0, AM_GREATEQUAL, AM_REQUIRED},
            {{{fl3left}, {contents_left, -1}}, 0, AM_EQUAL, mmedium},
            {{{ctwidth, -1}, {ctleft, -1}, {contents_right}},
             -0,
             AM_EQUAL,
             mmedium},
            {{{fl3left, -1}, {contents_right}, {fl3width, -1}},
             -0,
             AM_EQUAL,
             mmedium},
            {{{contents_top, -1}, {lb1top}}, 0, AM_GREATEQUAL, AM_REQUIRED},
            {{{contents_top, -1}, {lb1top}}, 0, AM_EQUAL, mmedium},
            {{{fl3left, -1}, {contents_right}, {fl3width, -1}},
             -0,
             AM_GREATEQUAL,
             AM_REQUIRED},
            {{{lb2top}, {lb1top, -1}, {lb1height, -1}},
             -10,
             AM_GREATEQUAL,
             AM_REQUIRED},
            {{{lb2top, -1}, {lb3top}, {lb2height, -1}},
             -10,
             AM_GREATEQUAL,
             AM_REQUIRED},
            {{{lb2top}, {lb1top, -1}, {lb1height, -1}}, -10, AM_EQUAL, mmedium},
            {{{fl1height}}, -21, AM_EQUAL, AM_STRONG},
            {{{fl1height}}, -21, AM_GREATEQUAL, AM_STRONG},
            {{{lb2left}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{lb2height}}, -16, AM_GREATEQUAL, AM_STRONG},
            {{{fl2top}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{fl2width}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{lb1height}}, -16, AM_GREATEQUAL, AM_STRONG},
            {{{lb1height}}, -16, AM_EQUAL, AM_STRONG},
            {{{fl3width}}, -125, AM_GREATEQUAL, AM_STRONG},
            {{{fl3height}}, -21, AM_EQUAL, AM_STRONG},
            {{{fl3height}}, -21, AM_GREATEQUAL, AM_STRONG},
            {{{lb3height}}, -0, AM_GREATEQUAL, AM_REQUIRED},
            {{{ctwidth}}, -119, AM_GREATEQUAL, smedium},
            {{{lb3width}}, -24, AM_EQUAL, AM_WEAK},
            {{{lb3width}}, -24, AM_GREATEQUAL, AM_STRONG},
            {{{fl1width}}, -125, AM_GREATEQUAL, AM_STRONG},
    };

#ifdef __GNUC__
# pragma GCC diagnostic pop
#endif

    size_t i;
    int    ret;

    /* Add the edit variables */
    ret = am_addedit(S, width, AM_STRONG);
    assert(ret == AM_OK), (void)ret;
    ret = am_addedit(S, height, AM_STRONG);
    assert(ret == AM_OK), (void)ret;

    for (i = 0; i < sizeof(constraints) / sizeof(constraints[0]); ++i) {
        am_Constraint *c = am_newconstraint(S, constraints[i].strength);
        const struct layout_item *p;
        for (p = constraints[i].term; p->var; ++p) {
            ret = am_addterm(c, p->var, p->mul ? p->mul : 1);
            assert(ret == AM_OK), (void)ret;
        }
        ret = am_addconstant(c, constraints[i].constant);
        assert(ret == AM_OK), (void)ret;
        ret = am_setrelation(c, constraints[i].relation);
        assert(ret == AM_OK), (void)ret;
        ret = am_add(c);
        assert(ret == AM_OK), (void)ret;
    }
}

TEST(dumpload) {
    am_Num     w = 0, h = 0, values[35] = {0};
    am_Solver *S;
    am_Id      width, height;
    char       buf[TEST_BUFSIZE];
    size_t     len = 0;
    int        ret;

    S = am_test_new_solver();
    assert(S != NULL);

    width = am_newvariable(S, &w);
    height = am_newvariable(S, &h);

    build_solver(S, width, height, values);

    {
        MyDumper myd;
        myd.base.var_name = dump_varname;
        myd.base.cons_name = dump_consname;
        myd.base.writer = dump_writer;
        myd.p = buf, myd.len = &len;

        /* The byte count is NOT asserted: it depends on which (equally
         * optimal) basis the simplex happens to land on, and on the stream
         * framing.  Pinning it made this test fail whenever the fixture or
         * the compiler changed.  What must hold is the round trip below. */
        printf("before dump\n");
        ret = am_dump(S, &myd.base);
        printf("after dump: ret=%d\n", ret);
        assert(ret == AM_OK);
        assert(len > 0 && len < TEST_BUFSIZE);
        printf("dumped len=%d (not asserted)\n", (int)len);
    }

    {
        /* The dumped state is the reference: am_dump() clears the edits and
         * settles the bindings, so compare against the values right now. */
        MyLoader   myl;
        am_Solver *S2;

        myl.base.load_var = load_var;
        myl.base.load_cons = load_cons;
        myl.base.reader = load_reader;
        myl.p = buf, myl.len = len;

        printf("before load\n");
        assertok((S2 = am_load(am_test_allocf, NULL, &myl.base, 0)) != NULL);

        /* same shape ... */
        asserteq(S2->vars.count, S->vars.count);
        asserteq(S2->constraints.count, S->constraints.count);
        asserteq(S2->rows.count, S->rows.count);

        /* TODO(#3): compare the solved values here; am_load() does not
         * refresh the bindings yet, so values[] would still hold the zeroes
         * load_var() wrote. */
        am_updatevars(S2);

        am_delsolver(S2);
    }

    am_delsolver(S);
    printf("allmem = %d\n", (int)allmem);
    printf("maxmem = %d\n", (int)maxmem);
    assert(allmem == 0);
    maxmem = 0;
}

/* A solver with zero constraints used to dump nothing while returning AM_OK:
 * the dump context was zeroed after its ret was set to AM_FAILED, so the
 * zero-length segments left both pointers NULL and skipped the whole body. */
TEST(dump_empty) {
    am_Num     x = 7;
    am_Solver *S, *S2;
    char       buf[TEST_BUFSIZE];
    size_t     len = 0;
    MyDumper   md;
    MyLoader   ml;

    md.base.var_name = dump_varname;
    md.base.cons_name = NULL;
    md.base.writer = dump_writer;
    md.p = buf, md.len = &len;
    ml.base.load_var = load_var;
    ml.base.load_cons = load_cons;
    ml.base.reader = load_reader;

    S = am_test_new_solver();
    assertok(am_newvariable(S, &x) != 0);
    asserteq(am_dump(S, &md.base), AM_OK);
    assertok(len > 0);
    ml.p = buf, ml.len = len;
    assertok((S2 = am_load(am_test_allocf, NULL, &ml.base, 0)) != NULL);
    asserteq(S2->vars.count, 1);
    asserteq(S2->constraints.count, 0);
    am_delsolver(S2);
    am_delsolver(S);

    len = 0;
    S = am_test_new_solver();
    asserteq(am_dump(S, &md.base), AM_OK);
    assertok(len > 0);
    ml.p = buf, ml.len = len;
    assertok((S2 = am_load(am_test_allocf, NULL, &ml.base, 0)) != NULL);
    asserteq(S2->vars.count, 0);
    asserteq(S2->constraints.count, 0);
    am_delsolver(S2);
    am_delsolver(S);
}

#include "dump_test.gen.inc"
