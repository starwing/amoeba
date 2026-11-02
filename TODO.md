# TODO

> 基于 2026-11-02 的工作。优先级：**P0** 影响正确性/可信度；**P1** 工程化与健壮性；
> **P2** 能力增强；**D** 是「可视化约束排版 Demo」的项目提案。
>
> 每条都写了「现状 → 做法 → 验收」。细节见 [notes/](notes/)。

---

## 复核结论（2026-11-02，HEAD `2337a3a`）

用一个 `AM_IMPLEMENTATION` 的最小程序 + ASan/UBSan 逐条复现（实验都在仓库副本里做），结论：

1. **复核时 #2～#7 在 HEAD 全部复现**，没有一个是「已经悄悄修好」的（#2、#7 已修复并提交，见下面的修复队列）。
2. **A1 已不成立**：HEAD 上 `am_add()` 失败会走 `am_remove_errors()` 清 marker
   （`amoeba.h:985-986`），重试返回 `AM_UNSATISFIED` 而不是 `AM_FAILED`。这一条可以从
   P0 队列里删掉（详见 §A1）。
3. 六个问题不是六件独立的事，按根因分三组 + 一个格式问题，彼此有依赖。

### 根因分组

| 组 | 成员 | 共同根因 |
|---|---|---|
| S：状态机 | #2、#7 | `am_setstrength()` 的 REQUIRED 分支被写成一条逗号表达式：既不分「在不在 solver 里」，也不管 re-add 失败 |
| R：记账 | #6（原语）、#4（load 绕过记账） | refcount 按「调用次数」增加，而不是按「表达式里真正活着的项」 |
| L：恢复与校验 | #3、#4、#5、A2 | `am_load()` 只恢复 tableau，不恢复周边不变量（绑定、refcount），也不校验输入 |
| 格式 | A3 | 流里没有 magic / 版本号 |

### 依赖与冲突

```
#2 ──> #7        同一段两行；#2 引入的 added 标志正是 #7 回滚要用的判据
                 （实测：#2 修好后 #7 的用例仍然是红的）
#6 ──> #4        若 #4 想复用「加项」helper；#4 也可以自己在 am_readconstraints 补计数而解耦
#4 ──> #3        仅当 #3 借脏链刷新；用全表刷新则不依赖
#5、A2 ──> A3    A3 改流头，会让 #5/A2 手搓的字节流全部重写
#5 <──> A2      两者在同一段校验里，且必须对「什么算合法流」给出一致答案
```

两个必须留意的点（都已核对）：

- **#5 与 A2 对「合法流」的定义冲突**：`load_accepts_correct_arena` 那条「正确」流里，行项
  引用了 `offset+1/+2`，但整个流只声明 1 个符号（`total=4` 只是留位）。按 A2 设计的
  `(value>>2) < total`，这条流会通过；按「符号必须真实存在」检查，则 #5 的用例反而变红。
  建议做 #5 时把用例改成「`am_dump()` 生成真流 + 只改 arena 字段」。
- **L 组五项挤在 `am_load` 的约 120 行**（`amoeba.h:1549-1665`），一次只能改一个，
  后一个必然在前一个的 diff 上 rebase。

### 改动量 vs 严重度（不同向，排期要看两列）

| # | 改动 | 严重度 | 依赖 | 现成用例 | 风险面 |
|---|---|---|---|---|---|
| 2 | 1 函数 2 行 | 中：静默改变 solver 成员 | 无 | pending 1 条 | 极低：纯前置条件 |
| 7 | 同一段 +5 行 | 高：约束被丢掉 | #2 的 `added` | pending 1 条 | 中：失败路径的返回值策略要定 |
| 5 | `amH_alloc` + arena 游标 | **最高**：堆越界（ASan 实测） | 与 A2 同区 | pending 2 条 | 中高：动的是所有表共用的分配器 |
| A2 | `am_readrow`/`am_readmarker` | 高：畸形文件 → abort / NULL 解引用 | 与 #5 同区 | 需新写 | 中 |
| 6 | `am_addterm`/`am_mergeconstraint` | 中：`am_Var` 泄漏 | 无 | pending 2 条 | **高**：refcount 与脏链共用 |
| 3 | `am_load` 尾部刷新 | 中：绑定停留在旧值 | 独立 | pending 值断言 | 中：非基本变量取 0 的语义 |
| 4 | `am_readconstraints` | 高：会回收仍被引用的变量 | 建议 #6 先 | 需新建 | 中 |
| A3 | 流头 | 中：静默版本错读 | 放最后 | — | 中 |

