/* tests/dump_test.c -- dump/load round trip and public loader contract
 *
 * Only externally observable behaviour is asserted here: the values, names,
 * metadata and edit state that survive a round trip, the loader/dumper
 * callbacks, the load error paths (bad width, truncated or trailing input) and
 * the zero-allocation window between am_load() and the first am_suggest().
 * The stream is a black box whose outer msgpack framing a third party can
 * scan; only load_tolerates_a_zeroed_row_key opens the scratch bin, to corrupt
 * one row key.
 */

#include "amoeba_test.h"
#include "stream.h"

#define TEST_BUFSIZE 32768
#define MYLOADER_MAX 64

typedef struct MyDumper {
    am_Dumper base;
    char     *p;
    size_t   *len;
} MyDumper;

/* names are derived from the id so a load can map a stream variable back to
 * the source variable the fixture kept a pointer to */
static char dump_varbuf[32];

/* C89-safe unsigned formatting: sprintf is deprecated by the macOS SDK */
static void putuint(char *p, unsigned v) {
    char     tmp[16];
    unsigned i = 0;
    do tmp[i++] = (char)('0' + v % 10), v /= 10;
    while (v);
    while (i) *p++ = tmp[--i];
    *p = 0;
}

static const char *dump_varname(am_Dumper *d, unsigned idx, am_Id var,
                                am_Num *value) {
    (void)d, (void)idx, (void)value;
    dump_varbuf[0] = 'v';
    putuint(dump_varbuf + 1, var);
    return dump_varbuf;
}

static const char *dump_consname(am_Dumper *d, unsigned idx,
                                 am_Constraint *cons) {
    const char *name = "a-very-long-name-that-excceed-32-bytes";
    (void)d, (void)cons;
    return idx == 0 ? name : NULL;
}

static int dump_writer(am_Dumper *d, const void *buf, size_t len) {
    MyDumper *md = (MyDumper *)d;
    assertok(*md->len + len <= TEST_BUFSIZE);
    if (*md->len + len > TEST_BUFSIZE) return AM_FAILED;
    memcpy(md->p + *md->len, buf, len);
    *md->len += len;
    return AM_OK;
}

typedef struct MyLoader {
    am_Loader      base;
    am_Num         values[MYLOADER_MAX];
    am_Id          ids[MYLOADER_MAX]; /* loaded id per stream index */
    unsigned       src[MYLOADER_MAX]; /* source id from the "v<id>" name */
    unsigned       nvars;
    am_Constraint *cons[8]; /* first public cons, stream order */
    const char    *p;
    size_t         len;
    char           cname[64];
} MyLoader;

static am_Num *load_var(am_Loader *l, const char *name, unsigned i, am_Id var) {
    MyLoader *ml = (MyLoader *)l;
    assertok(i < MYLOADER_MAX);
    /* a sentinel makes a missing settle visible even when 0 is expected */
    ml->ids[i] = var;
    ml->src[i] = name && name[0] == 'v' ? (unsigned)atoi(name + 1) : 0;
    if (ml->nvars <= i) ml->nvars = i + 1;
    return ml->values[i] = -12345, &ml->values[i];
}

static void load_cons(am_Loader *l, const char *name, unsigned i,
                      am_Constraint *cons) {
    MyLoader *ml = (MyLoader *)l;
    if (i < 8) ml->cons[i] = cons;
    if (name == NULL) return;
    if (strlen(name) < sizeof(ml->cname)) strcpy(ml->cname, name);
}

/* one byte per call: the chunked reader */
static const char *load_reader(am_Loader *l, size_t *plen) {
    MyLoader *ml = (MyLoader *)l;
    if (ml->len == 0) return NULL;
    ml->len -= 1;
    return *plen = 1, ml->p++;
}

/* Solutions come back in the build's am_Num width; compare with a relative
 * tolerance instead of bit equality. */
static void assertclose(am_Num got, am_Num want, const char *what) {
    double tol = 1e-5 * (1.0 + fabs((double)want));
    if (fabs((double)got - (double)want) > tol) {
        test_log("FAIL %s: got %g, expected %g (tol %g)\n", what, (double)got,
                 (double)want, tol);
        abort();
    }
}

/* am_terms() walks from an anchor id; find one variable's stored coefficient */
static int term_find(const am_Constraint *c, am_Id var, am_Num *coef) {
    am_Id id = 0;
    while ((id = am_terms(c, id, coef)) != 0)
        if (id == var) return 1;
    return 0;
}

static void loader_prepare(MyLoader *ml, const char *buf, size_t len) {
    memset(ml, 0, sizeof(*ml));
    ml->base.load_var = load_var;
    ml->base.load_cons = load_cons;
    ml->base.reader = load_reader;
    ml->p = buf, ml->len = len;
}

/* NULL is a legitimate answer here: the failure tests expect it */
static am_Solver *load_maybe(const char *buf, size_t len, MyLoader *ml) {
    loader_prepare(ml, buf, len);
    return am_load(am_test_allocf, NULL, &ml->base, 0);
}

static am_Solver *load_from(const char *buf, size_t len, MyLoader *ml) {
    am_Solver *L = load_maybe(buf, len, ml);
    assertok(L != NULL);
    return L;
}

static size_t dump_into(am_Solver *S, char *buf) {
    MyDumper md = AM_ZERO;
    size_t   len = 0;
    md.base.var_name = dump_varname;
    md.base.cons_name = dump_consname;
    md.base.writer = dump_writer;
    md.p = buf, md.len = &len;
    assertok(am_dump(S, &md.base) == AM_OK);
    return len;
}

static unsigned loaded_slot(const MyLoader *ml, unsigned src) {
    unsigned i;
    for (i = 0; i < ml->nvars; ++i)
        if (ml->src[i] == src) return i;
    assertok(0);
    return 0;
}

static am_Id loaded_id(const MyLoader *ml, unsigned src) {
    return ml->ids[loaded_slot(ml, src)];
}

static am_Num loaded_value(const MyLoader *ml, unsigned src) {
    return ml->values[loaded_slot(ml, src)];
}

/* the tests may read struct fields, but must not call internal helpers */
static am_Suggest *suggest_of(am_Solver *S, am_Id id) {
    unsigned i;
    for (i = 0; i < S->suggests.size; ++i)
        if (S->suggests.keys[i].sym.u.id == id)
            return *(am_Suggest **)((char *)S->suggests.vals
                                    + i * S->suggests.value_size);
    return NULL;
}

static am_Size vec_len(const void *a) {
    return a ? ((const am_Vec *)a)[-1].len : 0;
}

/* count what the public walkers expose; the table shape is private */
static unsigned count_vars(const am_Solver *S) {
    am_Id    id = 0;
    unsigned n = 0;
    while ((id = am_vars(S, id)) != 0) ++n;
    return n;
}

static unsigned count_cons(const am_Solver *S) {
    am_Constraint *c = NULL;
    unsigned       n = 0;
    while ((c = am_constraints(S, c)) != NULL) ++n;
    return n;
}

static unsigned count_terms(const am_Constraint *c) {
    am_Id    id = 0;
    am_Num   coef;
    unsigned n = 0;
    while ((id = am_terms(c, id, &coef)) != 0) ++n;
    return n;
}

/* ---- re-encoding a real dump at either float width ----
 *
 * A double build writes every number as msgpack float64, a float build as
 * float32, and the reader takes either.  A test binary is one build or the
 * other, so the other width's stream is made by re-encoding a real dump
 * instead of hand-writing one that could drift from the writer.
 *
 * v5 has one bin (the scratch) and the foreign num_size in meta is enough to
 * reject the stream, so the re-encoder copies the bin literally. */
typedef struct reenc {
    const unsigned char *r;
    unsigned char       *w;
    int                  bits; /* float width to re-encode every number to */
} reenc;

static void reenc_value(reenc *x);

static void reenc_raw(reenc *x, unsigned n) {
    while (n--) *x->w++ = *x->r++;
}

static void reenc_word(reenc *x, unsigned n) {
    *x->w++ = (unsigned char)(n >> 24), *x->w++ = (unsigned char)(n >> 16);
    *x->w++ = (unsigned char)(n >> 8), *x->w++ = (unsigned char)n;
}

static void reenc_seq(reenc *x, unsigned n, unsigned head) {
    unsigned i;
    reenc_raw(x, head);
    for (i = 0; i < n; ++i) reenc_value(x);
}

/* re-encode one float at the target width; copies when the source already
 * has that width */
static void reenc_float(reenc *x, unsigned ty, int be) {
    am_Bits  u;
    unsigned i;
    if (ty == 0xCA && x->bits == 64) {
        x->r += 1, *u.u32 = 0;
        for (i = 0; i < 4; ++i) *u.u32 = (*u.u32 << 8) | (unsigned)*x->r++;
        *x->w++ = 0xCB, u.f64 = (double)u.f32;
        reenc_word(x, u.u32[be ? 0 : 1]);
        reenc_word(x, u.u32[be ? 1 : 0]);
    } else if (ty == 0xCB && x->bits == 32) {
        x->r += 1;
        for (i = 0; i < 2; ++i) {
            unsigned w = 0;
            int      j;
            for (j = 0; j < 4; ++j) w = (w << 8) | (unsigned)*x->r++;
            u.u32[be ? i : 1 - i] = w;
        }
        *x->w++ = 0xCA, u.f32 = (float)u.f64;
        reenc_word(x, u.u32[0]);
    } else
        reenc_raw(x, ty == 0xCA ? 5 : 9);
}

/* the scratch bin is opaque here: same width in the header, copied whole */
static void reenc_bin(reenc *x) {
    unsigned ty = *x->r, n = 0, head;
    if (ty == 0xC4) {
        n = (unsigned)x->r[1], head = 2;
    } else if (ty == 0xC5) {
        n = ((unsigned)x->r[1] << 8) | x->r[2], head = 3;
    } else {
        n = ((unsigned)x->r[1] << 24) | ((unsigned)x->r[2] << 16)
          | ((unsigned)x->r[3] << 8) | x->r[4];
        head = 5;
    }
    reenc_raw(x, head + n);
}

/* walks the writer's msgpack subset: ints, arrays, strings, floats, bins */
static void reenc_value(reenc *x) {
    unsigned probe = 1, head, i, n, ty = *x->r;
    int      be = *(const unsigned char *)&probe == 0;
    if (ty == 0xC4 || ty == 0xC5 || ty == 0xC6)
        reenc_bin(x);
    else if (ty == 0xCA || ty == 0xCB)
        reenc_float(x, ty, be);
    else if (ty >= 0x90 && ty <= 0x9F)
        reenc_seq(x, ty - 0x90, 1);
    else if (ty == 0xDC || ty == 0xDD) {
        head = ty == 0xDC ? 3u : 5u, n = 0;
        for (i = 1; i < head; ++i) n = (n << 8) | (unsigned)x->r[i];
        reenc_seq(x, n, head);
    } else if (ty >= 0xA0 && ty <= 0xBF)
        reenc_raw(x, 1 + (unsigned)(ty - 0xA0));
    else if (ty == 0xD9)
        reenc_raw(x, 2 + (unsigned)x->r[1]);
    else if (ty == 0xCC)
        reenc_raw(x, 2);
    else if (ty == 0xCD)
        reenc_raw(x, 3);
    else if (ty == 0xCE)
        reenc_raw(x, 5);
    else if (ty == 0xD0 || ty >= 0xE0) /* int8 / negative fixint */
        reenc_raw(x, 1);
    else if (ty == 0xD1)
        reenc_raw(x, 3);
    else if (ty == 0xD2)
        reenc_raw(x, 5);
    else {
        assertok(ty <= 0x7F);
        reenc_raw(x, 1);
    }
}

