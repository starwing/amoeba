/*
 * gui_layout_benchmark.c -- a real GUI layout as a Cassowary workload.
 *
 * The scene is the DeepSeek Harness desktop window: a left sidecar, a
 * middle transcript and a right file list.  Every leaf is a box that
 * tightly frames one run of text; containers are boxes too.  Six resize
 * edits drive the layout (window width/height, both sidecar widths, the
 * transcript text insets) and each of the three lists is a viewport plus
 * a taller inner container moved by a scroll edit, so the list content
 * can be dragged behind its viewport.
 *
 * The first half is the toolkit: linear expressions and a handful of
 * relations (fill, stack, align, centre, pin).  The second half reads
 * like the window tree and never positions a leaf by hand.  Text widths
 * are kept in gui_layout_scene.h, which is also what the Yoga port
 * (yoga_gui_benchmark.cpp) builds from.
 *
 * Without arguments the program prints one box per line:
 *
 *     kind  x  y  w  h  clip-group  name
 *
 * kind is B (container), T (text) or I (icon); clip-group is 0 for the
 * page and 1/2/3 for the left list, transcript and file list viewports.
 * misc/render_layout.py turns that dump into a PNG.  --bench measures
 * build, suggest, dump and load instead.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
# define _POSIX_C_SOURCE 199309L /* clock_gettime */
#endif

#define AM_IMPLEMENTATION
#include "amoeba.h"
#include "bench_clock.h"
#include "gui_layout_scene.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __APPLE__
# include <mach/mach_time.h>
#endif

#define UI_MAXBOX  320
#define UI_MAXTERM 8

enum {
    ED_W,
    ED_H,
    ED_LEFT,
    ED_RIGHT,
    ED_TL,
    ED_TR,
    ED_SL,
    ED_SC,
    ED_SF,
    ED_COUNT
};

typedef struct {
    am_Id  id;
    am_Num k;
} LTerm;

typedef struct {
    LTerm  t[UI_MAXTERM];
    int    n;
    am_Num c;
} LExpr;

typedef struct {
    const char *name;
    char        kind;
    char        clip;
    am_Id       x, y, w, h;
    am_Num      vx, vy, vw, vh;
} LBox;

typedef struct {
    am_Solver *S;
    LBox       box[UI_MAXBOX];
    int        nbox;
    int        ncons;
    char       clip;
    am_Id      edit[ED_COUNT];
    am_Num     eval[ED_COUNT];
} LLayout;

static LExpr lx_num(am_Num c) {
    LExpr e;
    e.n = 0;
    e.c = c;
    return e;
}

static LExpr lx_var(am_Id id) {
    LExpr e;
    e.n = 1;
    e.c = 0;
    e.t[0].id = id;
    e.t[0].k = 1;
    return e;
}

static LExpr lx_add(LExpr a, LExpr b) {
    LExpr e;
    int   i;
    assert(a.n + b.n <= UI_MAXTERM);
    e = a;
    for (i = 0; i < b.n; ++i) e.t[e.n++] = b.t[i];
    e.c += b.c;
    return e;
}

static LExpr lx_scale(LExpr a, am_Num k) {
    LExpr e;
    int   i;
    e = a;
    for (i = 0; i < e.n; ++i) e.t[i].k *= k;
    e.c *= k;
    return e;
}

static LExpr lx_sub(LExpr a, LExpr b) { return lx_add(a, lx_scale(b, -1)); }

static LExpr lx_left(LBox b) { return lx_var(b.x); }
static LExpr lx_top(LBox b) { return lx_var(b.y); }
static LExpr lx_width(LBox b) { return lx_var(b.w); }
static LExpr lx_height(LBox b) { return lx_var(b.h); }
static LExpr lx_right(LBox b) { return lx_add(lx_var(b.x), lx_var(b.w)); }
static LExpr lx_bottom(LBox b) { return lx_add(lx_var(b.y), lx_var(b.h)); }
static LExpr lx_cx(LBox b) {
    return lx_add(lx_var(b.x), lx_scale(lx_var(b.w), 0.5f));
}
static LExpr lx_cy(LBox b) {
    return lx_add(lx_var(b.y), lx_scale(lx_var(b.h), 0.5f));
}

static void lay_term(am_Constraint *c, am_Id id, am_Num k) {
    int ret = am_addterm(c, id, k);
    assert(ret == AM_OK);
    (void)ret;
}

static void lay_setrel(am_Constraint *c, int rel) {
    int ret = am_setrelation(c, rel);
    assert(ret == AM_OK);
    (void)ret;
}

static void lay_const(am_Constraint *c, am_Num k) {
    int ret = am_addconstant(c, k);
    assert(ret == AM_OK);
    (void)ret;
}

static void lay_rel(LLayout *L, LExpr a, int rel, LExpr b, am_Num strength) {
    am_Constraint *c;
    int            i, ret;
    c = am_newconstraint(L->S, strength);
    assert(c != NULL);
    for (i = 0; i < a.n; ++i) lay_term(c, a.t[i].id, a.t[i].k);
    for (i = 0; i < b.n; ++i) lay_term(c, b.t[i].id, -b.t[i].k);
    lay_setrel(c, rel);
    lay_const(c, b.c - a.c);
    ret = am_add(c);
    assert(ret == AM_OK);
    (void)ret;
    ++L->ncons;
}

static void lay_eq(LLayout *L, LExpr a, LExpr b) {
    lay_rel(L, a, AM_EQUAL, b, AM_REQUIRED);
}

static void lay_le(LLayout *L, LExpr a, LExpr b) {
    lay_rel(L, a, AM_LESSEQUAL, b, AM_REQUIRED);
}

static void lay_fix(LLayout *L, LExpr a, am_Num v) { lay_eq(L, a, lx_num(v)); }

static void lay_below(LLayout *L, LBox b, LBox a, am_Num gap) {
    lay_eq(L, lx_top(b), lx_add(lx_bottom(a), lx_num(gap)));
}

static void lay_center_x(LLayout *L, LBox b, LBox p) {
    lay_eq(L, lx_cx(b), lx_cx(p));
}

static void lay_center_y(LLayout *L, LBox b, LBox p) {
    lay_eq(L, lx_cy(b), lx_cy(p));
}

static void lay_same_rect(LLayout *L, LBox b, LBox a) {
    lay_eq(L, lx_left(b), lx_left(a));
    lay_eq(L, lx_top(b), lx_top(a));
    lay_eq(L, lx_width(b), lx_width(a));
    lay_eq(L, lx_height(b), lx_height(a));
}

static void lay_fill_w(LLayout *L, LBox b, LBox p, am_Num l, am_Num r) {
    lay_eq(L, lx_left(b), lx_add(lx_left(p), lx_num(l)));
    lay_eq(L, lx_right(b), lx_sub(lx_right(p), lx_num(r)));
}

