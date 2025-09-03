/*-----------------------------------------------------------------------------
| Copyright (c) 2020, Nucleic Development Team.
|
| Distributed under the terms of the Modified BSD License.
|
| The full license is in the file LICENSE, distributed with this software.
|----------------------------------------------------------------------------*/

// Time updating an EditVariable in a set of constraints typical of enaml use.

#define ANKERL_NANOBENCH_IMPLEMENT
#include "nanobench.h"

#define AM_IMPLEMENTATION
#include "amoeba.h"

static void build_solver(am_Solver* S, am_Id width, am_Id height, am_Num *values) {
    /* Create custom strength */
    am_Num mmedium = AM_MEDIUM * 1.25;
    am_Num smedium = AM_MEDIUM * 100;

    /* Create the variable */
    am_Id left            = am_newvariable(S, &values[0]);
    am_Id top             = am_newvariable(S, &values[1]);
    am_Id contents_top    = am_newvariable(S, &values[2]);
    am_Id contents_bottom = am_newvariable(S, &values[3]);
    am_Id contents_left   = am_newvariable(S, &values[4]);
    am_Id contents_right  = am_newvariable(S, &values[5]);
    am_Id midline         = am_newvariable(S, &values[6]);
    am_Id ctleft          = am_newvariable(S, &values[7]);
    am_Id ctheight        = am_newvariable(S, &values[8]);
    am_Id cttop           = am_newvariable(S, &values[9]);
    am_Id ctwidth         = am_newvariable(S, &values[10]);
    am_Id lb1left         = am_newvariable(S, &values[11]);
    am_Id lb1height       = am_newvariable(S, &values[12]);
    am_Id lb1top          = am_newvariable(S, &values[13]);
    am_Id lb1width        = am_newvariable(S, &values[14]);
    am_Id lb2left         = am_newvariable(S, &values[15]);
    am_Id lb2height       = am_newvariable(S, &values[16]);
    am_Id lb2top          = am_newvariable(S, &values[17]);
    am_Id lb2width        = am_newvariable(S, &values[18]);
    am_Id lb3left         = am_newvariable(S, &values[19]);
    am_Id lb3height       = am_newvariable(S, &values[20]);
    am_Id lb3top          = am_newvariable(S, &values[21]);
    am_Id lb3width        = am_newvariable(S, &values[22]);
    am_Id fl1left         = am_newvariable(S, &values[23]);
    am_Id fl1height       = am_newvariable(S, &values[24]);
    am_Id fl1top          = am_newvariable(S, &values[25]);
    am_Id fl1width        = am_newvariable(S, &values[26]);
    am_Id fl2left         = am_newvariable(S, &values[27]);
    am_Id fl2height       = am_newvariable(S, &values[28]);
    am_Id fl2top          = am_newvariable(S, &values[29]);
    am_Id fl2width        = am_newvariable(S, &values[30]);
    am_Id fl3left         = am_newvariable(S, &values[31]);
    am_Id fl3height       = am_newvariable(S, &values[32]);
    am_Id fl3top          = am_newvariable(S, &values[33]);
    am_Id fl3width        = am_newvariable(S, &values[34]);

#ifdef __GNUC__
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wmissing-field-initializers"
# pragma GCC diagnostic ignored "-Wc99-extensions"
#endif

    /* Add the constraints */
    const struct Info {
        struct Item {
            am_Id  var;
            am_Num mul;
        } term[5];
        am_Num constant;
        int    relation;
        am_Num strength;
    } constraints[] = {
        { {{left}},                                                -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{height}},                                              0,            AM_EQUAL,      AM_MEDIUM   },
        { {{top}},                                                 -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{width}},                                               -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{height}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{top,-1},{contents_top}},                               -10,          AM_EQUAL,      AM_REQUIRED },
        { {{lb3height}},                                           -16,          AM_EQUAL,      AM_STRONG   },
        { {{lb3height}},                                           -16,          AM_GREATEQUAL, AM_STRONG   },
        { {{ctleft}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{cttop}},                                               -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{ctwidth}},                                             -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{ctheight}},                                            -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl3left}},                                             0,            AM_GREATEQUAL, AM_REQUIRED },
        { {{ctheight}},                                            -24,          AM_GREATEQUAL, smedium     },
        { {{ctwidth}},                                             -1.67772e+07, AM_LESSEQUAL,  smedium     },
        { {{ctheight}},                                            -24,          AM_LESSEQUAL,  smedium     },
        { {{fl3top}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl3width}},                                            -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl3height}},                                           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1width}},                                            -67,          AM_EQUAL,      AM_WEAK     },
        { {{lb2width}},                                            -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2height}},                                           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl2height}},                                           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb3left}},                                             -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl2width}},                                            -125,         AM_GREATEQUAL, AM_STRONG   },
        { {{fl2height}},                                           -21,          AM_EQUAL,      AM_STRONG   },
        { {{fl2height}},                                           -21,          AM_GREATEQUAL, AM_STRONG   },
        { {{lb3top}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb3width}},                                            -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1left}},                                             -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl1width}},                                            -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1width}},                                            -67,          AM_GREATEQUAL, AM_STRONG   },
        { {{fl2left}},                                             -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2width}},                                            -66,          AM_EQUAL,      AM_WEAK     },
        { {{lb2width}},                                            -66,          AM_GREATEQUAL, AM_STRONG   },
        { {{lb2height}},                                           -16,          AM_EQUAL,      AM_STRONG   },
        { {{fl1height}},                                           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl1top}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2top}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2top,-1},{lb3top},{lb2height,-1}},                   -10,          AM_EQUAL,      mmedium     },
        { {{lb3top,-1},{lb3height,-1},{fl3top}},                   -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{lb3top,-1},{lb3height,-1},{fl3top}},                   -10,          AM_EQUAL,      mmedium     },
        { {{contents_bottom},{fl3height,-1},{fl3top,-1}},          -0,           AM_EQUAL,      mmedium     },
        { {{fl1top},{contents_top,-1}},                            0,            AM_GREATEQUAL, AM_REQUIRED },
        { {{fl1top},{contents_top,-1}},                            0,            AM_EQUAL,      mmedium     },
        { {{contents_bottom},{fl3height,-1},{fl3top,-1}},          -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{left,-1},{width,-1},{contents_right}},                 10,           AM_EQUAL,      AM_REQUIRED },
        { {{top,-1},{height,-1},{contents_bottom}},                10,           AM_EQUAL,      AM_REQUIRED },
        { {{left,-1},{contents_left}},                             -10,          AM_EQUAL,      AM_REQUIRED },
        { {{lb3left},{contents_left,-1}},                          0,            AM_EQUAL,      mmedium     },
        { {{fl1left},{midline,-1}},                                0,            AM_EQUAL,      AM_STRONG   },
        { {{fl2left},{midline,-1}},                                0,            AM_EQUAL,      AM_STRONG   },
        { {{ctleft},{midline,-1}},                                 0,            AM_EQUAL,      AM_STRONG   },
        { {{fl1top},{fl1height,0.5},{lb1top,-1},{lb1height,-0.5}}, 0,            AM_EQUAL,      AM_STRONG   },
        { {{lb1left},{contents_left,-1}},                          0,            AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1left},{contents_left,-1}},                          0,            AM_EQUAL,      mmedium     },
        { {{lb1left,-1},{fl1left},{lb1width,-1}},                  -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1left,-1},{fl1left},{lb1width,-1}},                  -10,          AM_EQUAL,      mmedium     },
        { {{fl1left,-1},{contents_right},{fl1width,-1}},           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{width}},                                               0,            AM_EQUAL,      AM_MEDIUM   },
        { {{fl1top,-1},{fl2top},{fl1height,-1}},                   -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{fl1top,-1},{fl2top},{fl1height,-1}},                   -10,          AM_EQUAL,      mmedium     },
        { {{cttop},{fl2top,-1},{fl2height,-1}},                    -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{ctheight,-1},{cttop,-1},{fl3top}},                     -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{contents_bottom},{fl3height,-1},{fl3top,-1}},          -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{cttop},{fl2top,-1},{fl2height,-1}},                    -10,          AM_EQUAL,      mmedium     },
        { {{fl1left,-1},{contents_right},{fl1width,-1}},           -0,           AM_EQUAL,      mmedium     },
        { {{lb2top,-1},{lb2height,-0.5},{fl2top},{fl2height,0.5}}, 0,            AM_EQUAL,      AM_STRONG   },
        { {{contents_left,-1},{lb2left}},                          0,            AM_GREATEQUAL, AM_REQUIRED },
        { {{contents_left,-1},{lb2left}},                          0,            AM_EQUAL,      mmedium     },
        { {{fl2left},{lb2width,-1},{lb2left,-1}},                  -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{ctheight,-1},{cttop,-1},{fl3top}},                     -10,          AM_EQUAL,      mmedium     },
        { {{contents_bottom},{fl3height,-1},{fl3top,-1}},          -0,           AM_EQUAL,      mmedium     },
        { {{lb1top}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1width}},                                            -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1height}},                                           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl2left},{lb2width,-1},{lb2left,-1}},                  -10,          AM_EQUAL,      mmedium     },
        { {{fl2left,-1},{fl2width,-1},{contents_right}},           -0,           AM_EQUAL,      mmedium     },
        { {{fl2left,-1},{fl2width,-1},{contents_right}},           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb3left},{contents_left,-1}},                          0,            AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1left}},                                             -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{ctheight,0.5},{cttop},{lb3top,-1},{lb3height,-0.5}},   0,            AM_EQUAL,      AM_STRONG   },
        { {{ctleft},{lb3left,-1},{lb3width,-1}},                   -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{ctwidth,-1},{ctleft,-1},{contents_right}},             -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{ctleft},{lb3left,-1},{lb3width,-1}},                   -10,          AM_EQUAL,      mmedium     },
        { {{fl3left},{contents_left,-1}},                          0,            AM_GREATEQUAL, AM_REQUIRED },
        { {{fl3left},{contents_left,-1}},                          0,            AM_EQUAL,      mmedium     },
        { {{ctwidth,-1},{ctleft,-1},{contents_right}},             -0,           AM_EQUAL,      mmedium     },
        { {{fl3left,-1},{contents_right},{fl3width,-1}},           -0,           AM_EQUAL,      mmedium     },
        { {{contents_top,-1},{lb1top}},                            0,            AM_GREATEQUAL, AM_REQUIRED },
        { {{contents_top,-1},{lb1top}},                            0,            AM_EQUAL,      mmedium     },
        { {{fl3left,-1},{contents_right},{fl3width,-1}},           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2top},{lb1top,-1},{lb1height,-1}},                   -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2top,-1},{lb3top},{lb2height,-1}},                   -10,          AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2top},{lb1top,-1},{lb1height,-1}},                   -10,          AM_EQUAL,      mmedium     },
        { {{fl1height}},                                           -21,          AM_EQUAL,      AM_STRONG   },
        { {{fl1height}},                                           -21,          AM_GREATEQUAL, AM_STRONG   },
        { {{lb2left}},                                             -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb2height}},                                           -16,          AM_GREATEQUAL, AM_STRONG   },
        { {{fl2top}},                                              -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{fl2width}},                                            -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{lb1height}},                                           -16,          AM_GREATEQUAL, AM_STRONG   },
        { {{lb1height}},                                           -16,          AM_EQUAL,      AM_STRONG   },
        { {{fl3width}},                                            -125,         AM_GREATEQUAL, AM_STRONG   },
        { {{fl3height}},                                           -21,          AM_EQUAL,      AM_STRONG   },
        { {{fl3height}},                                           -21,          AM_GREATEQUAL, AM_STRONG   },
        { {{lb3height}},                                           -0,           AM_GREATEQUAL, AM_REQUIRED },
        { {{ctwidth}},                                             -119,         AM_GREATEQUAL, smedium     },
        { {{lb3width}},                                            -24,          AM_EQUAL,      AM_WEAK     },
        { {{lb3width}},                                            -24,          AM_GREATEQUAL, AM_STRONG   },
        { {{fl1width}},                                            -125,         AM_GREATEQUAL, AM_STRONG   },
    };