static void reenc_rejected(am_Num want) {
    am_Num         x = 0;
    am_Solver     *S;
    am_Id          xv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    unsigned char  enc[TEST_BUFSIZE];
    size_t         len = 0, enclen;
    MyDumper       md = AM_ZERO;
    MyLoader       ml = AM_ZERO;
    reenc          xr;
    stream_spot    s;

    S = am_test_new_solver();
    xv = am_newvariable(S, &x);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_addconstant(c, -want) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    am_updatevars(S);

    md.base.var_name = dump_varname;
    md.base.cons_name = NULL;
    md.base.writer = dump_writer;
    md.p = buf, md.len = &len;
    assertok(am_dump(S, &md.base) == AM_OK && len > 0);

    xr.r = (const unsigned char *)buf, xr.w = enc;
    xr.bits = sizeof(am_Num) == sizeof(float) ? 64 : 32;
    reenc_value(&xr);
    assertok(xr.r == (const unsigned char *)buf + len); /* one pass */
    enclen = (size_t)(xr.w - enc);
    assertok(enclen > 0 && enclen < TEST_BUFSIZE);

    /* every number is now foreign width, including meta's num_size: that
     * field alone must make am_load() refuse the stream */
    stream_scan(enc, &s);
    asserteq(stream_u32len(enc + s.version_at), 1);
    enc[s.version_at + 1] = (unsigned char)(sizeof(am_Num) == 4 ? 8 : 4);
    assertok(load_maybe((const char *)enc, enclen, &ml) == NULL);
    am_delsolver(S);
}

TEST(load_rejects_a_foreign_width_stream) {
    reenc_rejected((am_Num)(1.0 / 3.0));
}

/* A number tag of the other width is a numeric problem, not a structural
 * one: the header still names this build's width, so the loader converts the
 * value instead of refusing the stream. */
TEST(load_converts_a_foreign_width_number) {
    am_Num         want = (am_Num)(1.0 / 3.0);
    am_Num         x = 0;
    am_Solver     *S, *S2;
    am_Id          xv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    unsigned char  enc[TEST_BUFSIZE];
    size_t         len = 0, enclen;
    MyDumper       md = AM_ZERO;
    MyLoader       ml = AM_ZERO;
    reenc          xr;
    stream_spot    s;

    S = am_test_new_solver();
    xv = am_newvariable(S, &x);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_addconstant(c, -want) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    am_updatevars(S);

    md.base.var_name = dump_varname;
    md.base.writer = dump_writer;
    md.p = buf, md.len = &len;
    assertok(am_dump(S, &md.base) == AM_OK && len > 0);
    xr.r = (const unsigned char *)buf, xr.w = enc;
    xr.bits = sizeof(am_Num) == sizeof(float) ? 64 : 32;
    reenc_value(&xr);
    assertok(xr.r == (const unsigned char *)buf + len); /* one pass */
    enclen = (size_t)(xr.w - enc);
    assertok(enclen > 0 && enclen < TEST_BUFSIZE);
    stream_scan(enc, &s);

    /* every number is now the other width; put this build's width back in
     * meta so the width check passes and the numbers are converted */
    asserteq(stream_u32len(enc + s.version_at), 1);
    enc[s.version_at + 1] = (unsigned char)sizeof(am_Num);
    S2 = load_maybe((const char *)enc, enclen, &ml);
    assertok(S2 != NULL);
    assertclose(loaded_value(&ml, xv), want, "foreign width value");
    am_delsolver(S2);
    am_delsolver(S);
}

/* ---- a layout-shaped round trip fixture ---- */

/* One constraint of the enaml-like layout below.  The table is constant
 * data: terms carry an index into layout_var (not an am_Id, which only
 * exists at run time), so a strict C89 build can initialize it. */
struct layout_item {
    int    var; /* index into layout_var, -1 ends the list */
    am_Num mul;
};

struct layout_constraint {
    struct layout_item term[5];
    am_Num             constant;
    int                relation;
    am_Num             strength;
};

enum layout_var {
    WIDTH,
    HEIGHT,
    LEFT,
    TOP,
    CONTENTS_TOP,
    CONTENTS_BOTTOM,
    CONTENTS_LEFT,
    CONTENTS_RIGHT,
    MIDLINE,
    CTLEFT,
    CTHEIGHT,
    CTTOP,
    CTWIDTH,
    LB1LEFT,
    LB1HEIGHT,
    LB1TOP,
    LB1WIDTH,
    LB2LEFT,
    LB2HEIGHT,
    LB2TOP,
    LB2WIDTH,
    LB3LEFT,
    LB3HEIGHT,
    LB3TOP,
    LB3WIDTH,
    FL1LEFT,
    FL1HEIGHT,
    FL1TOP,
    FL1WIDTH,
    FL2LEFT,
    FL2HEIGHT,
    FL2TOP,
    FL2WIDTH,
    FL3LEFT,
    FL3HEIGHT,
    FL3TOP,
    FL3WIDTH,
    LAYOUT_NVARS
};

/* custom strengths, between AM_MEDIUM and AM_STRONG: macros, not const
 * objects, so the table below stays a constant initializer */
#define M_MEDIUM (AM_MEDIUM * 1.25)
#define S_MEDIUM (AM_MEDIUM * 100)

