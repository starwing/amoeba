/* tests/dump_test.c -- dump/load suites
 *
 * Ported from the old test.c "test dumpload" section.  The suite pins:
 *
 *   dumpload                       dump -> load preserves the shape and every
 *                                 variable value, not just the byte count
 *   dump_roundtrip_is_exact        a double build's solution survives bit for
 *                                 bit; float builds pass it trivially
 *   load_reads_float64_stream      a float64 stream loads in either build
 *   load_reads_float32_stream      a float32 stream loads in either build
 *   load_settles_nonbasic_binding  a variable without a row reads 0 after load
 *   dump_empty                     a solver with no constraints dumps and loads
 *   load_then_remove_keeps_vars    load restores the per-term reference counts
 *   load_hands_over_the_solver_before_the_body  the loader sees the solver
 *                                 once, before any variable or constraint
 *   load_hands_over_the_solver_without_variables  an empty stream still hands
 *                                 it over, though load_var() never runs
 *   load_skips_the_solver_callback_for_a_bad_header  a header failure hands
 *                                 nothing out, so nothing has to be dropped
 *   load_failure_releases_the_solver  a later failure frees the handed solver
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
    assertok(*md->len + len <= TEST_BUFSIZE);
    if (*md->len + len > TEST_BUFSIZE) return AM_FAILED;
    memcpy(md->p + *md->len, buf, len);
    *md->len += len;
    return AM_OK;
}

typedef struct MyLoader {
    am_Loader      base;
    am_Num         values[37];
    am_Id          ids[37]; /* ids this load assigned, in stream order */
    am_Constraint *cons[8]; /* constraints this load created (first 8) */
    const char    *p;
    size_t         len;
} MyLoader;

static am_Num *load_var(am_Loader *l, const char *name, unsigned i, am_Id var) {
    MyLoader *ml = (MyLoader *)l;
    /* a sentinel makes a missing settle visible even when 0 is expected */
    ml->ids[i] = var; /* the id is new after a load: address vars by index */
    return (void)name, ml->values[i] = -12345, &ml->values[i];
}

void load_cons(am_Loader *l, const char *name, unsigned i,
               am_Constraint *cons) {
    MyLoader *ml = (MyLoader *)l;
    (void)name;
    if (i < 8) ml->cons[i] = cons;
}

const char *load_reader(am_Loader *l, size_t *plen) {
    MyLoader *ml = (MyLoader *)l;
    /* reads from char by char */
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

/* ---- re-encoding a real dump at either float width ----
 *
 * A double build writes every number as msgpack float64, a float build as
 * float32, and the reader takes either.  A test binary is one build or the
 * other, so the other width's stream is made by re-encoding a real dump
 * instead of hand-writing one that could drift from the writer. */
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

/* walks the writer's msgpack subset: ints, arrays, strings, floats */
static void reenc_value(reenc *x) {
    unsigned probe = 1, head, i, n, ty = *x->r;
    int      be = *(const unsigned char *)&probe == 0;
    if (ty == 0xCA || ty == 0xCB)
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
    else {
        assertok(ty <= 0x7F);
        reenc_raw(x, 1);
    }
}

/* Dump a one-variable solver holding want, re-encode every float to bits
 * (32 or 64), load the result and require the binding back bit for bit.
 * The fixture must be exact in both widths. */
static void reenc_roundtrip(am_Num want, int bits) {
    am_Num         x = 0;
    am_Solver     *S, *S2;
    am_Id          xv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    char           enc[TEST_BUFSIZE];
    size_t         len = 0, enclen;
    MyDumper       md;
    MyLoader       ml;
    reenc          xr;

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

    xr.r = (const unsigned char *)buf, xr.w = (unsigned char *)enc;
    xr.bits = bits;
    reenc_value(&xr);
    assertok(xr.r == (const unsigned char *)buf + len); /* one pass */
    enclen = (size_t)(xr.w - (unsigned char *)enc);
    assertok(enclen > 0 && enclen < TEST_BUFSIZE);

    memset(&ml, 0, sizeof(ml));
    ml.base.load_var = load_var;
    ml.base.load_cons = load_cons;
    ml.base.reader = load_reader;
    ml.p = enc, ml.len = enclen;
    assertok((S2 = am_load(am_test_allocf, NULL, &ml.base, 0)) != NULL);
    assertok(memcmp(&ml.values[0], &want, sizeof(want)) == 0);

    am_delsolver(S2);
    am_delsolver(S);
}

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
    int        ret;

    S = am_test_new_solver();
    assertok(S != NULL);

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
        assertok(ret == AM_OK);
        assertok(len > 0 && len < TEST_BUFSIZE);
        printf("dumped len=%d (not asserted)\n", (int)len);
    }

    {
        /* The dumped state is the reference: am_dump() clears the edits and
         * settles the bindings, so compare against the values right now. */
        MyLoader   myl;
        am_Solver *S2;
        size_t     i;

        memset(&myl, 0, sizeof(myl));
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

        /* ... and the same solution: a load settles the bindings itself */
        assertclose(myl.values[0], w, "width");
        assertclose(myl.values[1], h, "height");
        for (i = 0; i < 35; ++i) assertclose(myl.values[2 + i], values[i], "v");

        /* a later am_updatevars() must not undo or re-collect anything */
        am_updatevars(S2);
        assertclose(myl.values[0], w, "width after update");
        asserteq(S2->vars.count, S->vars.count);

        am_delsolver(S2);
    }

    am_delsolver(S);
    printf("allmem = %d\n", (int)allmem);
    printf("maxmem = %d\n", (int)maxmem);
    assertok(allmem == 0);
    maxmem = 0;
}

