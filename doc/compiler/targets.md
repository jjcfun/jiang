# Targets

当前 release host 仍只承诺 macOS arm64 hosted `jiangc`。其他 target 先固定 target model、
LLVM/object 输出和 executable 诊断边界，不承诺可运行 release。

## Supported Matrix

| Target | LLVM IR | Object | Executable |
| --- | --- | --- | --- |
| default host macOS arm64 | supported | supported | supported |
| `arm64-apple-macosx` / `aarch64-apple-darwin` | supported | supported | supported on macOS host |
| `x86_64-unknown-linux-gnu` | supported | supported | `target_executable_requires_toolchain` |
| `aarch64-unknown-linux-gnu` | supported | supported | `target_executable_requires_toolchain` |
| `wasm32-unknown-unknown` | supported | supported | `target_executable_runtime_unsupported` |
| `x86_64-pc-windows-msvc` | supported | supported | `target_executable_runtime_unsupported` |
| `aarch64-pc-windows-msvc` | supported | supported | `target_executable_runtime_unsupported` |

Linux hosted executable 还没有稳定的 linker/sysroot/toolchain 发现策略。macOS host 上编译 Linux
executable 必须早停诊断，不能误用 host `cc` 链接 Linux object。

Wasm 和 Windows 当前只承诺 LLVM/object 输出。可运行 module、CRT/startup、host import 或 Windows
linker integration 都是后续任务。

no-libc / freestanding executable 不属于当前 release。相关设计迁移到 proposal，后续随 inline asm、
target runtime object 和 platform entry 设计推进。

## Linker Boundary

target-specific linker argv 由 `backend/linker.jiang` 统一生成：

- `-target <triple>` 来自 `TargetInfo.llvm_triple`。
- macOS SDK root 来自 `TargetInfo.sysroot_strategy`，优先读 `SDKROOT`，否则通过 `xcrun` 查询。
- pipeline 只决定是否允许 executable 输出、生成 object、组装 `LinkPlan`，不拼 target-specific
  linker 参数。

`TargetInfo.executable_support()` 是 executable 诊断的唯一依据。新增 target 时应先补齐这个状态，再补
pipeline smoke 中的 diagnostic code。