static const struct layout_constraint constraints[] = {
        {{{LEFT, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{HEIGHT, 1}, {-1, 0}}, 0, AM_EQUAL, AM_MEDIUM},
        {{{TOP, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{WIDTH, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{HEIGHT, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{TOP, -1}, {CONTENTS_TOP, 1}, {-1, 0}}, -10, AM_EQUAL, AM_REQUIRED},
        {{{LB3HEIGHT, 1}, {-1, 0}}, -16, AM_EQUAL, AM_STRONG},
        {{{LB3HEIGHT, 1}, {-1, 0}}, -16, AM_GREATEREQUAL, AM_STRONG},
        {{{CTLEFT, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{CTTOP, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{CTWIDTH, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{CTHEIGHT, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{FL3LEFT, 1}, {-1, 0}}, 0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{CTHEIGHT, 1}, {-1, 0}}, -24, AM_GREATEREQUAL, S_MEDIUM},
        {{{CTWIDTH, 1}, {-1, 0}}, -1.67772e+07, AM_LESSEQUAL, S_MEDIUM},
        {{{CTHEIGHT, 1}, {-1, 0}}, -24, AM_LESSEQUAL, S_MEDIUM},
        {{{FL3TOP, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{FL3WIDTH, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{FL3HEIGHT, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{LB1WIDTH, 1}, {-1, 0}}, -67, AM_EQUAL, AM_WEAK},
        {{{LB2WIDTH, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{LB2HEIGHT, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{FL2HEIGHT, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{LB3LEFT, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{FL2WIDTH, 1}, {-1, 0}}, -125, AM_GREATEREQUAL, AM_STRONG},
        {{{FL2HEIGHT, 1}, {-1, 0}}, -21, AM_EQUAL, AM_STRONG},
        {{{FL2HEIGHT, 1}, {-1, 0}}, -21, AM_GREATEREQUAL, AM_STRONG},
        {{{LB3TOP, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{LB3WIDTH, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{LB1LEFT, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{FL1WIDTH, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{LB1WIDTH, 1}, {-1, 0}}, -67, AM_GREATEREQUAL, AM_STRONG},
        {{{FL2LEFT, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{LB2WIDTH, 1}, {-1, 0}}, -66, AM_EQUAL, AM_WEAK},
        {{{LB2WIDTH, 1}, {-1, 0}}, -66, AM_GREATEREQUAL, AM_STRONG},
        {{{LB2HEIGHT, 1}, {-1, 0}}, -16, AM_EQUAL, AM_STRONG},
        {{{FL1HEIGHT, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{FL1TOP, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{LB2TOP, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{LB2TOP, -1}, {LB3TOP, 1}, {LB2HEIGHT, -1}, {-1, 0}},
         -10,
         AM_EQUAL,
         M_MEDIUM},
        {{{LB3TOP, -1}, {LB3HEIGHT, -1}, {FL3TOP, 1}, {-1, 0}},
         -10,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{LB3TOP, -1}, {LB3HEIGHT, -1}, {FL3TOP, 1}, {-1, 0}},
         -10,
         AM_EQUAL,
         M_MEDIUM},
        {{{CONTENTS_BOTTOM, 1}, {FL3HEIGHT, -1}, {FL3TOP, -1}, {-1, 0}},
         -0,
         AM_EQUAL,
         M_MEDIUM},
        {{{FL1TOP, 1}, {CONTENTS_TOP, -1}, {-1, 0}},
         0,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{FL1TOP, 1}, {CONTENTS_TOP, -1}, {-1, 0}}, 0, AM_EQUAL, M_MEDIUM},
        {{{CONTENTS_BOTTOM, 1}, {FL3HEIGHT, -1}, {FL3TOP, -1}, {-1, 0}},
         -0,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{LEFT, -1}, {WIDTH, -1}, {CONTENTS_RIGHT, 1}, {-1, 0}},
         10,
         AM_EQUAL,
         AM_REQUIRED},
        {{{TOP, -1}, {HEIGHT, -1}, {CONTENTS_BOTTOM, 1}, {-1, 0}},
         10,
         AM_EQUAL,
         AM_REQUIRED},
        {{{LEFT, -1}, {CONTENTS_LEFT, 1}, {-1, 0}}, -10, AM_EQUAL, AM_REQUIRED},
        {{{LB3LEFT, 1}, {CONTENTS_LEFT, -1}, {-1, 0}}, 0, AM_EQUAL, M_MEDIUM},
        {{{FL1LEFT, 1}, {MIDLINE, -1}, {-1, 0}}, 0, AM_EQUAL, AM_STRONG},
        {{{FL2LEFT, 1}, {MIDLINE, -1}, {-1, 0}}, 0, AM_EQUAL, AM_STRONG},
        {{{CTLEFT, 1}, {MIDLINE, -1}, {-1, 0}}, 0, AM_EQUAL, AM_STRONG},
        {{{FL1TOP, 1},
          {FL1HEIGHT, 0.5},
          {LB1TOP, -1},
          {LB1HEIGHT, -0.5},
          {-1, 0}},
         0,
         AM_EQUAL,
         AM_STRONG},
        {{{LB1LEFT, 1}, {CONTENTS_LEFT, -1}, {-1, 0}},
         0,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{LB1LEFT, 1}, {CONTENTS_LEFT, -1}, {-1, 0}}, 0, AM_EQUAL, M_MEDIUM},
        {{{LB1LEFT, -1}, {FL1LEFT, 1}, {LB1WIDTH, -1}, {-1, 0}},
         -10,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{LB1LEFT, -1}, {FL1LEFT, 1}, {LB1WIDTH, -1}, {-1, 0}},
         -10,
         AM_EQUAL,
         M_MEDIUM},
        {{{FL1LEFT, -1}, {CONTENTS_RIGHT, 1}, {FL1WIDTH, -1}, {-1, 0}},
         -0,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{WIDTH, 1}, {-1, 0}}, 0, AM_EQUAL, AM_MEDIUM},
        {{{FL1TOP, -1}, {FL2TOP, 1}, {FL1HEIGHT, -1}, {-1, 0}},
         -10,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{FL1TOP, -1}, {FL2TOP, 1}, {FL1HEIGHT, -1}, {-1, 0}},
         -10,
         AM_EQUAL,
         M_MEDIUM},
        {{{CTTOP, 1}, {FL2TOP, -1}, {FL2HEIGHT, -1}, {-1, 0}},
         -10,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{CTHEIGHT, -1}, {CTTOP, -1}, {FL3TOP, 1}, {-1, 0}},
         -10,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{CONTENTS_BOTTOM, 1}, {FL3HEIGHT, -1}, {FL3TOP, -1}, {-1, 0}},
         -0,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{CTTOP, 1}, {FL2TOP, -1}, {FL2HEIGHT, -1}, {-1, 0}},
         -10,
         AM_EQUAL,
         M_MEDIUM},
        {{{FL1LEFT, -1}, {CONTENTS_RIGHT, 1}, {FL1WIDTH, -1}, {-1, 0}},
         -0,
         AM_EQUAL,
         M_MEDIUM},
        {{{LB2TOP, -1},
          {LB2HEIGHT, -0.5},
          {FL2TOP, 1},
          {FL2HEIGHT, 0.5},
          {-1, 0}},
         0,
         AM_EQUAL,
         AM_STRONG},
        {{{CONTENTS_LEFT, -1}, {LB2LEFT, 1}, {-1, 0}},
         0,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{CONTENTS_LEFT, -1}, {LB2LEFT, 1}, {-1, 0}}, 0, AM_EQUAL, M_MEDIUM},
        {{{FL2LEFT, 1}, {LB2WIDTH, -1}, {LB2LEFT, -1}, {-1, 0}},
         -10,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{CTHEIGHT, -1}, {CTTOP, -1}, {FL3TOP, 1}, {-1, 0}},
         -10,
         AM_EQUAL,
         M_MEDIUM},
        {{{CONTENTS_BOTTOM, 1}, {FL3HEIGHT, -1}, {FL3TOP, -1}, {-1, 0}},
         -0,
         AM_EQUAL,
         M_MEDIUM},
        {{{LB1TOP, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{LB1WIDTH, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{LB1HEIGHT, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{FL2LEFT, 1}, {LB2WIDTH, -1}, {LB2LEFT, -1}, {-1, 0}},
         -10,
         AM_EQUAL,
         M_MEDIUM},
        {{{FL2LEFT, -1}, {FL2WIDTH, -1}, {CONTENTS_RIGHT, 1}, {-1, 0}},
         -0,
         AM_EQUAL,
         M_MEDIUM},
        {{{FL2LEFT, -1}, {FL2WIDTH, -1}, {CONTENTS_RIGHT, 1}, {-1, 0}},
         -0,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{LB3LEFT, 1}, {CONTENTS_LEFT, -1}, {-1, 0}},
         0,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{LB1LEFT, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{CTHEIGHT, 0.5},
          {CTTOP, 1},
          {LB3TOP, -1},
          {LB3HEIGHT, -0.5},
          {-1, 0}},
         0,
         AM_EQUAL,
         AM_STRONG},
        {{{CTLEFT, 1}, {LB3LEFT, -1}, {LB3WIDTH, -1}, {-1, 0}},
         -10,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{CTWIDTH, -1}, {CTLEFT, -1}, {CONTENTS_RIGHT, 1}, {-1, 0}},
         -0,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{CTLEFT, 1}, {LB3LEFT, -1}, {LB3WIDTH, -1}, {-1, 0}},
         -10,
         AM_EQUAL,
         M_MEDIUM},
        {{{FL3LEFT, 1}, {CONTENTS_LEFT, -1}, {-1, 0}},
         0,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{FL3LEFT, 1}, {CONTENTS_LEFT, -1}, {-1, 0}}, 0, AM_EQUAL, M_MEDIUM},
        {{{CTWIDTH, -1}, {CTLEFT, -1}, {CONTENTS_RIGHT, 1}, {-1, 0}},
         -0,
         AM_EQUAL,
         M_MEDIUM},
        {{{FL3LEFT, -1}, {CONTENTS_RIGHT, 1}, {FL3WIDTH, -1}, {-1, 0}},
         -0,
         AM_EQUAL,
         M_MEDIUM},
        {{{CONTENTS_TOP, -1}, {LB1TOP, 1}, {-1, 0}},
         0,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{CONTENTS_TOP, -1}, {LB1TOP, 1}, {-1, 0}}, 0, AM_EQUAL, M_MEDIUM},
        {{{FL3LEFT, -1}, {CONTENTS_RIGHT, 1}, {FL3WIDTH, -1}, {-1, 0}},
         -0,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{LB2TOP, 1}, {LB1TOP, -1}, {LB1HEIGHT, -1}, {-1, 0}},
         -10,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{LB2TOP, -1}, {LB3TOP, 1}, {LB2HEIGHT, -1}, {-1, 0}},
         -10,
         AM_GREATEREQUAL,
         AM_REQUIRED},
        {{{LB2TOP, 1}, {LB1TOP, -1}, {LB1HEIGHT, -1}, {-1, 0}},
         -10,
         AM_EQUAL,
         M_MEDIUM},
        {{{FL1HEIGHT, 1}, {-1, 0}}, -21, AM_EQUAL, AM_STRONG},
        {{{FL1HEIGHT, 1}, {-1, 0}}, -21, AM_GREATEREQUAL, AM_STRONG},
        {{{LB2LEFT, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{LB2HEIGHT, 1}, {-1, 0}}, -16, AM_GREATEREQUAL, AM_STRONG},
        {{{FL2TOP, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{FL2WIDTH, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{LB1HEIGHT, 1}, {-1, 0}}, -16, AM_GREATEREQUAL, AM_STRONG},
        {{{LB1HEIGHT, 1}, {-1, 0}}, -16, AM_EQUAL, AM_STRONG},
        {{{FL3WIDTH, 1}, {-1, 0}}, -125, AM_GREATEREQUAL, AM_STRONG},
        {{{FL3HEIGHT, 1}, {-1, 0}}, -21, AM_EQUAL, AM_STRONG},
        {{{FL3HEIGHT, 1}, {-1, 0}}, -21, AM_GREATEREQUAL, AM_STRONG},
        {{{LB3HEIGHT, 1}, {-1, 0}}, -0, AM_GREATEREQUAL, AM_REQUIRED},
        {{{CTWIDTH, 1}, {-1, 0}}, -119, AM_GREATEREQUAL, S_MEDIUM},
        {{{LB3WIDTH, 1}, {-1, 0}}, -24, AM_EQUAL, AM_WEAK},
        {{{LB3WIDTH, 1}, {-1, 0}}, -24, AM_GREATEREQUAL, AM_STRONG},
        {{{FL1WIDTH, 1}, {-1, 0}}, -125, AM_GREATEREQUAL, AM_STRONG},
};

static void build_solver(am_Solver *S, am_Id width, am_Id height,
                         am_Num *values) {
    am_Id  vars[LAYOUT_NVARS];
    size_t i;
    int    ret;

    vars[WIDTH] = width, vars[HEIGHT] = height;
    vars[LEFT] = am_newvariable(S, &values[0]);
    vars[TOP] = am_newvariable(S, &values[1]);
    vars[CONTENTS_TOP] = am_newvariable(S, &values[2]);
    vars[CONTENTS_BOTTOM] = am_newvariable(S, &values[3]);
    vars[CONTENTS_LEFT] = am_newvariable(S, &values[4]);
    vars[CONTENTS_RIGHT] = am_newvariable(S, &values[5]);
    vars[MIDLINE] = am_newvariable(S, &values[6]);
    vars[CTLEFT] = am_newvariable(S, &values[7]);
    vars[CTHEIGHT] = am_newvariable(S, &values[8]);
    vars[CTTOP] = am_newvariable(S, &values[9]);
    vars[CTWIDTH] = am_newvariable(S, &values[10]);
    vars[LB1LEFT] = am_newvariable(S, &values[11]);
    vars[LB1HEIGHT] = am_newvariable(S, &values[12]);
    vars[LB1TOP] = am_newvariable(S, &values[13]);
    vars[LB1WIDTH] = am_newvariable(S, &values[14]);
    vars[LB2LEFT] = am_newvariable(S, &values[15]);
    vars[LB2HEIGHT] = am_newvariable(S, &values[16]);
    vars[LB2TOP] = am_newvariable(S, &values[17]);
    vars[LB2WIDTH] = am_newvariable(S, &values[18]);
    vars[LB3LEFT] = am_newvariable(S, &values[19]);
    vars[LB3HEIGHT] = am_newvariable(S, &values[20]);
    vars[LB3TOP] = am_newvariable(S, &values[21]);
    vars[LB3WIDTH] = am_newvariable(S, &values[22]);
    vars[FL1LEFT] = am_newvariable(S, &values[23]);
    vars[FL1HEIGHT] = am_newvariable(S, &values[24]);
    vars[FL1TOP] = am_newvariable(S, &values[25]);
    vars[FL1WIDTH] = am_newvariable(S, &values[26]);
    vars[FL2LEFT] = am_newvariable(S, &values[27]);
    vars[FL2HEIGHT] = am_newvariable(S, &values[28]);
    vars[FL2TOP] = am_newvariable(S, &values[29]);
    vars[FL2WIDTH] = am_newvariable(S, &values[30]);
    vars[FL3LEFT] = am_newvariable(S, &values[31]);
    vars[FL3HEIGHT] = am_newvariable(S, &values[32]);
    vars[FL3TOP] = am_newvariable(S, &values[33]);
    vars[FL3WIDTH] = am_newvariable(S, &values[34]);

    /* Add the edit variables */
    ret = am_addedit(S, width, AM_STRONG);
    assertok(ret == AM_OK);
    ret = am_addedit(S, height, AM_STRONG);
    assertok(ret == AM_OK);

    for (i = 0; i < sizeof(constraints) / sizeof(constraints[0]); ++i) {
        am_Constraint *c = am_newconstraint(S, constraints[i].strength);
        const struct layout_item *p;
        for (p = constraints[i].term; p->var >= 0; ++p) {
            ret = am_addterm(c, vars[p->var], p->mul);
            assertok(ret == AM_OK);
        }
        ret = am_addconstant(c, constraints[i].constant);
        assertok(ret == AM_OK);
        ret = am_setrelation(c, constraints[i].relation);
        assertok(ret == AM_OK);
        ret = am_add(c);
        assertok(ret == AM_OK);
    }
}

TEST(dumpload) {
    am_Num     w = 0, h = 0, values[35] = {0};
    am_Solver *S;
    am_Id      width, height;
    char       buf[TEST_BUFSIZE];
    size_t     len = 0;
    MyDumper   md = AM_ZERO;
    MyLoader   ml = AM_ZERO;
    am_Solver *S2;
    size_t     i;

    S = am_test_new_solver();
    assertok(S != NULL);

    width = am_newvariable(S, &w);
    height = am_newvariable(S, &h);

    build_solver(S, width, height, values);

    md.base.var_name = dump_varname;
    md.base.cons_name = dump_consname;
    md.base.writer = dump_writer;
    md.p = buf, md.len = &len;

    /* The byte count is NOT asserted: it depends on which (equally optimal)
     * basis the simplex happens to land on, and on the stream framing. */
    assertok(am_dump(S, &md.base) == AM_OK);
    assertok(len > 0 && len < TEST_BUFSIZE);

    S2 = load_from(buf, len, &ml);

    /* same public shape: the walkers see the same owners */
    assertok(am_refcount(S2, ml.ids[0]) > 0);
    asserteq(count_vars(S2), count_vars(S));
    asserteq(count_cons(S2), count_cons(S));

    /* ... and the same solution: a load settles the bindings itself */
    assertclose(loaded_value(&ml, width), w, "width");
    assertclose(loaded_value(&ml, height), h, "height");
    for (i = 0; i < 35; ++i)
        assertclose(loaded_value(&ml, (am_Id)(3 + i)), values[i], "v");

    /* a later am_updatevars() must not undo or re-collect anything */
    am_updatevars(S2);
    assertclose(loaded_value(&ml, width), w, "width after update");
    asserteq(count_vars(S2), count_vars(S));

    am_delsolver(S2);
    am_delsolver(S);
    assertok(allmem == 0);
    maxmem = 0;
}

TEST(dump_roundtrip_is_exact) {
    am_Num         want = (am_Num)(1.0 / 3.0);
    am_Num         x = 0;
    am_Solver     *S, *S2;
    am_Id          xv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    size_t         len = 0;
    MyDumper       md = AM_ZERO;
    MyLoader       ml = AM_ZERO;

    S = am_test_new_solver();
    assertok(S != NULL);
    xv = am_newvariable(S, &x);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_addconstant(c, -want) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    am_updatevars(S);
    /* the fixture itself must be exact, or a mismatch would not be dump's */
    assertok(memcmp(&x, &want, sizeof(x)) == 0);

    md.base.var_name = dump_varname;
    md.base.cons_name = NULL;
    md.base.writer = dump_writer;
    md.p = buf, md.len = &len;
    assertok(am_dump(S, &md.base) == AM_OK);

    S2 = load_from(buf, len, &ml);
    assertok(memcmp(&ml.values[0], &want, sizeof(want)) == 0);

    am_delsolver(S2);
    am_delsolver(S);
}

/* x + y == 10 leaves one variable without a row: its tableau value is 0, so a
 * load must overwrite whatever load_var() put in that binding. */
TEST(load_settles_nonbasic_binding) {
    am_Num         x = 0, y = 0;
    am_Solver     *S, *S2;
    am_Id          xv, yv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    size_t         len = 0;
    MyLoader       ml = AM_ZERO;

    S = am_test_new_solver();
    xv = am_newvariable(S, &x), yv = am_newvariable(S, &y);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_addterm(c, yv, 1) == AM_OK);
    assertok(am_addconstant(c, -10) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    am_updatevars(S);
    assertclose(x, 10, "pre-dump x");
    assertclose(y, 0, "pre-dump y");

    len = dump_into(S, buf);
    S2 = load_from(buf, len, &ml);

    /* y has no row: it reads 0, not the sentinel load_var() left there */
    assertclose(*am_varvalue(S2, loaded_id(&ml, xv), NULL), 10, "loaded x");
    assertclose(*am_varvalue(S2, loaded_id(&ml, yv), NULL), 0, "loaded y");

    am_delsolver(S2);
    am_delsolver(S);
}

/* A solver with zero constraints used to dump nothing while returning AM_OK:
 * the dump context was zeroed after its ret was set to AM_FAILED, so the
 * zero-length segments left both pointers NULL and skipped the whole body. */
TEST(dump_empty) {
    am_Num     x = 7;
    am_Solver *S, *S2;
    char       buf[TEST_BUFSIZE];
    size_t     len = 0;
    MyDumper   md = AM_ZERO;
    MyLoader   ml = AM_ZERO;

    md.base.var_name = dump_varname;
    md.base.cons_name = NULL;
    md.base.writer = dump_writer;
    md.p = buf, md.len = &len;

    S = am_test_new_solver();
    assertok(am_newvariable(S, &x) != 0);
    asserteq(am_dump(S, &md.base), AM_OK);
    assertok(len > 0);
    S2 = load_from(buf, len, &ml);
    asserteq(count_vars(S2), 1);
    asserteq(count_cons(S2), 0);
    am_delsolver(S2);
    am_delsolver(S);

    len = 0;
    S = am_test_new_solver();
    md.p = buf, md.len = &len;
    asserteq(am_dump(S, &md.base), AM_OK);
    assertok(len > 0);
    S2 = load_from(buf, len, &ml);
    asserteq(count_vars(S2), 0);
    asserteq(count_cons(S2), 0);
    am_delsolver(S2);
    am_delsolver(S);
}

/* A loaded solver must carry the same references as the original.
 * Reading a constraint's expression used to skip the per-term accounting, so
 * the first am_delconstraint() after a load collected variables that other
 * constraints still used -- and am_addterm() on them then failed. */
TEST(load_then_remove_keeps_vars) {
    am_Num         x = 0, y = 0;
    am_Solver     *S, *S2;
    am_Id          xv, yv;
    am_Constraint *c, *victim = NULL;
    char           buf[TEST_BUFSIZE];
    size_t         len = 0;
    MyLoader       ml;

    S = am_test_new_solver();
    assertok(S != NULL);
    xv = am_newvariable(S, &x), yv = am_newvariable(S, &y);

    c = am_newconstraint(S, AM_MEDIUM);
    assertok(am_addterm(c, xv, 2) == AM_OK);
    assertok(am_addterm(c, yv, 1) == AM_OK);
    assertok(am_addconstant(c, -10) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);

    c = am_newconstraint(S, AM_MEDIUM);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_addconstant(c, -3) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    am_updatevars(S);
    assertclose(x, 3, "pre-dump x");
    assertclose(y, 4, "pre-dump y");

    len = dump_into(S, buf);
    S2 = load_from(buf, len, &ml);
    xv = ml.ids[0], yv = ml.ids[1];

    /* one own reference + one per live term, exactly as before the dump */
    asserteq(am_refcount(S2, xv), 3);
    asserteq(am_refcount(S2, yv), 2);

    /* load_cons() recorded the constraints of this load; the two-term one is
     * the constraint to drop (ids are new, the order is the stream's) */
    assertok(ml.cons[0] != NULL && ml.cons[1] != NULL);
    victim = count_terms(ml.cons[0]) == 2 ? ml.cons[0] : ml.cons[1];
    asserteq(count_terms(victim), 2);

    /* With auto_update on, am_delconstraint() collects unreferenced variables
     * right away: the remaining constraint still owns x, y keeps its own
     * reference. */
    am_autoupdate(S2, 1);
    am_delconstraint(victim);
    asserteq(count_vars(S2), 2);
    asserteq(am_refcount(S2, xv), 2);
    asserteq(am_refcount(S2, yv), 1);

    /* the solver is still usable: the surviving variable accepts new terms */
    c = am_newconstraint(S2, AM_REQUIRED);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_addconstant(c, -7) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    am_updatevars(S2);
    assertclose(ml.values[0], 7, "post-remove x");
    assertclose(ml.values[1], 0, "post-remove y");

    am_delsolver(S2);
    am_delsolver(S);
}

/* ---- the loader's view of the solver while loading ----
 *
 * load_var() is not called for a stream with no variables, so the solver is
 * handed over separately, read-only, before the body is parsed.  These tests
 * pin when that happens, what the solver looks like at that moment, and what
 * becomes of it when the load fails. */

#define SOLVER_VALUES 4

typedef struct SolverLoader {
    am_Loader        base;
    const char      *p;
    size_t           len;
    am_Num           values[SOLVER_VALUES];
    const am_Solver *seen;      /* solver handed to the callback */
    unsigned         calls;     /* solver callbacks */
    unsigned         vars;      /* load_var calls */
    unsigned         cons;      /* load_cons calls */
    unsigned         steps;     /* callback sequence counter */
    unsigned         at_solver; /* where the solver callback fell */
    unsigned         at_var;    /* ... and the first load_var */
    unsigned         at_cons;   /* ... and the first load_cons */
    int              empty;     /* no variables and no constraints then */
} SolverLoader;

static void loader_solver(am_Loader *l, const am_Solver *S) {
    SolverLoader *sl = (SolverLoader *)l;
    sl->seen = S, sl->calls += 1, sl->at_solver = sl->steps++;
    sl->empty = (am_vars(S, 0) == 0 && am_constraints(S, NULL) == NULL);
}

static am_Num *loader_var(am_Loader *l, const char *name, unsigned i,
                          am_Id var) {
    SolverLoader *sl = (SolverLoader *)l;
    (void)name, (void)var;
    if (sl->vars == 0) sl->at_var = sl->steps++;
    sl->vars += 1;
    return &sl->values[i % SOLVER_VALUES];
}

static void loader_cons(am_Loader *l, const char *name, unsigned i,
                        am_Constraint *cons) {
    SolverLoader *sl = (SolverLoader *)l;
    (void)name, (void)i, (void)cons;
    if (sl->cons == 0) sl->at_cons = sl->steps++;
    sl->cons += 1;
}

static const char *loader_reader(am_Loader *l, size_t *plen) {
    SolverLoader *sl = (SolverLoader *)l;
    if (sl->len == 0) return NULL;
    *plen = sl->len, sl->len = 0;
    return sl->p;
}

static void loader_init(SolverLoader *sl, const char *p, size_t len) {
    memset(sl, 0, sizeof(*sl));
    sl->base.solver = loader_solver;
    sl->base.load_var = loader_var;
    sl->base.load_cons = loader_cons;
    sl->base.reader = loader_reader;
    sl->p = p, sl->len = len;
}

TEST(load_hands_over_the_solver_before_the_body) {
    am_Num         x = 0;
    am_Solver     *S, *S2;
    am_Id          xv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    size_t         len;
    SolverLoader   sl;

    S = am_test_new_solver();
    xv = am_newvariable(S, &x);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_addconstant(c, -10) == AM_OK);
    assertok(am_setrelation(c, AM_GREATEREQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);

    len = dump_into(S, buf);
    loader_init(&sl, buf, len);
    assertok((S2 = am_load(am_test_allocf, NULL, &sl.base, 0)) != NULL);

    asserteq(sl.calls, 1);   /* once per load, not once per object */
    assertok(sl.seen == S2); /* the solver that comes back to the caller */
    asserteq(sl.empty, 1);   /* nothing was built yet */
    asserteq(sl.vars, 1);
    assertok(sl.at_solver < sl.at_var);
    assertok(sl.at_solver < sl.at_cons);
    assertclose(sl.values[0], 10, "load_var binding written");

    am_delsolver(S2);
    am_delsolver(S);
}

TEST(load_hands_over_the_solver_without_variables) {
    am_Solver   *S;
    am_Solver   *S2;
    char         buf[TEST_BUFSIZE];
    size_t       len;
    SolverLoader sl;

    S = am_test_new_solver();
    len = dump_into(S, buf);
    loader_init(&sl, buf, len);
    assertok((S2 = am_load(am_test_allocf, NULL, &sl.base, 0)) != NULL);

    /* an empty stream never calls load_var: this hook is the only way in */
    asserteq(sl.calls, 1);
    asserteq(sl.vars, 0);
    asserteq(sl.cons, 0);
    asserteq(sl.empty, 1);
    asserteq(count_vars(S2), 0);
    asserteq(count_cons(S2), 0);

    am_delsolver(S2);
    am_delsolver(S);
}

TEST(load_skips_the_solver_callback_for_a_bad_header) {
    SolverLoader sl;

    loader_init(&sl, "not a dump", 10);
    assertok(am_load(am_test_allocf, NULL, &sl.base, 0) == NULL);

    /* nothing was handed out, so there is nothing for the loader to drop */
    asserteq(sl.calls, 0);
    asserteq(sl.vars, 0);
}

TEST(load_failure_releases_the_solver) {
    am_Num         x = 0;
    am_Solver     *S;
    am_Id          xv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    size_t         len;
    SolverLoader   sl;

    S = am_test_new_solver();
    xv = am_newvariable(S, &x);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_addconstant(c, -10) == AM_OK);
    assertok(am_setrelation(c, AM_GREATEREQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    len = dump_into(S, buf);
    am_delsolver(S); /* from here on only the failed load may hold memory */

    loader_init(&sl, buf, len - 1);
    assertok(am_load(am_test_allocf, NULL, &sl.base, 0) == NULL);
    asserteq(sl.calls, 1); /* it ran, then the stream ran out */
    asserteq(allmem, 0);   /* the half-built solver was released */
}

/* After the stream the app may append its own bytes; the documented
 * convention is "your payload, then its length in the last 4 bytes".  The
 * loader stops at the end of the stream and never looks at what follows. */
TEST(load_ignores_trailing_bytes) {
    am_Num         x = 0;
    am_Solver     *S, *S2;
    am_Id          xv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    size_t         len;
    SolverLoader   sl;

    S = am_test_new_solver();
    xv = am_newvariable(S, &x);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_addconstant(c, -7) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    len = dump_into(S, buf);

    buf[len + 0] = 'a', buf[len + 1] = 'p', buf[len + 2] = 'p'; /* app data */
    buf[len + 3] = 0, buf[len + 4] = 0, buf[len + 5] = 0;       /* length 3 */
    buf[len + 6] = 3;
    loader_init(&sl, buf, len + 7);
    assertok((S2 = am_load(am_test_allocf, NULL, &sl.base, 0)) != NULL);
    asserteq(count_vars(S2), 1);
    assertclose(sl.values[0], 7, "x with trailing app data");
    am_delsolver(S2);
    am_delsolver(S);
}

/* A corrupted stream may zero a row key.  am_dump() writes zero exactly once
 * (the objective, first), and the parser takes every zero key as the objective,
 * so a second one must neither assert nor leak. */
static unsigned le32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16)
         | ((unsigned)p[3] << 24);
}

static void put_le32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}

TEST(load_tolerates_a_zeroed_row_key) {
    am_Num         x = 0;
    am_Solver     *S, *S2;
    am_Id          xv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    unsigned char *bin;
    size_t         len, head, at;
    unsigned       rows, obj;
    MyLoader       ml = AM_ZERO;
    stream_spot    s;

    S = am_test_new_solver();
    xv = am_newvariable(S, &x);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_addconstant(c, -5) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    len = dump_into(S, buf);

    stream_scan((const unsigned char *)buf, &s);
    bin = (unsigned char *)buf + s.bin_at;
    head = bin[0] == 0xC4 ? 2 : bin[0] == 0xC5 ? 3 : 5;
    rows = le32(bin + head);        /* the first row is the objective */
    obj = le32(bin + head + 4);     /* its length, then the real rows */
    at = head + 4 + 4 * (rows + 1); /* ncons = 1, no edits */
    assertok(rows >= 2 && at + obj + 4 <= len);
    put_le32(bin + at + obj, 0); /* the first real row's key */

    S2 = load_from(buf, len, &ml);
    assertok(am_refcount(S2, 1) >= 1); /* the wake survives the dropped row */
    am_delsolver(S2);
    am_delsolver(S);
}

/* ---- records shorter than the walk expects ----
 *
 * Both streams below are legal, but a record carries fewer bytes than the
 * cursor step: a cons record with no terms, and a duplicate cache entry.  A
 * wake that skips the step reads the next record at the wrong offset, so each
 * shape gets its own test. */

TEST(wake_settles_after_a_cons_record_without_terms) {
    am_Num         x = 0, constants = 0, coef;
    am_Solver     *S, *L;
    am_Id          xv;
    am_Constraint *empty1, *empty2, *c;
    char           buf[TEST_BUFSIZE];
    size_t         len;
    unsigned       i, empties = 0;
    MyLoader       ml = AM_ZERO;

    S = am_test_new_solver();
    xv = am_newvariable(S, &x);
    empty1 = am_newconstraint(S, AM_REQUIRED);
    empty2 = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addconstant(empty1, 3) == AM_OK);
    assertok(am_addconstant(empty2, 4) == AM_OK);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_addconstant(c, -5) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    assertok(am_addedit(S, xv, AM_STRONG) == AM_OK);
    am_suggest(S, xv, 2);
    len = dump_into(S, buf);

    L = load_from(buf, len, &ml);
    am_suggest(L, loaded_id(&ml, 1), 2); /* wakes */
    assertclose(*am_varvalue(L, loaded_id(&ml, 1), NULL), 5, "required x");
    for (i = 0; i < 3; ++i) {
        assertok(ml.cons[i] != NULL);
        if (count_terms(ml.cons[i]) == 0) {
            empties += 1, constants += am_constant(ml.cons[i]);
            continue;
        }
        assertok(count_terms(ml.cons[i]) == 1);
        assertok(term_find(ml.cons[i], loaded_id(&ml, 1), &coef));
        assertclose(coef, -1, "coefficient of x");
    }
    assertok(empties == 2 && constants == 7);
    am_delsolver(L), am_delsolver(S);
}

/* the duplicate only consumes its slice: the entry after it must still be
 * read at its own offset.  Its key words are never looked at, so zeroing the
 * first one makes an off-by-4 read miss the suggests table at once. */
TEST(wake_consumes_a_duplicate_cache_entry) {
    am_Num         x = 0, y = 0, z = 0;
    am_Solver     *S, *L;
    am_Id          xv, yv, zv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    unsigned char *bin, *lens, *e0, *e1;
    size_t         len, head;
    unsigned       nrec, ncons, ncache = 3, i, terms = 0, n0, n1;
    am_Suggest    *sg;
    MyLoader       ml = AM_ZERO;
    stream_spot    s;

    S = am_test_new_solver();
    xv = am_newvariable(S, &x), yv = am_newvariable(S, &y);
    zv = am_newvariable(S, &z);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_setrelation(c, AM_LESSEQUAL) == AM_OK);
    assertok(am_addconstant(c, 5) == AM_OK);
    assertok(am_add(c) == AM_OK);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, yv, 1) == AM_OK);
    assertok(am_setrelation(c, AM_LESSEQUAL) == AM_OK);
    assertok(am_addconstant(c, 6) == AM_OK);
    assertok(am_add(c) == AM_OK);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, zv, 1) == AM_OK);
    assertok(am_setrelation(c, AM_LESSEQUAL) == AM_OK);
    assertok(am_addconstant(c, 7) == AM_OK);
    assertok(am_add(c) == AM_OK);
    assertok(am_addedit(S, xv, AM_STRONG) == AM_OK);
    assertok(am_addedit(S, yv, AM_STRONG) == AM_OK);
    assertok(am_addedit(S, zv, AM_STRONG) == AM_OK);
    am_suggest(S, xv, 3), am_suggest(S, yv, 2), am_suggest(S, zv, 5);
    len = dump_into(S, buf);
    ncons = count_cons(S);

    stream_scan((const unsigned char *)buf, &s);
    bin = (unsigned char *)buf + s.bin_at;
    head = bin[0] == 0xC4 ? 2 : bin[0] == 0xC5 ? 3 : 5;
    nrec = le32(bin + head);
    lens = bin + head + 4; /* nrec + ncons + ncache lens words */
    for (i = 0; i < nrec + ncons; ++i) terms += le32(lens + 4 * i);
    n0 = le32(lens + 4 * (nrec + ncons)) / 4 - 1;     /* row count of entry 0 */
    n1 = le32(lens + 4 * (nrec + ncons + 1)) / 4 - 1; /* and of entry 1 */
    assertok(n0 > 1 && n0 == n1);
    e0 = lens + 4 * (nrec + ncons + ncache) + terms;
    e1 = e0 + le32(lens + 4 * (nrec + ncons)); /* lens = whole entry */
    assertok(le32(e0 + 4) != le32(e0 + 8));
    put_le32(e1, le32(e0));         /* entry 1 repeats entry 0's edit ... */
    put_le32(e1 + 4, le32(e0 + 8)); /* ... with the same rows, swapped: */
    put_le32(e1 + 8, le32(e0 + 4)); /* the first list must win */

    L = load_from(buf, len, &ml);
    assertok(am_refcount(L, loaded_id(&ml, 1)) >= 1); /* first call: wake */
    sg = suggest_of(L, loaded_id(&ml, 1));
    assertok(sg != NULL);
    assertok(vec_len(sg->dirtyset) == n0);
    assertok(sg->dirtyset[0].bits == le32(e0 + 4)); /* the first list won */
    am_suggest(L, loaded_id(&ml, 1), 3), am_suggest(L, loaded_id(&ml, 2), 2);
    am_suggest(L, loaded_id(&ml, 3), 5);
    assertclose(*am_varvalue(L, loaded_id(&ml, 1), NULL), 3, "x");
    assertclose(*am_varvalue(L, loaded_id(&ml, 2), NULL), 2, "y");
    assertclose(*am_varvalue(L, loaded_id(&ml, 3), NULL), 5, "z");
    am_delsolver(L), am_delsolver(S);
}

/* The scratch bin lists, per edit, the rows whose terms carry that edit's
 * marker.  The objective record carries the marker too, and it is not a row:
 * a cache list that names it gets a zero key, and amL_wake() stops at the
 * first key that is not a row and drops the whole list.  The loaded solver
 * then silently loses the edit's cached rows (the cached suggest path is
 * defeated) and recomputes on the first suggest.  The minimal fixture --
 * one edit, no constraints -- puts the marker in the objective, which is
 * exactly the shape that used to leak into the bin. */
TEST(load_keeps_the_edit_cache) {
    am_Num      x = 0;
    am_Solver  *S, *S2;
    am_Id       xv;
    am_Suggest *s;
    char        buf[TEST_BUFSIZE];
    size_t      len;
    MyLoader    ml = AM_ZERO;

    S = am_test_new_solver();
    xv = am_newvariable(S, &x);
    assertok(am_addedit(S, xv, AM_STRONG) == AM_OK);
    am_suggest(S, xv, 3);
    am_updatevars(S);
    len = dump_into(S, buf);

    S2 = load_from(buf, len, &ml);
    assertok(am_refcount(S2, loaded_id(&ml, 1)) >= 1); /* first call: wake */
    s = suggest_of(S2, loaded_id(&ml, 1));
    assertok(s != NULL);
    assertok(vec_len(s->dirtyset) > 0); /* the edit kept its cached rows */
    assertclose(*am_varvalue(S2, loaded_id(&ml, 1), NULL), 3, "x");

    am_delsolver(S2), am_delsolver(S);
}

/* the first payload word is the row count and it is scaled by four before the
 * lens area is read: reject the count itself, so a 32-bit build cannot wrap
 * 4 * count down to a lens size that fits. */
TEST(load_rejects_an_oversized_row_count) {
    am_Num         x = 0;
    am_Solver     *S;
    am_Id          xv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    unsigned char *bin;
    size_t         len, head, size;
    unsigned       i;
    MyLoader       ml = AM_ZERO;
    stream_spot    s;

    S = am_test_new_solver();
    xv = am_newvariable(S, &x);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_addconstant(c, -5) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    len = dump_into(S, buf);
    stream_scan((const unsigned char *)buf, &s);
    bin = (unsigned char *)buf + s.bin_at;
    head = bin[0] == 0xC4 ? 2 : bin[0] == 0xC5 ? 3 : 5;
    size = len - s.bin_at - head; /* the bin is the last top-level element */
    put_le32(bin + head, 0x40000000u);   /* 4 * count wraps to 0 at 32 bits */
    for (i = 3; i < (size - 4) / 4; ++i) /* keep a guardless walk reading */
        put_le32(bin + head + 4 + 4 * i, 1);
    assertok(load_maybe(buf, len, &ml) == NULL);
    /* total = nrec + ncons(1) + edits(0) fits the bin, 4 * total does not */
    put_le32(bin + head, (unsigned)((size - 4) / 4));
    assertok(load_maybe(buf, len, &ml) == NULL);
    put_le32(bin + head, ~0u);
    assertok(load_maybe(buf, len, &ml) == NULL);
    am_delsolver(S);
}

/* the first payload word is the row count; zero is not a valid document (the
 * objective is always first) and would turn nrec - 1 into a huge table
 * request, so the header check rejects it before the walk starts. */
TEST(load_rejects_a_zero_row_count) {
    am_Num         x = 0;
    am_Solver     *S;
    am_Id          xv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    unsigned char *bin;
    size_t         len, head;
    MyLoader       ml = AM_ZERO;
    stream_spot    s;

    S = am_test_new_solver();
    xv = am_newvariable(S, &x);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_addconstant(c, -5) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    len = dump_into(S, buf);

    stream_scan((const unsigned char *)buf, &s);
    bin = (unsigned char *)buf + s.bin_at;
    head = bin[0] == 0xC4 ? 2 : bin[0] == 0xC5 ? 3 : 5;
    assertok(le32(bin + head) >= 2); /* objective + the added row */
    put_le32(bin + head, 0);
    assertok(load_maybe(buf, len, &ml) == NULL);
    am_delsolver(S);
}

/* ---- edits survive a dump/load round trip ---- */

/* each lens word is the record's byte length and the walk bounds it by the
 * bytes left in the term area, so a length that runs past the area must be
 * rejected before it reaches the arena bookkeeping.  Both loops get their own
 * crafted stream. */
TEST(load_rejects_an_oversized_record_length) {
    am_Num         x = 0;
    am_Solver     *S;
    am_Id          xv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    unsigned char *bin, *lens;
    size_t         len, head;
    unsigned       nrec, ncons, i, terms;
    MyLoader       ml = AM_ZERO;
    stream_spot    s;

    S = am_test_new_solver();
    xv = am_newvariable(S, &x);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_addconstant(c, -5) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    ncons = count_cons(S);
    len = dump_into(S, buf);
    stream_scan((const unsigned char *)buf, &s);
    bin = (unsigned char *)buf + s.bin_at;
    head = bin[0] == 0xC4 ? 2 : bin[0] == 0xC5 ? 3 : 5;
    nrec = le32(bin + head);
    lens = bin + head + 4;
    assertok(nrec >= 2 && ncons >= 1);

    put_le32(lens, AM_TERM_SIZE); /* row 0: one term unit */
    put_le32(lens + 4, ~0u);      /* row 1: past the end of the term area */
    assertok(load_maybe(buf, len, &ml) == NULL);

    len = dump_into(S, buf);
    stream_scan((const unsigned char *)buf, &s);
    bin = (unsigned char *)buf + s.bin_at;
    head = bin[0] == 0xC4 ? 2 : bin[0] == 0xC5 ? 3 : 5;
    nrec = le32(bin + head);
    lens = bin + head + 4;
    for (i = 0, terms = 0; i < nrec; ++i) terms += le32(lens + 4 * i);
    put_le32(lens + 4 * nrec, ~0u - terms); /* cons 0: past the area end */
    assertok(load_maybe(buf, len, &ml) == NULL);
    am_delsolver(S);
}

/* ---- structural lengths must be whole units ----
 *
 * Every record and hint is walked in fixed-size steps, so a length that is
 * not a whole number of units would make the walk leave its slice: the two
 * record loops would read a term past the bin, and a zero-length hint would
 * read its var word at the bin end.  Each crafted stream below keeps the
 * messagepack framing valid and only breaks the stride; am_load() must
 * reject all three before the walk starts. */

TEST(load_rejects_a_misaligned_row_length) {
    am_Num         x = 0, y = 0;
    am_Solver     *S;
    am_Id          vx, vy;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    unsigned char *bin, *lens;
    size_t         len, head;
    unsigned       nrec, ncons, i, k = 0, last;
    MyLoader       ml = AM_ZERO;
    stream_spot    s;

    S = am_test_new_solver();
    vx = am_newvariable(S, &x), vy = am_newvariable(S, &y);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, vx, 1) == AM_OK);
    assertok(am_addterm(c, vy, 1) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_addconstant(c, 10) == AM_OK);
    assertok(am_add(c) == AM_OK);
    ncons = count_cons(S);
    len = dump_into(S, buf);
    stream_scan((const unsigned char *)buf, &s);
    bin = (unsigned char *)buf + s.bin_at;
    head = bin[0] == 0xC4 ? 2 : bin[0] == 0xC5 ? 3 : 5;
    nrec = le32(bin + head);
    lens = bin + head + 4;
    assertok(nrec >= 2 && ncons == 1);
    last = nrec + ncons - 1; /* the cons record */
    for (i = 1; i < nrec; ++i)
        if (le32(lens + 4 * i) >= 2 * AM_TERM_SIZE) k = i;
    assertok(k != 0); /* a row with at least one term */

    /* trade one byte between the row and the cons record: the byte total
     * stays exact, only the stride is broken */
    put_le32(lens + 4 * last, le32(lens + 4 * last) + 1);
    put_le32(lens + 4 * k, le32(lens + 4 * k) - 1);
    assertok(load_maybe(buf, len, &ml) == NULL);
    am_delsolver(S);
}