**建议顺序**：`#2`、`#7`（已完成）`→ #5 → A2 → #6 → #4 → #3 → A3`。
理由：#2/#7 共用一段代码；#5/A2 都是「畸形输入」，同一个读路径一起收口；#6 先落地 #4 才能
干净复用；A3 改格式头必须最后。若以安全优先，把 #5 提到最前也成立，只是不再是「最小步长」。

## 修复队列：还剩 4 个（#3～#6），另加 A2、A3

顺序 = 上面的建议顺序，**一次只改一个**。每个提交必须自带用例并且 `just check` 全绿；
同一处被审后返工用 `git commit --amend`，不追加「修修补补」提交。
完成一项就把它从这里移到 [notes/changes.md](notes/changes.md)。

| # | 问题 | 改动量 | 状态 | 用例 |
|---|---|---|---|---|
| 2 | `am_setstrength()` 对**未加入**的约束会 `am_remove`（空操作）+ `am_add`，把约束偷偷加进 solver | 3 行 | **已完成**（并入 baseline 提交）：只改 strength，不碰 solver（`amoeba.h:1041`） | `setstrength_unadded_does_not_register`（已搬回 live） |
| 3 | `am_load()` 之后绑定值不刷新（脏链表为空，`am_updatevars()` 空转） | ~6 行 | 待做 | `dump_test::dumpload` 的值断言 |
| 4 | `am_load()` 不恢复每项引用计数：load 后删一条约束会把别的约束还在用的变量回收 | ~8 行 | 待做（建议先做 #6） | 新建 `load_then_remove_keeps_vars` |
| 5 | `am_load()` 信任流里声明的 arena 槽数，`amH_alloc()` 越界写（ASan 复现堆溢出） | ~10 行 | 待做（用例按上面的建议重写） | `load_rejects_short_arena` + `load_accepts_correct_arena` |
| 6 | `am_addterm()`/`am_mergeconstraint()` 无条件 `refcount += 1`，合并/抵消时计数漂移（`am_Var` 泄漏） | ~10 行 | 待做 | `addterm_refcount_balances` + `mergeconstraint_refcount_balances` |
| 7 | `am_setstrength()` 收紧到 REQUIRED 失败时，约束既没恢复强度也不在 solver 里 | ~8 行 | **已完成**：失败时回滚到旧强度并重新加入（`amoeba.h:1041`） | `setstrength_tighten_failure_restores`（已搬回 live） |
| A2 | `am_load()` 不校验符号索引上界（见 §A2） | ~2 行 | 待做 | 需新写 |
| A3 | dump/load 没有 magic + 版本号（见 §A3） | ~8 行 | 待做（最后） | 改 `load_rejects_short_arena` 用带 magic 的流 |

**#3～#6 的用例现在放在 `tests/pending/`**（`api_test.c` 快照 + `dump_test.c` 快照里的 #3
值断言；#2、#7 的用例已随修复搬回 live），不参与构建，见 [tests/pending/README.md](tests/pending/README.md)。每修完一个
就按 B9 把对应用例搬回 live，不要直接覆盖。

## 提交计划

### 粒度（2026-11-02 定）

所有 bug 修在**唯一的 format 提交之前**；修复按「一个 fix 一个提交」拆。

- **在所有当前问题修完之前，不要 format**：不跑 `just format`、不手工重排格式，新改动按
  现有风格写。提前格式化会让「唯一一次机械重排」这个前提失效。
