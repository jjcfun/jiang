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
当前 Stage2 已启动，目标是在 `compiler/` 中逐步建立新的正式主线，实现重构后的前端、JIR 与多后端分层。

当前约定：

*   `bootstrap/` 对应当前真实 Stage1 主线
*   `compiler/` 对应当前 Stage2 主线目录

也就是说，当前仓库里不再把 `compiler/` 视为 Stage1 后期实现主线。Stage1 已经完成，Stage2 当前的第一阶段是先固定一条新的 `frontend -> JIR -> C` 最小闭环。

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

### 已完成：Stage2 主线实现

*   [x] 建立 `compiler/` Stage2 分层骨架
*   [x] 跑通 `compiler/` 内 `frontend -> HIR -> JIR -> C`
*   [x] 跑通 `compiler/` 内 `frontend -> HIR -> JIR -> LLVM`
*   [x] 建立 `stage1 -> stage2` 构建链
*   [x] 建立 Stage2 的 `emit-c` / `run` / `error` / `llvm` / `llvm-error` smoke
*   [x] 完成多模块、`public`、alias import、导出表第一版
*   [x] 完成 `struct` / `enum` 基础语义与 C/LLVM lowering
*   [x] 完成 `UInt8` 与 `UInt8[]` 的最小类型闭环
*   [x] 收口 `stage2c` 最小正式 CLI（默认 `emit-c`，支持 `--emit-llvm` 与 `--help`）
*   [x] 收口 `stage2_complete_smoke.sh` 作为 Stage2 单点验收入口
*   [x] 完成 slice / 字符串表达式主线
*   [x] 完成数组与聚合类型第一版
*   [x] 完成 builtin / named / 复合类型第一版（`Int` / `Bool` / `()` / `UInt8`、`struct` / `enum`、array / slice / pointer）
*   [x] 让 LLVM 后端在代表性 Stage2 样例上与 C 后端建立对齐回归
*   [x] 继续收紧 `unknown` 容忍路径
*   [x] 继续加强聚合类型在多模块、赋值、参数、返回值场景下的一致性
*   [x] 把 Stage2 收成唯一继续演进的主线

### 当前优先主线：Stage2 持续演进 + 默认 LLVM 后端持续评估

当前 `--backend llvm` 的 Stage0/Stage1 回归仍然保留；同时 Stage2 自己已经具备独立 LLVM 后端和独立 smoke。
Stage1 当前主线已经完成并冻结；当前更值得做的是：

*   继续在现有回归约束下演进 Stage2 语言与后端能力
*   让聚合类型在多模块和两套后端中的行为继续收敛
*   评估 LLVM 是否已经达到未来 Stage2 默认后端候选的成熟度
*   按 `doc/jiang.md` 的正式语法说明，逐项推进 Stage2 语法对齐

当前最新执行清单见 `TODO.md`。

### Stage2 与 `jiang.md` 的当前关系

当前不再把 Stage2 的目标定义为“继续补零散能力”，而是：

*   先把 `TODO.md` 中列出的 Stage2 对齐表稳定下来
*   再按 `doc/jiang.md` 的正式语法说明推进剩余语法

当前可以粗分为三层：

*   已基本对齐：
    *   基本命令式函数
    *   `if / else`、`while`
    *   `Int` / `Bool` / `()` / `UInt8`
    *   `struct` / `enum`
    *   array / slice / pointer 第一版
    *   多模块、`public`、alias import、导出表
*   部分对齐：
    *   字符串
    *   数组：当前已支持 `Int[_] x = ...`、`Int[_] { ... }` typed constructor 与基础 tuple/struct 场景，仍未覆盖可变性与更完整语义
    *   指针自动解引用 / 所有权相关语义
    *   `T[]` 的完整泛化
    *   `T?` / `T!`：当前已支持最小 optional / mutable qualifier 语法、`null`、基础 expected-type / 推断传播、`Int?[2][3]` / `Int[2]!` 这类后缀组合与 C/LLVM codegen；但 `!` 仍未收紧赋值约束
    *   `for`：当前已支持 range、单变量容器迭代、tuple 解构迭代与 `indexed()`；仍未覆盖更完整 pattern / binding 与迭代协议
    *   `struct init`、可变字段、`init`
    *   枚举：当前已支持 `.ok` 一类 expected-type shorthand、显式值与 `.value`，仍未覆盖底层类型与更完整推断规则
    *   类型推断：当前已支持 `_ x = expr`、`Int[_] x = ...`、expected-type shorthand、基础 tuple/binding 与 typed array constructor，尚未覆盖更完整的统一推断规则
    *   `union`：当前已支持最小声明、构造、payload binding 和带穷尽性检查的 `switch`，尚未覆盖简写构造、多值解构与更完整布局语义
    *   元组：当前已支持 `()`、一元组归一化、first-class tuple value/type、索引、return 与 destructuring/binding，尚未覆盖更完整 tuple ABI、pattern 与多返回值语义
    *   更完整模块语义
*   仍未开始：
    *   类型转换
    *   `switch`：当前已支持最小 Int / enum / union case、union payload binding、重复 case 诊断、enum/union 穷尽性检查与 `else:`，尚未覆盖更完整 pattern
    *   模式匹配 / binding
    *   泛型
    *   `async`
    *   FFI 作为正式用户语法

### Stage2 接管 Stage1 的判定条件

当前固定的接管标准是：

*   `script/build_stage2.sh` 持续通过，且默认构建链已采用“已有旧版 Stage2 编译器 -> 当前 Stage2 自重编”的 bootstrap 路径；当本地没有可用旧版 `stage2c` 时仍可回退到 Stage1 引导
*   当前开发阶段，“已有旧版 Stage2 编译器”默认指本地上一次成功构建保留下来的 `build/stage2c`；进入正式 release 节奏后，应切换为“上一版 release 的 `stage2c`”
*   `script/stage2_complete_smoke.sh` 持续通过
*   `script/stage1_complete_smoke.sh` 持续通过
*   `script/stage2_selfhost_smoke.sh` 已固定 roundtrip：`stage2c -> compiler -> stage2c.selfhost -> stage2c.roundtrip`
*   Stage2 的 `emit-c` 与 `--emit-llvm` 都保持正式可用
*   Stage2 已覆盖当前日常主语法子集：
    *   多模块与 `public`
    *   `struct` / `enum`
    *   `UInt8` / `UInt8[]` / array / pointer
    *   命令式函数、控制流、赋值、调用

当前状态：

*   `compiler/` 已成为唯一继续演进的主线
*   `bootstrap/` 已冻结为 Stage1 基线，只保留 bootstrap 与回归职责
*   `stage2_complete_smoke.sh` 已成为 Stage2 单点验收入口
*   Stage2 bootstrap 当前采用 Rust/Go 风格迭代策略：旧版 Stage2 编译器编译新版 Stage2，Stage1 仅保留历史兜底职责

仓库定位也已经相应变化：当前仓库应视为 Stage2 主线仓库；`src/`、`bootstrap/` 与历史 bootstrap 资产继续保留，但职责已经缩到 bootstrap、冷启动与历史回归。后续如果需要进一步清理主仓库历史负担，推荐把 bootstrap orchestration 和历史阶段整理进单独的 `jiang-bootstrap` 仓库，而当前仓库继续只承载 Stage2+ 主线演进。

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