TEST(load_rejects_a_zero_length_hint) {
    am_Num         x = 0;
    am_Solver     *S;
    am_Id          xv;
    char           buf[TEST_BUFSIZE];
    unsigned char *bin, *lens;
    size_t         len, head, terms, end, hint;
    unsigned       nrec, ncons, ns, i;
    MyLoader       ml = AM_ZERO;
    stream_spot    s;

    S = am_test_new_solver();
    xv = am_newvariable(S, &x);
    assertok(am_addedit(S, xv, AM_STRONG) == AM_OK);
    am_suggest(S, xv, 3);
    am_updatevars(S);
    ncons = count_cons(S); /* the edit's hidden equality is not public */
    ns = 1;                /* one edit, so one hint */
    len = dump_into(S, buf);
    stream_scan((const unsigned char *)buf, &s);
    bin = (unsigned char *)buf + s.bin_at;
    head = bin[0] == 0xC4 ? 2 : bin[0] == 0xC5 ? 3 : 5;
    nrec = le32(bin + head);
    lens = bin + head + 4;
    assertok(ncons == 0);
    for (i = 0, terms = 0; i < nrec + ncons; ++i) terms += le32(lens + 4 * i);
    hint = le32(lens + 4 * (nrec + ncons));
    assertok(hint >= 4);
    end = (size_t)(bin - (unsigned char *)buf) + head + 4
        + 4 * (nrec + ncons + ns) + terms + hint;
    assertok(end == len); /* the bin is the last top-level element */

    /* drop the hint's payload from the bin and zero its length: the walk
     * now has no var word to read at the (new) bin end */
    put_le32(lens + 4 * (nrec + ncons), 0);
    if (bin[0] == 0xC4)
        bin[1] = (unsigned char)(bin[1] - hint);
    else {
        unsigned v = ((unsigned)bin[1] << 8) | bin[2];
        v -= (unsigned)hint;
        bin[1] = (unsigned char)(v >> 8), bin[2] = (unsigned char)v;
    }
    assertok(load_maybe(buf, len - hint, &ml) == NULL);
    am_delsolver(S);
}