- 每个 `fix:` 提交**只修一个问题**（用户指定的那个），不要把几个 bug 连着修——那样用户
  没法逐个审。提交里可以带为这一个问题服务的东西（用例、文档、记账），不是「除了代码
  什么都不许有」。
- 不推荐把这一串 fix squash 成一笔「pre-format 大提交」：`changes.md` 能补文档，补不回
  `git bisect` 和单点 revert 的能力。（#2 是唯一例外：当时按 amend 并进了 baseline 提交。）

### 顺序

1. `fix: ...` × 6（#3～#6、A2、A3），顺序见上（`#5 → A2 → #6 → #4 → #3 → A3`），
   每笔只修一个问题。**#2** 已并入 baseline 提交、**#7** 已提交，用例都已搬回 live。
2. 白盒清理（B8、B9 的剩余部分），单独一个 `tests:` 提交。
3. 单独一个 `style: apply .clang-format`
   （`amoeba.h`、`lua_amoeba.c`、两个 benchmark；约 1800 行机械改动）。它必须是**唯一一次
   机械重排**：`git diff -w` 为空，且之后不再有语义改动混进来。
4. 格式提交落地后：把它的 sha 写进 `.git-blame-ignore-revs`，再把 CI 的 `format-check`
   从 advisory 改成 blocking（见下面 B1）。

---

## A. P0：正确性

### A1. ~~失败的约束仍然不能直接重试~~（已满足，不是待办；2026-11-02 复核）

- 结论：HEAD 上 `am_add()` 失败会走 `am_remove_errors()` 清掉 `marker`/`other`
  （`amoeba.h:985-986`），再调 `am_add()` 返回 `AM_UNSATISFIED` 而不是 `AM_FAILED`，
  此时 `am_hasconstraint()` 为 0。本条的「现状」和 `notes/api-contract.md §6` 都是旧描述。
- 别和 #2 混淆：只有被 `am_setstrength()` 偷偷加入之后，`am_add()` 才返回 `AM_FAILED`
  （约束已在 solver 里）。那是 #2，不是本条。
- 可选（P1，防回归，不是修 bug）：补一条 `failed_add_can_be_retried`，钉住
  「失败 → 重试 → 仍 `AM_UNSATISFIED`」。

### A2. `am_load()` 的符号索引仍然没有上界

- 现状：arena 大小和符号索引**都**没校验（arena 那条是 #5）。row 里的 `value >> 2`
  索引不校验上界，伪造的行会指向不存在的变量；实测：一条行键是未声明的 external id 的流，
  `am_load()` 返回 `AM_OK`，之后任意 `am_add()` 在 `am_markdirty()` 的 `assert(ve != NULL)`
  处 abort（release 下是 NULL 解引用）。
- 做法：`am_readrow`/`am_readmarker` 检查 `(value >> 2) < total`；与 #5 一起定「合法流」。
- 验收：伪造 id 超界的 stream 返回 `AM_FAILED`。

### A3. dump/load 加 magic + 版本号

- 现状：没有任何版本信息，换个版本读写就是静默错误。
- 做法：头部加 4 字节 magic + 1 字节版本；只在版本匹配时接受。
- 验收：旧格式/错误 magic 明确 `AM_FAILED`；`load_rejects_short_arena` 改成用带 magic 的流。

### A4. 工作区清理（需要你确认后再删）

- [x] `.gitignore` 补 `AGENTS.md`、`notes/`、`tests/build/`、`tests/*.gen.inc`、产物
- [ ] 根目录的 `amoeba.old.h` / `amoeba.v3.h` / `amoeba.v4.h` / `amoeba.work*.h` /
      `amoeba.split.h` / `amoeba.h.new`：留档还是删除，你定。
- [ ] `.travis.yml` 删除（内容已被新 CI 覆盖）。

---

## B. P1：健壮性与工程化

### B1. CI 的格式门禁

