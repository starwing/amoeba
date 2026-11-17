/*
 * yoga_gui_benchmark.cpp -- the gui_layout_benchmark.c scene in Yoga.
 *
 * Same window tree, same 9 edits, same scene data from
 * gui_layout_scene.h.  Yoga needs a few structural spacer nodes, so it
 * builds 288 nodes against the 272 boxes of the amoeba scene.
 * Text intrinsic sizes are passed in, exactly like the amoeba scene, so
 * the comparison measures layout work, not text shaping.
 *
 * Build with:
 *   c++ -O2 -std=c++20 -DNDEBUG -I. -Ibench -I~/Work/Sources/yoga \
 *       bench/yoga_gui_benchmark.cpp bench/yogaone.cpp -o yoga_gui_benchmark
 */

#include <yoga/Yoga.h>

#include "gui_layout_scene.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

static int             g_nodes;
static volatile double g_sink;

typedef struct Scene {
    YGNodeRef root, left, right;
    YGNodeRef chat, composer;
    YGNodeRef thread_content, chat_content, file_content;
    int       nodes;
} Scene;

static YGNodeRef mk(YGNodeRef parent, float w, float h) {
    YGNodeRef n = YGNodeNew();
    if (w >= 0.0f) YGNodeStyleSetWidth(n, w);
    if (h >= 0.0f) YGNodeStyleSetHeight(n, h);
    if (parent != NULL)
        YGNodeInsertChild(parent, n, YGNodeGetChildCount(parent));
    ++g_nodes;
    return n;
}

static YGNodeRef box(YGNodeRef parent, float w, float h) {
    YGNodeRef n = mk(parent, w, h);
    YGNodeStyleSetFlexDirection(n, YGFlexDirectionColumn);
    return n;
}

static YGNodeRef row(YGNodeRef parent, float h) {
    YGNodeRef n = mk(parent, -1.0f, h);
    YGNodeStyleSetFlexDirection(n, YGFlexDirectionRow);
    return n;
}

static YGNodeRef leaf(YGNodeRef parent, float w, float h) {
    return mk(parent, w, h);
}

static YGNodeRef abs_box(YGNodeRef parent, float x, float y, float w, float h) {
    YGNodeRef n = mk(parent, w, h);
    YGNodeStyleSetPositionType(n, YGPositionTypeAbsolute);
    YGNodeStyleSetPosition(n, YGEdgeLeft, x);
    YGNodeStyleSetPosition(n, YGEdgeTop, y);
    return n;
}

static YGNodeRef abs_right(YGNodeRef parent, float right, float y, float w,
                           float h) {
    YGNodeRef n = mk(parent, w, h);
    YGNodeStyleSetPositionType(n, YGPositionTypeAbsolute);
    YGNodeStyleSetPosition(n, YGEdgeRight, right);
    YGNodeStyleSetPosition(n, YGEdgeTop, y);
    return n;
}

static void margin(YGNodeRef n, YGEdge edge, float v) {
    YGNodeStyleSetMargin(n, edge, v);
}

static float text_w(const char *s) {
    float w = 0.0f;
    while (*s) {
        unsigned      cp;
        int           n = 1;
        unsigned char c = (unsigned char)s[0];
        if (c < 0x80)
            cp = c;
        else if ((c & 0xE0) == 0xC0)
            cp = (unsigned)(c & 0x1F) << 6, n = 2;
        else if ((c & 0xF0) == 0xE0)
            cp = (unsigned)(c & 0x0F) << 12, n = 3;
        else
            cp = (unsigned)(c & 0x07) << 18, n = 4;
        w += cp < 0x80 ? (cp == ' ' ? 6.0f : 11.0f) : 21.0f;
        s += n;
    }
    return w;
}

static void build_logo(YGNodeRef panel) {
    YGNodeRef logo = mk(panel, -1.0f, 60.0f);
    YGNodeStyleSetFlexDirection(logo, YGFlexDirectionRow);
    margin(logo, YGEdgeTop, 18.0f);
    abs_box(logo, 22, 20, 37, 24);
    abs_box(logo, 69, 23, 147, 22);
    abs_box(logo, 230, 24, 81, 22);
    abs_right(logo, 23, 24, 22, 22);
}

static void build_newchat(YGNodeRef panel) {
    YGNodeRef btn = row(panel, 57.0f);
    YGNodeStyleSetJustifyContent(btn, YGJustifyCenter);
    YGNodeStyleSetAlignItems(btn, YGAlignCenter);
    YGNodeStyleSetGap(btn, YGGutterAll, 12.0f);
    margin(btn, YGEdgeTop, 22.0f);
    leaf(btn, 19, 19);
    leaf(btn, 64, 24);
}