TEST(load_rejects_a_short_cons_record) {
    am_Num         x = 0;
    am_Solver     *S, *L;
    am_Id          xv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    unsigned char *bin, *lens;
    size_t         len, head, size, cut, terms;
    unsigned       nrec, ncons, last, i;
    MyLoader       ml = AM_ZERO;
    stream_spot    s;

    S = am_test_new_solver();
    xv = am_newvariable(S, &x);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, xv, 1) == AM_OK);
    assertok(am_addconstant(c, -5) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    ncons = count_cons(S);
    len = dump_into(S, buf);
    stream_scan((const unsigned char *)buf, &s);
    bin = (unsigned char *)buf + s.bin_at;
    head = bin[0] == 0xC4 ? 2 : bin[0] == 0xC5 ? 3 : 5;
    nrec = le32(bin + head);
    lens = bin + head + 4;
    assertok(nrec >= 1 && ncons >= 1);
    last = nrec + ncons - 1; /* the cons is the last record */
    size = le32(lens + 4 * last);
    assertok(size >= AM_TERM_SIZE);
    for (i = 0, terms = 0; i < nrec + ncons; ++i) terms += le32(lens + 4 * i);
    assertok(bin + head + 4 + 4 * (nrec + ncons) + terms
             == (unsigned char *)buf + len); /* the bin is last */

    /* leave four bytes of the cons record and take the rest off the bin: the
     * byte total stays exact, the length is just shorter than one unit */
    cut = size - 4;
    put_le32(lens + 4 * last, 4);
    if (bin[0] == 0xC4) {
        assertok((size_t)bin[1] >= cut);
        bin[1] = (unsigned char)(bin[1] - cut);
    } else {
        unsigned v = ((unsigned)bin[1] << 8) | bin[2];
        v -= (unsigned)cut;
        bin[1] = (unsigned char)(v >> 8), bin[2] = (unsigned char)v;
    }
    L = load_maybe(buf, len - cut, &ml);
    if (L) am_refcount(L, 1); /* the old walk asserts in amH_ref here */
    assertok(L == NULL);
    am_delsolver(L), am_delsolver(S);
}

