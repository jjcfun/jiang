# Backend 设计

backend 消费 elaborated JIL 和 layout，生成目标产物。当前 LLVM backend 放在 `backend/llvm`；
LLVM-specific lowering 不写进 JIL 或 layout。

## 输入

- JIL function bodies。
- `LayoutStore` 中的 concrete type layout。
- monomorph `MonomorphStore`。
- target 配置。
- root package import closure 中所有 reachable concrete functions。

## LLVM 关系

LLVM IR 是 backend 产物，不是 JIL 的替代品。JIL 保留 Jiang 语义、控制流、place、local 和
类型事实引用；LLVM lowering 负责把这些语义映射到 LLVM type、value、basic block 和 symbol。

backend-specific symbol/mangling 从 `BackendSymbolKey` 派生。key 包含：

- 当前 session 的 package/module/function identity。
- concrete type args 和 const args。
- runtime entry / coroutine variant。

`BackendSymbolKey` 只服务本轮 LLVM declaration/value 对齐，不写入长期 cache。真正写进 object
的内部函数名从 declaration stable id 派生；泛型后缀使用 stable type/const fingerprint，
不能使用 `DefId`、`TypeId` 或 package/module 数组下标。这样跨编译进程恢复的 caller/callee
仍拥有相同链接符号。

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

`system.process.arguments()` 直接读取 `__jiang_startup_state`。hosted path 仍由
`main(argc, argv)` 接收平台启动参数，但这个入口由源码普通函数加 `@link_symbol("main")`
定义。`__jiang_startup_state` 也是源码普通全局变量，通过 `@link_symbol("__jiang_startup_state")`
绑定链接层符号。backend 不再合成 startup state global；system provider 不再通过
`_NSGetArgc/_NSGetArgv` 或平台临时符号读取用户参数。

`libc`、`libSystem` 和 POSIX/C ABI 都不是 Jiang 语言语义的一部分。它们只属于 hosted
compatibility provider：

- `system/os/macos.jiang` 是 macOS hosted target provider，可以依赖 libSystem。
- `system/os/linux.jiang` 是 Linux hosted target provider，可以依赖 libc。
- `system/os/macos/libc.jiang` 和 `system/os/linux/libc.jiang` 是 hosted C ABI 边界。
- `src/system/*.jiang` 只 import `./os/provider.jiang`。该文件通过 `comptime` 根据 `build.target`
  选择具体 OS provider；resolver 不再对 system provider 做路径重写。
- `system/os/unsupported.jiang` 保持 type-check/object 输出路径可用；executable 是否支持仍由
  target/link plan 诊断决定。
- 当前不保留可 import 的 `system/os/posix/*` 实现层；POSIX 只作为未来 façade / 语义分组，
  避免把 POSIX 固定成 hosted libc。
- no-libc provider 不能通过 hosted libc ABI 间接依赖 libc；它必须走 syscall、compiler
  intrinsic、inline asm、Wasm host import 或 target runtime object。inline asm 基础链路已经通过
  builtin provider `#asm` / `#jiang.asm` 接入语法、Semantic Model、JIL 和 LLVM lowering；真实 no-libc
  executable 和 target runtime object 仍是后续 proposal。

`--no-link-libc` 不是单纯少传 linker 参数。pipeline 会先根据 target 判断是否存在 hosted
libc/CRT，再结合用户请求得到 effective link-libc 模式。当前 object/LLVM 输出已经按这个模式选择
`malloc/free` 或 `__jiang_malloc/__jiang_free`；no-libc executable 仍明确诊断为暂不支持。

target-specific linker argv 统一由 `backend/linker.jiang` 生成。pipeline 只负责 executable 支持状态、
object emission 和 `LinkPlan`，不拼 `-target`、`-isysroot` 等具体 linker 参数。当前 target
支持矩阵见 [Targets](targets.md)。

macOS arm64 与 Linux x86_64 release compiler 都静态链接固定的 Jiang LLVM SDK。用户程序仍通过
host PATH 中的 `cc` driver 链接系统 libc/CRT；release compiler 不动态依赖 `libLLVM` 或 `liblld`。
Linux package 阶段使用 `readelf` / `ldd` 审计动态边界，并根据 GNU symbol version requirements
生成 `ABI.txt`，避免从构建 runner 名称推导最低 glibc。

## 编译模式

backend 当前区分 debug/release：

- debug：object emission 使用 LLVM codegen opt level 0，不跑 module pass pipeline。
- release：object emission 使用 LLVM codegen opt level 2，并在写 object 前通过 LLVM
  `LLVMRunPasses` 运行 `default<O2>` module pass pipeline。

backend profile 由 driver options 统一描述，包含 mode、codegen opt level 和 pass pipeline。
这个 profile 必须进入 object cache key，避免优化策略变化后复用旧 object。

## Codegen unit 与 object cache

backend-independent `CodegenUnit` 把最终 JIL 分为：

- source unit：拥有同一 source module 的普通 concrete function、global 和 hosted entry wrapper。
- monomorph unit：拥有该 source 最终 JIL 实际引用的 concrete generic instance 闭包。

external declaration 不拥有 object。普通 definition 只由一个 source unit 发出；同一 concrete
instance 可以由多个调用方 monomorph unit 以 weak/linkonce 定义发出。unit、function、global
和最终 link input 都按 stable identity 排序。单个 unit 内 session-local `TypeId` 不同但稳定
symbol identity 相同的 concrete instance 只发出一次。只包含 global 的 module 仍拥有 source unit；
每个 source 最多生成 `<stable-source-id>.o` 和 `<stable-source-id>.mono.o` 两个 debug object。

