# Jiang `compiler/` 主线启动计划

## Summary

目标是在当前 Stage1 已达到“可启动下一代 Jiang 编译器主线”的成熟度后，为 `compiler/` 建立长期目录和实现入口。

本阶段不是直接宣布进入 README 定义里的正式 Stage2，也不是立刻切默认后端；重点是给 `compiler/` 一个稳定、长期不会过时的根目录和实现边界。

默认决策：

- `compiler/` 作为长期目录名继续保留
- Stage1 继续保留在 `src/` 与 `bootstrap/`
- 同仓开发，不拆新仓库
- LLVM 继续作为可选完整后端，不在本阶段强制切默认

完成标准：

- `compiler/` 目录存在并有明确职责文档
- `compiler/compiler.jiang` 与最小前端链模块已经建立
- 仓库文档明确 `src/`、`bootstrap/`、`compiler/` 的职责边界
- `compiler/` 主线的第一批实现目标收口成明确顺序

## Key Changes

### 1. 固定目录边界

目录分工固定为：

- `src/`: 当前宿主 C 编译器
- `bootstrap/`: Stage1 Jiang 自举资产与回归面
- `compiler/`: Stage1 后期用于孵化下一代 Jiang 编译器的长期目录

### 2. `compiler/` 第一批目标

启动顺序固定为：

1. `compiler.jiang`
   固定当前主入口与最小 smoke 链路
2. `token.jiang` / `lexer.jiang` / `parser.jiang`
   先把最小前端链跑起来
3. `ast.jiang` / `semantic.jiang` / `hir.jiang` / `jir.jiang`
   逐步把 dump 骨架补成真实中间层
4. `c.jiang`
   作为当前最小 `emit_c` 占位层，先让主链产出可读 C 文本

当前先不拆子目录，避免目录设计先于实现。等根层模块长出真实职责后，再按实际边界拆分。

### 3. 当前明确不做

- 不复制整份 `bootstrap/` 到 `compiler/`
- 不立刻切默认后端
- 不把 README 定义里的正式 Stage2 和新语法特性同时大爆炸推进
- 不在本阶段拆仓库

## Test Plan

本阶段以目录与计划收口为主，不要求新增编译回归。

要求：

- 仓库结构清晰
- 文档与当前实现状态一致
- 不影响现有 `script/test.sh`

## Assumptions

- Stage1 已达到可启动下一代编译器主线的成熟度
- `compiler/` 会长期存在，因此目录名应使用稳定的 `compiler/`，而不是临时的 `stage2/`
- 当前 `compiler/` 仍属于 Stage1 后期主线，而不是正式 Stage2