static void build_nav(YGNodeRef panel) {
    int i;
    for (i = 0; i < UI_NNAV; ++i) {
        YGNodeRef n = row(panel, 54.0f);
        YGNodeStyleSetAlignItems(n, YGAlignCenter);
        YGNodeStyleSetGap(n, YGGutterAll, 13.0f);
        YGNodeStyleSetPadding(n, YGEdgeLeft, 9.0f);
        if (i == 0) margin(n, YGEdgeTop, 18.0f);
        leaf(n, 22, 22);
        leaf(n, text_w(ui_nav[i]), 22);
    }
}

static void build_workspace(YGNodeRef panel) {
    YGNodeRef head = row(panel, 40.0f);
    YGNodeRef proj = row(panel, 44.0f);
    YGNodeStyleSetAlignItems(head, YGAlignCenter);
    margin(head, YGEdgeTop, 15.0f);
    YGNodeStyleSetPadding(head, YGEdgeLeft, 8.0f);
    leaf(head, 63, 20);
    {
        YGNodeRef spacer = leaf(head, -1.0f, 0.0f);
        YGNodeStyleSetFlexGrow(spacer, 1.0f);
    }
    leaf(head, 20, 20);
    margin(leaf(head, 20, 20), YGEdgeLeft, 30.0f);
    margin(leaf(head, 20, 20), YGEdgeLeft, 30.0f);
    YGNodeStyleSetAlignItems(proj, YGAlignCenter);
    YGNodeStyleSetGap(proj, YGGutterAll, 13.0f);
    YGNodeStyleSetPadding(proj, YGEdgeLeft, 9.0f);
    margin(proj, YGEdgeTop, 14.0f);
    leaf(proj, 22, 22);
    leaf(proj, 80, 20);
}

static void build_usage(YGNodeRef panel) {
    YGNodeRef card = mk(panel, -1.0f, 108.0f);
    margin(card, YGEdgeTop, 9.0f);
    abs_box(card, 5, 13, 140, 8);
    abs_box(card, 157, 10, 33, 16);
    abs_box(card, 211, 10, 179, 16);
    abs_box(card, 4, 47, 34, 17);
    abs_box(card, 117, 47, 70, 17);
    abs_box(card, 206, 47, 36, 17);
    abs_right(card, 7, 47, 59, 17);
    abs_box(card, 89, 80, 100, 13);
    abs_box(card, 205, 83, 185, 8);
}

static void build_left_bottom(YGNodeRef panel) {
    YGNodeRef remote = row(panel, 48.0f);
    YGNodeRef sect = row(panel, 40.0f);
    YGNodeRef user = row(panel, 53.0f);
    YGNodeStyleSetAlignItems(remote, YGAlignCenter);
    YGNodeStyleSetGap(remote, YGGutterAll, 14.0f);
    YGNodeStyleSetPadding(remote, YGEdgeLeft, 24.0f);
    margin(remote, YGEdgeTop, 10.0f);
    leaf(remote, 20, 20);
    leaf(remote, 77, 22);
    margin(leaf(remote, 40, 42), YGEdgeLeft, 43.0f);
    abs_box(remote, 183, 14, 11, 16);
    YGNodeStyleSetAlignItems(sect, YGAlignCenter);
    YGNodeStyleSetGap(sect, YGGutterAll, 15.0f);
    YGNodeStyleSetPadding(sect, YGEdgeLeft, 20.0f);
    margin(sect, YGEdgeTop, 36.0f);
    leaf(sect, 19, 19);
    leaf(sect, 130, 20);
    YGNodeStyleSetAlignItems(user, YGAlignCenter);
    YGNodeStyleSetGap(user, YGGutterAll, 14.0f);
    YGNodeStyleSetPadding(user, YGEdgeLeft, 22.0f);
    margin(user, YGEdgeTop, 23.0f);
    margin(user, YGEdgeBottom, 16.0f);
    leaf(user, 36, 36);
    leaf(user, 80, 22);
}

static void build_thread_row(YGNodeRef content, const UIThread *t) {
    YGNodeRef r = row(content, 48.0f);
    YGNodeStyleSetAlignItems(r, YGAlignCenter);
    YGNodeStyleSetJustifyContent(r, YGJustifySpaceBetween);
    YGNodeStyleSetPadding(r, YGEdgeLeft, 50.0f);
    YGNodeStyleSetPadding(r, YGEdgeRight, 12.0f);
    leaf(r, (float)t->w, 22);
    leaf(r, (float)t->tw, 20);
}

