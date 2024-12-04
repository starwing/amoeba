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

void build_solver(am_Solver* S, am_Var* width, am_Var* height)
{
    // Create custom strength
    am_Num mmedium = AM_MEDIUM * 1.25;
    am_Num smedium = AM_MEDIUM * 100;

    // Create the variable
    am_Var *left            = am_newvariable(S);
    am_Var *top             = am_newvariable(S);
    am_Var *contents_top    = am_newvariable(S);
    am_Var *contents_bottom = am_newvariable(S);
    am_Var *contents_left   = am_newvariable(S);
    am_Var *contents_right  = am_newvariable(S);
    am_Var *midline         = am_newvariable(S);
    am_Var *ctleft          = am_newvariable(S);
    am_Var *ctheight        = am_newvariable(S);
    am_Var *cttop           = am_newvariable(S);
    am_Var *ctwidth         = am_newvariable(S);
    am_Var *lb1left         = am_newvariable(S);
    am_Var *lb1height       = am_newvariable(S);
    am_Var *lb1top          = am_newvariable(S);
    am_Var *lb1width        = am_newvariable(S);
    am_Var *lb2left         = am_newvariable(S);
    am_Var *lb2height       = am_newvariable(S);
    am_Var *lb2top          = am_newvariable(S);
    am_Var *lb2width        = am_newvariable(S);
    am_Var *lb3left         = am_newvariable(S);
    am_Var *lb3height       = am_newvariable(S);
    am_Var *lb3top          = am_newvariable(S);
    am_Var *lb3width        = am_newvariable(S);
    am_Var *fl1left         = am_newvariable(S);
    am_Var *fl1height       = am_newvariable(S);
    am_Var *fl1top          = am_newvariable(S);
    am_Var *fl1width        = am_newvariable(S);
    am_Var *fl2left         = am_newvariable(S);
    am_Var *fl2height       = am_newvariable(S);
    am_Var *fl2top          = am_newvariable(S);
    am_Var *fl2width        = am_newvariable(S);
    am_Var *fl3left         = am_newvariable(S);
    am_Var *fl3height       = am_newvariable(S);
    am_Var *fl3top          = am_newvariable(S);
    am_Var *fl3width        = am_newvariable(S);

    // Add the edit variables
    am_addedit(width, AM_STRONG);
    am_addedit(height, AM_STRONG);


    // Add the constraints
    const
    struct Info {
        struct Item {
            am_Var *var;
            am_Num  mul;
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

    for (const auto& constraint : constraints) {
        am_Constraint *c = am_newconstraint(S, constraint.strength);
        for (auto* p = constraint.term; p->var; ++p) {
            am_addterm(c, p->var, p->mul ? p->mul : 1);
        }
        am_addconstant(c, constraint.constant);
        am_setrelation(c, constraint.relation);
        int r = am_add(c);
        assert(r == AM_OK);
    }
}

int main()
{
    ankerl::nanobench::Bench().minEpochIterations(100).run("building solver", [&] {
        am_Solver *S = am_newsolver(NULL, NULL);
        am_Var *width = am_newvariable(S);
        am_Var *height = am_newvariable(S);
        build_solver(S, width, height);
        ankerl::nanobench::doNotOptimizeAway(S); //< prevent the compiler to optimize away the S
        am_delsolver(S);
    });

    struct Size
    {
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

    am_Solver *S = am_newsolver(NULL, NULL);
    am_Var *widthVar = am_newvariable(S);
    am_Var *heightVar = am_newvariable(S);
    build_solver(S, widthVar, heightVar);

    for (const Size& size : sizes)
    {
        am_Num width = size.width;
        am_Num height = size.height;

        ankerl::nanobench::Bench().minEpochIterations(100000).run("suggest value " + std::to_string(size.width) + "x" + std::to_string(size.height), [&] {
            am_suggest(widthVar, width);
            am_suggest(heightVar, height);
            am_updatevars(S);
        });
    }

    am_delsolver(S);
    return 0;
}

// cc: flags+='-O2 -std=c++11'
