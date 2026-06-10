<p align="center">
  <img src="doc/logo.svg" alt="Jiang" width="160">
</p>

# Jiang语言

当前 Jiang 语言编译器处于 stage2 开发阶段，已经可以稳定自举。stage0 和 stage1 由 vibe coding
产生（在 stage1 分支）；stage2 将采取人工方式编写和审核代码。日常开发默认使用 stage2/next
产物，stage1 只作为 bootstrap 输入保留。

Jiang 目前仍处于 0.2 版本阶段，现阶段看上去或许平平无奇。这里先卖个关子：0.4 版本会引入一个
杀手级特性，它会是这门语言真正拉开差异的起点。

[官网与语言文档](https://jiang-lang.org/)



## 构建稳定自举编译器

当前 0.2 开发目标只支持 macOS arm64。编译和发布包都依赖本机 LLVM 21；默认路径按
Homebrew 的 `llvm@21` 约定查找。

```bash
bash ./script/install_llvm_macos.sh
```

先在 stage1 worktree 或 stage1 分支中构建 bootstrap 编译器：

```bash
git switch stage1
bash ./script/build_stage1.sh
```

构建完成后，把 stage1 编译器安装到用户目录：

```bash
mkdir -p ~/.jiang/stage1/bin
cp dist/stage1/jiangc ~/.jiang/stage1/bin/jiangc
```

stage2 分支中运行稳定自举构建：

```bash
bash ./script/build_stable.sh
```

该脚本会依次构建：

```text
stage1 -> build/jiangc -> build/jiangc.next -> build/jiangc.next2
```

并默认用 `build/jiangc.next2` 跑 smoke、backend CLI smoke 和 lang check。通过后会复制
稳定候选到：

```text
build/jiangc.stable
```

如需临时指定 bootstrap 编译器，可以设置 `STAGE1_BIN`。如只想构建不跑验证，可设置
`VERIFY=none`；只跑 smoke 可设置 `VERIFY=smoke`。

稳定构建默认从根目录 `package.ini` 的 `[package].version` 读取编译器版本，并校验
`build/jiangc.stable --version` 的输出。也可以用 `JIANG_VERSION=...` 临时覆盖。



## 测试与发布

基础测试：

```bash
JIANGC=./build/jiangc.stable bash ./script/smoke.sh
JIANGC=./build/jiangc.stable bash ./script/backend_cli_smoke.sh
JIANGC=./build/jiangc.stable bash ./script/lang_check.sh
```

`lang_check.sh` 默认的 `run/` 用例仍走 `--emit-llvm` 后用 LLVM clang 链接。需要验证
release object/executable 路径和 LLVM O2 pass pipeline 时，打开 release run：

```bash
LANG_CHECK_RELEASE_RUNS=1 JIANGC=./build/jiangc.stable bash ./script/lang_check.sh
```

macOS arm64 release 包：

```bash
bash ./script/package_macos_release.sh
```

该脚本默认从 `package.ini` 读取版本，要求 `build/jiangc.stable --version` 与包版本一致。
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
