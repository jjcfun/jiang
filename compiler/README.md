# Jiang Compiler Stage2 Placeholder

`compiler/` 当前不再承载 Stage1 主线实现。

当前仓库中的目录分工固定为：

- `src/`: 宿主 C 编译器
- `bootstrap/`: 当前 Stage1 Jiang 自举编译器主线
- `compiler/`: 未来 Stage2 的预留目录

这次收口的目标是先把 Stage1 明确固定在 `bootstrap/`，避免同时维护两条 Jiang 编译器主线。

当前 `compiler/` 故意保持为空壳目录，只保留占位说明；等 `bootstrap/` 真正完成接管后，再从这里重新启动 Stage2。