static void build_threads(YGNodeRef panel, Scene *s) {
    YGNodeRef view = box(panel, -1.0f, -1.0f);
    YGNodeRef content;
    int       i;
    YGNodeStyleSetFlexGrow(view, 1.0f);
    YGNodeStyleSetFlexBasis(view, 0.0f);
    YGNodeStyleSetOverflow(view, YGOverflowHidden);
    margin(view, YGEdgeTop, 2.0f);
    content = mk(view, -1.0f, 323.0f);
    YGNodeStyleSetFlexDirection(content, YGFlexDirectionColumn);
    YGNodeStyleSetPositionType(content, YGPositionTypeAbsolute);
    YGNodeStyleSetPosition(content, YGEdgeLeft, 0.0f);
    YGNodeStyleSetPosition(content, YGEdgeRight, 0.0f);
    YGNodeStyleSetPosition(content, YGEdgeTop, 0.0f);
    YGNodeStyleSetPadding(content, YGEdgeTop, 10.0f);
    YGNodeStyleSetGap(content, YGGutterAll, 5.0f);
    s->thread_content = content;
    for (i = 0; i < UI_NTHREAD; ++i)
        build_thread_row(content, &thread_items[i]);
}

static void build_left(Scene *s, float w) {
    YGNodeRef panel = box(s->root, w, -1.0f);
    s->left = panel;
    build_logo(panel);
    build_newchat(panel);
    build_nav(panel);
    build_workspace(panel);
    build_threads(panel, s);
    build_usage(panel);
    build_left_bottom(panel);
}

static void build_header_left(YGNodeRef head) {
    int i;
    leaf(head, 262, 22);
    margin(leaf(head, 17, 20), YGEdgeLeft, 32.0f);
    margin(leaf(head, 73, 22), YGEdgeLeft, 10.0f);
    for (i = 0; i < 7; ++i) {
        YGNodeRef n = leaf(head, 20, 20);
        margin(n, YGEdgeLeft, i == 0 ? 32.0f : 34.0f);
    }
    {
        YGNodeRef fee = row(head, 32.0f);
        YGNodeStyleSetWidth(fee, 104.0f);
        YGNodeStyleSetAlignItems(fee, YGAlignCenter);
        YGNodeStyleSetPadding(fee, YGEdgeLeft, 14.0f);
        margin(fee, YGEdgeLeft, 24.0f);
        leaf(fee, 73, 22);
    }
}

static void build_header_right(YGNodeRef head) {
    YGNodeRef spacer = leaf(head, -1.0f, 0.0f);
    YGNodeRef last;
    YGNodeStyleSetFlexGrow(spacer, 1.0f);
    margin(leaf(head, 21, 21), YGEdgeLeft, 38.0f);
    margin(leaf(head, 21, 21), YGEdgeLeft, 35.0f);
    last = leaf(head, 21, 21);
    margin(last, YGEdgeLeft, 48.0f);
    margin(last, YGEdgeRight, 48.0f);
}

static void build_header(YGNodeRef panel) {
    YGNodeRef head = row(panel, 62.0f);
    YGNodeStyleSetAlignItems(head, YGAlignCenter);
    YGNodeStyleSetPadding(head, YGEdgeLeft, 43.0f);
    build_header_left(head);
    build_header_right(head);
}

static void build_tabs(YGNodeRef panel) {
    YGNodeRef          bar = row(panel, 53.0f);
    static const float tw[] = {39, 39, 60, 39, 16};
    int                i;
    YGNodeStyleSetAlignItems(bar, YGAlignCenter);
    YGNodeStyleSetPadding(bar, YGEdgeLeft, 43.0f);
    for (i = 0; i < 5; ++i)
        margin(leaf(bar, tw[i], 22), YGEdgeLeft, i == 0 ? 0.0f : 52.0f);
    abs_box(bar, 41, 51, 42, 3);
}

static void build_chat_line(YGNodeRef content, const UIChat *c, int first) {
    YGNodeRef n;
    if (c->icon) {
        YGNodeRef t;
        n = row(content, (float)c->h);
        YGNodeStyleSetAlignItems(n, YGAlignCenter);
        YGNodeStyleSetGap(n, YGGutterAll, 12.0f);
        leaf(n, 16, 16);
        t = leaf(n, c->w ? (float)c->w : 0.0f, (float)c->h);
        if (!c->w) {
            YGNodeStyleSetFlexGrow(t, 1.0f);
            margin(t, YGEdgeRight, 28.0f);
        }
    } else {
        n = leaf(content, (float)c->w, (float)c->h);
    }
    if (!first) margin(n, YGEdgeTop, (float)c->gap);
}

