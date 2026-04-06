# Jiang Stage1 Bootstrap Compiler LLVM Plan

## Summary

目标是在现有 `lexer/parser/hir` 已能稳定走 LLVM 的基础上，把 bootstrap LLVM 覆盖继续推进到更接近编译器主链的入口，优先收口：

- `bootstrap/entries/jir.jiang`
- `bootstrap/entries/compiler_source_loader.jiang`
- `bootstrap/compiler_core.jiang`

本阶段不要求完整自举，也不切默认后端。完成标准是这些入口至少能在 LLVM 下完成稳定的 smoke build/run，并且对应输出被接入仓库回归。

## Order

1. `bootstrap/entries/jir.jiang`
2. `bootstrap/entries/compiler_source_loader.jiang`
3. `bootstrap/compiler_core.jiang`

## Scope

本阶段只做：

- LLVM 下的 bootstrap 深入口 smoke
- 对应 golden/脚本更新
- 定位并修正 bootstrap 自身在这些入口暴露出的 parser/HIR/JIR 能力缺口

本阶段不做：

- 默认后端切换到 LLVM
- Stage2 自举
- 新 runtime intrinsic
- `jiang.build` 结构变更

## Test Plan

必须通过：

- `bash ./script/bootstrap_llvm_smoke.sh`
- `bash ./script/stage1_jir_smoke.sh`
- `bash ./script/test.sh`

在完成 `jir` 后，再逐步补：

- LLVM 下的 `entries/compiler_source_loader` smoke
- LLVM 下的 `compiler_core` smoke
