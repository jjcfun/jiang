# HIR 设计

HIR 是 resolve 之后的高级语义树。它已经完成名字解析，但仍未类型化。HIR 以 `DefId`
为 owner 组织：每个有语义所有权的 definition 对应一个 `HirDef`，`HirId` 只在所属
`HirDef` 内有效。

## 职责

- 保存 resolved source-level 语义结构。
- 作为 type check、monomorph、MIR lowering 和后续工具的主要输入。
- 把 AST 的语法细节整理成更稳定的语义节点。
- 保留 source map 需要的定位信息，但不把 span 写进类型或布局事实。

## 边界

- HIR 不保存 AST id。
- HIR 保存 source-level 语义结构，不保存 CFG。
- HIR 不保存 `TypeId`；类型事实由 `TypeCheckResults` 维护。
- HIR definition ownership tree 使用 `HirDef.members` 表达。
- HIR body 使用 def-local node table，便于后续按 definition 替换和缓存。
- HIR type ref、pattern、expression 都是 HIR node；type check 才产出 `TypeId`。

## Store

`HirStore` 挂在 `QuerySystem` 中，保存当前 package 的 HIR facts。它不是文件级 `HirFile`，
也不维护 package 级全量 def 顺序。

当前设计避免依赖一个全量 `def_order` 数组。需要遍历 package definition 时，应从
`ModuleGraph` root 和 `HirDef.members` 出发，按语义 ownership 递归。

## Id

- `DefId` 是跨阶段 definition 句柄。
- `HirId` 表示某个 `DefId` owner 内的 local HIR node。
- `HirId` 不是全局连续 node index，也不是 hash key。
- 需要从 `HirId` 找类型时查 `TypeCheckResults`，不写回 HIR node。

## 不变量

- resolve 完成后才能生成 HIR。
- type check 不回读 AST，只消费 HIR。
- MIR lowering 不重新 resolve，也不重新 type check。
- 后续增量应围绕 `DefId` owner 粒度替换 HIR body，而不是按 source file 保存 `HirFile`。