- 现状：`just format-check` 在 CI 里是 `continue-on-error: true`，因为工作流的
  clang-format（发行版）和本地 23.1.4 可能不一致（配置用了 19+ 的 key）。
- 做法：锁定版本（CI 装固定版本，或改用 `pip install clang-format==<ver>`），然后改成 blocking。
- 验收：故意打乱一个空格，CI 变红。

### B2. Windows / MSVC 的测试路径

- 现状：`tests/justfile` 的 recipe 是 bash；Windows recipe 只 echo 一句「需要 POSIX shell」。
- 做法：仿 znet 的 `tests/run.bat` + `check.bat`，至少覆盖 `/W4 /WX` 的编译检查。
- 验收：GitHub Actions 的 windows job 能跑 `just check-msvc`。

### B3. dump 的双精度保真

- 现状：`am_writefloat()` 永远写 float32，`AM_USE_DOUBLE` 下 dump/load 有精度损失
  （`check-double` 能过是因为断言用相对误差）。
- 做法：编码上区分 f32/f64（新增 `0xCB`），或者 `am_Num` 宽度写进版本头。
- 验收：double 模式下 dump→load 的变量值与原始值逐位相等。

### B4. Lua 绑定

- 现状：`just test-lua` 只跑 `test.lua` 这条 smoke（无断言），也没有 rockspec。
- 做法：把 `test.lua` 里的检查改成断言（或引入 `luaunit.lua` 那种自带断言），加 rockspec。
- 验收：绑定层回归能被 CI 抓到。

### B5. `am_flexbox.h` 决策

- 现状：未跟踪的草稿，`struct am_Node {}` 空，约 25 个声明无实现，没有被任何构建引用。
- 做法：删掉，或按 D 节的需求重新设计（不要从这个空壳继续）。
- 建议：先删。

### B6. `assert` 承担安全职责的地方

- 现状：`am_mapid()`（dump 侧）只有 `assert(id != NULL)`，release 构建里是 NULL 解引用。
- 做法：改成返回错误，让 `am_dump()` 返回 `AM_FAILED`；顺手扫一遍「`assert` 里做参数校验」的位置。

### B7. `tests/*.gen.inc` 的生成依赖

- 现状：跑测试需要 `lua`（`misc/gen_entries.lua`），CI 里要额外装 lua5.4。
- 做法：要么接受（现状，和 znet 一致），要么预生成并提交（省一个依赖，但多一步同步）。
- 验收：随便选一个，并在 notes/testing.md 写清楚。

### B8. live 测试里的白盒调用

- 现状：`tests/solver_test.c` 的调试打印机 `am_dumpsolver()` 直调 `am_itertable()` /
  `am_nextentry()` / `am_approx()`；`tests/api_test.c` 的 `symbol_fits_one_unsigned` 直调
  `am_null()`。按 AGENTS.md §8，这些都属于「测试里调用 static 函数」，锁不住任何契约。
- 做法：打印机改成直接读 `row->terms.keys/vals`、`S->rows.keys/vals`（读结构体是允许的）；
  `am_null()` 换成 `am_Symbol s = {0, 0};`。
- 验收：`grep -rn 'am_itertable\|am_nextentry\|am_approx\|am_null' tests/*.c`
  只剩注释；`just check` 仍绿。

### B9. `tests/pending/` 快照的回收

- 现状：`tests/pending/{dump_test,api_test}.c` 是从 live 摘出来的快照，不参与构建：
  `dump_test.c` 里有两块被移出的白盒 coverage 和 #3 的值断言；`api_test.c` 里有
  #5/#6 的回归用例（#2、#7 已搬回 live）。
- 做法：每修完一个 #N，只把**那一个**用例按当时的实现搬回 live；coverage 块改写成
  `am_dump()` 的 writer 回调 + `am_load()` 的 arena 之后再考虑回收。
- 验收：pending 目录最终清空（或只剩明确标注的历史档）；每次搬回都跑 `just check`。

### B10. 覆盖率口径：`-DNDEBUG`