static void build_chat_overlay(YGNodeRef view) {
    YGNodeRef jump = mk(view, 34, 34);
    abs_right(view, 4, 344, 8, 227);
    YGNodeStyleSetPositionType(jump, YGPositionTypeAbsolute);
    YGNodeStyleSetPosition(jump, YGEdgeRight, 20.0f);
    YGNodeStyleSetPosition(jump, YGEdgeBottom, 97.0f);
}

static void build_chat_content(Scene *s, float tl, float tr, float sc) {
    YGNodeRef view = box(NULL, -1.0f, -1.0f);
    YGNodeRef content;
    int       i;
    YGNodeStyleSetFlexGrow(view, 1.0f);
    YGNodeStyleSetFlexBasis(view, 0.0f);
    YGNodeStyleSetOverflow(view, YGOverflowHidden);
    margin(view, YGEdgeLeft, tl);
    margin(view, YGEdgeRight, tr);
    s->chat = view;
    content = mk(view, -1.0f, 1045.0f);
    YGNodeStyleSetFlexDirection(content, YGFlexDirectionColumn);
    YGNodeStyleSetPositionType(content, YGPositionTypeAbsolute);
    YGNodeStyleSetPosition(content, YGEdgeLeft, 0.0f);
    YGNodeStyleSetPosition(content, YGEdgeRight, 0.0f);
    YGNodeStyleSetPosition(content, YGEdgeTop, -sc);
    YGNodeStyleSetPadding(content, YGEdgeTop, 200.0f);
    s->chat_content = content;
    for (i = 0; i < UI_NCHAT; ++i)
        build_chat_line(content, &chat_lines[i], i == 0);
    build_chat_overlay(view);
}

static void build_task(YGNodeRef content) {
    YGNodeRef task = row(content, 54.0f);
    margin(task, YGEdgeLeft, -7.0f);
    margin(task, YGEdgeRight, -5.0f);
    abs_box(task, 25, 19, 16, 16);
    abs_box(task, 62, 16, 39, 22);
    abs_box(task, 119, 16, 193, 22);
    abs_right(task, 47, 21, 11, 11);
}

static void build_composer(Scene *s, float tl, float tr) {
    YGNodeRef card = box(NULL, -1.0f, 155.0f);
    YGNodeRef top = row(card, 36.0f);
    YGNodeRef bot = row(card, 44.0f);
    margin(card, YGEdgeLeft, tl - 28.0f);
    margin(card, YGEdgeRight, tr - 19.0f);
    margin(card, YGEdgeBottom, 7.0f);
    s->composer = card;
    margin(top, YGEdgeTop, 14.0f);
    abs_box(top, 24, 14, 512, 24);
    margin(bot, YGEdgeTop, 48.0f);
    abs_box(bot, 26, 13, 18, 18);
    abs_box(bot, 91, 10, 148, 20);
    abs_box(bot, 879, 10, 243, 20);
    abs_box(bot, 1196, -8, 52, 52);
}

static void build_status(YGNodeRef panel) {
    YGNodeRef bar = box(panel, -1.0f, 31.0f);
    abs_box(bar, 37, 7, 250, 20);
    abs_box(bar, 337, 7, 292, 20);
    abs_box(bar, 679, 7, 593, 20);
    abs_right(bar, 44, 7, 67, 20);
}

static void build_mid(Scene *s, float tl, float tr, float sc) {
    YGNodeRef panel = box(s->root, 0.0f, -1.0f);
    YGNodeStyleSetFlexGrow(panel, 1.0f);
    YGNodeStyleSetFlexBasis(panel, 0.0f);
    margin(panel, YGEdgeLeft, UI_SPLIT);
    build_header(panel);
    build_tabs(panel);
    build_chat_content(s, tl, tr, sc);
    YGNodeInsertChild(panel, s->chat, YGNodeGetChildCount(panel));
    build_task(s->chat_content);
    build_composer(s, tl, tr);
    YGNodeInsertChild(panel, s->composer, YGNodeGetChildCount(panel));
    build_status(panel);
}

