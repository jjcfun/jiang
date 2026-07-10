# MIR 设计

MIR 是 type check 之后的可执行语义 IR，用来承接后续 borrow check、drop 插入、优化和 backend。
MIR 的输入是 HIR、`TypeCheckStore`、monomorph `MonomorphStore`、`ModuleGraph`
和按需查询的 `LayoutStore`；它不回读 AST，不重新 resolve，也不重新 type check。

## 边界

- MIR 以 function 为主要 lowering/codegen 单位。
- MIR body 由 locals、basic blocks、statements 和 terminators 组成。
- HIR block 是表达式/语句容器；MIR basic block 是 CFG 节点。
- MIR place 表达语义位置，例如 local、field、index、deref。
- MIR field projection 保存 field `DefId`、field `TypeId` 和 layout field index，不保存 field offset。
- 泛型模板函数不直接生成 MIR body；只有 `MonomorphStore` 中的 concrete function instance
  会生成 MIR body。
- MIR lowering 不自己计算 layout，但可以按需查询 `LayoutStore` 来固定 field index、type info
  和 aggregate representation。layout 不需要在 MIR lowering 前批量完成。
- HIR `for in` 在 MIR 中统一降成 index-loop CFG；range、array、slice 只影响 index 来源。

MIR 生成完成后，borrow check、drop elaboration 和 backend 会继续把 MIR 与 layout 查询结果
组合使用。

`$` Intrinsic Operation 不在 MIR lowering 中重新按文本猜测。type check 先根据
`@intrinsic(value, Pattern)` / `@intrinsic(type, Pattern)` 的 receiver pattern 和 where 约束
选出 builtin lowering kind；MIR lowering 只消费这个 side table。带所有权副作用的
operation 需要显式表达：

- `move`：生成所有权转移，源 place 失效。
- `forget`：源 place 失效，不生成释放。
- `free`：生成释放，并使 receiver place 失效。

## Drop Elaboration

MIR lowering 初始产物只表达源码中已经显式形成的控制流和当前阶段能确定的 drop terminator。
隐式析构路径不在 HIR lowering 或 type check 中展开，而是在 borrow check 验证后由 drop
elaboration 改写 MIR。

固定顺序如下：

```text
HIR/type facts -> initial MIR
  -> borrow check
  -> drop elaboration
  -> backend
```

drop elaboration 的职责：

- 根据 locals 的 live range 和 CFG exit 插入隐式 drop。
- 只对 sema drop query 判定为需要 runtime drop 的 `Movable` 类型生成 drop。
- 对 `custom_drop` nominal type 先生成 `deinit` call，再生成自动递归字段 drop。
- 对 `recursive_drop` 类型递归展开字段/owner pointer drop。
- 保持所有插入的控制流仍然是普通 MIR basic block / terminator，不引入 backend-only 节点。

borrow check 负责在 drop elaboration 前证明已有 move/drop/use 不变量。drop elaboration 先按
sema drop query 判断是否需要 runtime drop，再按 layout drop category 改写 CFG；backend 只消费
elaborated MIR，不再自行推导析构顺序。

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

## Global Place

MIR place 支持 local base 和 global base。非 `const` 全局变量是 addressable storage，可以作为
place 参与 read/write、borrow、field projection、index projection 和 deref projection。

`const` 仍保持值语义，不承诺拥有唯一 storage，也不因为跨 package 使用就自动 materialize 成
只读 global。因此动态下标访问、取地址或需要稳定 storage 的表数据应声明为非 `const` global，
例如 `UInt8![N] TABLE = [...]`。编译器可以把只读使用的 global lowering 成只读 backend global，
但这属于实现优化，不改变 source 层 `const` 和 global variable 的语义边界。

标准库中生成的 Unicode XID 表使用 global array，以便 `std.jiang` tokenizer 能通过动态下标
查询压缩 bitmap。

backend 符号名不使用源码 global 名称。普通 Jiang global 使用 package/module/DefId 派生名：

```text
_Jp{package_index}_m{module_index}_g{def_id}
```

这样不同文件或不同 package 中同名 global 不会在 LLVM module 的全局符号表里冲突。`public`
只影响 Jiang package API，不表示 backend 必须导出源码同名符号。`extern` global 保留源码名，
用于和外部 ABI 对接；未来如果支持显式 link name/export attribute，也应在这里接入。

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
- `switch` 使用 discriminant/tag branch blocks；enum/union variant pattern 的具体选择来自
  `TypeCheckStore`。
- `for in` 对 range 使用 `[start, end)` index loop；对 array/slice 使用 `len` 和 indexed place。
- field/member access lowering 生成 concrete `MirPlace` projection。
- hosted entry lowering 只从 `ModuleGraph.root_module` 查找 language `main`，dependency
  package 的 `main` 不会生成 C ABI `main` wrapper。

## 泛型实例

MIR lowering 接收 `MonomorphStore`。非泛型函数按 `DefId` 直接 lower；泛型函数只按 concrete
`InstanceKey` lower。lowering 中的 type substitution 只用于当前 concrete body。

## 不变量

- MIR 不保存 AST id。
- MIR local 保留 `TypeId`；类型来源是 `TypeCheckStore`，不是 HIR nullable type 字段。
- MIR 不保存 field offset、size、align 或 ABI 信息；这些事实只来自 `LayoutStore`。
- backend-specific symbol/mangling 不写入 MIR。