- 现状一：`just cov`（`tests/justfile`）的编译行是 `{{CC}} {{BASE}} -g -O0 --coverage`，
  **没有** `-DNDEBUG`，所以 assert 的行/分支会进统计。例如 `am_setstrength()` 里只在
  「回滚失败」时才触发的那句 `assert(ra == AM_OK)`，永远会被报成「分支未覆盖」——但按仓库
  口径，**单测覆盖率应在 `-DNDEBUG` 下测**，assert 本来就不该被要求覆盖（当前实测
  `DA:1050,3` / `BRDA:1050,0,1,0`，是带 assert 编译出来的数据）。
- 现状二：直接加 `-DNDEBUG` 现在**编不过**：`tests/solver_test.c` 有 8 处
  `-Werror,-Wunused-but-set-variable`（变量只被 `assert(...)` 用，例如 `:59 result`、
  `:87 ret`、`:322 nResult`、`:686 ret`）。`dump_test.c` / `api_test.c` 能编过。
- 现状三：tests 里共 **91 个裸 `assert(`**（`solver_test.c` 80、`dump_test.c` 11），NDEBUG
  下会静默消失：覆盖率照样收集，但那次运行不再校验结果。harness 自己的
  `asserteq` / `assertok` / `assertstreq` 不依赖 `assert`，NDEBUG 下仍然生效。
- 做法：
  1. 把 tests 里承载检查的裸 assert 换成 harness 宏（顺带消掉那 8 个 unused 变量）；
  2. 给 `cov` 的编译行加 `-DNDEBUG`；
  3. 把「覆盖率用 NDEBUG 口径、assert 不要求覆盖」写进 [notes/testing.md](notes/testing.md)。
- 验收：`just cov` 在 `-DNDEBUG` 下编过并收集；`tests/build/amoeba.info` 里不再出现 assert
  行的统计；覆盖率运行仍能靠 harness 宏发现失败（故意改坏一个断言，cov 运行要打印 FAILED）。
- 备注：`just uncovered`（`tests/justfile:236`）现在靠「跳过含 assert 的行」兜底；做完这条
  之后这个启发式可以保留，但不再必需。
- 规模：`tests:` 类改动，涉及 `tests/justfile` + 两个 suite 的断言写法；建议单独一笔。

---

## C. P2：能力与质量

### C1. 内省 API（对 GUI 极其重要）

- [ ] `am_constraint` 只读访问器：`am_consrelation` / `am_consstrength` / `am_consiter`（遍历项）
      / `am_consname`；以及 `am_consrow`（dump 出来的原始表达式）。
- [ ] `am_variter`：遍历 solver 里现存的变量。
- [ ] 有了这些，D 节的 Demo 才可能「从 solver 反查约束」，而不必自己复制一份。
- 验收：不定义 `AM_IMPLEMENTATION` 也能写出一个打印所有约束的程序。

### C2. 约束违反度

- [ ] `am_conserror(cons)`：返回当前解下该约束的误差量（非必需约束看 `ERROR` 变量的行常数；
      必需约束返回 0 或 NaN 表示不可判定）。UI 需要它来把「被违反的约束」标红。
- 验收：软约束 `x==100` 在 `x` 被强制为 0 时返回约 100。

### C3. `am_dump` 的副作用

- 现状：`am_dump()` 会 `am_clearedits()`（丢掉所有 suggest/edit）——已在
  notes/dump-format.md §7 写明，但接口本身仍然有副作用。
- 做法：要么改名/加参数表达「会重解」，要么先 dump 再恢复 edits（需要保存 edit 状态）。
- 验收：想清楚后写进 README；目前**至少不要**在拖拽过程中 dump。
- （magic/版本号 见 A3；双精度保真 见 B3。）

### C4. 数值

- [ ] `am_approx` 增加相对误差项（大坐标场景下绝对 eps 失效，见 notes/algorithm.md §8）；
- [ ] 评估把 `AM_NUM_EPS` 改成按 `am_Num` 的 `FLT_EPSILON * 大系数` 派生；
- [ ] 补一个「大坐标」测试（1e5 / 1e6 量级）。