static void lay_fill_h(LLayout *L, LBox b, LBox p, am_Num t, am_Num d) {
    lay_eq(L, lx_top(b), lx_add(lx_top(p), lx_num(t)));
    lay_eq(L, lx_bottom(b), lx_sub(lx_bottom(p), lx_num(d)));
}

static LBox *lay_box(LLayout *L, const char *name, char kind) {
    LBox *b;
    assert(L->nbox < UI_MAXBOX);
    b = &L->box[L->nbox++];
    b->name = name;
    b->kind = kind;
    b->clip = L->clip;
    b->x = am_newvariable(L->S, &b->vx);
    b->y = am_newvariable(L->S, &b->vy);
    b->w = am_newvariable(L->S, &b->vw);
    b->h = am_newvariable(L->S, &b->vh);
    assert(b->x && b->y && b->w && b->h);
    return b;
}

static void lay_edit(LLayout *L, int i, am_Num value) {
    int ret;
    L->eval[i] = value;
    L->edit[i] = am_newvariable(L->S, &L->eval[i]);
    ret = am_addedit(L->S, L->edit[i], AM_STRONG);
    assert(ret == AM_OK);
    (void)ret;
}

static int ui_utf8(const char *s, unsigned *cp) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return (*cp = c), 1;
    if ((c & 0xE0) == 0xC0) {
        *cp = (unsigned)(c & 0x1F) << 6 | ((unsigned char)s[1] & 0x3F);
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        *cp = (unsigned)(c & 0x0F) << 12 | (((unsigned char)s[1] << 6) & 0x3F)
            | ((unsigned char)s[2] & 0x3F);
        return 3;
    }
    *cp = (unsigned)(c & 0x07) << 18 | ((unsigned char)s[1] << 12)
        | ((unsigned char)s[2] << 6) | ((unsigned char)s[3] & 0x3F);
    return 4;
}

static am_Num ui_text_w(const char *s) {
    am_Num w = 0;
    while (*s) {
        unsigned cp;
        int      n = ui_utf8(s, &cp);
        w += cp < 0x80 ? (cp == ' ' ? 6 : 11) : 21;
        s += n;
    }
    return w;
}

static LBox *ui_text_at(LLayout *L, LBox ref, am_Num dx, am_Num dy, am_Num w,
                        am_Num h, const char *s) {
    LBox *t = lay_box(L, s, 'T');
    if (w <= 0) w = ui_text_w(s);
    lay_eq(L, lx_left(*t), lx_add(lx_left(ref), lx_num(dx)));
    lay_eq(L, lx_top(*t), lx_add(lx_top(ref), lx_num(dy)));
    lay_fix(L, lx_width(*t), w);
    lay_fix(L, lx_height(*t), h);
    return t;
}

static LBox *ui_text(LLayout *L, LBox ref, am_Num dx, am_Num w, am_Num h,
                     const char *s) {
    LBox *t = lay_box(L, s, 'T');
    if (w <= 0) w = ui_text_w(s);
    lay_eq(L, lx_left(*t), lx_add(lx_left(ref), lx_num(dx)));
    lay_fix(L, lx_width(*t), w);
    lay_fix(L, lx_height(*t), h);
    lay_center_y(L, *t, ref);
    return t;
}

static LBox *ui_text_right_w(LLayout *L, LBox ref, am_Num dr, am_Num h,
                             am_Num w, const char *s) {
    LBox *t = lay_box(L, s, 'T');
    lay_eq(L, lx_right(*t), lx_sub(lx_right(ref), lx_num(dr)));
    lay_fix(L, lx_width(*t), w);
    lay_fix(L, lx_height(*t), h);
    lay_center_y(L, *t, ref);
    return t;
}

static LBox *ui_icon_at(LLayout *L, LBox ref, am_Num dx, am_Num dy, am_Num w,
                        am_Num h, const char *name) {
    LBox *b = lay_box(L, name, 'I');
    lay_eq(L, lx_left(*b), lx_add(lx_left(ref), lx_num(dx)));
    lay_eq(L, lx_top(*b), lx_add(lx_top(ref), lx_num(dy)));
    lay_fix(L, lx_width(*b), w);
    lay_fix(L, lx_height(*b), h);
    return b;
}

static LBox *ui_icon(LLayout *L, LBox ref, am_Num dx, am_Num size,
                     const char *name) {
    LBox *b = lay_box(L, name, 'I');
    lay_eq(L, lx_left(*b), lx_add(lx_left(ref), lx_num(dx)));
    lay_fix(L, lx_width(*b), size);
    lay_fix(L, lx_height(*b), size);
    lay_center_y(L, *b, ref);
    return b;
}

static LBox *ui_icon_free(LLayout *L, LBox ref, am_Num size, const char *name) {
    LBox *b = lay_box(L, name, 'I');
    lay_fix(L, lx_width(*b), size);
    lay_fix(L, lx_height(*b), size);
    lay_center_y(L, *b, ref);
    return b;
}

static void ui_list(LLayout *L, LBox view, LBox content, int scroll) {
    lay_eq(L, lx_left(content), lx_left(view));
    lay_eq(L, lx_width(content), lx_width(view));
    lay_eq(L, lx_top(content), lx_sub(lx_top(view), lx_var(L->edit[scroll])));
}

static void ui_left_logo(LLayout *L, LBox p) {
    LBox *row = lay_box(L, "left.logo", 'B');
    LBox *tog;
    lay_fill_w(L, *row, p, 0, 0);
    lay_fix(L, lx_top(*row), 18);
    lay_fix(L, lx_height(*row), 60);
    ui_icon_at(L, *row, 22, 20, 37, 24, "icon.logo");
    ui_text_at(L, *row, 69, 23, 147, 22, "deepseek");
    ui_text_at(L, *row, 230, 24, 81, 22, "HARNESS");
    tog = lay_box(L, "left.logo.toggle", 'I');
    lay_eq(L, lx_right(*tog), lx_sub(lx_right(p), lx_num(23)));
    lay_fix(L, lx_top(*tog), 42);
    lay_fix(L, lx_width(*tog), 22);
    lay_fix(L, lx_height(*tog), 22);
}

static void ui_left_newchat(LLayout *L, LBox p) {
    LBox *btn = lay_box(L, "left.newchat", 'B');
    LBox *grp = lay_box(L, "left.newchat.group", 'B');
    lay_fill_w(L, *btn, p, 17, 17);
    lay_fix(L, lx_top(*btn), 100);
    lay_fix(L, lx_height(*btn), 57);
    lay_fix(L, lx_width(*grp), 95);
    lay_fix(L, lx_height(*grp), 24);
    lay_center_x(L, *grp, *btn);
    lay_center_y(L, *grp, *btn);
    ui_icon_at(L, *grp, 0, 2, 19, 19, "icon.plus");
    ui_text_at(L, *grp, 31, 0, 64, 24, "新会话");
}

