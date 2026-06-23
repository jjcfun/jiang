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

当前 release 分支仍以 macOS arm64 作为主要开发、验证和 release host。编译器本身依赖本机 LLVM 21。
构建脚本会通过 `script/llvm_env.sh` 查找 `LLVM_CONFIG`、`JIANG_LLVM_ROOT`、`LLVM_ROOT`、
`llvm-config-21`、Homebrew `llvm@21` 和 Linux 常见 `/usr/lib/llvm-21` 等路径。

```bash
bash ./script/install_llvm_macos.sh
```

构建当前源码默认依赖同级 worktree 中的 0.4.2 bootstrap 编译器：

```bash
../bootstrap-0.4.2/build/jiangc.next --version
```

也可以通过 `BOOTSTRAP_BIN` 显式指定另一个 0.4.2 bootstrap 编译器。

运行当前源码的自举构建：

```bash
bash ./script/build_next.sh
```

该脚本会依次构建：

```text
bootstrap jiangc -> build/jiangc.next
```

并默认用 `build/jiangc.next` 跑 smoke、backend CLI smoke 和 lang check。输出为：

```text
build/jiangc.next
```

构建脚本会检测 bootstrap compiler 版本，只接受 0.4.2 bootstrap 系列。如只想构建不跑验证，可设置 `VERIFY=none`；
只跑 smoke 可设置 `VERIFY=smoke`。

构建脚本默认从根目录 `package.ini` 的 `[package].version` 读取编译器版本，并校验
`build/jiangc.next --version` 的输出。也可以用 `JIANG_VERSION=...` 临时覆盖。release 阶段需要完整
两跳自举时，使用：

```bash
BOOTSTRAP_DEPTH=stable VERIFY=full bash ./script/build_next.sh
```

破坏性升级版本的开发流程见 [编译器开发流程](doc/develop.md)。正式 release 前需要在
`release/<version>` 上使用对应的 `bootstrap/<version>` 编译器完成构建、语言测试和 stable bootstrap。

当前 release 只承诺 macOS arm64 hosted `jiangc`。源码中已有 Linux x86_64/aarch64、
Wasm `wasm32-unknown-unknown` 和 Windows MSVC x86_64/aarch64 的 LLVM IR/object 输出 smoke，
但这些 target 的 executable、linker 和 startup 路径迁移到后续版本稳定。no-libc、syscall 和
inline asm 相关能力等待自定义 DSL 机制稳定后再进入实现阶段。



## 测试与发布

基础测试：

```bash
JIANGC=./build/jiangc bash ./script/smoke.sh
JIANGC=./build/jiangc bash ./script/backend_cli_smoke.sh
JIANGC=./build/jiangc bash ./script/lang_check.sh
```

`lang_check.sh` 默认的 `run/` 用例仍走 `--emit-llvm` 后用 LLVM clang 链接。需要验证
release object/executable 路径和 LLVM O2 pass pipeline 时，打开 release run：

```bash
LANG_CHECK_RELEASE_RUNS=1 JIANGC=./build/jiangc bash ./script/lang_check.sh
```

macOS arm64 release 包：

```bash
bash ./script/package_macos_release.sh
```

该脚本默认从 `package.ini` 读取版本，要求 `build/jiangc --version` 与包版本一致。
release zip 不内置 `libLLVM.dylib`，用户机器需要安装 `llvm@21`。包内 `install.sh` 会安装到
`~/.jiang/versions/<version>` 并更新 `~/.jiang/bin/jiangc`。

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
