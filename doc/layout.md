# Layout 设计

layout 是 MIR 之后按需查询或批量物化的 concrete type layout 层。它的顺序位置在 MIR 之后，
但数据来源不是 MIR body。layout 消费 HIR、`TypeCheckResults`、monomorph `InstancePlan` 和
`TargetLayout`，输出 `LayoutStore`。

## 边界

- layout 不回读 AST，不重新 resolve，不重新 type check。
- layout 不修改 HIR、`TypeCheckResults`、`TypeTable`、`InstancePlan` 或 MIR。
- layout 不遍历 MIR 来决定类型布局。
- layout key 表达 concrete type 语义身份，nominal generic type 必须带 type args。
- layout 负责 size、align、stride、field offset 和 target ABI 分类。
- field offset 只存在于 `LayoutStore`，不写回 MIR。
- target 变化必须使 layout 查询失效。

## 顺序

```text
HIR + TypeCheckResults + InstancePlan + TargetLayout
  -> layout query
  -> LayoutStore
```

borrow check 消费 `MIR + TypeCheckResults + LayoutStore`；backend 消费 `MIR + LayoutStore`。
borrow check 在 layout 之后运行，因为 move/copy/drop、niche、可能的 packed/alignment 规则都需要
concrete layout 支撑。

## Store

```text
LayoutStore
  keys: HashTable<LayoutKey, LayoutId>
  layouts: ArrayTable<LayoutId, TypeLayout>
  storage: Arena
```

`LayoutKey` 表达 concrete type 的语义身份。nominal generic type 必须包含
`InstanceKey { def_id, type_args }`，所以 `Box<Int>` 和 `Box<Bool>` 会得到不同 layout。

`TypeLayout` 保存：

- `size`
- `align`
- `stride`
- layout kind
- field layouts
- target ABI 分类

`FieldLayout` 保存 field `DefId?`、field `TypeId`、offset 和 field layout。

## TargetLayout

`TargetLayout` 描述目标平台基础规则：

- pointer size / align
- integer and float builtin layout
- bool layout
- function pointer layout
- aggregate alignment policy

不同 target 的 `LayoutKey` 查询结果不能复用。

## Cycle

layout 需要独立 active stack：

- by-value struct/record 自递归是 layout cycle。
- pointer/reference/slice/function pointer 后续接入后会打断 by-value layout cycle。
- cycle diagnostic 应通过 source map 指向参与 cycle 的 nominal definitions。

## 不变量

- MIR lowering 不依赖 layout。
- layout 不改变类型检查结果。
- backend 不能绕过 `LayoutStore` 自己推导 field offset。
- generic nominal type layout 必须使用 concrete type args。