unit emission 只声明并 lower 当前 unit 拥有的 function body。跨 unit 的直接 function reference
按需声明为 LLVM external declaration，不扫描或声明完整 JIL Store，也不把 callee body 复制进
当前 object。

debug 中的 stale unit 按 stable unit 顺序完成 JIL lowering。package-level symbol、layout、
vtable field type、参数属性和 CGU plan 只准备一次，每个 unit 使用独立 LLVM Context/Module。
lowering 完成后，owned LLVM unit 可以在受限 worker Domain 中并发执行 LLVM pass 和 object emission；
worker 不访问 CompilerStore、JIL、DiagnosticStore、BuildManifest 或 LinkPlan。

coordinator 会等待全部已启动 worker，再按 stable unit 顺序处理结果。成功 unit 的临时 object
会立即原子发布并写入 `.jbuild` work product；失败 unit 只清理自己的临时文件。
任一 unit 失败时本轮不链接，也不更新 `last_success`。
其他成功 unit 可以在下一次构建中复用。

`.ji` 不保存 object 信息。debug work-product 记录只保存在当前 target 的 `.jbuild` 中。
外层 `context-key` 隔离 compiler build、language version、target/ABI、LLVM/toolchain 和 backend
profile；命中还必须验证 input fingerprint 和稳定路径上的 object metadata/hash。

object 和 `.jbuild` 都先写同目录临时文件再原子替换。
只有 object 已完成发布且最终链接成功后，
`.jbuild` 才更新 `last_success`。同一 target/context 由 build lock 串行化并在等待后重查 no-op。
debug linker 同样先写临时 executable；发布前重新验证本轮 source snapshot，
再原子替换最终路径。源码在 emission 或 link 期间变化时，本轮失败并保留上一次
成功的 executable，下一轮重新规划。

debug executable 的 `LinkPlan` 只包含本轮 CGU plan 和 runtime object，不恢复历史 link closure。
release executable 始终使用 whole-package codegen 和整体优化，不读写细粒度 work products。

`--emit-obj -o file.o` 始终保持单文件输出。当前所有目标还没有统一的 relocatable merge contract，
因此该模式完整 lowering package 并直接生成一个 whole-package object；内部 debug units 不暴露给
用户。

开发时可用 `--artifact-stats` 观察 interface/object hit、miss、stale、emitted/reused unit 和
linked object 以及 `.jbuild` no-op hit 数量。`--jobs N` 控制 debug object emission 的 worker 上限；
默认使用当前进程可用的逻辑 CPU 数，平台无法查询时回退到 `1`。release、shared library、
`--emit-llvm` 和 `--emit-obj` 路径保持串行。
统计和 worker 数量都不进入 cache key。

`--jil-stats` 中的 `backend_unit_lower_ms` 记录串行 JIL-to-LLVM unit lowering，
`backend_unit_emit_ms` 记录 worker Task 创建、LLVM pass、object emission 和 await；
`backend_units_ms` 继续记录 lowering、emission 与串行 object 发布的总时间。

## C ABI classifier

`backend/abi.jiang` 是 backend-independent 的 C ABI classifier。它消费 semantic `TypeId`
和 `LayoutStore`，把函数返回值和参数分类为：

- `none`：zero-sized / Void / never，不进入 ABI 参数列表。
- `direct`：标量、handle、function pointer 和小 aggregate，直接映射成 LLVM value。
- `indirect`：大 aggregate 通过 hidden pointer 传递；参数使用 `byval`，返回值使用 `sret`。

`backend/llvm/abi.jiang` 只负责把这个 plan 翻译成 LLVM function type 和 `byval`/`sret`
attribute。LLVM declaration 和 call site 必须使用同一个 plan。

final JIL 的参数级 analysis 还会给出彼此独立的已证明事实。backend 只做机械翻译：

- 不捕获参数在 LLVM 22 中写成 `captures(none)`，对应概念上的 `nocapture`。
- `noalias` 只来自 Jiang 唯一引用语义，不从“没有逃逸”推断。
- `readonly` 必须同时没有 through-reference write 和未知效果调用。
- `dereferenceable(N)` 只用于 safe、sized reference，`N` 来自 concrete pointee layout。

fat reference（例如 trait object）在 LLVM ABI 中是 aggregate，不能直接携带 pointer-only
parameter attribute；即使 JIL 已证明语义事实，backend 也必须先检查 LLVM 参数形态是否兼容。
外部函数和任何未证明条件都不添加属性。

细粒度 release unit emission 在一次 prepared store 上复用完整参数级 dataflow；超过快速位集
范围的参数单独回退为精确分析。whole-package release emission 只注入 Jiang 类型语义直接保证的
`noalias` 和 `dereferenceable`，把 `captures(none)`、`readonly` 等全包推导交给 LLVM O2；
debug emission 使用与参数一一对应的保守事实，不运行这组优化分析。

## 边界

- backend 不重新 type check。
- backend 不从 JIL 自己推导 field offset；field offset 只能来自 `LayoutStore`。
- backend ABI classifier 可以读取 `LayoutStore` 的 size/align，但不能修改 layout facts。
- backend 不修改 Semantic Model、JIL 或 `TypeCheckStore`。
- backend 不把 LLVM-specific 表达泄漏到 JIL 数据结构。

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
