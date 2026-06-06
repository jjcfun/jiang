# Layout 设计

layout 是按需查询或批量物化的 concrete type layout 层。它的数据来源不是 MIR body。
layout 消费 HIR、`TypeCheckStore`、monomorph `MonomorphStore` 和 `TargetLayout`，
输出 `LayoutStore`。MIR lowering 可以触发 layout 查询，但不能自己计算 layout。

## 边界

- layout 不回读 AST，不重新 resolve，不重新 type check。
- layout 不修改 HIR、`TypeCheckStore`、`TypeStore`、`MonomorphStore` 或 MIR。
- layout 不遍历 MIR 来决定类型布局。
- layout key 表达 concrete type 语义身份，nominal generic type 必须带 type args。
- layout 负责 size、align、stride 和 field offset。
- C ABI 参数/返回值分类由 backend ABI classifier 消费 layout facts 后完成。
- field offset 只存在于 `LayoutStore`，不写回 MIR。
- target 变化必须使 layout 查询失效。

## 顺序

```text
HIR + TypeCheckStore + MonomorphStore + TargetLayout
  -> layout query
  -> LayoutStore
```

MIR lowering、borrow check、drop elaboration 和 backend 都可以按需触发 layout 查询。
layout 不要求在任何阶段之前批量完成。

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

`FieldLayout` 保存 field `DefId?`、field `TypeId`、offset 和 field layout。

`DropCategoryKind` 保存 concrete type 的结构化析构类别。它不是自动 drop 的入口；
自动 drop 入口由 sema drop query 根据 `Movable` 语义决定。

- `no_drop`：标量、function pointer、non-owning handle 等不需要析构。
- `trivial_drop`：aggregate/optional/union 本身不需要自定义析构，成员也没有 owning drop。
- `recursive_drop`：类型自身或成员包含 `T^` owning pointer，需要 drop elaboration 递归处理。
- `custom_drop`：nominal type 定义了 `deinit`，drop elaboration 先调用 custom deinit，
  再继续展开递归字段自动 drop。

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
- pointer/reference/slice/function pointer 会打断 by-value layout cycle。
- cycle diagnostic 应通过 source map 指向参与 cycle 的 nominal definitions。

## 不变量

- MIR lowering 不自己计算 layout；布局事实统一来自 `LayoutStore`。
- layout 不改变类型检查结果。
- backend 不能绕过 `LayoutStore` 自己推导 field offset。
- backend ABI classifier 可以读取 `LayoutStore` 的 size/align，但不写 layout facts。
- generic nominal type layout 必须使用 concrete type args。
