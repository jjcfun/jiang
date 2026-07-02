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

当前 release 分支仍以 macOS arm64 作为主要开发、验证和 release host。编译器本身依赖 LLVM 22。
LLVM 源码通过 `vendor/llvm-project` submodule 固定到 Jiang 维护的 LLVM 22.1.8 分支：

```text
https://github.com/jjcfun/llvm-project.git
branch: jiang/22.1.8
tag: llvmorg-22.1.8
```

LLVM 本地工具链安装在 Jiang home 下：

```text
$JIANG_HOME/toolchains/llvm/<version>/<host>
```

未设置 `JIANG_HOME` 时使用 `~/.jiang`。CMake build tree 仍放在仓库 build cache 内：

```text
build/toolchains/llvm/<version>/<host>/build
```

构建脚本会通过 `script/llvm_env.sh` 查找 Jiang 托管的 LLVM：
`$JIANG_HOME/toolchains/llvm/<version>/<host>/bin/llvm-config`。LLVM 源码来自
`vendor/llvm-project` submodule，构建临时目录在 `build/toolchains/llvm/`，不会写入
submodule 目录。脚本不会 fallback 到系统全局 LLVM，也不接受外部 `LLVM_CONFIG` 覆盖。

```bash
bash ./script/install_llvm.sh
```

`install_llvm.sh` 默认从 `vendor/llvm-project` 构建 LLVM，并安装到
`$JIANG_HOME/toolchains/llvm/<version>/<host>`。如果在 release 包中运行脚本且没有 submodule，
脚本会从 `jjcfun/llvm-project` 的 `jiang/22.1.8` 分支浅克隆源码。已存在的本地 LLVM 22
会直接复用；需要强制重建时设置 `JIANG_LLVM_FORCE_BUILD=1`。LLVM 库默认以静态库形式链接进
`jiangc`，release 用户不需要安装 LLVM runtime。
macOS 下默认使用 `JIANG_MACOS_DEPLOYMENT_TARGET=11.0` 构建 LLVM 和链接 `jiangc`，需要
调整最低系统版本时应统一设置这个变量。

构建当前源码默认依赖 `bootstrap/0.4.3` 分支产出的过渡编译器。推荐目录结构：

```text
../bootstrap-0.4.3/build/bin/jiangc.next
```

如果没有同级 bootstrap worktree，也可以把 0.4.3 bootstrap 编译器安装到
`~/.jiang/versions/0.4.3/bin/jiangc`，或通过 `BOOTSTRAP_BIN` 显式指定。

运行当前源码的自举构建：

```bash
bash ./script/build_next.sh
```

该脚本会依次构建：

```text
../bootstrap-0.4.3/build/bin/jiangc.next -> build/bin/jiangc.next
```

并默认用 `build/bin/jiangc.next` 跑 smoke、backend CLI smoke 和 lang check。输出为：

```text
build/bin/jiangc.next
```

构建脚本会检测 bootstrap compiler 版本，只接受 `jiang 0.4.3`。如只想构建不跑验证，
可设置 `VERIFY=none`；只跑 smoke 可设置 `VERIFY=smoke`。

构建脚本默认从根目录 `package.ini` 的 `[package].version` 读取编译器版本，并校验
`build/bin/jiangc.next --version` 的输出。也可以用 `JIANG_VERSION=...` 临时覆盖。release 阶段需要完整
两跳自举时，使用：

```bash
BOOTSTRAP_DEPTH=stable VERIFY=full bash ./script/build_next.sh
```

破坏性升级版本的开发流程见 [编译器开发流程](doc/develop.md)。正式 release 前需要在
`release/<version>` 上使用对应的 `bootstrap/<version>` 编译器完成构建、语言测试和 stable bootstrap。

当前 release 只承诺 macOS arm64 hosted `jiangc`。Linux `jiangc` release 暂缓到语法和
bootstrap pipeline 稳定之后。源码中已有 Linux x86_64/aarch64、Wasm `wasm32-unknown-unknown`、
WASI `wasm32-wasi` 和 Windows MSVC x86_64/aarch64 的 LLVM IR/object 输出 smoke。
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
JIANGC=./build/bin/jiangc bash ./script/smoke.sh
JIANGC=./build/bin/jiangc bash ./script/backend_cli_smoke.sh
JIANGC=./build/bin/jiangc bash ./script/lang_check.sh
```

`lang_check.sh` 默认的 `run/` 用例仍走 `--emit-llvm` 后用 LLVM clang 链接。需要验证
release object/executable 路径和 LLVM O2 pass pipeline 时，打开 release run：

```bash
LANG_CHECK_RELEASE_RUNS=1 JIANGC=./build/bin/jiangc bash ./script/lang_check.sh
```

macOS arm64 release 包：

```bash
bash ./script/package_macos_release.sh
```

该脚本默认从 `package.ini` 读取版本，要求 `build/bin/jiangc --version` 与包版本一致。
release zip 中的 `jiangc` 静态链接 LLVM，不依赖本机 `libLLVM.dylib`。包内 `install.sh`
会安装到 `~/.jiang/versions/<version>` 并更新 `~/.jiang/bin/jiangc`。

验证完整 release 链路：

```bash
bash ./script/release_smoke.sh
```

该脚本会复用或安装本地 LLVM，执行 stable bootstrap 构建，生成 macOS release zip，解包运行
`jiangc --version`，用临时 `PREFIX` 验证包内 `install.sh`，并检查产物没有动态依赖
`libLLVM` / `liblld`。

## 文档

- [官网与语言文档](https://jiang-lang.org/)
- [架构文档](doc/architecture.md)
- [编译器开发流程](doc/develop.md)
- [Std incubator](doc/std.md)
- 阶段设计：[AST](doc/compiler/ast.md)、[Resolve](doc/compiler/resolve.md)、[HIR](doc/compiler/hir.md)、
  [Type Check](doc/compiler/type-check.md)、[Monomorph](doc/compiler/monomorph.md)、
  [MIR](doc/compiler/mir.md)、[Layout](doc/compiler/layout.md)、
  [Borrow Check](doc/compiler/borrow-check.md)、[Backend](doc/compiler/backend.md)、
  [Startup](doc/compiler/startup.md)、[Targets](doc/compiler/targets.md)
- [PEG 语法](doc/grammar.md)
- [语言设计](doc/language-design.md)



## License

Apache License 2.0。详见 [LICENSE](./LICENSE)。
