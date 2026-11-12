# TODO — amoeba 待做清单

> 本文件只放**未做**的事。已提交 / 已否决 / 已决定不做见 [notes/changes.md](notes/changes.md)；
> 条目 ID 共用命名空间，已关闭的 ID 不再分配新含义。
>
> 当前状态以 `git status` / `git log` 为准；本文件不记录状态，也不规定顺序。优先级是价值判断，
> 依赖是技术事实；先做哪个由用户当次决定。
>
> 基线：2026-11-03（仅用于定位，不是承诺）。HEAD 用 `git log -1` 现算；行号会漂移，按函数名
> 定位；新写的内容不引用行号。

## 0. 优先级总览

- **P1（恢复与健壮性）**：A3、B1、B2、B4、B5、B7。
- **P2（能力与质量）**：C1–C6。
- **P3（清理）**：A4。
- **提案（未批准）**：D 节的可视化 Demo。

## 1. 核心待做表（正确性与 load）

| ID | P | 问题 | 依赖 | 用例 |
|---|---|---|---|---|
| A3 | P1 | dump/load 没有 magic + 版本号 | 无 | 需新建（真 dump） |

## 2. 背景与依赖（事实，不是顺序）

- **根因分组**：`A3` 是流没有版本头。
- **依赖**：
  - `A3`：无前置；继续用「真 dump + 只改目标字段」的策略。
- **A3 的用例策略**：继续用 `am_dump()` 生成真流、只改目标字段；手搓流会和真实 writer 不一致。

## 3. 重点条目（A3 / A4）

### A3. dump/load 加 magic + 版本号

- 现状：顶层是 5 元数组，没有版本信息；换布局会静默误读。
- 候选设计（未落地；msgpack 合法性已验证）：顶层 6 元数组
  `[["amoeba", 1], total, vars, constraints, rows, objective]`。分析见
  [notes/dump-format.md](notes/dump-format.md) §8；上一版里实现过，后随旧提案一起 reset。
- 前置：无；继续用真 dump + 只改目标字段的用例策略。

### A4. 工作区清理（需要用户确认后再删）

- [ ] `archive/` 里的旧头文件 / 草稿（`amoeba.*.h`、`demo.c`、`example.c`、`test_*.c`）：
      留档还是删除，你定。
- [ ] `.travis.yml` 删除（内容已被新 CI 覆盖）。

## 4. 健壮性与工程化（B1、B2、B4、B5、B7）

### B1. CI 的格式门禁

- 现状：`just format-check` 在 CI 里是 `continue-on-error: true`，因为工作流的 clang-format
  （发行版）和本地 23.1.4 可能不一致（配置用了 19+ 的 key）。
- 做法：锁定版本（CI 装固定版本，或改用 `pip install clang-format==<ver>`），然后改成 blocking。
- 验收：故意打乱一个空格，CI 变红。
- 前置：唯一的 `style:` 提交落地——已满足（`50ab0a9`），可以直接做版本锁定。

### B2. Windows / MSVC 的测试路径

- 现状：`tests/justfile` 的 recipe 是 bash；Windows recipe 只 echo 一句「需要 POSIX shell」。
- 做法：仿 znet 的 `tests/run.bat` + `check.bat`，至少覆盖 `/W4 /WX` 的编译检查。
- 验收：GitHub Actions 的 windows job 能跑 `just check-msvc`。

### B4. Lua 绑定

- 现状：`just test-lua` 只跑 `test.lua` 这条 smoke（无断言），也没有 rockspec。
- 做法：把 `test.lua` 里的检查改成断言（或引入 `luaunit.lua`），加 rockspec。
- 验收：绑定层回归能被 CI 抓到。

### B5. `am_flexbox.h` 决策

- 现状：`archive/am_flexbox.h` 是未实现的草稿，`struct am_Node {}` 空，约 25 个声明无实现，
  没有被任何构建引用。
- 做法：删掉，或按 D 节的需求重新设计（不要从这个空壳继续）。
- 建议：先删。

### B7. `tests/*.gen.inc` 的生成依赖

- 现状：跑测试需要 `lua`（`misc/gen_entries.lua`），CI 里要额外装 lua5.4。
- 做法：要么接受（现状，和 znet 一致），要么预生成并提交（省一个依赖，但多一步同步）。
- 验收：随便选一个，并在 notes/testing.md 写清楚。

## 5. 能力与质量（C1–C6）

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
- （magic/版本号 见 A3。）

### C4. 数值

- [ ] `amN_approx` 增加相对误差项（大坐标场景下绝对 eps 失效，见 notes/algorithm.md §8）；
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

## 6. 可视化约束排版 Demo（提案，未批准）

> 这一节是**提案**，不是已批准的计划。开工前先按 AGENTS.md §1 让用户确认范围。

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
- [ ] 序列化：JSON（人可读、可 diff、可手写）；`am_dump` 只当缓存，见
      notes/dump-format.md §9

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
   - (a) 松手即 `am_deledit()`，view 弹回约束决定的位置（最「约束派」，但手感像拉橡皮筋）；
   - (b) 松手保留 suggest（位置被「手动覆盖」，直到重建 solver）——最像 Figma；
   - (c) 松手把偏移量写成一个 `AM_WEAK` 的 nudge 约束存进 doc（可持久化、可撤销）。
     **建议 (c)**：所见即所得 + 可保存 + 不破坏约束体系（弱约束让真正的强约束依然生效）。
4. **求解失败怎么办？**
   任何一次 doc 变更都在「临时 solver」上试算；失败则丢弃、回滚 doc、弹出原因。
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
| 求解失败后约束不可重试 | 编辑走「临时 solver 试算 + 回滚」（D3 决策 4）；失败后先 `am_remove()`/调整再重加 |
| 拖动时抖动/震荡 | 强度分级：拖动变量 STRONG、nudge WEAK、真正布局约束 REQUIRED；必要时加「拖动期间锁定结构约束」 |
| 坐标精度（像素 + float32） | 用**相对坐标/归一化坐标**存储，渲染时再乘缩放的画布尺寸；或直接开 `AM_USE_DOUBLE` |
| `am_load` 读不可信文件不安全 | loader 只保证 load 期结构校验、`am_load()` 本身不 crash；非 dump 数据 load 后的正确性/后续 crash 不保证，A3 也没有 magic/version。**不要** load 不可信文件；文档用 JSON，二进制 dump 只作本地缓存 |
| 约束越多越慢 | 先全量重建（< 200 条约束时够快，需用 M1 实测确认），再考虑增量 |

### D6. 开工前需要你拍板的三件事

1. **正式版语言**：接受 C++ + Dear ImGui（推荐），还是坚持纯 C + nuklear？
2. **松手语义**：D3 决策 3 的 (a)/(b)/(c)，我推荐 (c) 写回弱约束。
3. **交付形态**：先出 native 桌面 demo，还是一开始就 `emscripten` 出 web/wasm？
   （建议：native 优先，wasm 是 M4 的打包问题，不影响架构。）

## 7. 相关

- 已提交 / 已否决 / 已决定不做：见 [notes/changes.md](notes/changes.md)。
- 当前状态：`git status` / `git log`；本文件不记录状态。
