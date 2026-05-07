# Jiang Stage1 TODO

当前状态：

- Stage1 自举 smoke、测试基线、package/source/driver、semantic table 基线、type check/resolve 基线已完成。
- TODO 只保留未完成任务；已完成阶段和已转为硬断言的缺口不再列入。
- 剩余工作按依赖排序：语义身份 -> 跨 module layout -> lowering -> ABI/runtime -> 诊断/文档 -> 增量编译准备。

依赖关系：

- `ResolvedCallee` / `InstanceKey` 是 call target、generic specialization、symbol mangling、增量编译 hash 的共同前置。
- 跨 module struct/enum/union/alias/method/slice/array 缺口依赖统一 nominal layout 和 import visibility。
- struct init / `new` / deinit 先于更复杂 lifetime 和 runtime 行为。
- control-flow、pattern、tuple destructuring、for variants 应在 union/trait/errorable ABI 大改前收敛 lowering 形状。
- 增量编译依赖稳定 semantic identity、module graph、layout hash，不在这些基础稳定前开始声明级增量。

## 第一阶段：HIR / JIR 语义身份与职责边界

- [x] 固化 Stage1 语法边界。
  - [x] `if` / `else if` condition 的圆括号可选。
  - [x] `if` / `else` body 的大括号不能省略，包括 if expression。
  - [x] 给省略大括号的 statement/expression 形式补负例测试。
- [x] 补齐 coalesce throw early-exit 语法。
  - [x] 支持 `optional ?? throw error;`。
  - [x] 明确 `?? throw` 的 type rule、HIR/JIR lowering 和 defer 交互。
  - [x] 增加最小正例和错误边界负例。
- [ ] 收敛结构化 expression / call identity。
  - [ ] 设计并落地 `ResolvedCallee`，覆盖普通函数、method、static method、trait/concept method、imported method、constructor/init。
    - [x] 普通函数、method、static method、imported function、Equatable.equal 已记录 callee kind + `DeclId`。
    - [x] 本地和 imported struct constructor 已记录 callee kind + struct `DeclId`。
    - [x] HIR/JIR call 节点携带 callee kind、target `DeclId`、unresolved method receiver/symbol。
    - [x] init overload index 进入 `ResolvedCallee`，并向 HIR/JIR call identity 透传。
    - [x] trait/concept method 统一进入 `ResolvedCallee`，用 trait owner `DeclId` + method index 表达签名身份。
  - [ ] 设计并落地 `InstanceKey`，作为 generic/type specialization 的稳定身份。
    - [x] 在 semantic 层定义 `InstanceKey` 的最小结构。
    - [x] generic function specialization/mangling 改用 `InstanceKey` 的 `DeclId` + signature hash。
    - [ ] generic nominal layout 改用 `InstanceKey`。
  - [ ] 用结构化 identity 替换 `TypeCheckResult` 中 expression/type-ref/call target 的并行 span/index 查表。
    - [x] call target 优先按 AST expression pointer 查询，span 仅作为兼容 fallback。
    - [x] type-ref lookup 优先使用 `TypeRefId`，span 仅作为兼容 fallback。
    - [x] call target 并行数组收敛为结构化 `ResolvedCalleeEntry` 表。
    - [x] type-ref 并行数组收敛为结构化 `TypeRefTypeEntry` 表。
  - [x] backend 对已解析 call target 优先消费 HIR/JIR/semantic identity，不再对明确 `DeclId` 的 call 重新按 receiver/symbol 猜目标。
  - [x] backend 剩余 fallback 仅保留 trait/unresolved method dispatch 路径，并随后续 trait ABI 收敛。
- [ ] 继续消除 HIR/JIR `.unsupported` fallback。
  - [ ] 将现有 fallback 逐项对应到 `stage1_test.sh` 的具体 known gap。
  - [x] HIR/JIR lowering 的兜底 fallback 改为先触发断言，避免静默生成 unsupported IR。
  - [x] 新增 fallback 必须配测试和 TODO。
  - [ ] fallback 清零前不扩大新语法。
- [ ] 固化 symbol mangling 规则。
  - [x] generic function mangling 输入使用 `InstanceKey` 的 `DeclId` + signature hash / module identity。
  - [ ] 剩余 mangling 输入只使用 `ResolvedCallee` / `InstanceKey` / module identity。
  - [x] mangling 只在 backend 边界生成。
  - [x] module-scoped symbol 使用长度前缀编码，避免 parser/source grammar 相关字符。
  - [x] generic specialization 后缀使用固定宽度 hash 编码。

## 第二阶段：跨 Module / Import / Nominal Layout

- [ ] 统一跨 module nominal type layout。
  - [ ] imported struct/enum/union 的字段、方法、init、generic 参数从 semantic/JIR identity 查询。
  - [x] 修复 multi-file/namespaced struct return/layout。
  - [x] 修复 multi-file/namespaced struct array layout。
  - [x] 修复 multi-file/namespaced slice return layout。
  - [x] 修复 multi-file/namespaced enum field shorthand。
- [ ] 补齐 public/import alias/type visibility。
  - [x] `alias_import_type_minimal`
  - [x] `public_alias_type_minimal`
  - [x] `public_import_type_minimal`
- [ ] 补齐跨 module method target。
  - [x] `public_import_instance_method_minimal`
  - [x] `public_import_static_method_minimal`
  - [x] `private_method_called_by_public_method_minimal`
  - [x] 保持 private method/type 负例继续硬断言。