/* x + y == 10, an edit on x; the edit is the only thing that pins x. */
static void build_edited(am_Solver *S, am_Num *x, am_Num *y, am_Id *vx,
                         am_Id *vy) {
    am_Constraint *c;
    *vx = am_newvariable(S, x), *vy = am_newvariable(S, y);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, *vx, 1.f) == AM_OK);
    assertok(am_addterm(c, *vy, 1.f) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_addconstant(c, 10.f) == AM_OK);
    assertok(am_add(c) == AM_OK);
    assertok(am_addedit(S, *vx, AM_STRONG) == AM_OK);
    am_suggest(S, *vx, 3);
    am_updatevars(S);
}

static am_Id find_edited(const am_Solver *L, const MyLoader *ml, unsigned n,
                         unsigned *slot) {
    unsigned i;
    for (i = 0; i < n; ++i)
        if (am_hasedit(L, ml->ids[i])) {
            *slot = i;
            return ml->ids[i];
        }
    return 0;
}

TEST(dump_settles_bindings_only) {
    am_Num         x = 0, y = 0;
    am_Solver     *S = am_test_new_solver();
    am_Id          vx, vy;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    size_t         len = 0;
    MyDumper       md = AM_ZERO;

    am_autoupdate(S, 0);
    vx = am_newvariable(S, &x), vy = am_newvariable(S, &y);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, vx, 1.f) == AM_OK);
    assertok(am_addterm(c, vy, 1.f) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_addconstant(c, 10.f) == AM_OK);
    assertok(am_add(c) == AM_OK);
    assertok(am_addedit(S, vx, AM_STRONG) == AM_OK);
    am_suggest(S, vx, 3);
    assertclose(x, 0, "auto_update off: nothing is settled yet");

    md.base.var_name = dump_varname;
    md.base.writer = dump_writer;
    md.p = buf, md.len = &len;
    asserteq(am_dump(S, &md.base), AM_OK);

    /* the dump settles the bindings: the documented side effect */
    assertclose(x, 3, "settled x");
    assertclose(y, 7, "settled y");
    asserteq(am_hasedit(S, vx), 1); /* the edit is untouched */
    am_delsolver(S);
}

