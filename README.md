<p align="center">
  <img src="doc/logo.svg" alt="Jiang" width="160">
</p>

# Jiang语言

当前 Jiang 语言编译器处于 stage2 开发阶段，已经初步完成自举。stage0 和 stage1 由 vibe coding
产生（在 stage1 分支）；stage2 将采取人工方式编写和审核代码。

Jiang 目前仍处于 0.2 版本阶段，现阶段看上去或许平平无奇。这里先卖个关子：0.4 版本会引入一个
杀手级特性，它会是这门语言真正拉开差异的起点。

[官网与语言文档](https://jiang-lang.org/)



## 安装 stage1 编译器

stage2 当前使用 stage1 编译器编译和运行 smoke。先在 stage1 worktree 或 stage1 分支中构建：

```bash
git switch stage1
bash ./script/build_stage1.sh
```

构建完成后，把 stage1 编译器安装到用户目录：

```bash
mkdir -p ~/.jiang/stage1/bin
cp dist/stage1/jiangc ~/.jiang/stage1/bin/jiangc
```

stage2 smoke 默认使用 `~/.jiang/stage1/bin/jiangc`。如需临时指定其他编译器，
可以设置 `STAGE1_BIN`。



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
