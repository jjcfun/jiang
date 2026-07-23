# Borrow Check 设计

borrow check 在 MIR 和 layout 之后运行。它消费 MIR 控制流、`TypeCheckStore` 类型事实和
`LayoutStore` 中的 concrete layout，不重新推导类型，不重新计算布局。

Jiang 的 borrow check 处理所有权、move/use-after-move、引用逃逸、析构安全边界，以及
`T&!` 的唯一可变借用。共享引用可以共存；只要某个 `T&!` 仍会被使用，指向重叠 place 的
共享借用、另一个可变借用和对来源 place 的直接访问都会报错。借用活跃区间按 CFG 上的
后续使用计算，因此最后一次使用结束后，来源 place 可以恢复访问。

唯一借用只约束语言引用，不负责证明并发代码整体没有 data race。跨 domain 的可变引用另由
domain borrow 规则检查；同步共享状态仍应使用 mutex、atomic 等显式机制。
裸指针的创建、转换和显式释放由 sema 的 unsafe effect gate 检查；borrow check 不再重复做
unsafe/capability gate。`T*` / `T*!` 不参与 shared/mutable alias 冲突证明；borrow check 只在
裸指针操作影响 owner、lifetime 或 drop safety 时介入。

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
- 检查 `T&!` 与仍活跃的共享/可变借用之间不存在重叠 place 冲突。
- 检查 `T&!` 存活期间不能直接读写其来源 place。
- 检查 `T&!` / `T[]&!` 的普通按值传播执行 move；调用参数位置可以建立临时 reborrow。
- 检查同一调用中多个 `T&!` 实参不能指向重叠 place。
- 检查可变 receiver 或可变引用参数的要求已经由函数签名中的 `T&!` 表达。
- 检查需要析构的值在所有 CFG 路径上至多析构一次。
- 为 drop 插入和后续 backend 提供约束结果。

返回引用只来自同一种输入来源时，该来源 lifetime 默认覆盖当前函数返回值。如果函数显式声明了
`@life(... > return)`，borrow check 只允许标注中的来源。返回引用可能来自多种输入来源时，必须
显式写出所有允许来源；跨函数调用返回引用、返回含引用字段的聚合值、或 public API 需要表达
返回来源时，仍应使用 `@life(source > return)`。

binding/place 的基本可写性由 type check 阶段检查：`T name!` 表示该存储位置可写，但不改变
`TypeId`。字段、tuple 元素、union payload 和数组元素的写能力沿 place 传播；共享引用 `T&`
不会授予写能力。borrow check 再处理需要 CFG 与 lifetime 信息的唯一借用冲突。

## 数据结构

当前实现使用以下核心结构：

```text
MovePath
  place: MirPlace
  parent: MovePathId?
  children: MovePathId[]

Loan
  kind: shared_reference | mutable_reference | raw_pointer
  source: MovePathId
  target: MovePathId
  domain_type: DefId?

ActiveLoanFact
  target: MovePathId
  loan_id: LoanId

BorrowCheckStore
  ok
  loans
  inferred_return_lifetime_sources
```

`MovePath` 按 MIR place tree 建模。`x`、`x.field`、`x.field.inner` 是同一棵 move path tree
里的不同节点。移动父 path 会使子 path 不可用；重新赋值父 path 会重新初始化整棵子树。

moved/live dataflow 以 `MovePathId` 为下标存入 bitset。父 path 的清理通过 move-path child table
遍历真实子树，不扫描整个函数的全部 path，也不为每个候选 path 重走祖先链。consumed Task 与 active
loan 仍是稀疏事实列表；StorageDead 清理先探测是否存在匹配事实，无匹配时不重建列表。这样顺序创建
大量 scoped Task 时，清理成本与当前 local 的子树和实际事实数量相关，而不是与全函数 MovePath 数量
相乘。

`Loan` 表示某个 MIR location 产生的引用或指针视图。当前实现区分 shared reference、mutable
reference 和 raw pointer view，并同时记录来源与承载该 view 的目标 place。引用 loan 既用于
lifetime/逃逸检查，也用于 shared/mutable 冲突检查；raw pointer view 不参与别名排他性判断。

## 分析流程

```text
build move paths from MIR places
  -> compute copy/drop category from TypeCheckStore + LayoutStore
  -> forward dataflow: maybe-uninitialized / maybe-moved
  -> forward dataflow: active loans
  -> query future uses to shorten loan activity after the last use
  -> validate returns, stores, calls and drops
  -> emit BorrowCheckStore
```

MIR basic block 是 borrow check 的 CFG 单元。每条 statement/terminator 内部的位置用
`MirLocation { block, statement_or_terminator_index }` 表达。

## Layout 的作用

layout 不决定 borrow 语义，但它会影响以下分类：

- 类型是否零大小。
- 类型是否有 concrete runtime drop category。
- Copyable/Movable 事实由 sema 提供；layout 不自行推导复制或移动语义。
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

drop elaboration 先读取 sema drop query。是否需要 runtime drop 由 ownership、字段与自定义 `deinit`
决定，不由 Movable 标记替代；`!Movable` 值仍在固定 place 的生命周期末尾正常析构。
`T*` / `T*!` 派生 place 是 raw memory，不做隐式 drop。确认需要 drop 后，再读取 layout
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

- 更精确的 region/lifetime 推导，以及复杂循环和聚合 reborrow 的诊断质量。
- packed/alignment 对 borrow 的限制。
- 与 `@life(...)` annotation 的集成。
- 闭包捕获和 async/generator 状态机中跨挂起点借用的完整规则。