TEST(load_edit_then_suggest_updates_rows) {
    am_Num     x = 0, y = 0;
    am_Solver *S = am_test_new_solver(), *L;
    am_Id      vx, vy, loaded;
    char       buf[TEST_BUFSIZE];
    size_t     len = 0;
    unsigned   slot = 0;
    MyLoader   ml = AM_ZERO;

    build_edited(S, &x, &y, &vx, &vy);
    len = dump_into(S, buf);
    L = load_from(buf, len, &ml);
    loaded = find_edited(L, &ml, 2, &slot);
    assertok(loaded != 0);

    /* the same follow-up suggest must move the same rows */
    am_suggest(S, vx, 7), am_updatevars(S);
    am_suggest(L, loaded, 7), am_updatevars(L);
    assertclose(x, 7, "source x");
    assertclose(y, 3, "source y");
    assertclose(ml.values[slot], 7, "loaded x");
    assertclose(ml.values[slot == 0 ? 1 : 0], 3, "loaded y");

    am_delsolver(L);
    am_delsolver(S);
}

TEST(load_edit_then_clearedits) {
    am_Num     x = 0, y = 0;
    am_Solver *S = am_test_new_solver(), *L;
    am_Id      vx, vy, loaded;
    char       buf[TEST_BUFSIZE];
    size_t     len = 0;
    unsigned   slot = 0;
    MyLoader   ml = AM_ZERO;

    build_edited(S, &x, &y, &vx, &vy);
    len = dump_into(S, buf);
    L = load_from(buf, len, &ml);
    loaded = find_edited(L, &ml, 2, &slot);
    assertok(loaded != 0);

    am_clearedits(S), am_clearedits(L);
    asserteq(am_hasedit(S, vx), 0);
    asserteq(am_hasedit(L, loaded), 0);
    assertclose(ml.values[slot], x, "loaded x after clear");
    assertclose(ml.values[slot == 0 ? 1 : 0], y, "loaded y after clear");

    am_delsolver(L);
    am_delsolver(S);
}

TEST(load_edit_then_deledit) {
    am_Num     x = 0, y = 0;
    am_Solver *S = am_test_new_solver(), *L;
    am_Id      vx, vy, loaded;
    char       buf[TEST_BUFSIZE];
    size_t     len = 0;
    unsigned   slot = 0;
    MyLoader   ml = AM_ZERO;

    build_edited(S, &x, &y, &vx, &vy);
    len = dump_into(S, buf);
    L = load_from(buf, len, &ml);
    loaded = find_edited(L, &ml, 2, &slot);
    assertok(loaded != 0);

    am_deledit(S, vx), am_deledit(L, loaded);
    asserteq(am_hasedit(S, vx), 0);
    asserteq(am_hasedit(L, loaded), 0);
    assertclose(ml.values[slot], x, "loaded x after deledit");
    assertclose(ml.values[slot == 0 ? 1 : 0], y, "loaded y after deledit");

    am_delsolver(L);
    am_delsolver(S);
}

TEST(dump_load_dump_load_keeps_the_edit) {
    am_Num     x = 0, y = 0;
    am_Solver *S = am_test_new_solver(), *L, *L2;
    am_Id      vx, vy, loaded, loaded2;
    char       buf[TEST_BUFSIZE], again[TEST_BUFSIZE];
    size_t     len = 0, len2 = 0;
    unsigned   slot = 0, slot2 = 0;
    MyLoader   ml = AM_ZERO, ml2 = AM_ZERO;

    build_edited(S, &x, &y, &vx, &vy);
    len = dump_into(S, buf);
    L = load_from(buf, len, &ml);

    len2 = dump_into(L, again);
    L2 = load_from(again, len2, &ml2);

    loaded = find_edited(L, &ml, 2, &slot);
    loaded2 = find_edited(L2, &ml2, 2, &slot2);
    assertok(loaded != 0 && loaded2 != 0);
    assertclose(ml2.values[slot2], 3, "second-round x");
    assertclose(ml.values[slot], 3, "first-round x");
    am_delsolver(L2);
    am_delsolver(L);
    am_delsolver(S);
}

/* ---- edit and refcount fixture ---- */

/* x + y == 10 in the solver, x == 5 defined but never added, edit on x */
static void build_wake_fixture(am_Solver *S, am_Num *x, am_Num *y, am_Id *vx,
                               am_Id *vy) {
    am_Constraint *c;
    *vx = am_newvariable(S, x), *vy = am_newvariable(S, y);
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, *vx, 1.f), am_addterm(c, *vy, 1.f);
    am_setrelation(c, AM_EQUAL), am_addconstant(c, 10.f);
    assertok(am_add(c) == AM_OK);
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, *vx, 1.f), am_setrelation(c, AM_EQUAL);
    am_addconstant(c, 5.f); /* kept out of the solver on purpose */
    assertok(am_addedit(S, *vx, AM_STRONG) == AM_OK);
    am_suggest(S, *vx, 3);
    am_updatevars(S);
}

/* ---- the allocation-counting allocator ---- */

static size_t count_allocs;

static void *count_allocf(void **pud, void *p, size_t n, size_t o, am_Type t) {
    if (n > 0) count_allocs += 1;
    return am_test_allocf(pud, p, n, o, t);
}

typedef struct oom {
    unsigned fail_at, count;
} oom;

static void *oom_allocf(void **pud, void *p, size_t n, size_t o, am_Type t) {
    oom *a = (oom *)*pud;
    if (n > 0) {
        a->count += 1;
        if (a->count == a->fail_at) return NULL;
    }
    return am_test_allocf(pud, p, n, o, t);
}

/* ---- load: values, constants and refcounts ---- */

TEST(load_settles_values_and_constants) {
    am_Num         v[3] = {0, 0, 0};
    am_Num         want[8];
    am_Solver     *S = am_test_new_solver(), *L;
    am_Id          sx, sy, su;
    char           buf[TEST_BUFSIZE];
    size_t         len;
    MyLoader       ml;
    unsigned       nwant = 0;
    am_Num         c0, c1;
    am_Constraint *c;

    sx = am_newvariable(S, &v[0]), sy = am_newvariable(S, &v[1]);
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, sx, 1.f), am_addterm(c, sy, 1.f);
    am_addconstant(c, 10.f), am_setrelation(c, AM_EQUAL);
    assertok(am_add(c) == AM_OK);
    c = am_newconstraint(S, AM_REQUIRED);
    am_addterm(c, sx, 1.f), am_addconstant(c, -5.f);
    am_setrelation(c, AM_EQUAL);   /* unadded */
    su = am_newvariable(S, &v[2]); /* no row: value must read 0 */
    assertok(am_addedit(S, sx, AM_STRONG) == AM_OK);
    am_suggest(S, sx, 3);
    am_updatevars(S);

    len = dump_into(S, buf);
    L = load_from(buf, len, &ml);

    /* before any tableau call the bindings and constants are ground truth */
    assertclose(*am_varvalue(L, loaded_id(&ml, sx), NULL), v[0], "x");
    assertclose(*am_varvalue(L, loaded_id(&ml, sy), NULL), v[1], "y");
    asserteq(*am_varvalue(L, loaded_id(&ml, su), NULL), 0);

    for (c = am_constraints(S, NULL); c != NULL; c = am_constraints(S, c))
        want[nwant++] = am_constant(c);
    assertok(nwant == 2 && ml.cons[1] != NULL);
    c0 = am_constant(ml.cons[0]), c1 = am_constant(ml.cons[1]);
    assertok((c0 == want[0] && c1 == want[1])
             || (c0 == want[1] && c1 == want[0]));

    am_delsolver(L);
    am_delsolver(S);
}

