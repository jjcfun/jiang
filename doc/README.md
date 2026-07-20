# Jiang 文档索引

这个目录同时保存语言用户文档和编译器开发文档。阅读时先按目标选择入口，避免把
语言规则和编译器实现细节混在一起。

## 语言用户文档

- [官网与语言文档](https://jiang-lang.org/)：面向语言使用者的主入口。
- [语法参考](grammar.md)：接近 parser 的语法规则，适合核对具体写法。
- [标准库孵化文档](std.md)：当前 `std` 暴露的模块、类型和稳定性边界。
- [语言设计草案](language-design.md)：语言规则、设计理由和仍在收敛的边界。
- [闭包设计与实现状态](closure-design.md)：捕获闭包、callable 类型和后续 async/data-race 关系。
- [Capability 设计草案](capability-design.md)：capability、effect、协程、异步和基于
  execution domain 的数据竞争防护方案。

`jiang.md` 是历史单文件指南，不作为规范入口；其中仍在使用的语法示例会与当前版本
同步。

## 编译器开发文档

- [编译器架构](architecture.md)：阶段边界、store 规则和源码目录约定。
- [编译器开发流程](develop.md)：常规版本自举、上一版 release 编译器依赖和破坏性升级时的
  双 worktree 流程。
- [compiler/](compiler/)：各阶段的详细设计，包括 AST、resolve、HIR、type check、MIR、
  borrow check、backend、incremental 和测试覆盖。

## 当前分支

当前 `release/0.4.7-2` 是破坏性可变性语法迁移分支，采用双 worktree 自举链：

```text
0.4.7-bootstrap seed
  -> bootstrap/0.4.7-2 的 jiangc.next
  -> release/0.4.7-2 的 jiangc.next
  -> release/0.4.7-2 的 stable jiangc
```

bootstrap worktree 只负责 seed 到 bootstrap next，不要求用 bootstrap next 再编译自己的旧语法
源码。release 使用 `name!`、`T&!`、`T*!` 等 canonical 语法，并由 release next 生成 stable。
详细命令和边界见 [编译器开发流程](develop.md)。
面向用户的语言文档应描述当前分支的可用语法；历史版本说明只在解释兼容边界时保留。