static void ui_left_nav(LLayout *L, LBox p) {
    int i;
    for (i = 0; i < UI_NNAV; ++i) {
        LBox *row = lay_box(L, "left.nav", 'B');
        lay_fill_w(L, *row, p, 20, 20);
        lay_fix(L, lx_top(*row), 175 + i * 54);
        lay_fix(L, lx_height(*row), 54);
        ui_icon(L, *row, 9, 22, "icon");
        ui_text(L, *row, 44, 0, 22, ui_nav[i]);
    }
}

static void ui_left_workspace(LLayout *L, LBox p) {
    LBox *head = lay_box(L, "left.workspace", 'B');
    LBox *item = lay_box(L, "left.project", 'B');
    LBox *plus, *sort, *find;
    lay_fill_w(L, *head, p, 20, 20);
    lay_fix(L, lx_top(*head), 514);
    lay_fix(L, lx_height(*head), 40);
    ui_text(L, *head, 8, 0, 20, "工作区");
    plus = ui_icon_free(L, *head, 20, "icon.plus");
    lay_eq(L, lx_right(*plus), lx_sub(lx_right(*head), lx_num(5)));
    sort = ui_icon_free(L, *head, 20, "icon.sort");
    lay_eq(L, lx_right(*sort), lx_sub(lx_left(*plus), lx_num(30)));
    find = ui_icon_free(L, *head, 20, "icon.find");
    lay_eq(L, lx_right(*find), lx_sub(lx_left(*sort), lx_num(30)));
    lay_fill_w(L, *item, p, 20, 20);
    lay_fix(L, lx_top(*item), 568);
    lay_fix(L, lx_height(*item), 44);
    ui_icon(L, *item, 9, 22, "icon.folder");
    ui_text(L, *item, 44, 80, 20, "amoeba");
}

static void ui_usage(LLayout *L, LBox card) {
    ui_icon_at(L, card, 5, 13, 140, 8, "usage.bar");
    ui_text_at(L, card, 157, 10, 33, 16, "平均");
    ui_text_at(L, card, 211, 10, 179, 16, "· 13 小时 16 分后进入高峰");
    ui_text_at(L, card, 4, 47, 34, 17, "今日");
    ui_text_at(L, card, 117, 47, 70, 17, "¥15.1041");
    ui_text_at(L, card, 206, 47, 36, 17, "余额");
    {
        LBox *t = lay_box(L, "¥42.77", 'T');
        lay_eq(L, lx_right(*t), lx_sub(lx_right(card), lx_num(7)));
        lay_eq(L, lx_top(*t), lx_add(lx_top(card), lx_num(47)));
        lay_fix(L, lx_width(*t), 59);
        lay_fix(L, lx_height(*t), 17);
    }
    ui_text_at(L, card, 89, 80, 100, 13, "317M tokens");
    ui_icon_at(L, card, 205, 83, 185, 8, "usage.bar2");
}

static void ui_left_remote(LLayout *L, LBox remote) {
    ui_icon_at(L, remote, 4, 10, 20, 20, "icon.remote");
    ui_text_at(L, remote, 34, 10, 77, 22, "Remote");
    ui_icon_at(L, remote, 154, 0, 40, 42, "badge");
    ui_text_at(L, remote, 183, 14, 11, 16, "2");
}

static void ui_left_section(LLayout *L, LBox sect) {
    ui_icon(L, sect, 0, 19, "icon.chat");
    ui_text(L, sect, 34, 130, 20, "会话与工作区");
}

static void ui_left_user(LLayout *L, LBox user) {
    ui_icon_at(L, user, 2, 12, 36, 36, "avatar");
    ui_text_at(L, user, 52, 22, 80, 22, "Xavier");
}

static LBox *ui_left_bottom(LLayout *L, LBox p) {
    LBox *card = lay_box(L, "left.usage", 'B');
    LBox *remote = lay_box(L, "left.remote", 'B');
    LBox *sect = lay_box(L, "left.section", 'B');
    LBox *user = lay_box(L, "left.user", 'B');
    lay_fill_w(L, *user, p, 20, 20);
    lay_fix(L, lx_height(*user), 53);
    lay_eq(L, lx_bottom(*user), lx_sub(lx_bottom(p), lx_num(16)));
    lay_fill_w(L, *sect, p, 20, 20);
    lay_fix(L, lx_height(*sect), 40);
    lay_eq(L, lx_bottom(*sect), lx_sub(lx_top(*user), lx_num(23)));
    lay_fill_w(L, *remote, p, 20, 20);
    lay_fix(L, lx_height(*remote), 48);
    lay_eq(L, lx_bottom(*remote), lx_sub(lx_top(*sect), lx_num(36)));
    lay_fill_w(L, *card, p, 20, 20);
    lay_fix(L, lx_height(*card), 108);
    lay_eq(L, lx_bottom(*card), lx_sub(lx_top(*remote), lx_num(10)));
    ui_left_remote(L, *remote);
    ui_left_section(L, *sect);
    ui_left_user(L, *user);
    ui_usage(L, *card);
    return card;
}

static LBox *ui_thread_row(LLayout *L, LBox content, int i, LBox *prev) {
    LBox *row = lay_box(L, "left.thread", 'B');
    lay_fill_w(L, *row, content, 0, 0);
    lay_fix(L, lx_height(*row), 48);
    if (prev)
        lay_below(L, *row, *prev, 5);
    else
        lay_eq(L, lx_top(*row), lx_add(lx_top(content), lx_num(10)));
    ui_text(L, *row, 50, thread_items[i].w, 22, thread_items[i].title);
    ui_text_right_w(L, *row, 12, 20, thread_items[i].tw, thread_items[i].time);
    return row;
}

static void ui_threads(LLayout *L, LBox p, LBox card) {
    LBox *view = lay_box(L, "left.list.view", 'B');
    LBox *content = lay_box(L, "left.list.content", 'B');
    LBox *last = NULL;
    int   i;
    lay_fill_w(L, *view, p, 14, 14);
    lay_fix(L, lx_top(*view), 614);
    lay_eq(L, lx_bottom(*view), lx_sub(lx_top(card), lx_num(9)));
    ui_list(L, *view, *content, ED_SL);
    L->clip = 1;
    for (i = 0; i < UI_NTHREAD; ++i) {
        LBox *row = ui_thread_row(L, *content, i, last);
        if (i == 2) {
            LBox *sel = lay_box(L, "left.thread.sel", 'B');
            lay_same_rect(L, *sel, *row);
        }
        last = row;
    }
    L->clip = 0;
    lay_eq(L, lx_height(*content), lx_sub(lx_bottom(*last), lx_top(*content)));
}

static void ui_left(LLayout *L, LBox p) {
    LBox *card;
    ui_left_logo(L, p);
    ui_left_newchat(L, p);
    ui_left_nav(L, p);
    ui_left_workspace(L, p);
    card = ui_left_bottom(L, p);
    ui_threads(L, p, *card);
}

