# Backend 设计

backend 消费 MIR 和 layout，生成目标产物。当前 backend 只保留 target 骨架；后续 LLVM backend
应放在 `backend/llvm`，LLVM-specific lowering 不写进 MIR 或 layout。

## 输入

- MIR function bodies。
- `LayoutStore` 中的 concrete type layout。
- monomorph `InstancePlan`。
- target 配置。

## LLVM 关系

LLVM IR 是 backend 产物，不是 MIR 的替代品。MIR 保留 Jiang 语义、控制流、place、local 和
类型事实引用；LLVM lowering 负责把这些语义映射到 LLVM type、value、basic block 和 symbol。

backend-specific symbol/mangling 可以从 `MirFunctionKey` 或 backend-specific key 派生。
这层 symbol 只服务目标代码生成，不应该反向写入 MIR。

## 边界

- backend 不重新 type check。
- backend 不从 MIR 自己推导 field offset 或 ABI layout。
- backend 不修改 HIR、MIR 或 `TypeCheckResults`。
- backend 不把 LLVM-specific 表达泄漏到 MIR 数据结构。

## 待设计

- LLVM type lowering。
- function symbol mangling。
- object file emission。
- target triple / data layout 接入。
- debug info 和 source location。
