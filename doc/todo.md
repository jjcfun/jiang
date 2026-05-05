# 语义 ID 与 JIR 存储重构

目标：让编译器里的语义身份在全局范围内明确且稳定，同时让 HIR/JIR
自己的存储 ID 只作为模块内部下标使用。JIR 不应该保存指向语义信息或其他
声明实体的 arena 指针；JIR 只保存稳定 ID，并在需要实体时通过辅助表查询。

设计原则：

- `ModuleId`、`DeclId`、`TypeId` 是全局稳定 ID。
- `HirDeclId`、`JirDeclId`、`JirStmtId`、`JirExprId` 是各自 HIR/JIR
  module 内部的本地存储下标。
- `DeclInfo` 只保存语义事实：所在模块、名字、声明种类、binding、声明类型、
  返回类型和声明 flags。
- `DeclInfo` 不保存 `HirDeclId`、`JirDeclId`、指针或 lowering 产物。
- HIR/JIR 的 lowering 映射使用独立 side table：
  - `DeclId -> HirDeclRef`
  - `DeclId -> JirDeclRef`
- nominal `TypeInfo` 应该引用声明该 nominal 类型的 `DeclId`。
- JIR 中的跨声明引用应该使用 `DeclId`、`BindingId` 或 `TypeId`；需要实体时
  再通过 `SemanticContext` helper 解析。

计划：

- [x] 先把当前 compiler 文件恢复到干净基线，再应用这次重构。
- [x] 引入全局稳定的 `ModuleId`，并在 `SemanticContext` 或
  `CompilerContext` 中建立 module table。
- [x] 将 `scope.DeclId` 从文件内下标语义改成全局稳定声明身份。
- [x] 在 resolve/type_check 后构建 `SemanticContext.decl_table`。
  - [x] 每个全局声明对应一个 `DeclInfo`。
  - [x] 保存 `ModuleId`、`BindingId`、symbol/name、kind、visibility、
    extern、static、mutable、声明类型和必要时的返回类型。
  - [x] 不把 HIR/JIR id 放进 `DeclInfo`。
- [x] 更新 nominal `TypeInfo`，让它引用 nominal 声明的稳定 `DeclId`，
  不再只依赖 `BindingId`。
- [x] 给 HIR declaration 传递并保存 `DeclId`。
- [x] 增加 HIR lowering side table：`DeclId -> HirDeclRef`。
- [x] 给 JIR declaration 传递并保存 `DeclId`。
- [x] 增加 JIR lowering side table：`DeclId -> JirDeclRef`。
- [x] 替换 JIR 中当前保存语义实体指针或 JIR declaration 指针的字段，
  改为保存稳定 ID。
  - [x] name/global/function 引用使用 `DeclId` 或 `BindingId`。
  - [x] nominal type 引用通过 `TypeId -> TypeInfo -> DeclId` 表达。
  - [x] call target 通过 `DeclId -> JirDeclRef` 解析。
- [x] 将 `JirModule` 的 decl、stmt、expr 存储从 `ArenaRefList` 改为
  `ArenaList`；`JirDeclId`、`JirStmtId`、`JirExprId` 只保留本地索引用途。
- [x] 增加常用查询 helper。
  - [x] `decl_info(DeclId) -> DeclInfo`
  - [x] `type_info(TypeId) -> TypeInfo`
  - [x] `nominal_decl(TypeId) -> DeclInfo`
  - [x] `hir_decl_ref(DeclId) -> HirDeclRef`
  - [x] `jir_decl_ref(DeclId) -> JirDeclRef`
  - [x] codegen 内部的 `jir_decl(JirDeclRef) -> JirDecl`
- [x] 重构 `lower_jir` 和 codegen，让它们使用新的 helper，
  不再依赖扫描式查找或指针式查找。
- [x] 继续重构 `lower_hir`，让它在需要跨声明语义信息时也优先使用 helper，
  不再依赖扫描式查找或指针式查找。
- [x] 在新的 `decl_table`、module table 和 lowering maps 覆盖旧用途后，
  删除过时的 semantic lookup 表。
  - [x] 删除 `SemanticContext.binding_types`。
  - [x] 删除 `SemanticContext.function_result_types`。

验证：

- [x] 跑 compiler 测试套件。
- [x] 跑 bootstrap/smoke 测试。
- [x] 增加有针对性的跨模块样例：
  - [x] function call
  - [x] global
  - [x] struct field 和 struct literal
  - [x] enum 和 union variant
  - [x] method 和 static method
  - [x] generic nominal type
