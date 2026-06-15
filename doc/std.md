# Std Incubator

0.3.1 开始保留顶层 `std/` 作为内部 std 孵化 package。它的目标不是提供完整标准库，而是让编译器和测试逐步
通过 std 形状使用已经存在的 `system/*` 能力，提前暴露命名和所有权问题。

使用方只应该导入入口 package：

```jiang
import std;
```

不要直接导入 `std/fs.jiang`、`std/io.jiang`、`std/process.jiang` 这类内部文件。当前模块都是薄封装：

- `std/fs.jiang` re-export `system/fs.jiang`
- `std/io.jiang` re-export `system/io.jiang`
- `std/process.jiang` re-export `system/process.jiang`，并把环境变量/可执行文件查找作为 process 相关能力暴露

这些 API 在 0.3.1 不承诺稳定，也不代表 no-libc/freestanding 已经可用。公开 std 之前，应先让内部
编译器代码和 smoke 覆盖一段时间，再根据实际使用收敛命名和模块边界。
