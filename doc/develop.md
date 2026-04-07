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

### Stage1：Jiang 自举完成
目标是使用 Stage0 编译器，完成 Jiang 版编译器前端与正式 Stage1 工作流收口。

当前 Stage1 已完成并固定为：

*   `bootstrap/` 是唯一正式 Stage1 主线，`bootstrap/entries/` 提供工具入口与 smoke driver
*   `compiler_core.compile_entry(path, mode)` 已稳定支持 `dump_ast` / `dump_hir` / `dump_jir` / `emit_c`
*   正式 `stage1c` CLI 已收口，支持 `--help` 与 `--mode emit-c|dump-ast|dump-hir|dump-jir`
*   `stage1_real_entry_smoke` 已覆盖 `source_loader.jiang`、`parser_core.jiang`、`compiler_core.jiang`，并扩到 `lexer_core.jiang`、`hir_core.jiang`、`module_loader.jiang`、`jir_lower.jiang`、`parser_store.jiang`、`buffer_int.jiang`、`buffer_bytes.jiang`、`intern_pool.jiang`、`module_paths.jiang`、`token_store.jiang`、`hir_store.jiang`、`jir_store.jiang`、`symbol_store.jiang`、`type_store.jiang`
*   `bootstrap/jiang.build` 已收口为正式 Stage1 manifest，支持 `lexer` 默认入口以及 `parser` / `hir` / `jir` / `compiler` target
*   `stage1_complete_smoke.sh` 已作为统一验收脚本，覆盖 Stage1 smoke、real-entry、build-system、`stage1c` build/link/run/selfhost
*   growable store 底层抽象与主链 store 已收口，当前 Stage1 正式回归路径已无稳定复现 warning 命中

### Stage2：Jiang 重构编译器并升级后端
当 `bootstrap/` 中的 Stage1 编译器真正收口并可接管主职责后，再考虑进入 Stage2，重构内部 IR、接入 LLVM 或其他原生后端。

当前约定：

*   `bootstrap/` 对应当前真实 Stage1 主线
*   `compiler/` 只保留为未来 Stage2 预留目录

也就是说，当前仓库里不再把 `compiler/` 视为 Stage1 后期实现主线。正式 Stage2 何时启动，取决于 `bootstrap/` 何时先完成接管。

---

## 🎯 里程碑规划

### 已完成：Stage0 收口

*   [x] 词法、语法、语义、JIR lowering、C 代码生成主链打通
*   [x] 多模块、导入解析、模块缓存、可见性规则
*   [x] Binding / Pattern / Union / `switch` 主路径
*   [x] 最小标准库与标准库导入机制
*   [x] 自动化测试脚本与项目型集成测试

### 已完成：Stage1 自举收口

*   [x] 建立 `bootstrap/` 目录，承载 Jiang 版编译器源码
*   [x] 实现文件加载、路径处理与最小模块边界约束
*   [x] 使用 Jiang 实现最小 Lexer，并用 golden 固定 token dump
*   [x] 使用 Jiang 实现 `bootstrap-first` Parser，并用 golden 固定 parse dump
*   [x] 使用 Jiang 实现最小 HIR / semantic 前端，并用真实 bootstrap 模块图 golden 固定 HIR dump
*   [x] 在 `bootstrap/` 内补最小 `HIR -> JIR -> C` 闭环，并让 Stage0 编译器稳定编译第一个 Jiang 工具程序
*   [x] 收口正式 `stage1c` CLI 与 Stage1 manifest/build workflow
*   [x] 用统一 Stage1 完成验收脚本固定 build/link/run/selfhost 回归

### 后续阶段：原生后端演进

*   [ ] 在 Jiang 版前端稳定后评估 HIR / MIR 分层
*   [ ] 评估 LLVM IR 或其他原生后端接入时机
*   [ ] 逐步摆脱 C 作为过渡后端

### 当前优先主线：Stage2 启动准备 + 默认 LLVM 后端持续评估

当前 `--backend llvm` 已经可以覆盖完整正向测试集，并通过 `script/test_llvm_backend.sh`、`script/bootstrap_llvm_smoke.sh` 与 `script/test.sh`。
Stage1 当前主线已经完成；当前更值得做的是：

*   是否开始正式启动 Stage2 主线
*   C 与 LLVM 在代表入口上的行为是否已经稳定一致
*   LLVM 是否已经达到未来默认后端候选的成熟度

当前最新计划见 `doc/plan_stage1_default_llvm.md`。
`compiler/` 当前仅是 Stage2 预留目录；现阶段不再推进其内部主线实现。

### 已完成阶段：LLVM Spike / LLVM C API 路径

在 build system、runtime 边界与 UTF-8 字节串支持收口之后，当前 Stage1 的下一阶段主线不是直接切正式后端，而是先做一个受控的 LLVM spike。

当前明确冻结的 Stage1 public surface：

*   `std.io`
*   `std.debug`
*   `std.fs`

当前明确冻结的宿主 runtime ABI：

*   `__intrinsic_print`
*   `__intrinsic_assert`
*   `__intrinsic_read_file`
*   `__intrinsic_file_exists`
*   `__intrinsic_alloc_ints`
*   `__intrinsic_alloc_bytes`

边界规则：

*   普通 Jiang 项目优先使用 `std.*`，而不是直接调用 `__intrinsic_*`
*   raw intrinsic 继续允许标准库实现、bootstrap 编译器内部基础设施与编译器生成的过渡 glue 直接使用
*   libc 依赖当前继续存在，但应集中在 `include/runtime.h` 与宿主编译器实现中，不继续向 Jiang 标准库表面扩散

### 当前字符串语义：UTF-8 字节串

当前 Stage1 的 UTF-8 支持只覆盖“字节层正确性”：

*   源码按 UTF-8 字节流读取
*   非 ASCII UTF-8 字节允许出现在字符串字面量和注释中
*   `UInt8[]` 仍然表示字节切片
*   `.length` 按字节数计算
*   `s[i]` 按字节访问

当前明确不做：

*   Unicode 标识符
*   code point / rune / grapheme 语义
*   单独的文本字符串类型

### 当前 LLVM Spike 范围

当前计划只验证最小 `JIR -> LLVM IR` 可行性，不替换正式 `JIR -> C` 主线。

当前建议的最小覆盖范围：

*   `Int`、`Bool`
*   局部变量、基础算术、基础比较
*   `if`、`while`
*   普通函数、参数、调用、`return`
*   继续复用当前冻结的 6 个 runtime intrinsic

当前明确不做：

*   完整标准库 lowering
*   Union / Pattern / Binding 全覆盖
*   大规模 JIR 重构
*   直接切换默认后端