static void ui_mid_header(LLayout *L, LBox p) {
    const char        *ui_title = "窄化数据性能提升原理疑问";
    LBox              *head = lay_box(L, "mid.header", 'B');
    static const short ix[] = {469, 523, 577, 630, 685, 738, 793};
    int                i;
    lay_fill_w(L, *head, p, 0, 0);
    lay_fix(L, lx_top(*head), 0);
    lay_fix(L, lx_height(*head), 62);
    ui_text_at(L, *head, 43, 22, 262, 22, ui_title);
    ui_icon(L, *head, 337, 20, "icon.mode");
    ui_text(L, *head, 364, 73, 22, "标准模式");
    for (i = 0; i < (int)(sizeof(ix) / sizeof(ix[0])); ++i) {
        ui_icon(L, *head, ix[i], 20, "icon.tool");
    }
    ui_icon_at(L, *head, 837, 16, 104, 32, "mid.fee");
    ui_text_at(L, *head, 851, 21, 73, 22, "费用明细");
    {
        LBox *a = ui_icon_free(L, *head, 21, "icon.split");
        LBox *b = ui_icon_free(L, *head, 21, "icon.down");
        LBox *c = ui_icon_free(L, *head, 21, "icon.more");
        lay_eq(L, lx_right(*c), lx_sub(lx_right(*head), lx_num(48)));
        lay_eq(L, lx_right(*b), lx_sub(lx_left(*c), lx_num(35)));
        lay_eq(L, lx_right(*a), lx_sub(lx_left(*b), lx_num(38)));
    }
}

static void ui_mid_tabs(LLayout *L, LBox p) {
    LBox                    *bar = lay_box(L, "mid.tabbar", 'B');
    static const short       tx[] = {43, 134, 225, 337, 418};
    static const short       tw[] = {39, 39, 60, 39, 16};
    static const char *const tab[] = {
            "对话", "轨迹", "上下文", "编队", "...",
    };
    int i;
    lay_fill_w(L, *bar, p, 0, 0);
    lay_fix(L, lx_top(*bar), 62);
    lay_fix(L, lx_height(*bar), 53);
    for (i = 0; i < 5; ++i) ui_text_at(L, *bar, tx[i], 16, tw[i], 22, tab[i]);
    ui_icon_at(L, *bar, 41, 51, 42, 3, "mid.underline");
}

static void ui_status(LLayout *L, LBox bar) {
    ui_text_at(L, bar, 37, 7, 250, 20, "3 轮 200 步 · 207 to...");
    ui_text_at(L, bar, 337, 7, 292, 20, "55.2M tok · 缓存命中 99...");
    ui_text_at(L, bar, 679, 7, 593, 20,
               "本会话 ¥2.2982 · 命中 99.6% · 输入 219K · "
               "缓存 54.8M");
    {
        LBox *t = lay_box(L, "45%", 'T');
        lay_eq(L, lx_right(*t), lx_sub(lx_right(bar), lx_num(44)));
        lay_eq(L, lx_top(*t), lx_add(lx_top(bar), lx_num(7)));
        lay_fix(L, lx_width(*t), 67);
        lay_fix(L, lx_height(*t), 20);
    }
}

static void ui_composer_top(LLayout *L, LBox card) {
    LBox *top = lay_box(L, "mid.composer.top", 'B');
    lay_fill_w(L, *top, card, 0, 0);
    lay_eq(L, lx_top(*top), lx_add(lx_top(card), lx_num(14)));
    lay_fix(L, lx_height(*top), 36);
    ui_text_at(L, *top, 24, 14, 512, 24,
               "发消息或创建任务，/ 调用指令，@ 文件或对话");
}

static void ui_composer_bottom(LLayout *L, LBox card) {
    LBox *bot = lay_box(L, "mid.composer.bottom", 'B');
    LBox *send = lay_box(L, "mid.send", 'B');
    lay_fill_w(L, *bot, card, 0, 0);
    lay_eq(L, lx_top(*bot), lx_add(lx_top(card), lx_num(98)));
    lay_fix(L, lx_height(*bot), 44);
    ui_icon(L, *bot, 26, 18, "icon.plus");
    ui_text_at(L, *bot, 91, 10, 148, 20, "工作区内修改");
    ui_text_at(L, *bot, 879, 10, 243, 20, "DeepSeek-V4-Flash High");
    lay_fix(L, lx_width(*send), 52);
    lay_fix(L, lx_height(*send), 52);
    lay_eq(L, lx_left(*send), lx_add(lx_left(card), lx_num(1196)));
    lay_eq(L, lx_top(*send), lx_add(lx_top(card), lx_num(90)));
}

static LBox *ui_composer(LLayout *L, LBox view, LBox status) {
    LBox *card = lay_box(L, "mid.composer", 'B');
    lay_eq(L, lx_left(*card), lx_sub(lx_left(view), lx_num(28)));
    lay_eq(L, lx_right(*card), lx_add(lx_right(view), lx_num(19)));
    lay_fix(L, lx_height(*card), 155);
    lay_eq(L, lx_bottom(*card), lx_sub(lx_top(status), lx_num(7)));
    ui_composer_top(L, *card);
    ui_composer_bottom(L, *card);
    return card;
}

static LBox *ui_chat_lines(LLayout *L, LBox content) {
    LBox *prev = NULL;
    int   i;
    for (i = 0; i < UI_NCHAT; ++i) {
        const UIChat *c = &chat_lines[i];
        LBox         *t = lay_box(L, c->text, 'T');
        lay_eq(L, lx_left(*t),
               lx_add(lx_left(content), lx_num(c->icon ? 28 : 0)));
        if (c->w)
            lay_rel(L, lx_width(*t), AM_EQUAL, lx_num(c->w), AM_STRONG);
        else
            lay_eq(L, lx_width(*t), lx_sub(lx_width(content), lx_num(56)));
        lay_fix(L, lx_height(*t), c->h);
        lay_le(L, lx_right(*t), lx_sub(lx_right(content), lx_num(28)));
        if (prev)
            lay_below(L, *t, *prev, c->gap);
        else
            lay_eq(L, lx_top(*t), lx_add(lx_top(content), lx_num(200)));
        if (c->icon) ui_icon(L, *t, -28, 16, "icon");
        prev = t;
    }
    return prev;
}

static void ui_chat_task(LLayout *L, LBox view, LBox content, LBox last) {
    LBox *task = lay_box(L, "chat.task", 'B');
    LBox *up;
    lay_eq(L, lx_left(*task), lx_sub(lx_left(view), lx_num(7)));
    lay_eq(L, lx_right(*task), lx_add(lx_right(view), lx_num(5)));
    lay_fix(L, lx_height(*task), 54);
    lay_below(L, *task, last, 0);
    ui_icon(L, *task, 25, 16, "icon.task");
    ui_text(L, *task, 62, 39, 22, "任务");
    ui_text(L, *task, 119, 0, 22, "1 进行中 · 7 待处理");
    up = ui_icon_free(L, *task, 11, "icon.up");
    lay_eq(L, lx_right(*up), lx_sub(lx_right(*task), lx_num(47)));
    lay_eq(L, lx_height(content), lx_sub(lx_bottom(*task), lx_top(content)));
}