static void build_file_row(YGNodeRef content, const UIFile *f) {
    YGNodeRef r = row(content, 44.0f);
    YGNodeRef t;
    YGNodeStyleSetAlignItems(r, YGAlignCenter);
    YGNodeStyleSetGap(r, YGGutterAll, 13.0f);
    YGNodeStyleSetPadding(r, YGEdgeLeft, 58.0f);
    leaf(r, 18, 18);
    t = leaf(r, text_w(f->name), 22);
    if (f->mod) margin(leaf(r, 11, 18), YGEdgeLeft, 8.0f);
    (void)t;
}

static void build_file_tab(YGNodeRef tab) {
    YGNodeRef file = row(tab, 40.0f);
    YGNodeStyleSetWidth(file, 155.0f);
    YGNodeStyleSetPadding(file, YGEdgeLeft, 18.0f);
    YGNodeStyleSetAlignItems(file, YGAlignCenter);
    margin(file, YGEdgeLeft, 80.0f);
    leaf(file, 18, 18);
    margin(leaf(file, 42, 22), YGEdgeLeft, 20.0f);
    margin(leaf(file, 13, 20), YGEdgeLeft, 31.0f);
}

static void build_right_actions(YGNodeRef tab) {
    YGNodeRef spacer = leaf(tab, -1.0f, 0.0f);
    YGNodeRef last;
    YGNodeStyleSetFlexGrow(spacer, 1.0f);
    margin(leaf(tab, 21, 21), YGEdgeLeft, 36.0f);
    margin(leaf(tab, 20, 20), YGEdgeLeft, 35.0f);
    last = leaf(tab, 21, 21);
    margin(last, YGEdgeLeft, 27.0f);
    margin(last, YGEdgeRight, 27.0f);
}

static void build_right_tabs(YGNodeRef panel) {
    YGNodeRef tab = row(panel, 62.0f);
    YGNodeStyleSetAlignItems(tab, YGAlignCenter);
    YGNodeStyleSetPadding(tab, YGEdgeLeft, 33.0f);
    leaf(tab, 16, 16);
    margin(leaf(tab, 87, 22), YGEdgeLeft, 21.0f);
    margin(leaf(tab, 21, 21), YGEdgeLeft, 50.0f);
    margin(leaf(tab, 64, 22), YGEdgeLeft, 11.0f);
    build_file_tab(tab);
    margin(leaf(tab, 17, 17), YGEdgeLeft, 22.0f);
    build_right_actions(tab);
}

static void build_right_search(YGNodeRef panel) {
    YGNodeRef search = row(panel, 48.0f);
    YGNodeRef boxn;
    int       i;
    margin(search, YGEdgeLeft, 16.0f);
    boxn = row(search, 48.0f);
    YGNodeStyleSetWidth(boxn, 577.0f);
    YGNodeStyleSetPadding(boxn, YGEdgeLeft, 38.0f);
    YGNodeStyleSetAlignItems(boxn, YGAlignCenter);
    leaf(boxn, 128, 20);
    for (i = 0; i < 4; ++i) {
        YGNodeRef n = leaf(search, 20, 20);
        margin(n, YGEdgeLeft, i == 0 ? 17.0f : (i == 1 ? 39.0f : 30.0f));
        if (i == 3) margin(n, YGEdgeLeft, 31.0f);
    }
}

static void build_right_list(Scene *s, YGNodeRef panel, float sf) {
    YGNodeRef view = box(panel, -1.0f, -1.0f);
    YGNodeRef content;
    int       i;
    YGNodeStyleSetFlexGrow(view, 1.0f);
    YGNodeStyleSetFlexBasis(view, 0.0f);
    YGNodeStyleSetOverflow(view, YGOverflowHidden);
    margin(view, YGEdgeTop, 10.0f);
    content = mk(view, -1.0f, 1320.0f);
    YGNodeStyleSetFlexDirection(content, YGFlexDirectionColumn);
    YGNodeStyleSetPositionType(content, YGPositionTypeAbsolute);
    YGNodeStyleSetPosition(content, YGEdgeLeft, 0.0f);
    YGNodeStyleSetPosition(content, YGEdgeRight, 0.0f);
    YGNodeStyleSetPosition(content, YGEdgeTop, -sf);
    s->file_content = content;
    for (i = 0; i < UI_NFILE; ++i) build_file_row(content, &ui_files[i]);
}

static void build_right(Scene *s, float w, float sf) {
    YGNodeRef panel = box(s->root, w, -1.0f);
    s->right = panel;
    build_right_tabs(panel);
    build_right_search(panel);
    build_right_list(s, panel, sf);
    abs_right(panel, 8, 322, 8, 467);
}