- [ ] 补齐 generic imported nominal instance layout。
  - [x] `generic_import_struct_minimal`
  - [ ] imported generic instance 的 `InstanceKey` 与 layout 复用。

## 第三阶段：Struct / Init / Lifetime Lowering

- [x] 重新整理 struct init resolution / lowering。
  - [x] overload init selection。
  - [x] mutable default field override in init。
  - [x] mixed positional/named init args。
  - [x] named init sugar。
  - [x] branch-complete init analysis。
  - [x] failable init return/ABI。
  - [x] optional/mutable default assignment。
- [x] 补齐 `new` + constructor/init lowering。
  - [x] `struct_new_constructor_minimal`
  - [x] `struct_new_literal_with_init_minimal`
- [x] 补齐 deinit / runtime lifetime lowering。
  - [x] `deinit_minimal`
  - [x] owner pointer runtime smoke 需要走 executable/link 路径，避免 `lli` 外部符号解析造成误判。
- [x] 保持 owner/borrow receiver 一致性。
  - [x] 将 `struct_instance_method_pointer_base_minimal` 的 runtime harness/外部符号问题与真实 lowering 问题分离。

## 第四阶段：Control Flow / Pattern / Destructuring Lowering

- [ ] 补齐 coalesce early-exit lowering。
  - [x] `coalesce_break_minimal`
  - [x] `coalesce_continue_minimal`
  - [ ] defer 与 early-exit 组合回归。
- [x] 补齐 optional while-pattern narrowing/lowering。
  - [x] `optional_while_is_pattern_minimal`
- [x] 补齐 try/catch JIR lowering。
  - [x] statement lowering。
  - [x] expression lowering。
  - [x] error propagation with defer。
  - [x] Stage1 语法收敛为 `try errorable_call() catch (...) { ... }`，不支持多语句 try body 或裸 catch 表达式。
- [x] 补齐 switch/pattern 中间表示。
  - [x] exhaustiveness 需要的 JIR representation。
  - [x] union switch shorthand pattern。
  - [x] union tuple pattern bind。
- [x] 补齐 tuple destructuring。
  - [x] local/global/return destructure。
  - [x] inferred and mutable destructure。
  - [x] unary tuple local/global inference。
- [x] 补齐 for lowering variants。
  - [x] mutable binding。
  - [x] indexed for。
  - [x] indexed typed for。
  - [x] tuple binding typed/indexed/mutable。

## 第五阶段：Union / Generic / Trait / Errorable ABI

- [ ] 重新定义 union payload ABI。
  - [x] 非 `Int` payload。
  - [x] tuple payload。
  - [ ] nested union payload。
  - [ ] grouped/implicit tag variants。
  - [ ] generic union payload/layout。
  - [ ] union bind/switch shorthand lowering。
- [ ] 补齐 generic instance layout。
  - [ ] generic nominal layout keyed by `InstanceKey`。
  - [ ] generic union/function payload。
- [ ] 补齐 trait receiver / trait object ABI。
  - [ ] trait receiver direct call ABI。
  - [ ] trait object representation；如果 Stage1 不实现，需要显式 defer。
  - [ ] trait inheritance with where constraints。
  - [ ] extend trait inheritance。
- [ ] 补齐 errorable ABI。
  - [ ] value/error representation。
  - [ ] function return/call convention。
  - [ ] interaction with try/catch/defer。
- [ ] 使用 target data layout 驱动 scalar layout。
  - [ ] `Int` / `UInt` / pointer-sized layout。
  - [ ] target machine data layout source 统一。

## 第六阶段：Runtime / Builtin / LLVM API Surface

- [ ] 明确 Stage1 runtime/builtin 边界。
  - [ ] `assert_minimal`
  - [ ] `print_minimal`
  - [ ] `panic_minimal`
- [ ] 补齐 array repeat init lowering/runtime。
  - [ ] `array_repeat_init_minimal`
  - [ ] 区分 language lowering failure 与 allocator/runtime harness 问题。
- [ ] 收回 `llvm/api.jiang` 的 raw LLVM public surface。
  - [ ] opaque pointee type 改回 private。
  - [ ] LLVM extern 声明改回 private。
  - [ ] `codegen.jiang` 只通过 wrapper API 工作。

## 第七阶段：Diagnostics / Docs

- [ ] 增强 diagnostic 数据结构。
  - [ ] label。
  - [ ] note。
  - [ ] suggestion。
  - [ ] line/column 查询。
  - [ ] 多文件诊断输出。
- [ ] 同步文档状态。
  - [ ] README 阶段说明改成当前 Stage1 状态。
  - [ ] `doc/develop.md` 移除已过期的“尚未完成”列表。
  - [ ] `doc/language-design.md` 的 feature matrix 和测试基线对齐。
  - [ ] 从 `stage1_test.sh` 生成或记录 Stage1 已知缺口，避免散落在脚本注释里。

## 第八阶段：增量编译准备

- [ ] 完成增量编译前置依赖。
  - [ ] `ResolvedCallee`
  - [ ] `InstanceKey`
  - [ ] cross-module layout。
  - [ ] `DeclInfo` signature/body/layout hash。
- [ ] 实现 module-level incremental v1。
  - [ ] file hash。
  - [ ] module public API hash。
  - [ ] reverse dependency index。
  - [ ] unchanged module object reuse。
- [ ] 暂缓 decl/function-level incremental。
  - [ ] declaration stable key finalization。
  - [ ] signature/body/layout hash implementation。
  - [ ] generic instance invalidation model。