### C5. 性能

- [ ] 把 `enaml_like_benchmark` 的基线数据写进 README（现在只有「比 kiwi 快」的定性说法）；
- [ ] 加一个 `bench` CI job（阈值放宽，只防性能悬崖）；
- [ ] `yoga_benchmark.cpp` 依赖外部 yoga，建议加个「未安装则跳过」的检测，别让 `make` 挂掉。

### C6. 文档

- [ ] 从注释生成 API 文档（`amoeba.h` 的注释已经够写 doxygen）；
- [ ] README 补一节「线程模型 / 可重入性」；
- [ ] README 补一节「`AM_USE_DOUBLE` 与精度」；
- [ ] 补一页「从 kiwi/enaml 迁移」的对照表（用户最容易问的）。

---

## D. 可视化约束排版 Demo（大方向，待开工）

### D0. 目标

做一个**基于约束的所见即所得排版工具**原型：

1. 在面板里任意创建 view、任意创建约束；
2. 主画布上拖动 view 改位置、拖 handle 改大小，**其它 view 的位置/大小实时按约束重算**；
3. 每条约束在画布上显示成**双向箭头**；点击箭头 → 跳到面板里对应的约束条目（并高亮）；
4. 约束可以**分组、折叠**；分组和单条约束都有**显示开关**（只影响绘制，不影响求解）；
5. 文档可以**保存成文件 / 从文件载入**。

目标不是做产品，而是**验证 amoeba 的表达能力与交互手感**，同时得到一个能对外展示的 demo。

### D1. 需求拆解（功能清单）

**文档模型（source of truth）**

- [ ] `View { id, name, 变量: left/top/width/height, (可选) parent, style }`
- [ ] `Constraint { id, group, enabled, strength, relation, terms[], constant, 人类可读文本 }`
- [ ] `Group { id, name, collapsed, visible }`
- [ ] 序列化：JSON（人可读、可 diff、可手写）；`am_dump` 只当缓存，见 notes/dump-format.md §8

**画布**

- [ ] 命中测试、拖动移动（8 个 resize handle + 内部拖动）
- [ ] 拖动时只做 `am_suggest()`，松手后把结果固化（见 D3 的决策）
- [ ] 约束箭头：端点吸附到相关 view 的边，双向箭头 + 标签（`强度/关系`）
- [ ] 选中态：选中约束 ↔ 高亮箭头（双向联动）
- [ ] 求解失败（`AM_UNSATISFIED`/`AM_UNBOUND`）时红框提示并**回滚该次编辑**

**面板**

- [ ] view 列表（新建/删除/重命名/显隐）
- [ ] 约束列表（按组折叠；每条显示表达式、强度、启停开关、可见开关）
- [ ] 约束编辑器：选变量（view.边）× 系数 + 关系 + 常数 + 强度档位
- [ ] **约束模板**（这是体验关键，纯手工写方程没人受得了）：
      左对齐 / 右对齐 / 顶对齐 / 等宽 / 等高 / 间距相等 / 水平居中 / 垂直居中 / 吸附父容器边距
- [ ] 从「当前选中」直接生成模板约束（选两个 view → 弹出可用的对齐方式）

**持久化**

- [ ] `保存/另存/打开`、最近文件、脏标记提示
- [ ] 版本号字段（未来迁移用）

**加分项**

- [ ] undo/redo（doc 是纯数据 → 命令模式，成本低收益高）
- [ ] 导出 SVG / PNG
- [ ] 约束覆盖率提示（哪些 view 没有任何约束 → 会到处乱跑）

### D2. 技术选型：我的判断

先说结论：**先用 Lua + LÖVE 做语义原型（1-2 天），再用 C++ + Dear ImGui + SDL2/GLFW 做正式
版本并 emscripten 出 wasm。** 如果你坚持「纯 C、单头、和库保持一致」，那用 nuklear.h + SDL2，
但要有心理准备：nuklear 只帮你做 20% 的 UI。

