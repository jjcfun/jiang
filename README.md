<p align="center">
  <img src="doc/logo.svg" alt="Jiang" width="160">
</p>

# Jiang语言

当前jiang语言编译器处于 stage2 开发阶段。stage0 和 stage1 由 vibe coding 产生（在stage1分支）；
stage2 将采取人工方式编写和审核代码。



## 安装 stage1 编译器

stage2 当前使用 stage1 编译器编译和运行 smoke。先在切到 stage1 分支并构建：

```bash
git switch stage1
bash ./script/build_stage1.sh
```

构建完成后，stage1 编译器产物位于：

```text
build/stage1/jiangc
```



## 文档

- [语言指南](doc/jiang.md)
- [架构文档](doc/architecture.md)
- [PEG 语法](doc/grammar.md)
- [语言设计](doc/language-design.md)



## License

Apache License 2.0。详见 [LICENSE](./LICENSE)。
