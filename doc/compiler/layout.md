# Layout 设计

layout 是 MIR 之后按需查询或批量物化的 concrete type layout 层。它的顺序位置在 MIR 之后，
但数据来源不是 MIR body。layout 消费 HIR、`TypeCheckResults`、monomorph `MonomorphInstances` 和
`TargetLayout`，输出 `LayoutStore`。

## 边界

- layout 不回读 AST，不重新 resolve，不重新 type check。
- layout 不修改 HIR、`TypeCheckResults`、`TypeTable`、`MonomorphInstances` 或 MIR。
- layout 不遍历 MIR 来决定类型布局。
- layout key 表达 concrete type 语义身份，nominal generic type 必须带 type args。
- layout 负责 size、align、stride、field offset 和 target ABI 分类。
- field offset 只存在于 `LayoutStore`，不写回 MIR。
- target 变化必须使 layout 查询失效。

## 顺序

```text
HIR + TypeCheckResults + MonomorphInstances + TargetLayout
  -> layout query
  -> LayoutStore
```

borrow check 消费 `MIR + TypeCheckResults + LayoutStore`；drop elaboration 和 backend 消费
`MIR + LayoutStore`。layout 查询由这些阶段按需触发，不要求在 MIR lowering 之前批量完成。

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

`DropCategoryKind` 保存 concrete type 的析构类别：

- `no_drop`：标量、function pointer、non-owning handle 等不需要析构。
- `trivial_drop`：aggregate/optional/union 本身不需要自定义析构，成员也没有 owning drop。
- `recursive_drop`：类型自身或成员包含 `T^` owning pointer，需要 drop elaboration 递归处理。
- `custom_drop`：nominal type 定义了 `deinit`，drop elaboration 先调用 custom deinit，
  再继续展开 owning field 自动 drop。

## TargetLayout

`TargetLayout` 描述目标平台基础规则：

- pointer size / align
- integer and float builtin layout
- bool layout
- function pointer layout
- optional layout：第一版使用显式 `{ tag, payload }`，不做 niche 优化
- `T&` / `T^` / `T*` / `T[*]` layout：pointer-sized scalar，layout key 保留 handle kind
- `T[]` layout：pointer + pointer-sized unsigned length
- enum layout：当前 enum 无 associated value，使用 target int discriminant scalar
- union layout：Jiang union 是 tagged union，第一版使用 target int tag + max payload slot
- aggregate alignment policy

不同 target 的 `LayoutKey` 查询结果不能复用。
因此 `LayoutStore.set_target(...)` 必须清空已缓存的 `keys/layouts/storage`。

aggregate layout 第一版使用自然 ABI 规则：每个 field offset 按 field align
向上对齐，最终 size/stride 按所有 field align 的最大值向上对齐。

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
