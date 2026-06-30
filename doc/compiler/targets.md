# Targets

当前 release host 仍只承诺 macOS arm64 hosted `jiangc`。Linux 用户级支持应优先走
Jiang no-libc 静态可执行路线；`linux-gnu` hosted 暂时只作为 backend target 验证和
外部 toolchain 模式保留，不作为 Jiang 官方 All in one Linux 承诺。其他 target 先固定
target model、LLVM/object 输出和 executable 诊断边界。

## Supported Matrix

| Target | LLVM IR | Object | Executable |
| --- | --- | --- | --- |
| default host macOS arm64 | supported | supported | supported |
| `arm64-apple-macosx` / `aarch64-apple-darwin` | supported | supported | supported on macOS host |
| `x86_64-unknown-linux-gnu` | experimental | experimental | `target_executable_requires_toolchain` |
| `aarch64-unknown-linux-gnu` | experimental | experimental | `target_executable_requires_toolchain` |
| Linux no-libc x86_64 | supported through `--no-link-libc` | supported | planned static executable |
| `wasm32-unknown-unknown` | supported | supported | `target_executable_runtime_unsupported` |
| `x86_64-pc-windows-msvc` | supported | supported | `target_executable_runtime_unsupported` |
| `aarch64-pc-windows-msvc` | supported | supported | `target_executable_runtime_unsupported` |

Linux glibc executable 需要 glibc sysroot / toolchain，Jiang 不维护 glibc。macOS host 上编译
`linux-gnu` hosted executable 必须早停诊断，不能误用 host `cc` 链接 Linux object。
Linux / WSL host 自举需要 Linux 可执行 bootstrap compiler，当前不属于 0.4.3 第一阶段目标。

Jiang 官方 Linux All in one 路线优先支持 no-libc 静态 executable。`src/system/os/provider.jiang`
是系统能力抽象入口，`src/system/os/linux/no_libc.jiang` 是移除 libc 依赖的 Linux provider。
入口、syscall、内存、panic/trap 和必要 runtime object 由 Jiang 维护并随 release 包集成；
默认情况下用户和 Jiang 开发者都不需要配置 libc/sysroot。

Wasm 和 Windows 当前只承诺 LLVM/object 输出。可运行 module、CRT/startup、host import 或 Windows
linker integration 都是后续任务。

no-libc / freestanding executable 不属于当前 release。inline asm 基础链路已经可用；相关 executable
设计后续随 target runtime object、syscall 封装和 platform entry 继续推进。

## Linker Boundary

target-specific linker argv 由 `backend/linker.jiang` 统一生成：

- `-target <triple>` 来自 `TargetInfo.llvm_triple`。
- macOS SDK root 来自 `TargetInfo.sysroot_strategy`，优先读 `SDKROOT`，否则通过 `xcrun` 查询。
- pipeline 只决定是否允许 executable 输出、生成 object、组装 `LinkPlan`，不拼 target-specific
  linker 参数。

`TargetInfo.executable_support()` 是 executable 诊断的唯一依据。新增 target 时应先补齐这个状态，再补
pipeline smoke 中的 diagnostic code。
