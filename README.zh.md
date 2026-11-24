# Amoeba —— 纯 C 实现的约束求解算法

[English](README.md) | 中文

[![CI](https://github.com/starwing/amoeba/actions/workflows/test.yml/badge.svg?branch=master)](https://github.com/starwing/amoeba/actions/workflows/test.yml)
[![覆盖率](https://coveralls.io/repos/github/starwing/amoeba/badge.svg?branch=master)](https://coveralls.io/github/starwing/amoeba?branch=master)

Amoeba 是 Cassowary 算法的纯 C 实现。它：

- 使用 Clean C，即 ANSI C89 与 C++ 的交集，和 Lua 语言一样。
- 是单文件库。更多单文件库见 stb 项目[这里][1]。
- 主要受 [kiwi][2]（Cassowary 算法的 C++ 实现）和算法[论文][3]启发。
- 带 Lua 绑定。
- 许可证与 [Lua 语言][4]相同。

[1]: https://github.com/nothings/stb
[2]: https://github.com/nucleic/kiwi
[3]: http://constraints.cs.washington.edu/solvers/uist97.html
[4]: https://www.lua.org/license.html

## 为什么选择 Amoeba

Amoeba 是一个 Cassowary 自动布局引擎，在 GUI 真正耗时的环节上比 Facebook Yoga
更快：一轮热编辑快约 12 倍；恢复一份 dump 出来的 solver 并应用第一轮编辑，比重建
Yoga 树再布局快约 2.2 倍。作为 Cassowary 求解器，它比 flexbox 灵活得多：可以指定
任意一组线性约束来实现布局效果。

基准场景是 DeepSeek Harness 窗口（272 个盒子、1104 条约束），用 `just bench` 和
`just bench-yoga` 在 `-O2` 下、Apple M4 上测得：

| 操作                   | Amoeba |                        Yoga |               |
| ---------------------- | -----: | --------------------------: | ------------- |
| 冷构建                 | 2.2 ms |                       83 us | 慢 26 倍      |
| 构建 + 第一次编辑      | 1.9 ms |                      130 us | 慢 15 倍      |
| 热编辑（9 次 suggest） | 4.0 us |                       47 us | **快 12 倍**  |
| 加载 dump 的 solver    |  22 us |               83 us（构建） | **快 3.8 倍** |
| 加载 + 第一次编辑      |  59 us | 130 us（构建 + 第一次编辑） | **快 2.2 倍** |

Cassowary 在构建时一次性完成约束编译，正是这笔一次性开销让冷构建更慢。本设计把它
变成缓存：`am_dump()` 在 44 us 内写出求解后的 tableau（这里是 135 KB），
`am_load()` 在 22 us 内恢复它。第一次需要项表（term table）的调用会在一次遍历中
生成它们，且不分配内存，所以恢复并应用第一次编辑比 Yoga 的构建 + 第一次编辑更快。

不做缓存的话，一次 suggest 要扫描每一行才能找出包含被编辑变量的行。每个 edit 精确
缓存这些行，所以热 suggest 只遍历它们（九次编辑 4.0 us，而不是 47 us）。dump 会把
该缓存作为提示一并写出；load 会校验并填充它，所以 load 之后的第一次 suggest 也不用
重新扫描。

Yoga 在一次性冷构建上仍然更快（83 us 对 2.2 ms）。优势从布局建好之后才出现，而这对
会缩放、滚动和编辑的窗口来说是常态。数字会因机器而异，重点看趋势。

相对 Kiwi 的改进：

- 便于分发的单头文件库。
- 提供 Lua 绑定（kiwi 用 Python 绑定）。
- 导出（dump）和加载（load）求解器状态。
- 每次编辑的 suggest 缓存和零分配唤醒（见上文）。

## 如何使用

这个库对外提供约束求解器接口。要解一个约束问题，可以按以下步骤使用：

- 新建求解器：`am_newsolver()`
- 创建变量：`am_newvariable()` 把每个变量绑定到一个 `am_Num` 值。
- 创建约束：`am_newconstraint()`。
- 用这些函数构造约束方程：
  - `am_addterm()` 往约束方程里加入一个 $a \times variable$ 项。
  - `am_setrelation()` 设置方程中间的 `=`、`>=` 或 `<=` 关系。
  - `am_addconstant()` 加入一个不带变量的数。
  - `am_setstrength()` 指定该约束在所有约束中的优先级。
- 约束构造好后，可以用 `am_add()` 把它加入求解器。
- 之后可以从绑定的 `am_Num` 读出每个变量的结果。
- 也可以用 `am_suggest()` 手动给变量指定新值。
- 完成后用 `am_delsolver()` 释放所有内存。

下面是一个演示这些步骤的小例子：

```c
/* 包含实现和接口 */
#define AM_IMPLEMENTATION
#include "amoeba.h"

int main(void) {
    /* 创建一个求解器 */
    am_Solver *S = am_newsolver(NULL, NULL);
    int ret;

    /* 创建变量 */
    am_Num l, m, r;
    am_Id vl = am_newvariable(S, &l);
    am_Id vm = am_newvariable(S, &m);
    am_Id vr = am_newvariable(S, &r);

    /* 创建约束 */
    am_Constraint *c1 = am_newconstraint(S, AM_REQUIRED);
    am_Constraint *c2 = am_newconstraint(S, AM_REQUIRED);

    /* c1：m 在 l 和 r 正中间，即 2*m = l + r */
    am_addterm(c1, vm, 2.f);
    am_setrelation(c1, AM_EQUAL);
    am_addterm(c1, vl, 1.f);
    am_addterm(c1, vr, 1.f);
    /* 加入 c1 */
    ret = am_add(c1);
    assert(ret == AM_OK);

    /* c2：r - l >= 100 */
    am_addterm(c2, vr, 1.f);
    am_addterm(c2, vl, -1.f);
    am_setrelation(c2, AM_GREATEREQUAL);
    am_addconstant(c2, 100.f);
    /* 加入 c2 */
    ret = am_add(c2);
    assert(ret == AM_OK);

    /* 把 l 设为 20 */
    am_suggest(S, vl, 20.f);

    /* 读回 m 和 r */
    am_updatevars(S);

    /* r 应该是 20 + 100 == 120 */
    assert(r == 120.f);

    /* m 应该在 l 和 r 正中间 */
    assert(m == 70.f);

    /* 用完求解器 */
    am_delsolver(S);
    return 0;
}
```


## 构建与测试

库本身只需要一个 C 编译器：

```c
#define AM_IMPLEMENTATION
#include "amoeba.h"
```

仓库由 [just](https://github.com/casey/just) 驱动：

```sh
just test            # 全部 suite，ASan/UBSan，每个测试一个进程
just test-release    # 同样的 suite，-O2
just test-double     # AM_USE_DOUBLE
just test-c89        # 严格 C89 编译检查（不运行）
just test-cpp03      # 严格 C++03 编译检查（不运行）
just test dump       # 单个 suite；just test dump dumpload 跑单个测试
just fuzz            # 随机操作 fuzz，每个种子一个进程
just cov             # lcov 报告
just format          # 用 clang-format 格式化整棵树
```

Windows CI 直接运行 `misc\run_tests_windows.bat`（MSVC）；严格 C89/C++03
检查与 Lua 绑定只在 POSIX 上运行。

测试源码位于 `tests/`；`misc/gen_entries.lua` 把 `TEST(name)` 函数变成入口表（因此
Lua 解释器是测试的构建依赖）。代码风格由 `.clang-format` 定义（clang-format >= 19，
CI 中固定为 23.1.5）。路线图见 [TODO.md](TODO.md)。

## 基准测试

`just bench` 用纯 C 跑一个 GUI 布局场景；`just bench-yoga` 用
[Yoga](https://github.com/facebook/yoga) 构建同一棵树（把 `YOGA_SRC` 指向 Yoga
源码树，默认 `~/Work/Sources/yoga`）以便对比。场景是 DeepSeek Harness 窗口：272 个
文本/容器盒子和 1104 条约束，由六次 resize 编辑（窗口宽/高、两个 sidecar 宽度、
transcript 文本内边距）和三次针对两层列表的滚动编辑驱动。文本大小是固有输入，所以
两个引擎比较的都是布局，不涉及字形排布。

Yoga 版本需要 288 个节点（少数 flex spacer 是结构性的）。对比数字见
[为什么选择 Amoeba](#为什么选择-amoeba)，于 2026-11-23 在 Apple Silicon 机器上用
`-O2` 测得；数字会因机器而异，重点看趋势。

`just layout 2748 1263` 把每个盒子导出为 `kind x y w h clip name`；
`misc/render_layout.py` 把该导出渲染为 PNG，并可叠加参考截图。

## Lua 绑定

`lua_amoeba.c` 把库嵌入 Lua。绑定的契约——支持的运行时（LuaJIT 2.1 / LÖVE 11.x 和
Lua 5.5）、生命周期与句柄有效性、名称、`dump` 的副作用以及每个方法——记录在
[amoeba.d.lua](amoeba.d.lua)（LuaLS 类型声明，英文）中。它还说明了当 `amoeba.lua`
遮蔽 C 模块时如何加载它。

`just test-lua`、`just test-lua-luajit` 和 `just test-lua-double` 会构建模块，并分别
针对 Lua 5.x、LuaJIT 2.1 和 `AM_USE_DOUBLE` 构建运行断言套件 `test.lua`。

## 参考

### 返回值

下面所有返回 `int` 的函数都返回错误码：

- `AM_OK`：操作成功。
- `AM_FAILED`：操作失败：求解器拒绝了一个合法请求（例如添加一个已在求解器中的约束）。
- `AM_PARAM`：参数检查失败（空指针、变量 id 为零、负的 strength、无效的 relation，
  或来自不同求解器的约束）。
- `AM_UNSATISFIED`：所添加的约束无法被满足。
- `AM_UNBOUND`：约束与求解器中已有的约束冲突（人工变量无法被驱动到零）。它与
  `AM_UNSATISFIED` 是同一类失败，只是通过不等式路径到达。

### Dump 与 Load

Amoeba 可以把求解器的完整状态存成二进制数据，并把它加载进一个新的求解器
（`am_load()`）。

`am_dump()` 写出整个求解器状态，包括每一次 suggest/edit：edit 的变量、strength 和
值都在流中。`am_load()` 创建一个干净的求解器并返回它；流无法加载时返回 `NULL`。
edit 会连同其隐藏的等式约束一起重建，因此 `am_hasedit()` 立即为真，`am_suggest()`
会从 dump 的值继续。绑定也会从流中恢复：每个变量拿到它 dump 时的值，`load_var()`
决定该值放在哪里。

loader 可以在解析 body 之前看到新求解器：如果设置了 `am_Loader::solver`，它会在流头
校验通过之后、读取任何变量或约束之前被调用一次。此时求解器是空的，并以
`const am_Solver *` 传入：需要的话可以保留指针，但不要在它之上构建对象。如果之后加载
失败，求解器会被销毁，因此该指针必须丢弃，且绝不能释放。

只支持由 `am_dump()` 产生的流。加载任何其它数据都不受支持：loader 只承诺
`am_load()` 本身不会崩溃。成功加载非 dump 流之后求解器会做什么（包括后续调用中的
assert 或崩溃）不在契约内；loader 只检查结构，不校验 tableau 语义。

`am_load()` 在加载期间花费少于 `maxmem` 字节（`0` 表示选择 `AM_MAX_LOADMEM`，默认
16 MiB；在包含 `amoeba.h` 之前定义 `AM_MAX_LOADMEM` 可修改）。预算是严格的：单次
分配的增量等于剩余预算即失败。它限制的是向分配器请求的字节数；进程的实际内存
占用可能更高。加载成功后预算被释放。

要做 dump 或 load，准备一个 `am_Dumper` 或 `am_Loader` 来控制操作。

- `struct am_Dumper`

  用户定义的 dump 行为。`writer` 和 `var_name` 是必需的，`cons_name` 可选。
  例如：

  ```c
  /* 把 dump 结果写入固定大小的缓冲区 */
  typedef struct MyDumper {
      am_Dumper base;
      char buf[MY_BUF_SIZE];
      size_t len;
  } MyDumper;

  static int my_writer(am_Dumper *d, const void *buf, size_t len) {
      MyDumper *w = (MyDumper*)d;
      if (len > MY_BUF_SIZE - w->len) return AM_FAILED;
      memcpy(w->buf + w->len, buf, len);
      w->len += len;
      return AM_OK;
  }

  static const char *my_varname(am_Dumper *d, unsigned i, am_Id v, am_Num *value) {
      (void)d, (void)i, (void)v;
      return ((MyContext*)value)->name; /* value 是 MyContext 内的绑定值 */
  }

  MyDumper w;
  memset(&w, 0, sizeof(w));
  w.base.writer   = my_writer;
  w.base.var_name = my_varname;  /* 必需 */
  /* w.base.cons_name = my_consname;  可选 */

  int ret = am_dump(solver, &w.base);
  /* 现在可以使用 w.buf 和 w.len */
  ```

- `struct am_Loader`

  用户定义的 load 行为。设置回调前先清零（例如用 `memset`）：`reader` 和
  `load_var` 是必需的，`solver` 和 `load_cons` 可选，留空时跳过。例如：

  ```c
  /* 从一段内存加载 */
  typedef struct MyLoader {
      am_Loader base;
      const char *buf;
      size_t len;
  } MyLoader;

  static const char *my_reader(am_Loader *l, size_t *plen) {
      MyLoader *r = (MyLoader*)l;
      const char *p = r->buf;
      *plen = r->len;
      r->buf = NULL, r->len = 0; /* 数据只交出一次 */
      return p;
  }

  static am_Num *my_loadvar(am_Loader *l, const char *name, unsigned i, am_Id v) {
      (void)l, (void)i;
      return &my_var(name, v)->value; /* name 可为 NULL，要保留就复制 */
  }

  MyLoader r;
  memset(&r, 0, sizeof(r));
  r.base.reader   = my_reader;
  r.base.load_var = my_loadvar;  /* 必需 */
  /* r.base.solver = my_solver; r.base.load_cons = my_loadcons;  可选 */
  r.buf = data, r.len = size;

  am_Solver *solver = am_load(NULL, NULL, &r.base, 0);
  /* 现在可以使用 solver */
  ```

  `am_load()` 失败时返回 `NULL` 并释放半成品求解器；此时从 `solver`、`load_var` 或
  `load_cons` 保留的任何指针都已失效。

### 例程

- `am_Solver *am_newsolver(am_Allocf *allocf, void *ud);`

  用自定义内存分配器创建一个新求解器；传 NULL 表示使用默认分配器。

  所有内存都经过分配器。默认分配器（传 NULL）下，约束和 suggest 从内部 4 KB
  页池分配；传入自定义 `allocf` 时不做池化：每个约束和 suggest 都逐个分配，
  需要池化由调用方自己负责。

- `void am_resetsolver(am_Solver *solver);`

  从求解器中移除所有约束，已创建的所有变量和约束保留。
  tableau 被丢弃，因此没有任何变量再拥有行，但值绑定**不会**被刷新：`*pvalue` 保持
  reset 之前的值。此后绑定归调用者所有；`am_updatevars()` 只消费脏列表
  （dirty list），不会重新求解整个 tableau。

- `void am_delsolver(am_Solver *solver);`

  删除求解器并释放它使用的所有内存；此后由该求解器创建的所有变量/约束都被释放。
  传 `NULL` 什么也不做。

- `void am_updatevars(am_Solver *solver);`

  把变量的值刷新为求解后的值。
  只有自上次刷新以来值可能已改变的变量会入队，且只写入已入队的绑定。
  `am_add()` 和成功的 `am_setstrength()` 会把它求解过程中改动过的绑定全部入队
  （见下面的 `am_add()`）；`am_remove()` 立即写回一个离开的绑定，`am_load()`
  会自行写入每个绑定。
  `am_resetsolver()` 不入队任何东西。
  你可以用 `am_autoupdate()` 在 solver 变化时自动更新所有变量。
  `NULL` solver 什么也不做。

- `void am_autoupdate(am_Solver *solver, int auto_update);`

  设置自动更新标志。
  若已设置，任何对 `solver` 的改动完成后，所有变量会立即更新为最新值。

- `am_Id am_newvariable(am_Solver *solver, am_Num *pvalue);`

  创建一个新变量，并把值 `pvalue` 绑定到它。出错返回 `0`。

  通过 `am_dump()` 或 `am_varvalue()` 可以重新拿到 `pvalue` 指针，因此可以把上下文
  数据放在这里：

  ```c
  typedef struct MyVarContext {
      am_Num value;
      const char *name;
      /* 等等 */
  } MyVarContext;
  MyVarContext *ctx = new_context();
  am_Id var = am_newvariable(solver, &ctx->value);
  /* 取回上下文：*/
  MyVarContext *ctx = (MyVarContext*)am_varvalue(solver, var, NULL);
  ```

- `void am_delvariable(am_Solver *solver, am_Id var);`

  删除该变量；此后它不能加入后续约束。

- `am_Num *am_varvalue(am_Solver *solver, am_Id var, am_Num *newvalue);`

  取回或重设变量的值绑定。若 `newvalue` 为 `NULL`，只取回旧指针；否则绑定被重定向到
  `newvalue`，并返回原指针。

- `int am_refcount(const am_Solver *solver, am_Id var);`

  取回变量的内部引用计数：变量本身算一个，每个存活的项各算一个。它只用来判断
  无人引用的变量何时被丢弃，与用户层的所有权无关。

- `am_Id am_vars(const am_Solver *solver, am_Id var);`

  遍历求解器持有的变量。第一次传 `0`，之后传上一次的结果；`0` 表示“没有更多”。顺序
  取决于求解器内部的槽位，可能与创建顺序不同；对求解器的任何修改都可能重排它，
  所以每次修改后都要重新开始，不能跨修改保留锚点。

- `int am_addedit(am_Solver *solver, am_Id var, am_Num strength);`

  准备修改变量的值；变量已处于 edit 中时，则修改它的 `strength`。

- `void am_deledit(am_Solver *solver, am_Id var);`

  取消对变量的修改。
  `var` 的值绑定会按求解器恢复为它应有的值。

- `void am_suggest(am_Solver *solver, am_Id var, am_Num value);`

  实际改变变量 `var` 的值。
  求解器不认识的变量 id 会被忽略。

  改变之后，其它变量可能因求解器中的约束而变化。
  如果你不想改变 suggest 的 strength（默认 `AM_MEDIUM`），可以直接调用
  `am_suggest()` 而不调用 `am_addedit()`。

- `int am_hasedit(const am_Solver *solver, am_Id var);`

  检查变量在 `solver` 中是否有被 suggest 的值。

- `void am_clearedits(am_Solver *solver);`

  从 `solver` 中移除所有变量 suggest。

- `am_Constraint *am_newconstraint(am_Solver *solver, am_Num strength);`

  创建一个新约束。注意 `am_Constraint` 头部有一个指针的空间可用于存放上下文数据，
  例如：

  ```c
  am_Constraint *cons = am_newconstraint(solver, AM_REQUIRED);
  MyContext *ctx = new_my_context();
  ctx->mydata = 1;
  *(MyContext**)cons = ctx;
  ```

- `void am_resetconstraint(am_Constraint *cons);`

  从约束中移除所有项和变量，恢复为空白状态。

- `void am_delconstraint(am_Constraint *cons);`

  释放约束。如果它已加入求解器，先移除它。

  这是 free 语义：约束被回收，其表达式被释放，变量引用被丢弃。调用返回后 `cons` 即
  死亡——再次使用它（包括第二次传入）是未定义行为，且不会被检测。默认分配器把槽位
  放回约束池，因此第二次调用可能仍读到看似合理的内存：这只是槽位复用，不能当作
  承诺。

- `am_Constraint *am_cloneconstraint(am_Constraint *other, am_Num strength);`

  用新的 `strength` 从现有约束克隆一个新约束。
  新约束无法分配时返回 `NULL`。

- `am_Constraint *am_constraints(const am_Solver *solver, am_Constraint *cons);`

  以同样方式遍历注册在 `solver` 中的约束：`NULL` 开始遍历，`NULL` 结束。约束由
  `am_newconstraint()` 注册，并一直注册到 `am_delconstraint()` 为止，因此遍历不要求
  先添加它。一次 edit 为 `am_suggest()` 创建的隐藏约束不会被返回。锚点必须是该求解器
  的约束。

- `int am_addterm(am_Constraint *cons, am_Id var, am_Num multiplier);`

  往约束中加入一个项。

  例如 `2*m == x+y` 这样的约束有 `2*m`、`1*x` 和 `1*y` 三个项。因此可以这样构造：

  ```c
  /* 2*m == x + y */
  am_addterm(c, m, 2.0); /* 2*m */
  am_setrelation(c, AM_EQUAL); /* = */
  am_addterm(c, x, 1.0); /* x */
  am_addterm(c, y, 1.0); /* y */
  ```

- `int am_setrelation(am_Constraint *cons, int relation);`

  设置约束的关系，可取以下之一：

  - `AM_EQUAL`
  - `AM_GREATEREQUAL`
  - `AM_LESSEQUAL`

  在 `am_setrelation()` 之前加入的项成为约束的左手项，调用之后加入的项成为右手项。

- `int am_addconstant(am_Constraint *cons, am_Num constant);`

  加入一个不带变量的常数作为约束中的项。求解器视为数值零的常数会存为 `0`（低于
  `AM_NUM_EPS`：默认 `float` 构建中为 1e-4，`AM_USE_DOUBLE` 时为 1e-6）；
  `am_constant()` 报告存储后的值。

- `int am_setstrength(am_Constraint *cons, am_Num strength);`

  设置约束的 strength。

- `int am_merge(am_Constraint *cons, const am_Constraint *other, am_Num coef);`

  把 `other` 乘以系数 `coef` 后合并进 `cons`。

- `int am_add(am_Constraint *cons);`

  把约束加入它创建自的求解器。添加成功时，它算出的解会立即可见：开启自动更新时，
  调用返回时绑定已是最新；关闭时，下一次 `am_updatevars()` 会写入它们。添加失败则
  不改变任何绑定。

- `void am_remove(am_Constraint *cons);`

  移除已添加的约束。

- `int am_hasconstraint(const am_Constraint *cons);`

  检查约束是否已加入求解器。

- `am_Id am_terms(const am_Constraint *cons, am_Id var, am_Num *coef);`

  遍历约束表达式的项：第一个项传 `0`，之后传上一次的结果；`0` 表示“没有更多”。
  `*coef` 收到按存储形式给出的系数；对于在 `am_setrelation()` 之前写下的左手项，该
  系数是取负后的值（见 `am_setrelation()`）。约束加入求解器之前也可用。

- `int am_relation(const am_Constraint *cons);`

  返回 `am_setrelation()` 设置的关系；尚未设置时返回 `0`。

- `am_Num am_constant(const am_Constraint *cons);`

  约束表达式的常数，形式与 `am_terms()` 使用的存储形式相同（见
  `am_setrelation()`）：在关系之后写的常数保持符号，在关系之前写的常数与项一起取负。
  在求解器 epsilon 范围内（见 `am_addconstant()`）的值读回为 `0`。

- `am_Num am_strength(const am_Constraint *cons);`

  约束的 strength。它保持为 `am_Num`，因此 `AM_REQUIRED`（`+inf`）能完整往返；
  `NULL` 返回 `0`。

- `am_Num am_error(const am_Constraint *cons);`

  当前解中约束被违反的程度，以存储表达式的单位计：约束误差变量之和。required 约束
  没有误差变量，报告 `0`；不等式只计超过的部分；软等式报告绝对残差。该值直接读自
  tableau，因此无需 `am_updatevars()` 就是当前值，并且在 `am_dump()`/`am_load()` 之后
  仍保留。`NULL` 以及不在求解器中的约束返回 `0`。strength 不会被乘进去——如果需要
  目标函数的贡献，请自己乘以 `am_strength()`。

- `int am_dump(am_Solver *solver, am_Dumper *d);`

  把整个求解器状态（包含 edits）导出为二进制数据。

  当状态中含有 dump 没有索引的符号时返回 `AM_FAILED`。`am_load()` 接受这样的流
  （它校验结构，不校验符号归属），所以从手工编辑过的流加载出的求解器可能到达那种
  形态；失败之前写出的字节必须丢弃。

- `am_Solver *am_load(am_Allocf *allocf, void *ud, am_Loader *l, size_t maxmem);`

  创建一个新求解器，把 dump 流加载进去并返回它；流无法加载时返回 `NULL`。
  `allocf`/`ud` 会传给 `am_newsolver()`。`maxmem` 是加载的严格字节预算（`0` 表示
  `AM_MAX_LOADMEM`）；单次分配不得消耗掉全部剩余预算。只支持由 `am_dump()` 产生的
  流，变量绑定从流中恢复（见“Dump 与 Load”）。
