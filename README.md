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



## 文档

- [官网与语言文档](https://jiang-lang.org/)
- [架构文档](doc/architecture.md)
- 阶段设计：[AST](doc/ast.md)、[Resolve](doc/resolve.md)、[HIR](doc/hir.md)、
  [Type Check](doc/type-check.md)、[Monomorph](doc/monomorph.md)、[MIR](doc/mir.md)、
  [Layout](doc/layout.md)、[Borrow Check](doc/borrow-check.md)、[Backend](doc/backend.md)
- [PEG 语法](doc/grammar.md)
- [语言设计](doc/language-design.md)



## License

Apache License 2.0。详见 [LICENSE](./LICENSE)。
