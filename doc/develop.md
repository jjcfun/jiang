# Jiang Compiler Development Notes

本文档描述 Jiang 编译器当前的实现阶段、已完成范围以及下一阶段的开发重点。它应与仓库中的 `README.md`、`TODO.md` 和现有测试结果保持一致。

## 🌟 核心特性 (Key Features)

*   **极简而统一的类型系统**：遵循“从左往右，从里到外”的类型推导与声明逻辑（如 `Int?[2][3]`）。
*   **内存安全与所有权机制**：内置轻量级的所有权（Ownership）校验，结合手动的内存分配与释放（`new` 与 `sudo $free()`），在无 GC 的前提下保障内存安全。
*   **透明的智能指针（自动解引用）**：使用指针如字面量一般自然，而底层的代理与操作则隐藏在 `$` 语法之下（如 `$ptr.move()`）。
*   **高度灵活的聚合类型**：支持强大且直观的结构体（Struct）、多态枚举（Enum）、以及安全的联合体（Union）与模式匹配。
*   **泛型及无缝异步设计**：内置函数与结构体级别的泛型修饰，以及为适应现代 IO 设计的 `async` 原生支持。

---

## 🏗 当前架构阶段

Jiang 目前采用循序渐进的路线：

### Stage0：C 实现的可用编译器
目标是先把语言最小闭环跑通，并用测试把语义和编译链固定下来。

当前仓库中的 Stage0 已完成以下范围：

*   多模块与命名空间导入
*   Binding 体系、tuple binding、Union variant binding
*   `switch` 模式匹配与匿名 Union
*   最小标准库：`std.io`、`std.debug`、`std.fs`
*   标准库导入与 `--stdlib-dir`
*   主编译链稳定为 `AST -> JIR -> C`

当前状态：`TODO.md` 中定义的 Stage0 完成条件已经全部完成，仓库当前版本的测试集已经全部通过。

### Stage1：Jiang 自举起步
目标是使用 Stage0 编译器，开始实现 Jiang 版编译器前端与配套标准库能力。

当前建议的 Stage1 起步范围：

*   先实现最小自举子集，不直接重写整个编译器
*   优先实现文件加载、路径处理、import 解析、词法分析
*   继续约束 Stage1 对标准库的依赖范围，避免过早依赖复杂高层语法糖

当前状态已经进一步推进到“真实入口编译器”阶段：

*   `compiler_core.compile_entry(path, mode)` 已支持当前受支持入口模块的 `dump_ast` / `dump_hir` / `dump_jir` / `emit_c`
*   `stage1_real_entry_smoke` 已覆盖 `source_loader.jiang`、`parser_core.jiang`、`compiler_core.jiang`，并扩到 `lexer_core.jiang`、`hir_core.jiang`、`module_loader.jiang`、`jir_lower.jiang`、`parser_store.jiang`、`buffer_int.jiang`、`buffer_bytes.jiang`、`intern_pool.jiang`、`module_paths.jiang`、`token_store.jiang`、`hir_store.jiang`、`jir_store.jiang`、`symbol_store.jiang`、`type_store.jiang`
*   growable store 底层抽象与主链 store 已收口，当前 Stage1 正式回归路径已无稳定复现 warning 命中

### Stage2：Jiang 重构编译器并升级后端
当 Stage1 的 Jiang 版前端稳定后，再考虑重构内部 IR、接入 LLVM 或其他原生后端。

---

## 🎯 里程碑规划

### 已完成：Stage0 收口

*   [x] 词法、语法、语义、JIR lowering、C 代码生成主链打通
*   [x] 多模块、导入解析、模块缓存、可见性规则
*   [x] Binding / Pattern / Union / `switch` 主路径
*   [x] 最小标准库与标准库导入机制
*   [x] 自动化测试脚本与项目型集成测试

### 当前进行中：Stage1 自举准备

*   [x] 建立 `bootstrap/` 目录，承载 Jiang 版编译器源码
*   [x] 实现文件加载、路径处理与最小模块边界约束
*   [x] 使用 Jiang 实现最小 Lexer，并用 golden 固定 token dump
*   [x] 使用 Jiang 实现 `bootstrap-first` Parser，并用 golden 固定 parse dump
*   [x] 使用 Jiang 实现最小 HIR / semantic 前端，并用真实 bootstrap 模块图 golden 固定 HIR dump
*   [x] 在 `bootstrap/` 内补最小 `HIR -> JIR -> C` 闭环，并让 Stage0 编译器稳定编译第一个 Jiang 工具程序

### 后续阶段：原生后端演进

*   [ ] 在 Jiang 版前端稳定后评估 HIR / MIR 分层
*   [ ] 评估 LLVM IR 或其他原生后端接入时机
*   [ ] 逐步摆脱 C 作为过渡后端