static void ui_chat_scroll(LLayout *L, LBox view) {
    LBox *thumb = lay_box(L, "chat.scroll", 'B');
    LBox *jump = lay_box(L, "chat.jump", 'I');
    lay_eq(L, lx_right(*thumb), lx_sub(lx_right(view), lx_num(4)));
    lay_fix(L, lx_width(*thumb), 8);
    lay_eq(L, lx_top(*thumb), lx_add(lx_top(view), lx_num(344)));
    lay_fix(L, lx_height(*thumb), 227);
    lay_eq(L, lx_right(*jump), lx_sub(lx_right(view), lx_num(20)));
    lay_eq(L, lx_bottom(*jump), lx_sub(lx_bottom(view), lx_num(97)));
    lay_fix(L, lx_width(*jump), 34);
    lay_fix(L, lx_height(*jump), 34);
}

static void ui_chat(LLayout *L, LBox p, LBox status) {
    LBox *view = lay_box(L, "chat.view", 'B');
    LBox *content = lay_box(L, "chat.content", 'B');
    LBox *last;
    lay_eq(L, lx_left(*view), lx_add(lx_left(p), lx_var(L->edit[ED_TL])));
    lay_eq(L, lx_right(*view), lx_sub(lx_right(p), lx_var(L->edit[ED_TR])));
    lay_fix(L, lx_top(*view), 115);
    lay_eq(L, lx_bottom(*view), lx_sub(lx_top(status), lx_num(162)));
    ui_list(L, *view, *content, ED_SC);
    L->clip = 2;
    last = ui_chat_lines(L, *content);
    ui_chat_task(L, *view, *content, *last);
    L->clip = 0;
    ui_chat_scroll(L, *view);
    ui_composer(L, *view, status);
}

static void ui_mid(LLayout *L, LBox p) {
    LBox *status = lay_box(L, "mid.status", 'B');
    lay_fill_w(L, *status, p, 0, 0);
    lay_fix(L, lx_height(*status), 31);
    lay_eq(L, lx_bottom(*status), lx_bottom(p));
    ui_mid_header(L, p);
    ui_mid_tabs(L, p);
    ui_chat(L, p, *status);
    ui_status(L, *status);
}

static void ui_right_tab_actions(LLayout *L, LBox bar) {
    LBox *plus = ui_icon_free(L, bar, 17, "icon.plus");
    LBox *c = ui_icon_free(L, bar, 21, "icon.split");
    LBox *b = ui_icon_free(L, bar, 20, "icon.ext");
    LBox *a = ui_icon_free(L, bar, 21, "icon.side");
    lay_eq(L, lx_left(*plus), lx_add(lx_left(bar), lx_num(560)));
    lay_eq(L, lx_right(*c), lx_sub(lx_right(bar), lx_num(27)));
    lay_eq(L, lx_right(*b), lx_sub(lx_left(*c), lx_num(35)));
    lay_eq(L, lx_right(*a), lx_sub(lx_left(*b), lx_num(36)));
}

static void ui_right_tabs(LLayout *L, LBox p) {
    LBox *bar = lay_box(L, "right.tabbar", 'B');
    LBox *file = lay_box(L, "right.tab.file", 'B');
    lay_fill_w(L, *bar, p, 0, 0);
    lay_fix(L, lx_top(*bar), 0);
    lay_fix(L, lx_height(*bar), 62);
    ui_icon(L, *bar, 33, 16, "icon.tasks");
    ui_text(L, *bar, 70, 87, 22, "任务管理");
    ui_icon(L, *bar, 207, 21, "icon.ctx");
    ui_text(L, *bar, 239, 64, 22, "上下文");
    lay_eq(L, lx_left(*file), lx_add(lx_left(*bar), lx_num(383)));
    lay_fix(L, lx_top(*file), 12);
    lay_fix(L, lx_width(*file), 155);
    lay_fix(L, lx_height(*file), 40);
    ui_icon(L, *file, 18, 18, "icon.folder");
    ui_text(L, *file, 56, 42, 22, "文件");
    ui_text(L, *file, 129, 13, 20, "×");
    ui_right_tab_actions(L, *bar);
}

static void ui_right_search(LLayout *L, LBox p) {
    LBox              *box = lay_box(L, "right.search", 'B');
    static const short ax[] = {594, 653, 703, 754};
    int                i;
    lay_eq(L, lx_left(*box), lx_add(lx_left(p), lx_num(16)));
    lay_fix(L, lx_top(*box), 62);
    lay_fix(L, lx_width(*box), 577);
    lay_fix(L, lx_height(*box), 48);
    ui_text_at(L, *box, 38, 15, 128, 20, "按文件名搜索...");
    for (i = 0; i < 4; ++i) ui_icon(L, *box, ax[i], 20, "icon.action");
}

static LBox *ui_file_row(LLayout *L, LBox content, const UIFile *f,
                         LBox *prev) {
    LBox *row = lay_box(L, "right.file", 'B');
    LBox *t = ui_text(L, *row, 89, 0, 22, f->name);
    lay_fill_w(L, *row, content, 0, 0);
    lay_fix(L, lx_height(*row), 44);
    if (prev)
        lay_below(L, *row, *prev, 0);
    else
        lay_eq(L, lx_top(*row), lx_top(content));
    ui_icon(L, *row, 58, 18, f->dir ? "icon.folder" : "icon.file");
    if (f->mod) {
        LBox *m = lay_box(L, "M", 'T');
        lay_eq(L, lx_left(*m), lx_add(lx_right(*t), lx_num(8)));
        lay_fix(L, lx_width(*m), 11);
        lay_fix(L, lx_height(*m), 18);
        lay_center_y(L, *m, *row);
    }
    return row;
}

static void ui_right_list(LLayout *L, LBox p) {
    LBox *view = lay_box(L, "right.list.view", 'B');
    LBox *content = lay_box(L, "right.list.content", 'B');
    LBox *last = NULL;
    int   i;
    lay_fill_w(L, *view, p, 0, 15);
    lay_fix(L, lx_top(*view), 120);
    lay_eq(L, lx_bottom(*view), lx_bottom(p));
    ui_list(L, *view, *content, ED_SF);
    L->clip = 3;
    for (i = 0; i < UI_NFILE; ++i)
        last = ui_file_row(L, *content, &ui_files[i], last);
    L->clip = 0;
    lay_eq(L, lx_height(*content), lx_sub(lx_bottom(*last), lx_top(*content)));
}

static void ui_right(LLayout *L, LBox p) {
    LBox *thumb = lay_box(L, "right.scroll", 'I');
    ui_right_tabs(L, p);
    ui_right_search(L, p);
    ui_right_list(L, p);
    lay_eq(L, lx_right(*thumb), lx_sub(lx_right(p), lx_num(8)));
    lay_fix(L, lx_width(*thumb), 8);
    lay_fix(L, lx_top(*thumb), 322);
    lay_fix(L, lx_height(*thumb), 467);
}

