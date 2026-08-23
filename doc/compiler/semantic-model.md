# Semantic Model 设计

Semantic Model 是 resolve 之后的高级语义树。它已经完成名字解析，但仍未类型化。
Semantic Model 以 `DefId` 为 owner 组织：每个有语义所有权的 definition 对应一个
`sem.Def`，`sem.NodeId` 只在所属
`sem.Def` 内有效。

## 职责

- 保存 resolved source-level 语义结构。
- 作为 type check、template JIL lowering 和后续工具的主要输入。
- 把 AST 的语法细节整理成更稳定的语义节点。
- 保留 source map 需要的定位信息，但不把 span 写进类型或布局事实。

## 边界

- Semantic Model 不保存 AST id。
- Semantic Model 保存 source-level 语义结构，不保存 CFG。
- Semantic Model 不保存 `TypeId`；类型事实由 `TypeCheckStore` 维护。
- Semantic Model definition ownership tree 使用 `sem.Def.members` 表达。
- Semantic Model body 使用 def-local node table，便于后续按 definition 替换和缓存。
- Semantic Model type ref、pattern、expression 都是 Semantic Model node；type check 才产出 `TypeId`。

## Store

`sem_store.Store` 挂在 `CompilerStore` 中，保存当前 package 的 Semantic Model facts。它不是文件级 `sem.File`，
也不维护 package 级全量 def 顺序。

当前设计避免依赖一个全量 `def_order` 数组。需要遍历 package definition 时，应从
`ModuleGraph` root 和 `sem.Def.members` 出发，按语义 ownership 递归。

## Id

- `DefId` 是跨阶段 definition 句柄。
- `sem.NodeId` 表示某个 `DefId` owner 内的 local Semantic Model node。
- `sem.NodeId` 不是全局连续 node index，也不是 hash key。
- 需要从 `sem.NodeId` 找类型时查 `TypeCheckStore`，不写回 Semantic Model node。

## 不变量

- resolve 完成后才能生成 Semantic Model。
- type check 不回读 AST，只消费 Semantic Model。
- JIL lowering 不重新 resolve，也不重新 type check。
- Semantic Model 是 session-local 语义表；每轮 `CompilerContext.begin_compilation` 都会重建 `sem_store.Store`。
- 长期增量只保存稳定 fingerprint 和 public generic Semantic Model template，
  不保存当前轮的 `sem.NodeId` / `sem.Def`。
