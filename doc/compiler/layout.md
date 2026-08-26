# Layout 设计

layout 是按需物化的 concrete type layout 层。它的数据来源不是 JIL body。
layout 消费 Semantic Model、`TypeCheckStore`、`TypeStore` 和 `TargetLayout`，输出 `LayoutStore`。
JIL、drop 和 backend 可以触发 layout 查询，但不能自己计算 layout。

## 边界

- layout 不回读 AST，不重新 resolve，不重新 type check。
- layout 不修改 Semantic Model、`TypeCheckStore`、`TypeStore` 或 JIL。
- layout 不遍历 JIL 来决定类型布局。
- layout key 表达 concrete type 语义身份，nominal generic type 必须带 type args。
- layout 负责 size、align、stride 和 field offset。
- C ABI 参数/返回值分类由 backend ABI classifier 消费 layout facts 后完成。
- field offset 只存在于 `LayoutStore`，不写回 JIL。
- target 变化必须使 layout 查询失效。

## 顺序

```text
Semantic Model + TypeCheckStore + TypeStore + TargetLayout
  -> layout query
  -> LayoutStore
```

JIL lowering、borrow check、drop elaboration 和 backend 都可以按需触发 layout 查询。
layout 不要求在任何阶段之前批量完成。

## Store

```text
LayoutStore
  type_layouts: WyHashMap<TypeId, LayoutId>
  layouts: ArrayTable<LayoutId, TypeLayout>
  storage: Arena
```

`TypeId` 表达 concrete type 的语义身份。nominal generic instance 的 `TypeId` 已包含其 type
arguments，所以 `Box<Int>` 和 `Box<Bool>` 会得到不同 layout。

`TypeLayout` 保存：

- `size`
- `align`
- `stride`
- layout kind
- field layouts

`FieldLayout` 保存 field `DefId?`、field `TypeId`、offset 和 field layout。

`DropCategoryKind` 保存 concrete type 的结构化析构类别。它不是自动 drop 的入口；
自动 drop 入口由 sema drop query 根据 ownership、Copyable 和 concrete drop category 决定。
`Movable` 只约束值能否改变地址，不能代替析构判定。

- `no_drop`：标量、function pointer、non-owning handle 等不需要析构。
- `trivial_drop`：aggregate/optional/payload enum 本身不需要自定义析构，成员也没有 owning drop。
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
- `T&` / `T^` / `T*` / `T*!` layout：pointer-sized scalar；raw pointer 不携带 length 或 sentinel
- `T[]` / `T[:0]` 是 unsized array type，没有独立 by-value layout；`T[]&` / `T[:0]&` borrowed view layout 是 pointer + pointer-sized unsigned length，sentinel 不改变物理 layout，只改变类型语义
- `T[]^` / `T[:0]^` owned unsized array handle 当前也使用 pointer + pointer-sized unsigned length，但它表达 buffer 所有权，drop 时需要析构元素并释放 allocation
- 无 payload enum layout：使用 enum underlying integer scalar
- payload enum layout：使用同一 underlying integer type 作为 tag，并为最大 payload 保留共享 storage
- aggregate alignment policy

不同 target 的 `LayoutKey` 查询结果不能复用。
因此 `LayoutStore.set_target(...)` 必须清空已缓存的 `keys/layouts/storage`。

aggregate layout 第一版使用自然 ABI 规则：每个 field offset 按 field align
向上对齐，最终 size/stride 按所有 field align 的最大值向上对齐。

## Cycle

layout 需要独立 active stack：

- by-value struct 自递归是 layout cycle。
- pointer/reference/slice handle/function pointer 会打断 by-value layout cycle；裸 unsized array type 不能作为普通 by-value 字段。
- cycle diagnostic 应通过 source map 指向参与 cycle 的 nominal definitions。

## 不变量

- JIL lowering 不自己计算 layout；布局事实统一来自 `LayoutStore`。
- layout 不改变类型检查结果。
- backend 不能绕过 `LayoutStore` 自己推导 field offset。
- backend ABI classifier 可以读取 `LayoutStore` 的 size/align，但不写 layout facts。
- generic nominal type layout 必须使用 concrete type args。