static void lay_init(LLayout *L) {
    L->S = am_newsolver(NULL, NULL);
    assert(L->S != NULL);
    L->nbox = 0;
    L->ncons = 0;
    L->clip = 0;
}

static void ui_edits(LLayout *L, am_Num w, am_Num h) {
    lay_edit(L, ED_W, w);
    lay_edit(L, ED_H, h);
    lay_edit(L, ED_LEFT, UI_LEFT);
    lay_edit(L, ED_RIGHT, UI_RIGHT);
    lay_edit(L, ED_TL, UI_TEXT_L);
    lay_edit(L, ED_TR, UI_TEXT_R);
    lay_edit(L, ED_SL, 0);
    lay_edit(L, ED_SC, 100);
    lay_edit(L, ED_SF, 0);
}

static void ui_panels(LLayout *L, LBox win) {
    LBox *left = lay_box(L, "panel.left", 'B');
    LBox *mid = lay_box(L, "panel.mid", 'B');
    LBox *right = lay_box(L, "panel.right", 'B');
    lay_fill_h(L, *left, win, 0, 0);
    lay_fix(L, lx_left(*left), 0);
    lay_eq(L, lx_width(*left), lx_var(L->edit[ED_LEFT]));
    lay_fill_h(L, *mid, win, 0, 0);
    lay_eq(L, lx_left(*mid), lx_add(lx_right(*left), lx_num(UI_SPLIT)));
    lay_eq(L, lx_right(*mid), lx_sub(lx_right(win), lx_var(L->edit[ED_RIGHT])));
    lay_fill_h(L, *right, win, 0, 0);
    lay_eq(L, lx_right(*right), lx_right(win));
    lay_eq(L, lx_width(*right), lx_var(L->edit[ED_RIGHT]));
    ui_left(L, *left);
    ui_mid(L, *mid);
    ui_right(L, *right);
}

static void ui_build(LLayout *L, am_Num w, am_Num h) {
    LBox *win;
    lay_init(L);
    ui_edits(L, w, h);
    win = lay_box(L, "window", 'B');
    lay_fix(L, lx_left(*win), 0);
    lay_fix(L, lx_top(*win), 0);
    lay_eq(L, lx_width(*win), lx_var(L->edit[ED_W]));
    lay_eq(L, lx_height(*win), lx_var(L->edit[ED_H]));
    ui_panels(L, *win);
}

static void layout_apply(LLayout *L, const UISize *s) {
    am_suggest(L->S, L->edit[ED_W], s->w);
    am_suggest(L->S, L->edit[ED_H], s->h);
    am_suggest(L->S, L->edit[ED_LEFT], s->left);
    am_suggest(L->S, L->edit[ED_RIGHT], s->right);
    am_suggest(L->S, L->edit[ED_TL], s->tl);
    am_suggest(L->S, L->edit[ED_TR], s->tr);
    am_suggest(L->S, L->edit[ED_SL], s->sl);
    am_suggest(L->S, L->edit[ED_SC], s->sc);
    am_suggest(L->S, L->edit[ED_SF], s->sf);
    am_updatevars(L->S);
}

static void layout_build(LLayout *L, const UISize *s) {
    ui_build(L, s->w, s->h);
    layout_apply(L, s);
}

static void layout_print(const LLayout *L) {
    int i;
    printf("# kind\tx\ty\tw\th\tclip\tname\n");
    for (i = 0; i < L->nbox; ++i) {
        const LBox *b = &L->box[i];
        printf("%c\t%d\t%d\t%d\t%d\t%d\t%s\n", b->kind, (int)(b->vx + 0.5f),
               (int)(b->vy + 0.5f), (int)(b->vw + 0.5f), (int)(b->vh + 0.5f),
               b->clip, b->name);
    }
}

static void layout_free(LLayout *L) {
    am_delsolver(L->S);
    L->S = NULL;
}

typedef struct {
    char  *p;
    size_t n, cap;
} ByteBuf;

static void buf_add(ByteBuf *b, const void *p, size_t n) {
    size_t cap;
    if (b->n + n > b->cap) {
        cap = b->cap ? b->cap : 4096;
        while (cap < b->n + n) cap *= 2;
        b->p = (char *)realloc(b->p, cap);
        assert(b->p != NULL);
        b->cap = cap;
    }
    memcpy(b->p + b->n, p, n);
    b->n += n;
}

static const char *const ui_edit_name[ED_COUNT] = {
        "win_w",  "win_h",    "left_w",   "right_w",  "text_l",
        "text_r", "scroll_l", "scroll_c", "scroll_f",
};

typedef struct {
    am_Dumper base;
    ByteBuf  *buf;
    LLayout  *L;
} UIDump;

static int ui_dump_write(am_Dumper *d, const void *p, size_t n) {
    buf_add(((UIDump *)d)->buf, p, n);
    return AM_OK;
}

static am_Name ui_dump_name(am_Dumper *d, unsigned i, am_Id v, am_Num *val) {
    UIDump *x = (UIDump *)d;
    int     j;
    (void)i;
    (void)val;
    for (j = 0; j < ED_COUNT; ++j)
        if (x->L->edit[j] == v) return ui_edit_name[j];
    return NULL;
}

typedef struct {
    am_Loader   base;
    const char *buf;
    size_t      len;
    int         used;
    am_Id       edit[ED_COUNT];
    am_Num      vals[UI_MAXBOX * 4 + 16];
} UILoad;

static const char *ui_load_read(am_Loader *l, size_t *n) {
    UILoad *x = (UILoad *)l;
    if (x->used) return NULL;
    x->used = 1;
    *n = x->len;
    return x->buf;
}

static am_Num *ui_load_var(am_Loader *l, am_Name name, unsigned i, am_Id v) {
    UILoad *x = (UILoad *)l;
    int     j;
    assert(i < UI_MAXBOX * 4 + 16);
    if (name)
        for (j = 0; j < ED_COUNT; ++j)
            if (!strcmp(name, ui_edit_name[j])) x->edit[j] = v;
    return &x->vals[i];
}

static volatile double bench_sink;

typedef struct {
    LLayout      *L;
    LLayout      *tmp;
    const UISize *sizes;
    int           n, i;
} UIBench;

typedef struct {
    LLayout *L;
    UIDump   dump;
    ByteBuf  buf;
} UIDumpBench;

typedef struct {
    ByteBuf      *buf;
    UILoad        load;
    const UISize *sizes;
    int           n, i;
} UILoadBench;

static void bench_build(void *p) {
    UIBench *b = (UIBench *)p;
    layout_build(b->tmp, &b->sizes[0]);
    bench_sink += b->tmp->ncons;
    layout_free(b->tmp);
}

