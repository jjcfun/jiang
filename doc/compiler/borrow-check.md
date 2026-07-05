# Borrow Check 设计

borrow check 在 MIR 和 layout 之后运行。它消费 MIR 控制流、`TypeCheckStore` 类型事实和
`LayoutStore` 中的 concrete layout，不重新推导类型，不重新计算布局。

Jiang 的 borrow check 只处理所有权、move/use-after-move、引用逃逸和析构安全边界。
它不检查 shared/mutable aliasing，不负责 data-race freedom，也不根据外层 slot
是否可变决定内部字段能否写入。并发安全和数据竞争策略后续作为单独语言机制设计。
裸指针的创建、转换和显式释放由 sema 的 unsafe effect gate 检查；borrow check 不再重复做
unsafe/capability gate。borrow check 只在这些操作影响 owner/lifetime/drop safety 时介入。

## 输入

- MIR locals、places、moves、borrows、assignments 和 CFG。
- `TypeCheckStore` 中的 expression/local/member 类型事实。
- `LayoutStore` 中的 copy/drop/layout 相关 concrete type 信息。
- source map，用于把诊断定位回源码。

## 职责

- 检查 move 后使用。
- 检查 owner 被 move/drop/free 后，依赖它的引用不能继续使用。
- 检查局部引用不能逃出来源 owner 的有效范围。
- 检查引用存入字段、返回值、闭包捕获等逃逸位置时满足 lifetime 约束。
- 检查需要析构的值在所有 CFG 路径上至多析构一次。
- 为 drop 插入和后续 backend 提供约束结果。

返回引用只来自同一种输入来源时，该来源 lifetime 默认覆盖当前函数返回值。如果函数显式声明了
`@life(... > return)`，borrow check 只允许标注中的来源。返回引用可能来自多种输入来源时，必须
显式写出所有允许来源；跨函数调用返回引用、返回含引用字段的聚合值、或 public API 需要表达
返回来源时，仍应使用 `@life(source > return)`。

mutability 的基本 assignment 检查已经在 type check 阶段完成；borrow check 只处理需要 CFG
和 lifetime 信息的约束。字段、tuple 元素、union payload、数组元素能否写入，只由对应成员
类型自己的 `!` 可变性决定；不由 owner/local/reference slot 的可变性决定。

## 数据结构

第一版需要三类核心表：

```text
MovePath
  place: MirPlace
  parent: MovePathId?
  children: MovePathId[]

Loan
  borrowed_place: MovePathId
  handle_kind: reference | raw_pointer
  issued_at: MirLocation
  expires_at: RegionId?

BorrowCheckStore
  move_state per block
  active loans per block
  diagnostics
```

`MovePath` 按 MIR place tree 建模。`x`、`x.field`、`x.field.inner` 是同一棵 move path tree
里的不同节点。移动父 path 会使子 path 不可用；重新赋值父 path 会重新初始化整棵子树。

`Loan` 表示某个 MIR location 产生的引用或指针视图。`handle_kind` 只区分语言引用和裸指针，
不表达 shared/mutable 或只读/独占语义。第一版只需要足够表达防悬垂：loan 的来源 place
必须活到所有使用点之后。

## 分析流程

```text
build move paths from MIR places
  -> compute copy/drop category from TypeCheckStore + LayoutStore
  -> forward dataflow: maybe-uninitialized / maybe-moved
  -> forward dataflow: active loans
  -> validate returns, stores, calls and drops
  -> emit BorrowCheckStore
```

MIR basic block 是 borrow check 的 CFG 单元。每条 statement/terminator 内部的位置用
`MirLocation { block, statement_or_terminator_index }` 表达。

## Layout 的作用

layout 不决定 borrow 语义，但它会影响以下分类：

- 类型是否零大小。
- 类型是否进入 `Movable` 语义，以及是否需要 runtime drop。
- 类型是否允许 implicit copy。
- packed/alignment 规则是否限制对字段取引用。

borrow check 通过 sema drop query 判断所有权/drop 语义，通过 `LayoutStore` 查询 field offset
或 ABI layout；不能从 MIR 自己推导 ABI layout。

## Drop 插入

drop elaboration 不在 type check 或 layout 中完成，也不由 backend 临时推导。长期顺序固定为：

```text
MIR lowering
  -> borrow check 验证已有 drop/隐式 drop 候选是否合法
  -> drop elaboration 改写 MIR，插入具体 drop/deinit CFG
  -> backend
```

第一轮 borrow check 只处理语义合法性：一个需要 drop 的 place 在所有 CFG 路径上至多
drop 一次，并且 drop 时不会使仍然活跃的 loan 悬垂。它不展开自定义 `deinit` body，
也不生成字段析构 CFG。

drop elaboration 先读取 sema drop query。只有 `Movable` 类型会被考虑自动 drop；
`T*` / `T[*]` 派生 place 是 raw memory，不做隐式 drop。确认需要 drop 后，再读取 layout
的 drop category 决定具体展开方式：

- `no_drop`：不插入 drop。
- `trivial_drop` / `recursive_drop`：插入字段/owner pointer 的自动 drop 路径。
- `custom_drop`：先调用 nominal type 的 `deinit`，再按语言规则插入自动递归字段析构。

`custom_drop` 的事实来自 HIR owner 上的 `custom_deinit_def`。resolve 只记录该 fact；
layout 根据 fact 返回 `custom_drop`；真正调用哪个 deinit body 由 drop elaboration 在 MIR 层展开。

## 诊断

诊断必须通过 source map 回到 HIR/source 位置：

- move 发生的位置。
- use-after-move 的使用位置。
- 引用来源 owner 的定义/最后有效位置。
- 引用逃逸的位置。

MIR 不保存 AST id；需要源码定位时通过 lowering 写入的 `SourceMap` 查询。

## 边界

- borrow check 不回读 AST。
- borrow check 不重新做 name resolution。
- borrow check 不重新 type check。
- borrow check 不自己计算 field offset 或 ABI layout。
- borrow check 的诊断通过 HIR/source map 定位，不要求 MIR 保存 AST id。

## 待设计

- `T&`、`T&!`、`T[]&` 和 raw pointer 的精确 lifetime 规则。
- copy/drop trait 或 builtin copy 规则。
- packed/alignment 对 borrow 的限制。
- 与 `@life(...)` annotation 的集成。
- 闭包捕获和 async/generator 状态机的借用规则。
