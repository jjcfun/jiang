# 标准库孵化文档

顶层 `std/` 是 Jiang 当前的标准库孵化 package。它还不是完整标准库，主要用于把已经稳定下来的
系统能力、内建类型别名和基础容器先放到统一入口下，让编译器源码和语言测试以接近最终用户的方式使用。

普通代码优先导入入口 package：

```jiang
import std;
```

不要直接导入 `std/fs.jiang`、`std/io.jiang`、`std/process.jiang`、`std/vector.jiang`、
`std/string.jiang` 这类内部文件。内部文件路径和模块划分仍可调整。

## 当前入口

`std/std.jiang` 作为入口文件，负责 re-export 当前对外可见的标准库表面：

- `fs`：文件读写和路径相关能力，当前主要 re-export `system/fs.jiang`。
- `io`：标准输入输出能力，当前主要 re-export `system/io.jiang`。
- `process`：进程参数、环境变量和可执行文件查找，当前主要 re-export `system/process.jiang`。
- `Vector<T>`：可增长连续缓冲区，支持 `push`、`slice()` 和 `into_array()`。
- `String`：UTF-8 字节字符串，当前仍处于基础能力阶段。
- 内建类型和 trait 的公开别名：例如 `Option<T>`、`Result<T, E>`、`Box<T>`、`Reference<T>`、
  `Slice<T>`、`SentinelSlice<T, S>`、`RawPointer<T>`、`ManyPointer<T>` 等。
  其中 `Slice<T>` / `SentinelSlice<T, S>` 是 unsized array type 的公开名字；借用 view 需要通过
  `Reference<Slice<T>>` / `Reference<SentinelSlice<T, S>>`，也就是后缀语法 `T[]&` / `T[:S]&` 表达。

## 稳定性边界

`std` 当前仍处于 0.x 孵化阶段。模块路径、类型命名和方法集合会随语言功能继续收敛；用户代码应尽量依赖
`import std;` 后的顶层导出，而不是内部文件路径。

`std` 不代表 no-libc/freestanding 已经可用。当前稳定路径仍是 hosted target；freestanding、inline asm
和 target runtime object 后续在 0.4 之后继续设计。