| 方案 | 优点 | 缺点 | 结论 |
|---|---|---|---|
| **Lua + LÖVE2D**（用仓库自带 `amoeba.lua`） | 拖拽/绘制/即时反馈零成本；改一行就能跑；能直接用纯 Lua 版验证「约束语义对不对」 | 不是最终形态；性能一般 | **推荐做第一版原型** |
| **C++ + Dear ImGui + SDL2/GLFW** | 面板树/折叠/输入框/文本/颜色全现成；`ImDrawList` 画箭头/曲线/手柄很顺手；emscripten 直接出 web demo；社区活跃 | 引入 C++（与 Clean C 原则冲突，但 Demo 可以独立目录）；二进制偏大 | **推荐做正式版** |
| **nuklear.h + SDL2** | 单头 C、与项目哲学一致、能编到 wasm | 项目已基本停更；控件与交互体验偏 2000 年代；你的需求 80% 是自绘画布，nuklear 帮不上 | 备选（若要「纯 C」） |
| **纯 C + SDL2 自绘** | 依赖最少，完全可控 | 按钮/输入框/折叠/滚动都要自己写，至少多花一周 | 不推荐 |
| **TypeScript 重写求解器** | web 生态最舒服 | 违背「验证 amoeba」的初衷，等于换个项目 | 不推荐 |

理由展开（为什么不是 nuklear）：你的核心界面是「一块自绘画布 + 一个属性面板」。画布部分
（箭头、手柄、命中测试、吸附）任何 UI 库都帮不上，必须自绘；面板部分 ImGui 的
`CollapsingHeader` + `InputFloat` + `Selectable` 能直接满足「分组折叠 + 约束条目 + 参数编辑」。
nuklear 也能做，但要花更多时间调样式和交互，而它的维护状态是个长期风险。**「单头 C」是
amoeba 的卖点，不必是 demo 的枷锁**——demo 放独立目录、独立构建，反而不污染库。

### D3. 架构与关键决策

```
        ┌──────────────────────────────┐
        │  doc（views / constraints /  │  ← 唯一的 source of truth
        │       groups，纯数据）        │     保存/载入 JSON
        └──────────────┬───────────────┘
                       │ build（先全量重建，超过阈值再优化）
        ┌──────────────▼───────────────┐
        │  solver（amoeba）             │
        │  变量: v.left/top/w/h ...     │
        │  约束: doc 里的约束 + 内建约束 │
        └──────────────┬───────────────┘
                       │ am_updatevars / 读绑定值
        ┌──────────────▼───────────────┐
        │  render（立即模式）            │
        │  画矩形 / 手柄 / 箭头 / 面板   │
        └──────────────────────────────┘
```

必须现在想清楚的四个决策：

1. **约束的 source of truth 是 `doc`，不是 solver。**
   amoeba 没有内省 API（见 C1），`am_dump` 也不保存分组/名字/启停。所以应用层必须自己维护
   一份约束数据，solver 只是「求值器」。好处：约束的显示、分组、开关、序列化全在 doc 层，
   将来加 C1 的 API 也只是可选优化。
2. **拖动用 `am_suggest`，不要增删约束。**
   `am_suggest` 走脏行缓存（notes/algorithm.md §6），是唯一能做到「每帧重解」的路径；
   拖动期间**绝对不要** `am_add`/`am_remove`（会让缓存失效、`S->age` 变化）。
   强度用 `AM_STRONG`（拖动变量）或 `AM_MEDIUM`（普通 nudge），别用 `AM_REQUIRED`——
   `am_addedit` 会把 REQUIRED 夹到 STRONG，而且 REQUIRED 的 suggest 会让布局无法回弹。
