# Type Check 设计

type check 消费 resolved HIR，输出 `TypeCheckStore`。它负责类型引用解析、function
signature、expression/pattern type、member selection、generic arg 检查、trait bound 基础规则
和 lazy type query cycle guard。

## 输入

- `HirStore`：resolved HIR。
- `ResolveStore` / `DefStore` / namespace facts：名字和 definition 事实。
- `TypeStore`：类型驻留和 builtin type。
- `CompilerContext.diagnostics`：诊断输出。

type check 不回读 AST，不重新 resolve，也不直接修改 HIR。

## 输出

`TypeCheckStore` 是 MIR、layout、borrow check 和 backend 的共享输入：

- `DefId -> TypeId`：definition 的类型结果。
- `HirId -> TypeId`：expression、pattern、type ref 等 HIR node 的类型结果。
- member access、variant pattern、generic call/type ref 的语义选择 side table。
- `$` builtin operation 的语义选择 side table。`@builtin(value, Pattern)` /
  `@builtin(type, Pattern)` 只描述 receiver pattern、签名和 where 约束；
  type check 负责选择具体 builtin operation 并记录 lowering kind。
- default/named call argument 的签名顺序重排结果。
- generic instantiation 所需的 type args。
- 错误状态和必要诊断。

普通 definition 的类型结果写入 `TypeCheckStore`，不写回 `TypeStore`。

## Lazy Query

type check 需要 lazy query 入口处理跨定义依赖：

- `type_of_def`
- `function_signature`
- `field_type`
- alias target type
- associated type result

alias cycle、associated type default cycle、trait bound dependency 都应围绕这些 query 入口处理，
不能通过扫描 AST 临时判断。

## 泛型和 Trait

type check 只负责证明泛型和 trait 约束在源程序层面成立。它不复制 HIR，也不生成 concrete
函数体。需要 codegen 的 concrete instances 由 monomorph 阶段收集。

基础规则：

- generic params 进入 HIR ownership tree。
- semantic `NamedType` 保存 type args。
- generic type arg arity 在 type check 阶段诊断。
- nominal trait list / trait parent list 必须指向 trait def。
- associated type trait bounds 必须指向 trait def。
- where constraints 和 projected equality 写入类型事实，供 member lookup 和 monomorph 使用。

## 不变量

- type check 失败时后续 MIR/layout/backend 必须拒绝继续消费。
- HIR node 漏写类型属于编译器 bug；后续阶段可以诊断并停止，但不应补推断。
- `TypeCheckStore` 可以在 lowering/backend 完成后释放；长期增量再引入版本化缓存策略。
