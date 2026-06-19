# Jiang 文档索引

这个目录同时保存语言用户文档和编译器开发文档。阅读时先按目标选择入口，避免把语言规则和编译器实现细节混在一起。

## 语言用户文档

- [Jiang 语言指南](jiang.md)：面向语言使用者的主文档，按语法和常用功能组织。
- [语法参考](grammar.md)：接近 parser 的语法规则，适合核对具体写法。
- [标准库孵化文档](std.md)：当前 `std` 暴露的模块、类型和稳定性边界。
- [语言设计草案](language-design.md)：语言规则、设计理由和仍在收敛的边界。

## 编译器开发文档

- [编译器架构](architecture.md)：阶段边界、store 规则和源码目录约定。
- [编译器开发流程](develop.md)：破坏性版本升级时的 bootstrap / release 双 worktree 流程。
- [compiler/](compiler/)：各阶段的详细设计，包括 AST、resolve、HIR、type check、MIR、borrow check、backend、incremental 和测试覆盖。

## 当前分支

当前 `release/0.4.1` 分支使用 `bootstrap/0.4.1` 过渡编译器验证。面向用户的语言文档应描述
当前分支的可用语法；历史版本说明只在解释兼容边界时保留。