#ifdef __GNUC__
# pragma GCC diagnostic pop
#endif

    size_t i;
    int ret;

    /* Add the edit variables */
    ret = am_addedit(S, width, AM_STRONG);
    assert(ret == AM_OK), (void)ret;
    ret = am_addedit(S, height, AM_STRONG);
    assert(ret == AM_OK), (void)ret;

    for (i = 0; i < sizeof(constraints)/sizeof(constraints[0]); ++i) {
        am_Constraint *c = am_newconstraint(S, constraints[i].strength);
        const struct Info::Item *p;
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

int main() {
    am_Num values[35];
    am_Solver *S;

    // demo how to use a memory pool across solvers.
    am_MemPool mempool;
    am_initpool(&mempool, sizeof(am_Constraint));
    auto alloc = [&mempool](void *ptr, size_t ns, am_AllocType ty) {
        if (ty == am_AllocConstraint) {
            if (ns) return am_poolalloc(&mempool);
            return am_poolfree(&mempool, ptr), (void*)0;
        }
        if (ns) return realloc(ptr, ns);
        return free(ptr), (void*)0;
    };
    auto alloc_func = [](void** pud, void *ptr, size_t ns, size_t, am_AllocType ty) {
        const auto func = &decltype(alloc)::operator();
        auto alloc_ptr = *(decltype(alloc)**)(pud);
        return (alloc_ptr->*func)(ptr, ns, ty);
    };

#if !DISABLE_BUILD
    ankerl::nanobench::Bench().minEpochIterations(100).run("building solver", [&] {
        am_Solver *S = am_newsolver(alloc_func, &alloc);
        am_Num w, h;
        am_Id width = am_newvariable(S, &w);
        am_Id height = am_newvariable(S, &h);
        build_solver(S, width, height, values);
        ankerl::nanobench::doNotOptimizeAway(S); //< prevent the compiler to optimize away the S
        am_delsolver(S);
    });
#endif /* !DISABLE_BUILD */

#if !DISABLE_LOAD
    std::vector<char> buf; 
    {
        am_Num w, h;
        S = am_newsolver(NULL, NULL);
        am_Id width = am_newvariable(S, &w);
        am_Id height = am_newvariable(S, &h);
        build_solver(S, width, height, values);

        struct MyDumper {
            am_Dumper base;
            std::vector<char> *buf;
        };
        MyDumper d;
        d.buf = &buf;
        d.base.var_name = [](am_Dumper*, unsigned idx, am_Id, am_Num*) {
            return idx == 0 ? "width" : idx == 1 ? "height" : NULL;
        };
        d.base.cons_name = nullptr;
        d.base.writer = [](am_Dumper* rd, const void *buf, size_t len) {
            MyDumper *d = (MyDumper*)rd;
            d->buf->insert(d->buf->end(), (char*)buf, (char*)buf+len);
            return AM_OK;
        };
        int ret = am_dump(S, &d.base);
        assert(ret == AM_OK), (void)ret;
        am_delsolver(S);
    }

    ankerl::nanobench::Bench().minEpochIterations(2000).run("load solver", [&] {
        struct MyLoader {
            am_Loader base;
            am_Num values[37];
            std::vector<char> *buf;
        };
        MyLoader l;
        l.buf = &buf;
        l.base.load_var = [](am_Loader* rl, const char*, unsigned idx, am_Id) {
            MyLoader *l = (MyLoader*)rl;
            return &l->values[idx];
        };
        l.base.load_cons = nullptr;
        l.base.reader = [](am_Loader* rl, size_t *plen) {
            MyLoader *l = (MyLoader*)rl;
            std::vector<char> *buf = l->buf;
            if (buf) {
                *plen = buf->size();
                l->buf = nullptr;
                return (const char*)buf->data();
            }
            return (const char*)nullptr;
        };
        am_Solver *S = am_newsolver(alloc_func, &alloc);
        int ret = am_load(S, &l.base);
        assert(ret == AM_OK), (void)ret;
        ankerl::nanobench::doNotOptimizeAway(S); //< prevent the compiler to optimize away the S
        am_delsolver(S);
    });
#endif /* !DISABLE_LOAD */

#if !DISABLE_SUGGEST
    struct Size {
        int width;
        int height;
    };

    Size sizes[] = {
        { 400, 600 },
        { 600, 400 },
        { 800, 1200 },
        { 1200, 800 },
        { 400, 800 },
        { 800, 400 }
    };

    S = am_newsolver(NULL, NULL);
    am_Num width, height;
    am_Id widthVar = am_newvariable(S, &width);
    am_Id heightVar = am_newvariable(S, &height);
    build_solver(S, widthVar, heightVar, values);

    for (const Size& size : sizes) {
        am_Num width = size.width;
        am_Num height = size.height;

        ankerl::nanobench::Bench().minEpochIterations(100000).run("suggest value " + std::to_string(size.width) + "x" + std::to_string(size.height), [&] {
            am_suggest(S, widthVar, width);
            am_suggest(S, heightVar, height);
            am_updatevars(S);
        });
    }

    am_delsolver(S);
#endif /* !DISABLE_SUGGEST */

    am_freepool(&mempool);
    return 0;
}

// cc: flags+='-ggdb -O3 -DNDEBUG -DDISABLE_BUILD -DDISABLE_SUGGEST'
