# Jiang Stage1 TODO

当前状态：

- Stage1 前七阶段已收敛为自举、测试基线、package/source/driver、semantic table、HIR/JIR lowering、LLVM codegen、runtime builtin、diagnostic metadata 和文档状态同步。
- 前七阶段不再包含长期 ABI、跨 module/generic instance cache、完整 target data layout、diagnostic renderer、LSP 或声明级增量编译；这些进入 Stage2 / incremental prep。
- 后续 TODO 只保留 Stage1 之后仍需要继续推进的任务。

## 已完成：前七阶段

### 第一阶段：HIR / JIR 语义身份与职责边界

- [x] 固化 Stage1 语法边界。
  - [x] `if` / `else if` condition 的圆括号可选。
  - [x] `if` / `else` body 的大括号不能省略，包括 if expression。
  - [x] 给省略大括号的 statement/expression 形式补负例测试。
- [x] 补齐 coalesce throw early-exit 语法。
  - [x] 支持 `optional ?? throw error;`。
  - [x] 明确 `?? throw` 的 type rule、HIR/JIR lowering 和 defer 交互。
  - [x] 增加最小正例和错误边界负例。
- [x] 收敛结构化 expression / call identity。
  - [x] `ResolvedCallee` 覆盖普通函数、method、static method、trait/concept method、imported method、constructor/init。
  - [x] `InstanceKey` 已作为 generic function specialization/mangling 的稳定身份。
  - [x] call target/type-ref lookup 使用结构化 side table，span 只保留兼容 fallback。
  - [x] backend 对已解析 call target 优先消费 HIR/JIR/semantic identity。
- [x] 继续消除 HIR/JIR `.unsupported` fallback。
  - [x] 已知 fallback 对应到 `stage1_test.sh` 的具体测试基线。
  - [x] HIR/JIR lowering fallback 改为断言，避免静默生成 unsupported IR。
  - [x] 新增 fallback 必须配测试和 TODO。
- [x] 固化 symbol mangling 规则。
  - [x] generic function mangling 输入使用 `InstanceKey` 的 `DeclId` + signature hash / module identity。
  - [x] mangling 只在 backend 边界生成。
  - [x] module-scoped symbol 使用长度前缀编码。
  - [x] generic specialization 后缀使用固定宽度 hash 编码。

### 第二阶段：跨 Module / Import / Nominal Layout

- [x] 统一 Stage1 跨 module nominal type layout smoke。
  - [x] multi-file/namespaced struct return/layout。
  - [x] multi-file/namespaced struct array layout。
  - [x] multi-file/namespaced slice return layout。
  - [x] multi-file/namespaced enum field shorthand。
- [x] 补齐 public/import alias/type visibility。
  - [x] `alias_import_type_minimal`
  - [x] `public_alias_type_minimal`
  - [x] `public_import_type_minimal`
- [x] 补齐跨 module method target。
  - [x] `public_import_instance_method_minimal`
  - [x] `public_import_static_method_minimal`
  - [x] `private_method_called_by_public_method_minimal`
  - [x] private method/type 负例继续硬断言。
- [x] 补齐 generic imported nominal instance smoke。
  - [x] `generic_import_struct_minimal`

### 第三阶段：Struct / Init / Lifetime Lowering

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
  - [x] owner pointer runtime smoke 走 executable/link 路径。
- [x] 保持 owner/borrow receiver 一致性。

### 第四阶段：Control Flow / Pattern / Destructuring Lowering

- [x] 补齐 coalesce early-exit lowering。
  - [x] `coalesce_break_minimal`
  - [x] `coalesce_continue_minimal`
  - [x] defer 与 early-exit 组合回归。
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
- [x] 补齐 for lowering variants。

### 第五阶段：Union / Generic / Trait / Errorable ABI

- [x] 完成 Stage1 union payload ABI smoke。
  - [x] 非 `Int` payload。
  - [x] tuple payload。
  - [x] array/function pointer payload。
  - [x] grouped/implicit tag variants。
  - [x] union bind/switch shorthand lowering。
  - [x] generic union/function payload smoke。
- [x] 完成 Stage1 generic function specialization 和 generic imported struct smoke。
- [x] 完成 Stage1 trait direct call / inheritance / where constraints。
  - [x] trait inheritance with where constraints。
  - [x] extend trait inheritance。
- [x] 完成 Stage1 errorable tagged value lowering。
  - [x] try/catch/defer smoke。
  - [x] value/error basic representation。

### 第六阶段：Runtime / Builtin / LLVM API Surface

- [x] 明确 Stage1 runtime/builtin 边界。
  - [x] `assert_minimal`
  - [x] `print_minimal`
  - [x] `panic_minimal`
- [x] 补齐 array repeat init lowering/runtime。
  - [x] `array_repeat_init_minimal`
- [x] 记录 `llvm/api.jiang` raw LLVM public surface 的 Stage0 导出模型技术债。
  - [x] 文档中明确 wrapper API 是目标边界。
  - [x] raw opaque pointee type / extern 暂时保持 public 是 Stage0 兼容限制，不作为 Stage1 阻塞项。

### 第七阶段：Diagnostics / Docs

- [x] 增强 diagnostic 数据结构。
  - [x] label。
  - [x] note。
  - [x] suggestion。
  - [x] line/column 查询。
  - [x] 多文件渲染输出延期到 diagnostic renderer 阶段。
- [x] 同步文档状态。
  - [x] README 阶段说明改成当前 Stage1 状态。
  - [x] `doc/develop.md` 保留后续债务，但不再把已完成 smoke 列为 Stage1 未完成。
  - [x] `doc/language-design.md` 的 feature matrix 和测试基线对齐。
  - [x] Stage1 已知缺口集中记录在本 TODO 的后续阶段，不散落为前七阶段阻塞项。

## 第八阶段：增量编译准备

- [ ] 完成增量编译前置依赖。
  - [x] `ResolvedCallee`。
  - [x] `InstanceKey` 最小结构。
  - [ ] cross-module/generic instance layout cache。
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

## Stage2 / ABI 延期项

- [ ] generic nominal layout keyed by `InstanceKey`。
- [ ] imported generic instance 的 `InstanceKey` 与 layout 复用。
- [ ] nested union payload。
- [ ] generic union payload size keyed by `InstanceKey`。
- [ ] trait receiver/object ABI。
- [ ] 完整 errorable ABI calling convention。
- [ ] 使用 target data layout 驱动 `Int` / `UInt` / pointer-sized layout 和 aggregate layout。
- [ ] 回收 `llvm/api.jiang` raw LLVM public surface。
  - [ ] opaque pointee type 改回 private。
  - [ ] LLVM extern 声明改回 private。
  - [ ] `codegen.jiang` 只通过 wrapper API 工作。
- [ ] 实现完整 diagnostic renderer。
  - [ ] source file table。
  - [ ] 多文件 diagnostic 输出。
  - [ ] label/note/suggestion 渲染。