TEST(load_restores_refcounts_at_wake) {
    am_Num     x = 0, y = 0;
    am_Solver *S = am_test_new_solver(), *L;
    am_Id      vx, vy, lx, ly;
    char       buf[TEST_BUFSIZE];
    size_t     len;
    MyLoader   ml;

    build_wake_fixture(S, &x, &y, &vx, &vy);
    len = dump_into(S, buf);
    L = load_from(buf, len, &ml);
    lx = loaded_id(&ml, vx), ly = loaded_id(&ml, vy);

    /* own reference + one per expression term (the hidden edit counts too) */
    asserteq(am_refcount(L, lx), am_refcount(S, vx));
    asserteq(am_refcount(L, ly), am_refcount(S, vy));

    am_delsolver(L);
    am_delsolver(S);
}

TEST(refcount_wakes) {
    am_Num     x = 0, y = 0;
    am_Solver *S = am_test_new_solver(), *L;
    am_Id      vx, vy, lx;
    char       buf[TEST_BUFSIZE];
    size_t     len;
    MyLoader   ml;

    build_edited(S, &x, &y, &vx, &vy);
    len = dump_into(S, buf);
    L = load_from(buf, len, &ml);
    lx = loaded_id(&ml, vx);

    asserteq(am_refcount(L, lx), am_refcount(S, vx));
    assertclose(*am_varvalue(L, lx, NULL), x, "x after refcount wake");

    am_delsolver(L);
    am_delsolver(S);
}

static am_Id map_var(const MyLoader *m, am_Id from) {
    unsigned i;
    for (i = 0; i < m->nvars; ++i)
        if (m->src[i] == from) return m->ids[i];
    return 0;
}

static void same_expr(am_Constraint *a, am_Constraint *b, const MyLoader *m) {
    am_Id  id = 0;
    am_Num coef, other;
    asserteq(count_terms(b), count_terms(a));
    while ((id = am_terms(a, id, &coef)) != 0) {
        am_Id mapped = map_var(m, id);
        assertok(mapped != 0 && term_find(b, mapped, &other));
        assertok(memcmp(&coef, &other, sizeof(coef)) == 0);
    }
}

/* A loaded solver dumps after waking; the next load must carry the same
 * bindings, constraint metadata and expression terms (not bytes). */
TEST(dump_after_load_materializes) {
    am_Num     x = 0, y = 0;
    am_Solver *S = am_test_new_solver(), *L1, *L2;
    am_Id      vx, vy;
    char       one[TEST_BUFSIZE], two[TEST_BUFSIZE];
    size_t     len1, len2;
    MyLoader   m1, m2;
    unsigned   i;

    build_edited(S, &x, &y, &vx, &vy);
    len1 = dump_into(S, one), L1 = load_from(one, len1, &m1);
    len2 = dump_into(L1, two); /* wakes L1 */

    L2 = load_from(two, len2, &m2);
    for (i = 0; i < m2.nvars; ++i)
        assertclose(m2.values[i], *am_varvalue(L1, m2.src[i], NULL), "binding");
    assertok(m1.cons[0] != NULL && m2.cons[0] != NULL);
    asserteq(am_relation(m2.cons[0]), am_relation(m1.cons[0]));
    assertok(am_strength(m2.cons[0]) == am_strength(m1.cons[0]));
    assertnear(am_constant(m2.cons[0]), am_constant(m1.cons[0]), 0);
    same_expr(m1.cons[0], m2.cons[0], &m2);
    am_delsolver(L2);
    am_delsolver(L1);
    am_delsolver(S);
}

TEST(wake_has_no_allocations) {
    am_Num     x = 0, y = 0;
    am_Solver *S = am_test_new_solver(), *L;
    am_Id      vx, vy, lx;
    char       buf[TEST_BUFSIZE];
    size_t     len;
    MyLoader   ml;

    build_edited(S, &x, &y, &vx, &vy);
    len = dump_into(S, buf);

    loader_prepare(&ml, buf, len);
    assertok((L = am_load(count_allocf, NULL, &ml.base, 0)) != NULL);
    lx = loaded_id(&ml, vx);

    /* load return -> first suggest completion: no allocation at all */
    count_allocs = 0;
    am_suggest(L, lx, 3);
    asserteq(count_allocs, 0);
    assertclose(*am_varvalue(L, lx, NULL), 3, "cached suggest x");
    assertclose(*am_varvalue(L, loaded_id(&ml, vy), NULL), 7, "cached y");

    am_delsolver(L);
    am_delsolver(S);
}

TEST(delvariable_before_wake) {
    am_Num     x = 0, y = 0;
    am_Solver *S = am_test_new_solver(), *L;
    am_Id      vx, vy, lx;
    char       buf[TEST_BUFSIZE];
    size_t     len;
    MyLoader   ml;

    build_edited(S, &x, &y, &vx, &vy);
    len = dump_into(S, buf);
    L = load_from(buf, len, &ml);
    lx = loaded_id(&ml, vx);

    /* the variable is still referenced by the expressions: dropping the own
     * reference keeps it alive */
    am_delvariable(L, lx);
    asserteq(count_vars(L), 2);
    asserteq(am_refcount(L, lx), am_refcount(S, vx) - 1);
    assertok(am_varvalue(L, lx, NULL) != NULL);

    am_delsolver(L);
    am_delsolver(S);
}

/* ---- reset keeps the configured expressions ---- */

TEST(resetsolver_keeps_expressions) {
    am_Num         x = 0, y = 0;
    am_Solver     *S = am_test_new_solver(), *L;
    am_Id          vx, vy, lx;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    size_t         len;
    MyLoader       ml;
    am_Num         coef = 0, want = 0;

    build_edited(S, &x, &y, &vx, &vy);
    len = dump_into(S, buf);
    L = load_from(buf, len, &ml);
    lx = loaded_id(&ml, vx);
    assertok(am_refcount(L, lx) > 0); /* wakes */

    c = ml.cons[0];
    assertok(c != NULL);
    assertok(term_find(c, lx, &want));

    am_resetsolver(L);

    /* the expression is still readable, and the solver is reusable */
    assertok(term_find(c, lx, &coef));
    assertnear(coef, want, 0);
    c = am_newconstraint(L, AM_REQUIRED);
    assertok(c != NULL);
    assertok(am_addterm(c, lx, 1.f) == AM_OK);
    assertok(am_addconstant(c, -4.f) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    am_updatevars(L);
    assertclose(*am_varvalue(L, lx, NULL), 4, "after reset and re-add");

    am_delsolver(L);
    am_delsolver(S);
}

TEST(long_cons_name_roundtrip) {
    am_Num         x = 0, y = 0;
    am_Solver     *S = am_test_new_solver(), *L;
    am_Id          vx, vy;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    size_t         len;
    MyLoader       ml;
    const char    *want = "a-very-long-name-that-excceed-32-bytes";

    vx = am_newvariable(S, &x), vy = am_newvariable(S, &y);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, vx, 1.f) == AM_OK);
    assertok(am_addterm(c, vy, 1.f) == AM_OK);
    assertok(am_setrelation(c, AM_EQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);

    len = dump_into(S, buf); /* dump_consname writes a str8 name */
    L = load_from(buf, len, &ml);
    assertstreq(ml.cname, want);
    assertok(ml.cons[0] != NULL);
    asserteq(am_relation(ml.cons[0]), AM_EQUAL);
    am_delsolver(L);
    am_delsolver(S);
}

TEST(relation_ge_roundtrip) {
    am_Num         x = 0, y = 0;
    am_Solver     *S = am_test_new_solver(), *L;
    am_Id          vx, vy, lx;
    am_Constraint *c, *src;
    char           buf[TEST_BUFSIZE];
    size_t         len;
    MyLoader       ml;
    am_Num         coef = 0, want;

    vx = am_newvariable(S, &x), vy = am_newvariable(S, &y);
    c = am_newconstraint(S, AM_REQUIRED);
    assertok(am_addterm(c, vx, 1.f) == AM_OK);
    assertok(am_addterm(c, vy, 1.f) == AM_OK);
    assertok(am_addconstant(c, 10.f) == AM_OK);
    assertok(am_setrelation(c, AM_GREATEREQUAL) == AM_OK);
    assertok(am_add(c) == AM_OK);
    src = am_constraints(S, NULL);
    assertok(src != NULL);
    want = am_constant(src);

    len = dump_into(S, buf);
    L = load_from(buf, len, &ml);
    /* relation and constant are ground truth before the wake */
    assertok(ml.cons[0] != NULL);
    asserteq(am_relation(ml.cons[0]), AM_GREATEREQUAL);
    assertnear(am_constant(ml.cons[0]), want, 0);

    lx = loaded_id(&ml, vx);
    assertok(am_refcount(L, lx) > 0); /* wakes */
    assertok(term_find(ml.cons[0], lx, &coef));
    assertnear(coef, 1, 0);
    assertok(term_find(ml.cons[0], loaded_id(&ml, vy), &coef));
    assertnear(coef, 1, 0);

    am_delsolver(L);
    am_delsolver(S);
}

/* ---- chunked reader ---- */

TEST(load_accepts_a_byte_at_a_time_reader) {
    am_Num     x = 0, y = 0;
    am_Solver *S = am_test_new_solver(), *L;
    am_Id      vx, vy, lx;
    char       buf[TEST_BUFSIZE];
    size_t     len;
    MyLoader   ml;

    build_edited(S, &x, &y, &vx, &vy);
    len = dump_into(S, buf);

    /* load_reader hands out one byte per call: the loader must accept any
     * chunking of the stream, not assume it arrives as one block */
    loader_prepare(&ml, buf, len);
    assertok((L = am_load(am_test_allocf, NULL, &ml.base, 0)) != NULL);
    lx = loaded_id(&ml, vx);
    assertclose(*am_varvalue(L, lx, NULL), x, "chunked x");
    assertclose(*am_varvalue(L, loaded_id(&ml, vy), NULL), y, "chunked y");

    am_delsolver(L);
    am_delsolver(S);
}

/* ---- out of memory while loading ---- */

TEST(oom_load_paths) {
    am_Num     x = 0, y = 0;
    am_Solver *S = am_test_new_solver(), *L = NULL;
    am_Id      vx, vy;
    char       buf[TEST_BUFSIZE];
    size_t     len, base;
    unsigned   k;

    build_edited(S, &x, &y, &vx, &vy); /* the edit covers the hidden
                                          expression pre-resize */
    len = dump_into(S, buf);
    base = allmem;

    for (k = 1; k < 1000; ++k) {
        oom      a;
        MyLoader ml;
        a.fail_at = k, a.count = 0;
        loader_prepare(&ml, buf, len);
        L = am_load(oom_allocf, &a, &ml.base, 0);
        if (L != NULL) break;
        asserteq(allmem, base); /* every failed load released everything */
    }
    assertok(k > 1 && k < 1000 && L != NULL);
    am_delsolver(L);
    am_delsolver(S);
}

#include "dump_test.gen.inc"
