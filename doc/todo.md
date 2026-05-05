# Compiler 优化与清理计划

当前状态：

- 语义 ID 与 JIR 存储重构已经完成。
- `ModuleId`、`DeclId`、`TypeId` 已作为全局稳定 ID 使用。
- `DeclInfo` 已成为顶层声明的语义事实表。
- HIR/JIR lowering map 已使用 `DeclId -> HirDeclRef/JirDeclRef`。
- stage1 自举 smoke 已通过。

目标：

- 删除重构后遗留的扫描式查找和兼容 fallback。
- 让跨模块引用统一通过稳定 ID 和语义 helper 解析。
- 收敛 type_check/lowering/codegen 之间重复保存的声明信息。
- 保持每一步都能通过 compiler 测试和 stage1 smoke。

## 第一阶段：JIR 调用目标解析

- [x] 给 HIR/JIR call target 保留 resolve/type_check 已确定的 `DeclId`。
- [ ] 重构 `compiler.jiang` 中的 JIR call target 解析：
  - [ ] 删除按 `symbol_id + arg_count` 扫描所有 `JirDecl` 的逻辑。
  - [ ] 删除 `function_target_ref_for_shape`。
  - [ ] 删除 `function_target_ref_for_binding`，或只保留为过渡断言。
  - [ ] 跨模块调用统一走 `DeclId -> JirDeclRef`。
- [ ] 增加或保留覆盖：
  - [x] 跨模块同名函数不误命中。
  - [ ] 跨模块同名 method/static method 不误命中。
  - [ ] overload 调用仍能正确解析。

## 第二阶段：JIR type/variant 查询清理

- [ ] 让 type value、struct literal、variant expr/pattern 在 lowering 时优先携带 `DeclId`。
- [ ] 删除 `lower_jir.jiang` 中扫描式 type decl 查询：
  - [ ] `type_decl_for_binding`
  - [ ] `type_decl_for_symbol`
  - [ ] `type_decl_id_for_symbol`
- [ ] 将 field/variant metadata 查询改为：
  - [ ] `TypeId -> TypeInfo -> DeclId`
  - [ ] `DeclId -> JirDeclRef -> JirTypeDecl`
- [ ] 删除 codegen 中 nominal type 的 binding fallback：
  - [ ] `type_decl_for_binding`
  - [ ] 未使用的 `type_decl_for_symbol`
- [ ] 增加或保留覆盖：
  - [ ] 跨模块 struct literal 字段索引。
  - [ ] 跨模块 enum/union variant 索引。
  - [ ] generic nominal type 字段布局。

## 第三阶段：type_check 声明反查收敛

- [ ] 增加 AST 声明查询 helper：
  - [ ] `module_file(ModuleId) -> AstFile`
  - [ ] `decl_ast(DeclId) -> AstDecl`
  - [ ] 必要时增加 `decl_binding(DeclId) -> Binding`
- [ ] 重构 `type_check.decl_for_binding`：
  - [ ] 优先通过 `DeclInfo.decl_id/module_id/ast_index` 查询。
  - [ ] 删除对当前文件 top-level binding 的线性扫描依赖。
- [ ] 将字段、method、union/enum 查询逐步改成 `DeclId` 驱动。
- [ ] 保留本地变量和表达式 span lookup，暂不混入全局 decl_table。

## 第四阶段：重复 type 结果表清理

- [ ] 审计 `TypeCheckResult` 中的顶层 binding type 缓存：
  - [ ] `binding_ids`
  - [ ] `binding_type_ids`
  - [ ] `function_result_binding_ids`
  - [ ] `result_type_ids`
- [ ] 顶层声明类型优先从 `DeclInfo.type_id/result_type_id` 读取。
- [ ] 只保留 lowering 必须使用的本地/表达式类型表：
  - [ ] local binding type
  - [ ] expr type
  - [ ] pattern binding type

## 第五阶段：调试与编译入口清理

- [ ] 删除空实现 `debug_log` 以及无效果调用。
- [ ] 如果仍需要调试日志，改成显式 debug flag。
- [ ] 检查 `compiler.jiang` 的 compiler source preload：
  - [ ] 普通用户编译不应无条件 preload compiler 源。
  - [ ] self-host 编译器源码时再按 import 图加载。
- [ ] 检查 `jiangc.jiang` 的临时输出路径：
  - [ ] 避免固定写 `/tmp/jiang-stage1-temp.o`。
  - [ ] 支持更明确的临时文件策略。

## 每阶段验证

- [ ] `LLVM_CONFIG=/opt/homebrew/opt/llvm@21/bin/llvm-config bash ./script/test.sh`
- [ ] `LLVM_CONFIG=/opt/homebrew/opt/llvm@21/bin/llvm-config bash ./script/stage1_smoke.sh`
- [ ] 必要时用 `./build/stage1-smoke/jiangc` 编译新增样例。
