# Backend 设计

backend 消费 elaborated MIR 和 layout，生成目标产物。当前 LLVM backend 放在 `backend/llvm`；
LLVM-specific lowering 不写进 MIR 或 layout。

## 输入

- MIR function bodies。
- `LayoutStore` 中的 concrete type layout。
- monomorph `MonomorphStore`。
- target 配置。
- root package import closure 中所有 reachable concrete functions。

## LLVM 关系

LLVM IR 是 backend 产物，不是 MIR 的替代品。MIR 保留 Jiang 语义、控制流、place、local 和
类型事实引用；LLVM lowering 负责把这些语义映射到 LLVM type、value、basic block 和 symbol。

backend-specific symbol/mangling 从 `BackendSymbolKey` 派生。key 包含：

- package index
- module index
- function `DefId`
- concrete type args
- runtime entry flag

这层 symbol 只服务目标代码生成，不应该反向写入 MIR。普通 Jiang 函数会 mangling 成
`_Jp<package>_m<module>_f<def>` 形态，并在泛型实例后追加 concrete type args。这样不同
package/module 中同名函数不会在 LLVM module 里冲突。

只有 runtime entry wrapper 输出 C ABI `main`。language main 的选择只发生在 root package
root module 中；dependency package 即使也定义 `main`，也只是普通 Jiang 函数。

LLVM 对接通过 `backend/llvm/ffi.jiang` 的最小 C binding 完成。不要一次性封装完整
LLVM API；每个 lowering/emission 任务只补当前需要的少量 FFI 声明。
`backend/llvm` 直接调用这层 FFI，不维护自定义 LLVM IR 中间模型。

## C ABI classifier

`backend/abi.jiang` 是 backend-independent 的 C ABI classifier。它消费 semantic `TypeId`
和 `LayoutStore`，把函数返回值和参数分类为：

- `none`：zero-sized / unit / never，不进入 ABI 参数列表。
- `direct`：标量、handle、function pointer 和小 aggregate，直接映射成 LLVM value。
- `indirect`：大 aggregate 通过 hidden pointer 传递；参数使用 `byval`，返回值使用 `sret`。

`backend/llvm/abi.jiang` 只负责把这个 plan 翻译成 LLVM function type 和 `byval`/`sret`
attribute。LLVM declaration 和 call site 必须使用同一个 plan。

## 边界

- backend 不重新 type check。
- backend 不从 MIR 自己推导 field offset；field offset 只能来自 `LayoutStore`。
- backend ABI classifier 可以读取 `LayoutStore` 的 size/align，但不能修改 layout facts。
- backend 不修改 HIR、MIR 或 `TypeCheckStore`。
- backend 不把 LLVM-specific 表达泄漏到 MIR 数据结构。

## 当前覆盖

- LLVM type lowering。
- C ABI classifier：zero-sized/direct/indirect、`byval` 参数、`sret` 返回。
- function symbol key / mangling，包含 package/module/concrete type args。
- LLVM IR / object file / executable emission。
- target triple / data layout 接入。
- struct/tuple/array aggregate。
- enum/union tag 与 union payload。
- branch、switch、call、return、range/array/slice loop。

## 待设计

- debug info 和 source location。
