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
- `Vector<T>`：可增长连续缓冲区，支持 `append`、`slice()`、`many_pointer()` 和 `into_slice()`。
  `Vector<T>` 满足 `Contiguous`，其中 `Element == T`；`length()` 表示已初始化元素数量，
  不包含 `capacity()`。`capacity()` 只表示 `Vector` 自己管理的 spare capacity，
  不属于 `Contiguous` 语义。`truncate()`、`clear()` 和 `deinit` 会析构被移出已初始化区间的元素。
  内部 `length` / `capacity` 字段不是公开接口。
- `String`：UTF-8 字节字符串，`bytes()` 返回借用字节视图。
- `StringBuilder`：面向字符串构造的可增长 builder，支持追加字节切片、字符串、整数和浮点值；
  `into_string()` 生成 `String`，`into_slice()` 生成拥有所有权的 `UInt8[]^`。
- `Path` / `PathBuilder`：面向路径文本的 owned path 和 builder。`Path.text()` 返回借用视图，
  `Path.into_slice()` 可转成拥有所有权的字节切片。路径算法仍保留在 `std.path` namespace 下。
- 内建 primitive type 与 trait 的公开入口。optional、errorable、pointer、reference、array 和 slice
  只通过 `T?`、`T@E`、`T^`、`T&`、`T*`、`T[*]`、`T[N]`、`T[]`、`T[:S]` 等表面语法表达，
  compiler-owned constructor 名称不从 `std` re-export。
- `jiang`：Jiang 语言自身的词法和 syntax 辅助 API。当前包括 `std.jiang.syntax.*`、
  `std.jiang.Token`、`std.jiang.Tokenizer` 和 `std.jiang.ident`。
  这些 API 供 compiler 和 lang provider 共享，避免 DSL 从零实现 Jiang-compatible token 和
  syntax tree。

## std.jiang

`std.jiang.syntax` 是 lang provider 的公共 syntax ABI。provider 通过
`std.jiang.syntax.Builder.Any&` 构造 `NodeId` / `Tree`，并用 `std.jiang.syntax.Diagnostic`
报告 syntax 阶段错误。compiler 可以复用这些结构，再在 lang expansion 后转换到内部 AST。
`std.jiang.syntax.Provider` 是 `type = lang` package root `Lang` 需要实现的 trait；compiler
为该类型生成 host dynamic library wrapper，普通用户代码不直接调用 wrapper 符号。
builtin provider 也复用同一套 syntax ABI。当前 inline asm 由编译器内建 provider 实现，
用户源码可写 `#asm { ... }`，需要稳定指向内建实现时可写 `#jiang.asm { ... }`。

`std.jiang.Tokenizer` 是 Jiang 语言 tokenizer 的公共版本。它接受 `std.jiang.syntax.Source`，
每次 `next(builder)` 返回一个 `Token`，并把 lexer 诊断写入传入的 builder。`Token` 不保存
text 或 compiler 内部 symbol id；调用方按 `Token.span` 从 `Source.bytes` 取回文本，并在自己的
symbol store 中 intern。

identifier 判定由 `std.jiang.ident` 提供。ASCII 路径直接判断字节；UTF-8 路径使用 Unicode
`XID_Start` / `XID_Continue`。压缩 XID 表由 `script/gen_unicode_xid.js` 生成到
`std/jiang/text/generated/xid.jiang`，当前以 global array 保存，依赖 MIR 对 global array
动态下标访问的支持。

## 稳定性边界

`std` 当前仍处于 0.x 孵化阶段。模块路径、类型命名和方法集合会随语言功能继续收敛；用户代码应尽量依赖
`import std;` 后的顶层导出，而不是内部文件路径。

`std` 不代表 no-libc/freestanding 已经可用。当前稳定路径仍是 hosted target；inline asm 已作为
builtin provider 提供基础能力，但 freestanding runtime、target runtime object 和 Linux no-libc
静态 executable 仍在后续阶段继续设计。
