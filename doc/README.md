# Jiang 文档索引

这个目录同时保存语言用户文档和编译器开发文档。阅读时先按目标选择入口，避免把语言规则和编译器实现细节混在一起。

## 语言用户文档

- [官网与语言文档](https://jiang-lang.org/)：面向语言使用者的主入口。
- [语法参考](grammar.md)：接近 parser 的语法规则，适合核对具体写法。
- [标准库孵化文档](std.md)：当前 `std` 暴露的模块、类型和稳定性边界。
- [语言设计草案](language-design.md)：语言规则、设计理由和仍在收敛的边界。
- [闭包设计草案](closure-design.md)：捕获闭包、callable 类型和后续 async/data-race 关系。

`jiang.md` 是过时的历史单文件指南，只保留为迁移参考，不再作为文档入口维护。

## 编译器开发文档

- [编译器架构](architecture.md)：阶段边界、store 规则和源码目录约定。
- [编译器开发流程](develop.md)：常规版本自举、上一版 release 编译器依赖和破坏性升级时的双 worktree 流程。
- [compiler/](compiler/)：各阶段的详细设计，包括 AST、resolve、HIR、type check、MIR、borrow check、backend、incremental 和测试覆盖。

## 当前分支

当前 `release/0.4.4` 分支默认使用 `bootstrap/0.4.4` 分支产出的过渡编译器自举：

```text
../bootstrap-0.4.4/build/bin/jiangc.next -> build/bin/jiangc.next
```

0.4.4 继承 0.4.3 已实现的语言和编译器能力，包括 package / dependency、Lang Package
自定义语法、core 源码化入口、trait object、MIR/backend、layout/ABI、inline asm、WASI
输出和源码级语言测试。当前分支重点是关键字 options 语法收口、移除 `public [get]`，
以及标准库容器和编译器内部字符串所有权修复。
面向用户的语言文档应描述当前分支的可用语法；历史版本说明只在解释兼容边界时保留。