3. **松手之后怎么办？三种语义，选一种并写进文档：**
   - (a) 松手即 `am_deledit()`，view 弹回约束决定的位置（最"约束派"，但手感像拉橡皮筋）；
   - (b) 松手保留 suggest（位置被"手动覆盖"，直到重建 solver）——最像 Figma；
   - (c) 松手把偏移量写成一个 `AM_WEAK` 的 nudge 约束存进 doc（可持久化、可撤销）。
     **建议 (c)**：所见即所得 + 可保存 + 不破坏约束体系（弱约束让真正的强约束依然生效）。
4. **求解失败怎么办？**
   `am_add` 失败后约束处于「不能直接重试」的状态（notes/algorithm.md §3）。
   策略：任何一次 doc 变更都在「临时 solver」上试算；失败则丢弃、回滚 doc、弹出原因。
   禁止把「半失败」的 solver 留在界面后面。

还有一个容易被忽略的点：**内建约束**。父容器的边、view 的 `right = left + width`、
`bottom = top + height` 这类结构约束不属于用户文档，应该由 doc → solver 的构建过程自动生成，
并在 UI 里默认隐藏（可以提供一个「显示内建约束」的 debug 开关）。

### D4. 里程碑

| 阶段 | 内容 | 预估 | 验收 |
|---|---|---|---|
| **M0** | 空窗口 + 3 个硬编码 view（用 amoeba 求解）+ 画矩形 | 0.5 天 | 窗口里能看到被约束排好的 3 个矩形 |
| **M1** | 拖动移动 + 8 个 handle 缩放 + 实时重解 | 1-2 天 | 拖动 A 时 B/C 按约束跟随，无明显延迟（60fps） |
| **M2** | 面板创建 view / 创建约束 + 约束箭头 + 点击箭头跳转条目 | 2-3 天 | 「面板改约束 → 画布立即变」闭环可用 |
| **M3** | 分组折叠 + 显示开关 + JSON 保存/载入 | 1-2 天 | 存盘重启后完全还原（含分组折叠状态） |
| **M4** | 模板约束 + 失败提示 + 撤销 + wasm 发布 | 2-3 天 | 浏览器里可玩 |

M0-M1 建议先在 Lua 原型里验证（一天内就能看到拖动重算），确认手感后再进 M2。

### D5. 风险与对策

| 风险 | 对策 |
|---|---|
| amoeba 无内省，UI 无法反查约束 | doc 层自己存一份（D3 决策 1） |
| 求解失败后约束不可重试 | 编辑走「临时 solver 试算 + 回滚」（D3 决策 4） |
| 拖动时抖动/震荡 | 强度分级：拖动变量 STRONG、nudge WEAK、真正布局约束 REQUIRED；必要时加「拖动期间锁定结构约束」 |
| 坐标精度（像素 + float32） | 用**相对坐标/归一化坐标**存储，渲染时再乘缩放的画布尺寸；或直接开 `AM_USE_DOUBLE` |
| `am_load` 读不可信文件不安全 | 文档用 JSON，二进制 dump 只作本地缓存 |
| 约束越多越慢 | 先全量重建（< 200 条约束时够快，需用 M1 实测确认），再考虑增量 |

### D6. 开工前需要你拍板的三件事

1. **正式版语言**：接受 C++ + Dear ImGui（推荐），还是坚持纯 C + nuklear？
2. **松手语义**：D3 决策 3 的 (a)/(b)/(c)，我推荐 (c) 写回弱约束。
3. **交付形态**：先出 native 桌面 demo，还是一开始就 `emscripten` 出 web/wasm？
   （建议：native 优先，wasm 是 M4 的打包问题，不影响架构。）

---

## E. 明确不做 / 已决定

- **不**把 `amoeba.h` 拆成多文件，**不**去掉 Clean C 约束（这是项目的身份）。
- **不**为 demo 引入的依赖放进库本体（demo 独立目录、独立构建）。
- **不**尝试让 `am_dump` 变成通用文档格式（它是 tableau 快照，见 notes/dump-format.md §8）。
- **暂不**支持非线性约束（`max`/`min`、比例宽高在 Cassowary 里需要辅助变量或不等式近似）；
  Demo 里用「等宽/等高/固定比例」这类可线性化的模板代替。
