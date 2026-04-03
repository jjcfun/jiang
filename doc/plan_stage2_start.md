# Jiang `compiler/` Stage2 Placeholder Plan

## Summary

当前 `compiler/` 不再承载 Stage1 后期主线实现，而是明确收回为未来 Stage2 的预留目录。

当前固定职责：

- `src/`: 宿主 C 编译器
- `bootstrap/`: 当前 Stage1 Jiang 自举编译器主线
- `compiler/`: 未来 Stage2 的预留目录

这次收口的目标不是启动 Stage2，而是避免 Stage1 与未来 Stage2 在同一时间并行生长。

## Current Decision

- `compiler/` 目录名继续长期保留
- 当前不在 `compiler/` 中推进真实实现
- 当前不把 `compiler/` 纳入正式回归主线
- 等 `bootstrap/` 真正完成接管后，再决定 Stage2 从哪个入口恢复实现

## Current Non-Goals

- 不复制整份 `bootstrap/` 到 `compiler/`
- 不在本阶段继续孵化新的 `compiler/*.jiang` 骨架
- 不把 Stage2 与默认后端切换绑定推进

## Acceptance

- 仓库文档明确 `bootstrap/` 是 Stage1 主线
- `compiler/` 只保留占位说明，不再暗示当前主线实现
- `script/test.sh` 不再把 `compiler/` 当作当前正式回归的一部分
