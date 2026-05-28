# Monomorph 设计

monomorph 运行在 type check 之后、concrete MIR lowering 之前。它不是 type check 的一部分，
也不负责证明泛型约束正确；它只从 HIR reachable tree 和 `TypeCheckResults` 中收集需要生成的
concrete instances。

## 职责

- 从 root reachable function 开始收集需要生成的 concrete function instances。
- 收集 concrete nominal type instances，供 layout 和 backend 查询。
- 为 MIR lowering 提供 `MonomorphInstances`。
- 提供 generic member type substitution 查询。

## 核心数据

- `InstanceKey = DefId + TypeArgList`。
- `MonomorphInstances` 保存 concrete function instance 和 nominal type instance。
- `SubstitutionMap` 描述一个 generic owner 在某个 concrete instance 下的 type parameter 替换。

`SubstitutionMap` 不放在 `TypeCheckResults` 中。它是 monomorph/MIR lowering 针对单个 concrete
instance 的临时上下文，生命周期短于 type check 全局结果。

## 边界

- generic template function 不直接生成 MIR body。
- monomorph 不复制 HIR，不修改 `TypeCheckResults`。
- monomorph 不负责 concrete type layout；layout 使用 `InstanceKey` 查询 concrete nominal type。
- 实例化过程需要 active stack，避免递归实例无限生成。
- 递归实例化诊断接入 MIR lowering 或 monomorph 入口。

## 不变量

- 所有 concrete MIR function 都应来自 `MonomorphInstances` 或非泛型 reachable function。
- generic nominal type 的 field type 替换通过 `concrete_member_type` 查询。
- monomorph 不能重新做 name resolution 或 trait bound checking。
