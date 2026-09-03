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
- [compiler/](compiler/)：各阶段的详细设计，包括 AST、resolve、Semantic Model、type check、JIL、
  borrow check、backend、incremental 和测试覆盖。

## Release notes

- [Jiang 0.5.3](releases/0.5.3.md)
- [Jiang 0.5.2](releases/0.5.2.md)
- [Jiang 0.5.1](releases/0.5.1.md)

## 0.5.3 自举链

0.5.3 release 使用以下可复现 transition chain：

```text
0.5.2 stable
  -> bootstrap/0.5.3 next
  -> release/0.5.3 next
  -> release/0.5.3 stable
```

0.5.3 把普通 tagged union 迁移为 payload enum。bootstrap 只生成 next，不生成 stable；
release next 必须直接由该 bootstrap next 编译。完整命令见
[编译器开发流程](develop.md)。

## 0.5.2 自举链（历史）

0.5.2 release 使用以下可复现 transition chain：

```text
0.5.1 stable
  -> bootstrap/0.5.2 next
  -> bootstrap/0.5.2-2 next
  -> release/0.5.2 next
  -> release/0.5.2 stable
```

0.5.2 引入严格所有权检查及引用 lifetime shape 规则，0.5.1 无法直接自举该源码。过渡编译器
只负责生成下一阶段，不是 release。完整命令与严格验证边界见
[编译器开发流程](develop.md)。

## 0.5.0 自举链（历史）

0.5.0 release 使用以下可复现 transition chain：

```text
0.4.9 stable
  -> bootstrap/0.5.0 next
  -> bootstrap/0.5.0-2 next
  -> release/0.5.0 next
  -> release/0.5.0 stable
```

0.5.0 release 源码采用新的 Domain/Executor ABI，不能由 0.4.9 stable 直接编译。
历史复现链和详细命令见 [编译器开发流程](develop.md)；该版本发布后的常规开发
使用 0.5.0 stable。
面向用户的语言文档应描述当前分支的可用语法；历史版本说明只在解释兼容边界时保留。
