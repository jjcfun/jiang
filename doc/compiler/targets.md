# Targets

0.5.1 正在把 Linux x86_64 hosted/libc 提升为正式 release host。Linux native compiler
使用系统 libc 和本机 LLVM/linker toolchain 完成自举与 executable 链接；macOS -> Linux
hosted executable 仍保持 cross-toolchain 早停边界。Linux no-libc 不属于 0.5.1 验收范围。
其他 target 先固定 target model、LLVM/object 输出和 executable 诊断边界。

## Supported Matrix

| Target | LLVM IR | Object | Executable |
| --- | --- | --- | --- |
| default host macOS arm64 | supported | supported | supported |
| `arm64-apple-macosx` / `aarch64-apple-darwin` | supported | supported | supported on macOS host |
| `x86_64-unknown-linux-gnu` | supported | supported | supported on Linux x86_64 host |
| `aarch64-unknown-linux-gnu` | experimental | experimental | `target_executable_requires_toolchain` |
| Linux no-libc x86_64 | supported through `--no-link-libc` | supported | planned static executable |
| `wasm32-unknown-unknown` | supported | supported | `target_executable_runtime_unsupported` |
| `wasm32-wasi` | supported through `wasm32-wasip1` | supported | experimental with wasi-sdk |
| `x86_64-pc-windows-msvc` | supported | supported | `target_executable_runtime_unsupported` |
| `aarch64-pc-windows-msvc` | supported | supported | `target_executable_runtime_unsupported` |

Linux glibc executable 使用 native host 的 libc 与 toolchain，Jiang 不维护 glibc sysroot。
macOS host 上编译 `linux-gnu` hosted executable 必须早停诊断，不能误用 host `cc` 链接
Linux object。首次 Linux hosted port seed 流程见 [编译器开发流程](../develop.md)。

Linux release archive 内的 `ABI.txt` 从最终 `jiangc` ELF 的 GNU symbol version requirements
计算最低 glibc 版本，并记录 program interpreter、`DT_NEEDED` 和 SHA-256。这个产物审计结果才是
release 的最低 glibc 依据；Ubuntu runner 版本只定义验证环境，不自动成为兼容性声明。

`src/system/os/provider.jiang` 是系统能力抽象入口。0.5.1 使用
`src/system/os/linux.jiang` hosted provider；`src/system/os/linux/no_libc.jiang` 是独立的
实验路线，不参与本版本 hosted 验收。

裸 Wasm `wasm32-unknown-unknown` 当前只承诺 LLVM/object 输出。WASI 使用 `wasm32-wasi`
作为 Jiang CLI 入口，内部 LLVM triple 使用 LLVM 22 推荐的 `wasm32-wasip1`。WASI
executable 链接依赖 wasi-sdk，默认从 `$JIANG_HOME/toolchains/wasi-sdk` 查找；未设置
`JIANG_HOME` 时使用 `~/.jiang`。Windows 的可运行 CRT/startup 和 linker integration 仍是后续任务。

no-libc / freestanding executable 不属于当前 release。inline asm 基础链路已经可用；相关 executable
设计后续随 target runtime object、syscall 封装和 platform entry 继续推进。

## Linker Boundary

target-specific linker argv 由 `backend/linker.jiang` 统一生成：

- `-target <triple>` 来自 `TargetInfo.llvm_triple`。
- macOS SDK root 来自 `TargetInfo.sysroot_strategy`，优先读 `SDKROOT`，否则通过 `xcrun` 查询。
- Linux hosted 在 object 和用户 link args 之后显式追加 `-pthread`、`-ldl`，不依赖 glibc
  合并历史库后的隐式兼容行为。
- pipeline 只决定是否允许 executable 输出、生成 object、组装 `LinkPlan`，不拼 target-specific
  linker 参数。

Linux hosted 文件读写必须处理 partial result，并在 `EINTR` 后重试。provider 创建的临时 C string
由 provider 在 libc 调用返回后释放；`remove_tree` 必须先尝试 `unlink`，避免遗漏 dangling symlink。

Linux hosted process 首版支持 stdout/stderr inherit、stdout pipe 和 stderr discard。glibc
`posix_spawn_file_actions_t` 使用经 ABI probe 验证的 opaque storage；stderr pipe 需要双 pipe
并发 drain，不在 0.5.1 首批 process capability 范围内。

Linux hosted runtime 在启动时记录 main pthread，main-domain job 进入可由启动线程主动 pump
的 FIFO queue。启动线程等待 runtime group 或 task word 时必须同时 pump，避免 main-domain
continuation 与 blocking wait 相互等待。

`TargetInfo.executable_support()` 是 executable 诊断的唯一依据。新增 target 时应先补齐这个状态，再补
pipeline smoke 中的 diagnostic code。
