# Jiang Stage1 LLVM Spike Plan

## 目标

在 build system、runtime 边界与 UTF-8 字节串支持收口之后，当前下一阶段不直接把正式后端从 `JIR -> C` 切到 LLVM，而是先做一个受控的 LLVM spike。

这个 spike 的目标不是替换主线，而是回答三个问题：

- 当前 Stage1 前端和 JIR 是否足够稳定到可以落 LLVM IR
- 冻结后的 runtime ABI 是否足够支撑 LLVM 路线的最小闭环
- 未来若继续扩大 LLVM 覆盖面，最小阻塞点会落在哪些语义和接口上

本阶段完成条件：

- 仓库中有一条明确标记为实验性的 LLVM 后端入口
- 该入口可覆盖最小语义子集并产出 LLVM IR 或通过 `clang/llvm` 工具链编译
- 现有 `JIR -> C` 仍是默认且稳定的正式主线
- spike 路径有单独 smoke，不混入主线回归语义判断

## Spike 范围

本阶段只覆盖最小子集：

- 基础类型：`Int`、`Bool`
- 表达式：整数常量、局部变量读写、基础算术、基础比较
- 控制流：`if`、`while`
- 函数：普通函数定义、参数、调用、`return`
- 最小 runtime 交互：继续围绕当前 6 个 `__intrinsic_*`

本阶段明确不做：

- 不替换默认 `jiang -> C -> cc`
- 不做完整标准库 lowering
- 不做完整模块图迁移
- 不做 Union / Pattern / Binding 全覆盖
- 不做 LLVM 优化管线设计
- 不做 platform ABI 抽象层

## 实现策略

建议把 LLVM spike 做成一条平行实验路径，而不是直接侵入当前主线：

- 保留现有 `lower_to_jir -> jir_generate_c` 不动
- 新增实验性 LLVM 输出入口，例如 `--emit-llvm` 或内部测试专用路径
- 先从当前 JIR 中挑最小稳定子集做 lowering，不尝试一开始就覆盖全部 JIR 指令
- 优先生成可读的 LLVM IR，再决定是否进一步驱动 `clang` / `llc`

实现时应固定这几个约束：

- runtime ABI 继续只认当前 6 个 intrinsic
- `std` public surface 不因 spike 扩张
- `jiang.build`、build-system、模块模型和 UTF-8 语义都不改
- 任何 LLVM spike 失败都不能影响现有默认编译链与测试脚本

## 验收与停止条件

通过下面这些条件，才认为 spike 有结果：

- 至少一个最小样例可稳定产出 LLVM IR
- 至少一个最小样例可通过 LLVM 工具链继续走到可执行产物
- spike 覆盖的最小语义子集有单独 smoke
- `bash ./script/test.sh` 继续全通过

若遇到下面任一情况，应视为 spike 停止点，而不是继续扩张实现：

- 需要新增 runtime intrinsic 才能跑通最小闭环
- 需要大幅重写当前 JIR 才能映射 LLVM
- 需要先引入 MIR / SSA 重构才能推进

出现这些情况后，应先回到设计评估，再决定是否继续投入 LLVM 主线。
