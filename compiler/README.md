# Jiang Compiler Mainline

`compiler/` 是 Jiang 下一代编译器主线目录。

这里放的是未来要成为主实现的 Jiang 编译器代码，而不是 Stage1 的 bootstrap-first 过渡资产。
但按 README 当前的阶段定义，`compiler/` 里的实现仍属于 Stage1 后期主线，不应直接视为正式 Stage2。

当前仓库中的目录分工固定为：

- `src/`: 宿主 C 编译器，实现当前稳定的 Stage1/host compiler
- `bootstrap/`: Stage1 Jiang 自举资产、golden 与 smoke
- `compiler/`: Stage1 后期主线中用于孵化下一代 Jiang 编译器的长期目录

当前 `compiler/` 不直接复制整份 `bootstrap/`，而是先从一个最小但真实可运行的主链长出来。
当前目标不是宣布进入正式 Stage2，而是把 Stage1 后期主线从 `bootstrap/` 的实验资产进一步推进到一个更长期的编译器目录。

当前先不提前拆子目录，而是先用根层模块把最小链路站住：

- `compiler/compiler.jiang`
  当前主入口
- `compiler/token.jiang`
  token 定义
- `compiler/lexer.jiang`
  最小词法分析
- `compiler/parser.jiang`
  最小语法分析
- `compiler/ast.jiang`
  最小 AST 结构
- `compiler/semantic.jiang`
  最小语义层骨架
- `compiler/hir.jiang`
  最小 HIR 骨架
- `compiler/jir.jiang`
  最小 JIR 骨架
- `compiler/c.jiang`
  Stage1 后期最小 `emit_c` 占位层

## 当前原则

- Stage1 继续作为稳定参考实现
- 下一代编译器主线在同仓开发，不新建仓库
- LLVM 继续作为可选完整后端，不在这里立刻切默认
- 当前阶段优先做结构重组与最小主链，不优先做新语法和大规模标准库扩张

## 当前下一步

按 `doc/plan_stage2_start.md` 推进 `compiler/` 主线：

1. 先把最小前端链继续做实
2. 再逐步替换掉只做 dump 的骨架层
3. 最后再决定何时进入 README 定义里的正式 Stage2