/* The stream carries am_Num's own width, so a value float32 cannot hold must
 * survive a double build's dump/load bit for bit.  Float builds pass this
 * trivially: the fixture is exact in either width. */
TEST(dump_roundtrip_is_exact) {
    am_Num         want = (am_Num)(1.0 / 3.0);
    am_Num         x = 0;
    am_Solver     *S, *S2;
    am_Id          xv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    size_t         len = 0;
    MyDumper       md;
    MyLoader       ml;

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

    memset(&ml, 0, sizeof(ml));
    ml.base.load_var = load_var;
    ml.base.load_cons = load_cons;
    ml.base.reader = load_reader;
    ml.p = buf, ml.len = len;
    assertok((S2 = am_load(am_test_allocf, NULL, &ml.base, 0)) != NULL);
    assertok(memcmp(&ml.values[0], &want, sizeof(want)) == 0);

    am_delsolver(S2);
    am_delsolver(S);
}

/* The reader takes either float width: widen this build's stream to float64
 * and load it again (a copy when the build already writes float64). */
TEST(load_reads_float64_stream) { reenc_roundtrip((am_Num)(1.0 / 3.0), 64); }

/* ... and the other direction: an old or float-build stream (float32) must
 * load in a double build too. */
TEST(load_reads_float32_stream) { reenc_roundtrip((am_Num)0.25, 32); }

/* x + y == 10 leaves one variable without a row: its tableau value is 0, so a
 * load must overwrite whatever load_var() put in that binding. */
TEST(load_settles_nonbasic_binding) {
    am_Num         x = 0, y = 0;
    am_Solver     *S, *S2;
    am_Id          xv, yv;
    am_Constraint *c;
    char           buf[TEST_BUFSIZE];
    size_t         len = 0;
    MyDumper       md;
    MyLoader       ml;

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

    md.base.var_name = dump_varname;
    md.base.cons_name = NULL;
    md.base.writer = dump_writer;
    md.p = buf, md.len = &len;
    assertok(am_dump(S, &md.base) == AM_OK);

    memset(&ml, 0, sizeof(ml));
    ml.base.load_var = load_var;
    ml.base.load_cons = load_cons;
    ml.base.reader = load_reader;
    ml.p = buf, ml.len = len;
    assertok((S2 = am_load(am_test_allocf, NULL, &ml.base, 0)) != NULL);

    /* y has no row: it reads 0, not the sentinel load_var() left there */
    assertclose(ml.values[0], 10, "loaded x");
    assertclose(ml.values[1], 0, "loaded y");

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
    MyDumper   md;
    MyLoader   ml;

    md.base.var_name = dump_varname;
    md.base.cons_name = NULL;
    md.base.writer = dump_writer;
    md.p = buf, md.len = &len;
    memset(&ml, 0, sizeof(ml));
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
    MyDumper       md;
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

    md.base.var_name = dump_varname;
    md.base.cons_name = NULL;
    md.base.writer = dump_writer;
    md.p = buf, md.len = &len;
    assertok(am_dump(S, &md.base) == AM_OK);

    memset(&ml, 0, sizeof(ml));
    ml.base.load_var = load_var;
    ml.base.load_cons = load_cons;
    ml.base.reader = load_reader;
    ml.p = buf, ml.len = len;
    assertok((S2 = am_load(am_test_allocf, NULL, &ml.base, 0)) != NULL);

    /* one own reference + one per live term, exactly as before the dump */
    asserteq(am_refcount(S2, ml.ids[0]), 3);
    asserteq(am_refcount(S2, ml.ids[1]), 2);

    /* load_cons() recorded the constraints of this load; the two-term one is
     * the constraint to drop (ids are new, the order is the stream's) */
    assertok(ml.cons[0] != NULL && ml.cons[1] != NULL);
    victim = ml.cons[0]->expression.terms.count == 2 ? ml.cons[0] : ml.cons[1];
    asserteq(victim->expression.terms.count, 2);

    /* With auto_update on, am_delconstraint() collects unreferenced variables
     * right away: the remaining constraint still owns x, y keeps its own
     * reference. */
    am_autoupdate(S2, 1);
    am_delconstraint(victim);
    asserteq(S2->vars.count, 2);
    asserteq(am_refcount(S2, ml.ids[0]), 2);
    asserteq(am_refcount(S2, ml.ids[1]), 1);

    /* the solver is still usable: the surviving variable accepts new terms */
    c = am_newconstraint(S2, AM_REQUIRED);
    assertok(am_addterm(c, ml.ids[0], 1) == AM_OK);
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

/* dumps S into buf with the shared writer, returns the stream length */
static size_t dump_into(am_Solver *S, char *buf) {
    MyDumper md;
    size_t   len = 0;
    md.base.var_name = dump_varname;
    md.base.cons_name = NULL;
    md.base.writer = dump_writer;
    md.p = buf, md.len = &len;
    assertok(am_dump(S, &md.base) == AM_OK);
    return len;
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
    asserteq(S2->vars.count, 0);
    asserteq(S2->constraints.count, 0);

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

#include "dump_test.gen.inc"
