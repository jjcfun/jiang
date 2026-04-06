# Jiang Stage1 默认 LLVM 后端评估计划

## Summary

目标不是立刻把 Stage1 默认后端从 `JIR -> C` 切到 LLVM，而是把当前状态收口成“可评估、可比较、可决策”。

当前已知前提：

- `--backend llvm` 已通过完整正向测试集
- `script/bootstrap_llvm_smoke.sh` 已覆盖接近 `stage1_real_entry_smoke.sh` 的 bootstrap real-entry 范围
- `build/run/test --backend llvm` 已可用于 demo 与 bootstrap manifest 路径

本阶段要回答的问题只有一个：

- Stage1 现在是否已经足够成熟到可以把 LLVM 提升为默认后端候选

完成标准：

- 仓库中存在单独的“默认后端切换评估”脚本
- 该脚本会对比 C 与 LLVM 在代表路径上的行为，而不只是验证某一侧单独通过
- `TODO.md` / `doc/develop.md` / `README.md` 的当前优先级更新为“评估默认后端切换”
- `script/test.sh` 继续全通过

默认决策：

- 默认后端暂时仍保持 C
- 本阶段不修改 `jiang.build`
- 本阶段不新增 runtime intrinsic
- 本阶段不把 LLVM 切成默认行为

## Key Changes

### 1. 固定“默认 LLVM 后端候选”的评估标准

只有当下面这些条件同时成立，才值得考虑切默认后端：

- 正向测试集在 C 和 LLVM 下都全通过
- build-system 的默认 target / 额外 target / test target 在 C 和 LLVM 下都成立
- bootstrap 的代表入口在 C 和 LLVM 下都成立
- 关键代表入口的运行输出在两种后端下保持一致

这里的“代表入口”固定为：

- `bootstrap/entries/lexer.jiang`
- `bootstrap/entries/parser.jiang`
- `bootstrap/entries/hir.jiang`
- `examples/build_demo/jiang.build`
- `bootstrap/jiang.build`

### 2. 新增默认后端切换评估脚本

新增脚本：

- `script/evaluate_default_llvm.sh`

要求覆盖：

- 重新构建 `jiangc`
- 运行现有 `script/test_llvm_backend.sh`
- 对比 `bootstrap/entries/lexer.jiang` 在 C 与 LLVM 下的运行输出
- 对比 `bootstrap/entries/parser.jiang` 在 C 与 LLVM 下的运行输出
- 对比 `bootstrap/entries/hir.jiang` 在 C 与 LLVM 下的运行输出
- 对比 `examples/build_demo/jiang.build` 在 C 与 LLVM 下 `build/run/test`
- 对比 `bootstrap/jiang.build` 在 C 与 LLVM 下的 build 产物运行输出

脚本输出应该明确给出结论：

- “LLVM default candidate evaluation passed”

### 3. 更新当前文档优先级

把当前主线从“继续扩大 bootstrap LLVM 覆盖面”切换为：

- “评估是否把 LLVM 提升为 Stage1 默认后端候选”

文档要明确：

- bootstrap LLVM 收口已完成
- 当前不再优先继续扩更多 wrapper smoke
- 下一阶段是做默认后端切换的证据收集，而不是立即切换

### 4. 不提前做的事情

本阶段明确不做：

- 不把默认后端立即切成 LLVM
- 不删除 C 后端
- 不引入 LLVM 优化 pipeline
- 不继续无边界扩大 bootstrap LLVM smoke
- 不开始 Stage2 自举

## Test Plan

必须通过：

- `bash ./script/evaluate_default_llvm.sh`
- `bash ./script/test.sh`

`evaluate_default_llvm.sh` 至少验证：

- C 与 LLVM 在代表 bootstrap 入口上的输出一致
- C 与 LLVM 在 demo manifest 路径上都能 `build/run/test`
- C 与 LLVM 在 bootstrap manifest 路径上都能 `build` 并运行代表产物

## Assumptions

- 默认后端仍是 C
- LLVM 后端已是可选完整后端
- 本阶段的目标是判断“能否切”，不是“现在就切”
- 如果评估脚本通过，只说明 LLVM 已成为默认后端候选，不等于必须立刻切换