static void bench_build_suggest(void *p) {
    UIBench *b = (UIBench *)p;
    layout_build(b->tmp, &b->sizes[0]);
    layout_apply(b->tmp, &b->sizes[1]);
    bench_sink += b->tmp->nbox;
    layout_free(b->tmp);
}

static am_Num ui_editv(const UISize *s, int j) {
    switch (j) {
    case ED_W: return s->w;
    case ED_H: return s->h;
    case ED_LEFT: return s->left;
    case ED_RIGHT: return s->right;
    case ED_TL: return s->tl;
    case ED_TR: return s->tr;
    case ED_SL: return s->sl;
    case ED_SC: return s->sc;
    case ED_SF: return s->sf;
    }
    return 0;
}

static void bench_load_suggest(void *p) {
    UILoadBench  *b = (UILoadBench *)p;
    UILoad       *l = &b->load;
    am_Solver    *S;
    const UISize *s = &b->sizes[b->i];
    int           j;
    l->used = 0;
    l->buf = b->buf->p;
    l->len = b->buf->n;
    S = am_load(NULL, NULL, &l->base, 0);
    assert(S != NULL);
    for (j = 0; j < ED_COUNT; ++j) {
        assert(l->edit[j] != 0);
        am_suggest(S, l->edit[j], ui_editv(s, j));
    }
    am_updatevars(S);
    am_delsolver(S);
    b->i = (b->i + 1) % b->n;
}

static void bench_suggest(void *p) {
    UIBench *b = (UIBench *)p;
    layout_apply(b->L, &b->sizes[b->i]);
    b->i = (b->i + 1) % b->n;
}

static void bench_dump(void *p) {
    UIDumpBench *b = (UIDumpBench *)p;
    int          ret;
    b->buf.n = 0;
    ret = am_dump(b->L->S, &b->dump.base);
    assert(ret == AM_OK);
    (void)ret;
}

static void bench_load(void *p) {
    UILoadBench *b = (UILoadBench *)p;
    am_Solver   *S;
    b->load.used = 0;
    b->load.buf = b->buf->p;
    b->load.len = b->buf->n;
    S = am_load(NULL, NULL, &b->load.base, 0);
    assert(S != NULL);
    am_delsolver(S);
}

