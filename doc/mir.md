# MIR 设计

MIR 是 type check 之后的可执行语义 IR，用来承接后续 borrow check、drop 插入、优化和 backend。
MIR 的输入是 HIR、`TypeCheckResults`、monomorph `InstancePlan` 和 `ModuleGraph`；它不回读 AST，
不重新 resolve，也不重新 type check。

## 边界

- MIR 以 function 为主要 lowering/codegen 单位。
- MIR body 由 locals、basic blocks、statements 和 terminators 组成。
- HIR block 是表达式/语句容器；MIR basic block 是 CFG 节点。
- MIR place 表达语义位置，例如 local、field、index、deref。
- MIR field projection 保存 field `DefId` 和 field `TypeId`，不保存 field offset。
- 泛型模板函数不直接生成 MIR body；只有 `InstancePlan` 中的 concrete function instance
  会生成 MIR body。
- MIR lowering 不依赖 layout，也不等待 `LayoutStore` 先计算完成。

MIR 生成完成后，borrow check 和 backend 可以把 MIR 与 layout 查询结果组合使用。

## 结构

第一版 MIR 使用非 SSA 的 local + assignment 形式：

```text
MirStore
  functions: MirFunction table
  bodies: MirBody table

MirBody
  locals: ArrayList<MirLocal>
  blocks: ArrayList<BasicBlock>

BasicBlock
  statements: ArrayList<Statement>
  terminator: Terminator
```

`MirLocal` 包含 local kind、`TypeId` 和来源信息。function body 至少包含 return local、
params 和 user locals。temporary local 由 lowering 在需要 materialize complex operand 时创建。

## Operand、Rvalue、Place

- `Operand` 表达可以直接使用的值，例如 constant、copy place、move place。
- `Rvalue` 表达一次 assignment 右值，例如 use operand、binary op、aggregate、ref。
- `Place` 表达可读写位置，例如 local、field projection、index projection、deref projection。

complex operand 必须先 lower 到 temporary local，不能在 operand 中嵌套表达式树。

## Terminator

control flow 由 terminator 表达：

- `return`
- `goto`
- `branch`
- `call`
- `switch`
- `unreachable`

`call` 使用 terminator，返回值写入 destination，再跳 continuation。这样 borrow check 和后续
异常/cleanup 路径都能在 CFG 上表达。

## Lowering 规则

- HIR block lowering 按顺序把 statements 写入当前 MIR block。
- block tail expression 写入 destination，或在 function body 中写入 return local。
- `if` 使用 branch / then / else / join blocks。
- `loop` 和 `while` 使用 header / body / exit blocks，并维护 loop target stack。
- `return expr` 先把 expr lower 到 return local，再生成 return terminator。
- `switch` 先等 pattern lowering 规则稳定后接入。
- field/member access lowering 生成 concrete `MirPlace` projection。

## 泛型实例

MIR lowering 接收 `InstancePlan`。非泛型函数按 `DefId` 直接 lower；泛型函数只按 concrete
`InstanceKey` lower。lowering 中的 type substitution 只用于当前 concrete body。

## 不变量

- MIR 不保存 AST id。
- MIR local 保留 `TypeId`；类型来源是 `TypeCheckResults`，不是 HIR nullable type 字段。
- MIR 不保存 field offset、size、align 或 ABI 信息。
- backend-specific symbol/mangling 不写入 MIR。
