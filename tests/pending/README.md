# tests/pending — 排期素材（不参与构建）

本目录不在 `tests/justfile` 的 `SUITES` 里，`just check` / `just test` / `just cov` 都不会
编译这里的文件；它们只是把从 live 测试里摘出来的内容留在项目里，等排期处理。

- `dump_test.c` — `tests/dump_test.c` 的快照，多出两块从 live 测试移出的**白盒 coverage
  块**（直调 `am_writeuint32()` / `am_readuint32()` / `am_readfloat()` 等 `static` helper），
  以及 TODO #3 的值断言（`assertclose()`，等 `am_load()` 刷新绑定修好后启回 live）。
- `api_test.c` — `tests/api_test.c` 的快照，多出 TODO #5/#6/#7 的回归用例：
  `load_rejects_short_arena`、`addterm_refcount_balances`、
  `mergeconstraint_refcount_balances`、`setstrength_tighten_failure_restores`。
  （#2 的 `setstrength_unadded_does_not_register` 已随修复搬回 live。）

快照是历史状态，允许引用已经不存在或签名已变的内部函数（例如 `am_readraw_slow()` 已拆成
`am_readraw_slow16/32()`），所以**不要直接编译，也不要直接覆盖 live 测试**。搬回去时按当时
的实现和 AGENTS.md §8 的 harness 规范重写：白盒 coverage 块改成走 `am_dump()` 的 writer
回调 / `am_load()` 的 arena，回归用例跟着各自的修复提交。

排期见 [TODO.md](../../TODO.md)。