static double bench_wall(void) {
#ifdef _WIN32
    return bench_cpu(); /* QPC is the only high-res timer on Win32 */
#elif defined(__APPLE__)
    static mach_timebase_info_data_t tb = {0, 0};
    if (!tb.denom) mach_timebase_info(&tb);
    return (double)mach_absolute_time() * (double)tb.numer / (double)tb.denom
         * 1e-9;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

static void bench_run(const char *name, void (*fn)(void *), void *ctx) {
    double t0, dt;
    long   n = 0;
    fn(ctx);
    t0 = bench_cpu();
    do {
        fn(ctx);
        ++n;
        dt = bench_cpu() - t0;
    } while (dt < 0.20);
    printf("%-24s %10.1f ns/op  (%ld iters)\n", name, dt * 1e9 / n, n);
}

static void bench_init(UIBench *b, UIDumpBench *d, UILoadBench *l) {
    UISize s = ui_sizes[0];
    b->L = (LLayout *)malloc(sizeof(LLayout));
    b->tmp = (LLayout *)malloc(sizeof(LLayout));
    assert(b->L != NULL && b->tmp != NULL);
    b->sizes = ui_sizes;
    b->n = (int)(sizeof(ui_sizes) / sizeof(ui_sizes[0]));
    b->i = 1;
    memset(d, 0, sizeof(*d));
    memset(l, 0, sizeof(*l));
    layout_build(b->L, &s);
}

static void dump_init(UIBench *b, UIDumpBench *d) {
    d->L = b->L;
    d->buf.p = NULL;
    d->buf.n = d->buf.cap = 0;
    d->dump.base.var_name = ui_dump_name;
    d->dump.base.cons_name = NULL;
    d->dump.base.writer = ui_dump_write;
    d->dump.buf = &d->buf;
    d->dump.L = b->L;
    bench_dump(d);
}

static void load_init(UILoadBench *l, UIDumpBench *d) {
    l->buf = &d->buf;
    memset(l->load.edit, 0, sizeof(l->load.edit));
    l->load.base.reader = ui_load_read;
    l->load.base.load_var = ui_load_var;
    l->load.base.load_cons = NULL;
    l->load.base.solver = NULL;
    l->sizes = ui_sizes;
    l->n = (int)(sizeof(ui_sizes) / sizeof(ui_sizes[0]));
    l->i = 1;
}

static void bench_io(UIBench *b, UIDumpBench *d, UILoadBench *l) {
    dump_init(b, d);
    printf("dump: %lu bytes for %d boxes\n", (unsigned long)d->buf.n,
           b->L->nbox);
    bench_run("dump", bench_dump, d);
    load_init(l, d);
    bench_run("load", bench_load, l);
    bench_run("load+suggest", bench_load_suggest, l);
}

/* ---- --split / --loadonly: the plan-e2v8 §1.2 protocol ---- */

typedef struct {
    unsigned nalloc;
    size_t   live, peak;
} UIMem;

static UIMem ui_mem;

static void *ui_count_alloc(void **pud, void *p, size_t ns, size_t os,
                            am_Type t) {
    void *np;
    (void)pud, (void)t;
    if (ns == 0) {
        ui_mem.live -= os, free(p);
        return NULL;
    }
    np = realloc(p, ns);
    assert(np != NULL);
    ui_mem.nalloc += 1, ui_mem.live += ns - os;
    if (ui_mem.live > ui_mem.peak) ui_mem.peak = ui_mem.live;
    return np;
}

typedef struct {
    double load, first, rest, upd, del, total, sum;
} UISplit;

/* a value-only checksum: the loaded tableau must reproduce the same numbers */
static double split_sum(am_Solver *S) {
    am_Id  v = 0;
    double sum = 0;
    int    i = 0;
    while ((v = am_vars(S, v)) != 0)
        sum += (double)*am_varvalue(S, v, NULL) * (double)(++i);
    return sum;
}

static void split_suggest(UILoadBench *l, am_Solver *S, UISplit *t) {
    int j;
    for (j = 0; j < ED_COUNT; ++j) {
        double a = bench_wall();
        am_suggest(S, l->load.edit[j], ui_editv(&ui_sizes[1], j));
        a = (bench_wall() - a) * 1e9;
        if (j == 0)
            t->first = a;
        else
            t->rest += a;
    }
}

static void split_round(UILoadBench *l, UIDumpBench *d, UISplit *t,
                        unsigned *pnalloc, unsigned *pwake, size_t *plive,
                        size_t *ppeak) {
    am_Solver *S;
    double     a;
    memset(&ui_mem, 0, sizeof(ui_mem));
    l->load.used = 0, l->load.buf = d->buf.p, l->load.len = d->buf.n;
    a = bench_wall();
    S = am_load(ui_count_alloc, NULL, &l->load.base, 0);
    t->load = (bench_wall() - a) * 1e9;
    assert(S != NULL);
    *pnalloc = ui_mem.nalloc, *plive = ui_mem.live;
    split_suggest(l, S, t);
    *pwake = ui_mem.nalloc - *pnalloc;
    a = bench_wall(), am_updatevars(S), t->upd = (bench_wall() - a) * 1e9;
    t->sum = split_sum(S);
    a = bench_wall(), am_delsolver(S), t->del = (bench_wall() - a) * 1e9;
    *ppeak = ui_mem.peak;
    t->total = t->load + t->first + t->rest + t->upd;
    assert(ui_mem.live == 0);
}

static void split_keep(UISplit *best, const UISplit *t) {
    if (t->load < best->load) best->load = t->load;
    if (t->first < best->first) best->first = t->first;
    if (t->rest < best->rest) best->rest = t->rest;
    if (t->upd < best->upd) best->upd = t->upd;
    if (t->del < best->del) best->del = t->del;
    if (t->total < best->total) best->total = t->total;
    best->sum = t->sum;
}

static void split_print(const UISplit *t, unsigned allocs, unsigned wake,
                        size_t live, size_t peak) {
    printf("  load           %8.1f ns   allocs %u   live %lu\n", t->load,
           allocs, (unsigned long)live);
    printf("  first suggest  %8.1f ns   wake allocs %u\n", t->first, wake);
    printf("  rest 8 suggest %8.1f ns\n", t->rest);
    printf("  updatevars     %8.1f ns\n", t->upd);
    printf("  del            %8.1f ns\n", t->del);
    printf("  load+first     %8.1f ns   peak %lu   sum %.9f\n", t->total,
           (unsigned long)peak, t->sum);
}

static void bench_split(UIBench *b, UIDumpBench *d, UILoadBench *l) {
    UISplit  best;
    unsigned allocs = 0, wake = 0;
    size_t   live = 0, peak = 0;
    int      r;
    (void)b;
    best.load = best.first = best.rest = 1e18;
    best.upd = best.del = best.total = 1e18;
    for (r = 0; r < 402; ++r) {
        UISplit  t;
        unsigned na, nw;
        size_t   lv, pk;
        memset(&t, 0, sizeof(t));
        split_round(l, d, &t, &na, &nw, &lv, &pk);
        if (r < 201) continue; /* the first half warms the cpu */
        allocs = na, wake = nw, live = lv, peak = pk;
        split_keep(&best, &t);
    }
    split_print(&best, allocs, wake, live, peak);
}

static void run_bench(void) {
    UIBench     b;
    UIDumpBench d;
    UILoadBench l;
    bench_init(&b, &d, &l);
    printf("layout: %d boxes, %d constraints\n", b.L->nbox, b.L->ncons);
    bench_run("build", bench_build, &b);
    bench_run("build+suggest", bench_build_suggest, &b);
    bench_run("suggest (9 edits)", bench_suggest, &b);
    bench_io(&b, &d, &l);
    layout_free(b.L);
    free(d.buf.p);
    free(b.tmp);
    free(b.L);
    if (bench_sink < 0) printf("sink %f\n", (double)bench_sink);
}

static void run_split(void) {
    UIBench     b;
    UIDumpBench d;
    UILoadBench l;
    bench_init(&b, &d, &l);
    dump_init(&b, &d);
    printf("dump: %lu bytes for %d boxes\n", (unsigned long)d.buf.n, b.L->nbox);
    bench_run("dump", bench_dump, &d);
    load_init(&l, &d);
    printf("cache filled at load\n");
    bench_split(&b, &d, &l);
    layout_free(b.L);
    free(d.buf.p);
    free(b.tmp);
    free(b.L);
}

static double load_ns(UILoadBench *l, UIDumpBench *d) {
    am_Solver *S;
    double     a;
    l->load.used = 0, l->load.buf = d->buf.p, l->load.len = d->buf.n;
    a = bench_wall();
    S = am_load(NULL, NULL, &l->load.base, 0);
    a = (bench_wall() - a) * 1e9;
    assert(S != NULL);
    am_delsolver(S);
    return a;
}

static void run_loadonly(void) {
    UIBench     b;
    UIDumpBench d;
    UILoadBench l;
    double      best = 1e18;
    int         i;
    bench_init(&b, &d, &l);
    dump_init(&b, &d);
    load_init(&l, &d);
    for (i = 0; i < 501; ++i) {
        double a = load_ns(&l, &d);
        if (i >= 300 && a < best) best = a;
    }
    printf("loadonly        %8.1f ns\n", best);
    layout_free(b.L);
    free(d.buf.p);
    free(b.tmp);
    free(b.L);
}

static int ui_opt(int argc, char **argv, int *i, UISize *s, int *bench) {
    const char *a = argv[*i];
    if (!strcmp(a, "--bench")) {
        *bench = 1;
        return 1;
    }
    if (!strcmp(a, "--split")) {
        *bench = 2;
        return 1;
    }
    if (!strcmp(a, "--loadonly")) {
        *bench = 3;
        return 1;
    }
    if (*i + 1 >= argc) return 0;
    if (!strcmp(a, "-w"))
        s->w = (am_Num)atof(argv[++*i]);
    else if (!strcmp(a, "-h"))
        s->h = (am_Num)atof(argv[++*i]);
    else if (!strcmp(a, "-l"))
        s->left = (am_Num)atof(argv[++*i]);
    else if (!strcmp(a, "-r"))
        s->right = (am_Num)atof(argv[++*i]);
    else if (!strcmp(a, "-tl"))
        s->tl = (am_Num)atof(argv[++*i]);
    else if (!strcmp(a, "-tr"))
        s->tr = (am_Num)atof(argv[++*i]);
    else
        return 0;
    return 1;
}

static int ui_args(int argc, char **argv, UISize *s, int *bench) {
    int i;
    *bench = 0;
    s->w = UI_W;
    s->h = UI_H;
    s->left = UI_LEFT;
    s->right = UI_RIGHT;
    s->tl = UI_TEXT_L;
    s->tr = UI_TEXT_R;
    s->sl = 0;
    s->sc = 100;
    s->sf = 0;
    for (i = 1; i < argc; ++i) {
        if (ui_opt(argc, argv, &i, s, bench)) continue;
        fprintf(stderr,
                "usage: %s [-w W] [-h H] [-l left] [-r right]"
                " [-tl text_left] [-tr text_right] [--bench] [--split]"
                " [--loadonly]\n",
                argv[0]);
        return 2;
    }
    return 0;
}

int main(int argc, char **argv) {
    LLayout L;
    UISize  s;
    int     bench;
    if (ui_args(argc, argv, &s, &bench) != 0) return 2;
    if (bench == 1) return run_bench(), 0;
    if (bench == 2) return run_split(), 0;
    if (bench == 3) return run_loadonly(), 0;
    layout_build(&L, &s);
    layout_print(&L);
    layout_free(&L);
    return 0;
}
