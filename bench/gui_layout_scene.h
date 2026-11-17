#ifndef GUI_LAYOUT_SCENE_H
#define GUI_LAYOUT_SCENE_H

#define UI_W      2748
#define UI_H      1263
#define UI_LEFT   436
#define UI_RIGHT  820
#define UI_SPLIT  4
#define UI_TEXT_L 133
#define UI_TEXT_R 140

typedef struct {
    const char *text;
    char        icon;
    short       w;
    short       h;
    short       gap;
} UIChat;

typedef struct {
    const char *title;
    const char *time;
    short       w;
    short       tw;
} UIThread;

typedef struct {
    const char *name;
    char        dir;
    char        mod;
} UIFile;

static const UIChat chat_lines[] = {
        {"已完成，用时 12 分 9 秒", 0, 260, 24, 0},
        {"已注入上下文", 1, 156, 23, 55},
        {"我会先阅读计划文档和当前实现。", 0, 360, 24, 37},
        {"执行了命令，已读取文件，"
         "已搜索代码等",
         1, 458, 24, 36},
        {"运行命令 · List notes and git status", 1, 372, 23, 31},
        {"查找文件 · notes/plan-e2v2.md", 1, 332, 24, 23},
        {"思考 · Let me read the plan-e2v2.md.", 1, 397, 22, 25},
        {"读取 · notes/plan-e2v2.md", 1, 288, 25, 25},
        {"思考 · Now the key question. Let me look at the discussion §12 "
         "evidence, particularly §12.5, §12.10, §12.11. Als...",
         1, 0, 23, 22},
        {"运行命令 · List section headers of "
         "discussion doc",
         1, 518, 21, 24},
        {"运行命令 · Search for correlation claims "
         "in notes",
         1, 508, 20, 27},
        {"思考 · The user says \"计划里说（或者设计者会话有提）"
         "'序列化字节大小''和'性能'没有相关性\". Let me read disc...",
         1, 0, 23, 27},
        {"读取 · notes/discussion-dumpload.md", 1, 405, 25, 24},
        {"思考 · Now the key question. The user's "
         "confusion:",
         1, 531, 22, 23},
        {"运行命令 · Locate dump/load functions "
         "in amoeba.h",
         1, 496, 22, 25},
        {"运行命令 · Read discussion sections "
         "12.10-12.11",
         1, 496, 17, 25},
};

static const char *const ui_nav[] = {
        "插件", "插件市场", "自动化任务", "上下文洞察", "记忆宫殿", "更多",
};

static const UIThread thread_items[] = {
        {"我希望你在项目中做一份真...", "4 分钟", 283, 41},
        {"新会话", "5 分钟", 64, 41},
        {"窄化数据性能提升原理疑问", "17 分钟", 261, 47},
        {"继续执行handoff计划", "6 小时", 213, 41},
        {"新会话", "6 小时", 64, 41},
        {"你是 amoeba dump/load ...", "6 小时", 248, 41},
};

static const UIFile ui_files[] = {
        {"misc", 1, 0},
        {"notes", 1, 0},
        {"test.dSYM/Contents", 1, 0},
        {"test_binarytree.dSYM/Contents", 1, 0},
        {"tests", 1, 0},
        {"yoga_benchmark.dSYM/Contents", 1, 0},
        {".clang-format", 0, 0},
        {".gitignore", 0, 1},
        {".travis.yml", 0, 0},
        {"AGENTS.md", 0, 0},
        {"amoeba.d.lua", 0, 1},
        {"amoeba.dll", 0, 0},
        {"amoeba.h", 0, 1},
        {"amoeba.h.gcov", 0, 0},
        {"amoeba.lua", 0, 1},
        {"amoeba.so", 0, 0},
        {"bench_oldwork", 0, 0},
        {"bench_oldwork.exe", 0, 0},
        {"bench_oldwork.exp", 0, 0},
        {"bench_oldwork.lib", 0, 0},
        {"bench_swiss", 0, 0},
        {"bench_swiss.exe", 0, 0},
        {"bench_swiss.exp", 0, 0},
        {"bench_swiss.lib", 0, 0},
        {"bench_swiss.neon", 0, 0},
        {"bench_swiss.pdb", 0, 0},
        {"bench_win32.txt", 0, 0},
        {"dump.data", 0, 0},
        {"example", 0, 0},
        {"justfile", 0, 0},
};

#define UI_NCHAT   (int)(sizeof(chat_lines) / sizeof(chat_lines[0]))
#define UI_NNAV    (int)(sizeof(ui_nav) / sizeof(ui_nav[0]))
#define UI_NTHREAD (int)(sizeof(thread_items) / sizeof(thread_items[0]))
#define UI_NFILE   (int)(sizeof(ui_files) / sizeof(ui_files[0]))

typedef struct {
    float w, h, left, right, tl, tr, sl, sc, sf;
} UISize;

static const UISize ui_sizes[] = {
        {2748, 1263, 436, 820, 133, 140, 0, 100, 0},
        {2880, 1620, 480, 880, 150, 150, 20, 60, 120},
        {2560, 1400, 440, 800, 120, 120, 0, 160, 0},
        {3000, 1600, 500, 900, 160, 160, 40, 0, 240},
};

#endif
