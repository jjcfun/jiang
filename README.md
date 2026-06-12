<p align="center">
  <img src="doc/logo.svg" alt="Jiang" width="160">
</p>

# Jiang语言

当前 Jiang 语言编译器处于 stage2 开发阶段，已经可以稳定自举。stage0 和 stage1 由 vibe coding
产生（在 stage1 分支）；stage2 将采取人工方式编写和审核代码。0.2.2 之后的编译器开发只依赖
0.2 系列稳定版 `jiangc` 作为 bootstrap 输入，不再依赖 stage1 工作区或 stage1 产物。

Jiang 目前仍处于 0.2 版本阶段，现阶段看上去或许平平无奇。这里先卖个关子：0.4 版本会引入一个
杀手级特性，它会是这门语言真正拉开差异的起点。

[官网与语言文档](https://jiang-lang.org/)



## 构建自举编译器

当前 0.2 开发目标只支持 macOS arm64。编译和发布包都依赖本机 LLVM 21；默认路径按
Homebrew 的 `llvm@21` 约定查找。

```bash
bash ./script/install_llvm_macos.sh
```

构建当前 0.2.2 源码需要先安装 Jiang `0.2.1-bootstrap` 产物，并确保对应的 `jiangc` 已在
PATH 中。`0.2.1-bootstrap` 只作为 0.2.2 的自举锚点，不作为面向用户的正式 release：

```bash
jiangc --version
```

运行当前源码的自举构建：

```bash
bash ./script/build_next.sh
```

该脚本会依次构建：

```text
jiangc 0.2.1-bootstrap -> build/jiangc.next -> build/jiangc
```

并默认用最终产物 `build/jiangc` 跑 smoke、backend CLI smoke 和 lang check。输出为：

```text
build/jiangc
```

构建脚本会直接检测 PATH 中的 `jiangc`，并要求版本为 `0.2.x`。如只想构建不跑验证，可设置
`VERIFY=none`；只跑 smoke 可设置 `VERIFY=smoke`。

构建脚本默认从根目录 `package.ini` 的 `[package].version` 读取编译器版本，并校验
`build/jiangc --version` 的输出。也可以用 `JIANG_VERSION=...` 临时覆盖。



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
- 阶段设计：[AST](doc/compiler/ast.md)、[Resolve](doc/compiler/resolve.md)、[HIR](doc/compiler/hir.md)、
  [Type Check](doc/compiler/type-check.md)、[Monomorph](doc/compiler/monomorph.md)、
  [MIR](doc/compiler/mir.md)、[Layout](doc/compiler/layout.md)、
  [Borrow Check](doc/compiler/borrow-check.md)、[Backend](doc/compiler/backend.md)
- [PEG 语法](doc/grammar.md)
- [语言设计](doc/language-design.md)



## License

Apache License 2.0。详见 [LICENSE](./LICENSE)。
