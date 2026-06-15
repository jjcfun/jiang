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

只有 hosted entry wrapper 输出 C ABI `main`。language main 的选择只发生在 root package
root module 中；dependency package 即使也定义 `main`，也只是普通 Jiang 函数。

LLVM 对接通过 `backend/llvm/ffi.jiang` 的最小 C binding 完成。不要一次性封装完整
LLVM API；每个 lowering/emission 任务只补当前需要的少量 FFI 声明。
`backend/llvm` 直接调用这层 FFI，不维护自定义 LLVM IR 中间模型。

LLVM module 必须同时设置 target triple 和 target data layout。data layout 由 LLVM
`TargetMachine` 生成并写回 module，Jiang 侧不手写 macOS/Linux/Wasm/Windows 的 layout 字符串。

## Target 和 System Provider

`driver/target.jiang` 是 target triple 到编译策略的唯一入口。`TargetInfo` 描述：

- LLVM triple、arch、OS、ABI 和 object format。
- system provider kind。
- 默认 linker driver 和 sysroot 发现策略。
- target 是否有 hosted libc/CRT。

`system/startup.jiang` 固定 backend 和 startup object 共享的内部启动符号约定。平台入口可以是
hosted `main(argc, argv)`、no-libc `_start` 或 Wasm/Windows 的专用入口，但语言层入口统一是
`__jiang_main`。平台入口负责初始化 `__jiang_startup_state`，然后调用 `__jiang_main`。
`StartupState` 只保存启动瞬间由平台入口交给语言运行时的初始事实；当前包含
`ProgramArguments`。运行过程中会变化的 process 状态不放进 startup state。

0.3.0 仍保持 macOS hosted release 路径由 `_NSGetArgc/_NSGetArgv` 读取启动参数，因此源码不会
强依赖 0.2.2 backend 无法生成的 `__jiang_startup_state` 读取路径。后续以 0.3.0 release 为
bootstrap anchor 后，`system.process.arguments()` 可以改为直接读取 startup state。

`libc`、`libSystem` 和 POSIX/C ABI 都不是 Jiang 语言语义的一部分。它们只属于 hosted
compatibility provider：

- `system/os/macos.jiang` 是 macOS hosted target provider，可以依赖 libSystem。
- `system/os/linux.jiang` 是 Linux hosted target provider，可以依赖 libc。
- `system/os/macos/libc.jiang` 和 `system/os/linux/libc.jiang` 是 hosted C ABI 边界。
- `src/system/*.jiang` 只 import virtual `./os/provider.jiang`。resolver 根据
  `CompilerContext` 中的 target provider 和 effective link-libc 模式，把它映射到具体 OS provider。
- `system/os/provider.jiang` 只作为 0.2.2 bootstrap shim 存在，默认转发到 macOS hosted provider；
  mapping-aware compiler 在支持的 target 上应该先完成映射，不依赖这个 fallback 文件。
- 0.3.0 不保留可 import 的 `system/os/posix/*` 实现层；POSIX 只作为未来 façade / 语义分组，
  避免把 POSIX 固定成 hosted libc。
- no-libc provider 不能通过 hosted libc ABI 间接依赖 libc；它必须走 syscall、compiler
  intrinsic、inline asm、Wasm host import 或 target runtime object。真实 no-libc 和 inline asm
  后置到自定义 DSL 机制稳定后的 proposal。

`--no-link-libc` 不是单纯少传 linker 参数。pipeline 会先根据 target 判断是否存在 hosted
libc/CRT，再结合用户请求得到 effective link-libc 模式。当前 object/LLVM 输出已经按这个模式选择
`malloc/free` 或 `__jiang_malloc/__jiang_free`；no-libc executable 仍明确诊断为暂不支持。

## 编译模式

backend 当前区分 debug/release：

- debug：object emission 使用 LLVM codegen opt level 0，不跑 module pass pipeline。
- release：object emission 使用 LLVM codegen opt level 2，并在写 object 前通过 LLVM
  `LLVMRunPasses` 运行 `default<O2>` module pass pipeline。

backend profile 由 driver options 统一描述，包含 mode、codegen opt level 和 pass pipeline。
这个 profile 必须进入 object cache key，避免优化策略变化后复用旧 object。

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
- debug/release object emission；release 默认跑 LLVM `default<O2>`。
- target triple / data layout 接入。
- struct/tuple/array aggregate。
- enum/union tag 与 union payload。
- branch、switch、call、return、range/array/slice loop。

## 待设计

- debug info 和 source location。
