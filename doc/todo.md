# Jiang Stage1 TODO

当前状态：

- Stage1 自举 smoke 已通过。
- 第一轮 `DeclId` / `TypeId` / HIR / JIR 稳定 ID 清理已经完成。
- 现在重点不是新增大语法，而是把 Stage1 编译器从“能自举”推进到“可长期维护、可测试、可继续演进”。

目标：

- 建立可信测试基线，尤其恢复负例测试。
- 收敛 package / module / source / driver 边界。
- 继续清理 semantic table、type_check、lowering、codegen 的阶段职责。
- 为后续增量编译和更完整语言特性打基础。

## 第一阶段：测试基线硬化

- [x] 恢复 `script/stage1_test.sh` 中的负例测试。
  - [x] 删除 `run_compile_fail` 中的临时 skip。
  - [x] 将负例分成“应立即通过”和“已知缺口”两组。
  - [x] 已知缺口必须在脚本或文档中显式列名，不能静默跳过。
  - [x] 清空 `run_known_compile_gap`，所有负例编译缺口均改回 `run_compile_fail` 硬断言。
- [x] 将 Stage1 当前正向运行时/codegen 缺口显式列名。
  - [x] 已通过样例继续使用 `run_sample` 硬断言。
  - [x] 未达 Stage0 行为的样例使用 `run_known_stage1_gap`。
  - [x] 运行时非零缺口使用 `run_known_stage1_nonzero_gap`。
- [ ] 让 Stage1 compiler test 覆盖：
  - [ ] `--emit-llvm`
  - [ ] `--emit-obj`
  - [ ] executable link/run
  - [ ] multi-file import
  - [ ] compiler bootstrap smoke
- [ ] 给新增行为建立最小样例，不把大范围语言回归混进单个测试。
- [ ] 保持每个阶段都能通过：
  - [x] `LLVM_CONFIG=/opt/homebrew/opt/llvm@21/bin/llvm-config bash ./script/test.sh`
  - [x] `LLVM_CONFIG=/opt/homebrew/opt/llvm@21/bin/llvm-config bash ./script/build_stage1.sh`
  - [x] `LLVM_CONFIG=/opt/homebrew/opt/llvm@21/bin/llvm-config bash ./script/stage1_test.sh`

## 第二阶段：Package / Source / Driver

- [ ] 实现 `compiler/package_manifest.jiang` 的最小模型。
  - [ ] 解析 `package.ini` 的 package name / type / root。
  - [ ] 解析 dependency 条目。
  - [ ] 对非法 package name 和非法 import path 输出诊断。
- [ ] 将 `SourceManager` 从内存 registry 扩展到文件系统输入。
  - [ ] path normalization。
  - [ ] source root 识别。
  - [ ] 重复 source 去重。
  - [ ] import path string escape 处理。
- [ ] 将 `ModuleGraph` 接入 package/source discovery。
  - [ ] 支持 package root module。
  - [ ] 支持 package 内 module import。
  - [ ] 支持 package dependency import。
  - [ ] 输出完整 import cycle path。
- [ ] 整理 `jiangc.jiang` CLI。
  - [ ] 单文件编译入口。
  - [ ] package 编译入口。
  - [ ] 明确 `--emit-llvm` / `--emit-obj` / executable 输出规则。
  - [ ] 临时文件和 object 输出路径统一管理。

## 第三阶段：Semantic Table 收敛

- [ ] 减少 `SemanticContext` 中对 `decl_table` 的线性扫描。
  - [ ] 增加 `BindingId -> DeclId` 索引。
  - [ ] 增加 `(ModuleId, Symbol) -> DeclId` 索引。
  - [ ] 增加 `ModuleId -> AstFile` 直接索引。
  - [ ] 增加 `DeclId -> owner/module/file` helper。
- [ ] 将 `DeclInfo` 作为顶层声明事实表继续加强。
  - [ ] 记录 declaration stable key 的第一版结构。
  - [ ] 记录 signature/body/layout hash 的占位字段或计算入口。
  - [ ] 明确 tombstone / no-reuse ID 策略。
- [ ] 清理跨阶段重复 side table。
  - [ ] 顶层声明类型只从 `DeclInfo.type_id/result_type_id` 读取。
  - [ ] `TypeCheckResult` 只保留 expression/local/pattern 等阶段必要结果。
  - [ ] HIR/JIR lowering 不重新做名称解析或类型推导。

## 第四阶段：Type Check / Resolve 清理

- [ ] 将 overload resolution 从 fallback 逻辑整理成明确算法。
  - [ ] 函数 overload。
  - [ ] method overload。
  - [ ] static method overload。
  - [ ] ambiguous overload diagnostic。
- [ ] 继续收敛 trait / generic 基础语义。
  - [ ] trait declaration 基本检查。
  - [ ] extend declaration 基本检查。
  - [ ] associated type binding 检查。
  - [ ] trait conformance diagnostic。
  - [ ] 暂缓完整 trait solving 和 trait method lookup，直到调用目标模型稳定。
- [ ] 清理目标语言不保留的历史语法。
  - [ ] 参数 label。
  - [ ] 参数 default value。
  - [ ] record call syntax。
  - [ ] grouped declaration 的最终策略。
- [ ] 明确 optional/errorable 规则。
  - [ ] null-check narrowing 是否长期保留。
  - [ ] errorable ABI。
  - [ ] `try/catch` expression/statement type rule。
  - [ ] `throw` / `?? return` / `?? break` / `?? continue` 的诊断边界。

## 第五阶段：HIR / JIR / Codegen

- [ ] 继续消除 HIR/JIR `.unsupported` fallback。
  - [ ] 新增 fallback 时必须配测试和 TODO。
  - [ ] backend 不重新引入源码级 expression case。
- [ ] 将调用目标模型升级为结构化语义身份。
  - [ ] `ResolvedCallee`
  - [ ] `InstanceKey`
  - [ ] backend-only symbol mangling
  - [ ] 长度前缀或其他不会被 parser 误解的 mangling 编码
- [ ] 补齐 JIR control-flow lowering。
  - [ ] try/catch lowering。
  - [ ] switch/pattern exhaustiveness 需要的中间表示。
  - [ ] defer 与 early-exit 的组合回归。
  - [ ] 必要时再引入 CFG，不提前大改。
- [ ] 补齐 LLVM backend ABI。
  - [ ] 跨 module nominal type layout。
  - [ ] generic instance layout。
  - [ ] trait receiver / trait object ABI。
  - [ ] errorable ABI。
  - [ ] target data layout 驱动的 `Int` / `UInt` / pointer-sized layout。
- [ ] 收回 `llvm/api.jiang` 的 raw LLVM public surface。
  - [ ] opaque pointee type 改回 private。
  - [ ] LLVM extern 声明改回 private。
  - [ ] `codegen.jiang` 只通过 wrapper API 工作。

## 第六阶段：诊断与文档

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
  - [ ] 记录 Stage1 已知缺口，避免散落在脚本注释里。

## 后续准备：增量编译

- [ ] 先完成 package/module 边界，再进入增量编译实现。
- [ ] 第一版只做模块级增量。
  - [ ] 文件 hash。
  - [ ] module public API hash。
  - [ ] reverse dependency index。
  - [ ] 未变 module object 复用。
- [ ] 暂缓声明级和函数级增量，直到 stable key / DeclInfo / package graph 稳定。
