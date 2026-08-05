<p align="center">
  <img src="doc/logo.svg" alt="Jiang" width="160">
</p>

# Jiang语言

当前 Jiang 语言编译器已经可以稳定自举。常规版本开发只依赖上一版稳定 `jiangc` 作为
bootstrap 输入；如果新版本包含旧 release 编译器无法直接编译的破坏性升级，则使用
`bootstrap/<version>` 和 `release/<version>` 双 worktree 流程。

Jiang 仍处于早期版本阶段，语言、标准库和编译器内部结构会继续快速迭代。

[官网与语言文档](https://jiang-lang.org/)



## 构建自举编译器

当前 release 分支使用精确锁定的 LLVM 22.1.8。Jiang 提供 Linux x86_64 和 macOS arm64 的预构建
LLVM SDK，普通构建不需要下载或编译 LLVM 源码：

```text
release: jiang-sdk-llvm-22.1.8-1
https://github.com/jjcfun/llvm-project/releases/tag/jiang-sdk-llvm-22.1.8-1
```

LLVM 本地工具链默认安装在仓库 build 目录下：

```text
build/llvm/<host>/install
```

CMake build tree 放在同一组本地缓存目录：

```text
build/llvm/<host>/build
```

也可以把 LLVM 安装到 Jiang home，供多个 worktree 复用：

```text
$JIANG_HOME/toolchains/llvm/<version>/<host>
```

未设置 `JIANG_HOME` 时使用 `~/.jiang`。构建脚本会通过 `script/llvm_env.sh` 查找 Jiang
托管的 LLVM：优先使用 `build/llvm/<host>/install/bin/llvm-config`，再 fallback 到
`$JIANG_HOME/toolchains/llvm/<version>/<host>/bin/llvm-config`。脚本不会 fallback 到系统全局
LLVM，也不接受外部 `LLVM_CONFIG` 覆盖。

```bash
bash ./script/install_llvm.sh --local
bash ./script/install_llvm.sh --user
```

`install_llvm.sh` 默认等价于 `--local`，下载 SDK、校验锁定的 SHA-256，并安装到
`build/llvm/<host>/install`；下载归档缓存在 `build/downloads`。`--user` 会安装到
`$JIANG_HOME/toolchains/llvm/<version>/<host>`。只有排查或维护 LLVM 时才使用源码兜底：

```bash
bash ./script/install_llvm.sh --local --from-source
```

源码模式优先使用 `vendor/llvm-project` submodule；release 包没有 submodule 时，从 Jiang LLVM fork
浅克隆 `llvmorg-22.1.8`。`JIANG_LLVM_FORCE_BUILD=1` 仍表示强制源码重建。LLVM 库默认以静态库形式
链接进 `jiangc`，release 用户不需要安装 LLVM runtime。
macOS 下默认使用 `JIANG_MACOS_DEPLOYMENT_TARGET=11.0` 构建 LLVM 和链接 `jiangc`，需要
调整最低系统版本时应统一设置这个变量。

当前源码默认使用已发布的 Jiang 0.5.1 stable 编译器作为 bootstrap 输入。默认路径：

```text
~/.jiang/versions/0.5.1/bin/jiangc
```

安装 0.5.1 后可直接运行：

```bash
bash ./script/build_next.sh
```

该脚本会依次构建：

```text
~/.jiang/versions/0.5.1/bin/jiangc -> build/bin/jiangc.next
```

并默认用 `build/bin/jiangc.next` 跑 smoke、backend CLI smoke 和 lang check。输出为：

```text
build/bin/jiangc.next
```

构建脚本会检测 bootstrap compiler 版本。如只想构建不跑验证，可设置 `VERIFY=none`；
只跑 smoke 可设置 `VERIFY=smoke`。

构建脚本默认从根目录 `package.ini` 的 `[package].version` 读取编译器版本，并校验
`build/bin/jiangc.next --version` 的输出。也可以用 `JIANG_VERSION=...` 临时覆盖。release 阶段需要完整
两跳自举时，使用：

```bash
BOOTSTRAP_DEPTH=stable VERIFY=full bash ./script/build_next.sh
```

破坏性升级版本的开发流程见 [编译器开发流程](doc/develop.md)。Jiang 0.5.1 的可复现历史
自举链使用 `0.5.1-bootstrap` 过渡 tag；普通开发不需要保留对应 worktree。

Jiang 0.5.1 的正式 hosted release host 是 macOS arm64 与 Linux x86_64。Linux release 使用系统
glibc、pthread、dl 和 C++ runtime；安装包内的 `ABI.txt` 根据最终 ELF symbol version requirements
记录最低 glibc 版本，不把 CI runner 版本直接当作兼容性承诺。Linux aarch64 暂不提供正式安装包。
源码中另有 Linux aarch64、Wasm `wasm32-unknown-unknown`、WASI `wasm32-wasi` 和 Windows MSVC
x86_64/aarch64 的 LLVM IR/object 输出 smoke。
WASI executable 依赖本地 wasi-sdk，默认安装在：

```text
$JIANG_HOME/toolchains/wasi-sdk/<version>/<host>
```

未设置 `JIANG_HOME` 时使用 `~/.jiang`。可通过以下脚本安装：

```bash
bash ./script/install_wasi.sh
```

其他 target 的 executable、linker 和 startup 路径仍是实验能力。inline asm 已作为内建
DSL provider 提供基础 `#asm { ... }` / `#jiang.asm { ... }` 能力，用于后续 no-libc
syscall/runtime 路线；Linux no-libc 静态 executable 仍是后续阶段目标。



## 测试与发布

基础测试：

```bash
JIANGC=./build/bin/jiangc bash ./script/test.sh
TEST_ROOT=test/compiler JIANGC=./build/bin/jiangc bash ./script/test.sh
JIANGC=./build/bin/jiangc bash ./script/smoke.sh
JIANGC=./build/bin/jiangc bash ./script/backend_cli_smoke.sh
```

`script/test.sh` 统一发现 `check/`、`fail/`、`emit/` 和 `run/` 用例。默认运行
`test/lang`，通过 `TEST_ROOT` 可选择编译器内部测试。runner 默认使用逻辑 CPU 数和 4
中的较小值并行执行；`TEST_JOBS=1` 可用于串行复现。每个用例都有独立的工作目录和
artifact cache，避免并发编译共享可写状态。`TEST_FILTER` 可用正则选择任意类别的用例，
`TEST_LIST` 可指定按仓库相对路径逐行列出的用例清单：

```bash
TEST_JOBS=1 TEST_FILTER='tuple' JIANGC=./build/bin/jiangc bash ./script/test.sh
TEST_JOBS=4 TEST_TIMEOUT=120 JIANGC=./build/bin/jiangc bash ./script/test.sh
```

依赖宿主系统 API 的用例可用 `// test-platform: macos` 或 `// test-platform: linux`
限定运行平台；普通用例不应添加平台限制。

默认在首个失败后停止派发新用例，并等待已经启动的用例结束；设置
`TEST_KEEP_GOING=1` 可完成全部已选择用例。失败时 runner 会打印保留目录，其中的
`cases/<序号>-<类别>-<用例>/` 保存编译、链接、运行日志和独立 cache。成功用例默认清理；
调试时可用 `TEST_KEEP_WORK=1` 保留全部产物。`TEST_TIMING=1` 会输出每个阶段、每个用例和
整个 suite 的耗时。
`script/lang_check.sh` 暂时作为兼容入口。

`script/smoke.sh` 使用显式用例清单运行日常快速测试，不定义另一套测试语义。它默认
跳过较慢的 lang provider dylib 用例；需要覆盖该路径时，显式打开：

```bash
JIANG_SLOW_SMOKE=1 JIANGC=./build/bin/jiangc bash ./script/smoke.sh
```

`test.sh` 默认的 `run/` 用例仍走 `--emit-llvm` 后用 LLVM clang 链接。需要验证
release object/executable 路径和 LLVM O2 pass pipeline 时，打开 release run：

```bash
TEST_RELEASE_RUNS=1 JIANGC=./build/bin/jiangc bash ./script/test.sh
```

runner 自身的调度契约可独立验证：

```bash
bash ./script/test_runner_self_test.sh
```

生成当前 host 的 release 包：

```bash
bash ./script/package_macos_release.sh
bash ./script/package_linux_release.sh
```

两个入口复用 `package_release.sh` 的公共 staging/install 流程，默认从 `package.ini` 读取版本，
并要求 `build/bin/jiangc --version` 与包版本一致。macOS 产物是 `.zip`，Linux x86_64 产物是
`.tar.gz`。其中 `jiangc` 静态链接 LLVM，不动态依赖 `libLLVM` / `liblld`；包内 `install.sh`
会安装到 `~/.jiang/versions/<version>` 并更新 `~/.jiang/bin/jiangc`。Linux 包额外包含 `ABI.txt`。

验证完整 release 链路：

```bash
bash ./script/release_smoke.sh
```

该脚本会复用或安装本地 LLVM，执行 stable bootstrap 构建，生成当前 host 的 release archive，
用临时 `PREFIX` 验证包内 `install.sh`，并使用安装后的 compiler 编译运行 Hello 和 hosted capability
sample。macOS 使用 `otool`，Linux 使用 `readelf` / `ldd` 检查产物不动态依赖 `libLLVM` / `liblld`。
Linux port seed 或 CI 已经生成 stable compiler 时，可设置 `RELEASE_SMOKE_BUILD=0` 避免重复自举。

## 文档

- [官网与语言文档](https://jiang-lang.org/)
- [架构文档](doc/architecture.md)
- [编译器开发流程](doc/develop.md)
- [Std incubator](doc/std.md)
- 阶段设计：[AST](doc/compiler/ast.md)、[Resolve](doc/compiler/resolve.md)、[Semantic Model](doc/compiler/semantic-model.md)、
  [Type Check](doc/compiler/type-check.md)、[Monomorph](doc/compiler/monomorph.md)、
  [JIL](doc/compiler/jil.md)、[Layout](doc/compiler/layout.md)、
  [Borrow Check](doc/compiler/borrow-check.md)、[Backend](doc/compiler/backend.md)、
  [Startup](doc/compiler/startup.md)、[Targets](doc/compiler/targets.md)
- [PEG 语法](doc/grammar.md)
- [语言设计](doc/language-design.md)



## License

Apache License 2.0。详见 [LICENSE](./LICENSE)。