static Scene build_scene(const UISize *e) {
    Scene s;
    g_nodes = 0;
    std::memset(&s, 0, sizeof(s));
    s.root = mk(NULL, e->w, e->h);
    YGNodeStyleSetFlexDirection(s.root, YGFlexDirectionRow);
    build_left(&s, e->left);
    build_mid(&s, e->tl, e->tr, e->sc);
    build_right(&s, e->right, e->sf);
    YGNodeCalculateLayout(s.root, e->w, e->h, YGDirectionLTR);
    s.nodes = g_nodes;
    return s;
}

static void apply(Scene *s, const UISize *e) {
    YGNodeStyleSetWidth(s->root, e->w);
    YGNodeStyleSetHeight(s->root, e->h);
    YGNodeStyleSetWidth(s->left, e->left);
    YGNodeStyleSetWidth(s->right, e->right);
    YGNodeStyleSetMargin(s->chat, YGEdgeLeft, e->tl);
    YGNodeStyleSetMargin(s->chat, YGEdgeRight, e->tr);
    YGNodeStyleSetMargin(s->composer, YGEdgeLeft, e->tl - 28.0f);
    YGNodeStyleSetMargin(s->composer, YGEdgeRight, e->tr - 19.0f);
    YGNodeStyleSetPosition(s->thread_content, YGEdgeTop, -e->sl);
    YGNodeStyleSetPosition(s->chat_content, YGEdgeTop, -e->sc);
    YGNodeStyleSetPosition(s->file_content, YGEdgeTop, -e->sf);
    YGNodeCalculateLayout(s->root, e->w, e->h, YGDirectionLTR);
}

static void bench_build(void *p) {
    Scene s = build_scene((const UISize *)p);
    g_sink += (double)YGNodeLayoutGetWidth(s.root);
    YGNodeFreeRecursive(s.root);
}

static void bench_build_suggest(void *p) {
    const UISize *s = (const UISize *)p;
    Scene         sc = build_scene(&s[0]);
    apply(&sc, &s[1]);
    g_sink += (double)YGNodeLayoutGetWidth(sc.root);
    YGNodeFreeRecursive(sc.root);
}

typedef struct SuggestCtx {
    Scene         scene;
    const UISize *sizes;
    int           n, i;
} SuggestCtx;

static void bench_suggest(void *p) {
    SuggestCtx *c = (SuggestCtx *)p;
    apply(&c->scene, &c->sizes[c->i]);
    c->i = (c->i + 1) % c->n;
}

static double now(void) { return (double)clock() / (double)CLOCKS_PER_SEC; }

static void run(const char *name, void (*fn)(void *), void *ctx) {
    double t0, dt;
    long   n = 0;
    fn(ctx);
    t0 = now();
    do {
        fn(ctx);
        ++n;
        dt = now() - t0;
    } while (dt < 0.20);
    std::printf("%-24s %12.1f ns/op  (%ld iters)\n", name, dt * 1e9 / n, n);
}

static void print_rect(const char *name, YGNodeRef n) {
    std::printf("%-8s %6.1f %6.1f %6.1f %6.1f\n", name,
                (double)YGNodeLayoutGetLeft(n), (double)YGNodeLayoutGetTop(n),
                (double)YGNodeLayoutGetWidth(n),
                (double)YGNodeLayoutGetHeight(n));
}

int main(int argc, char **argv) {
    SuggestCtx ctx;
    UISize     s = ui_sizes[0];
    int        n = (int)(sizeof(ui_sizes) / sizeof(ui_sizes[0]));
    ctx.sizes = ui_sizes;
    ctx.n = n;
    ctx.i = 1;
    ctx.scene = build_scene(&s);
    std::printf("yoga: %d nodes\n", ctx.scene.nodes);
    if (argc > 1 && !std::strcmp(argv[1], "--layout")) {
        print_rect("root", ctx.scene.root);
        print_rect("left", ctx.scene.left);
        print_rect("chat", ctx.scene.chat);
        print_rect("right", ctx.scene.right);
        print_rect("composer", ctx.scene.composer);
        YGNodeFreeRecursive(ctx.scene.root);
        return 0;
    }
    run("build", bench_build, &s);
    run("build+suggest", bench_build_suggest, (void *)ui_sizes);
    run("suggest (9 edits)", bench_suggest, &ctx);
    YGNodeFreeRecursive(ctx.scene.root);
    if (g_sink < 0.0) std::printf("sink %f\n", (double)g_sink);
    return 0;
}
