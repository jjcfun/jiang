# Jiang 编译器开发进度与计划

## 当前状态

- Stage0 已完成，`TODO.md` 中原先定义的完成条件已全部达成。
- 当前主线任务已切换到 Stage1：使用 Stage0 编译器启动 Jiang 自举。
- 现阶段的 TODO 应优先服务于“把 Stage1 收成真实入口可工作的 Jiang 版编译器”，而不是继续扩张 Stage0 的语法面。
- Stage1 的“真实入口可工作”主链、build system、runtime 边界与 UTF-8 字节串支持已经基本收口；LLVM 后端当前已通过 LLVM C API 路径跑通完整正向测试集，并作为可选完整后端进入回归。

## 🎯 当前优先级调整（Priority Reset）

- 第一优先级：评估是否把 LLVM 提升为 Stage1 默认后端候选，重点从“覆盖更多 smoke”切换到“C / LLVM 行为对比与切换证据”。
- 第二优先级：在默认后端评估完成后，再决定是否真的切默认后端，或继续保持 C 默认、LLVM 作为成熟可选后端。
- 第三优先级：在后端路线更清楚后，再正式启动 Stage2 的编译器重构与高级特性实现。

当前阶段暂不作为主线推进的事项：

- [ ] 不直接开始 Stage2 重构。
- [x] 不把“移除 C、接 LLVM”作为 Stage1 后续第一目标。
- [ ] 不在 build system 缺席的前提下大幅扩张标准库与包管理能力。

相关执行计划见 `doc/plan_build_system.md`。
当前阶段的下一份执行计划见 `doc/plan_runtime_boundary.md`。
当前最新执行计划见 `doc/plan_llvm_spike.md`。
当前下一阶段执行计划见 `doc/plan_llvm_c_api.md`。
当前当前优先执行计划见 `doc/plan_stage1_default_llvm.md`。

## ✅ 已完成 (Completed)
- [x] **内置符号重构**: 将 `print` 和 `assert` 从硬编码关键字改为预制标识符（Predefined Identifiers）。
- [x] **断言功能实现**: 在 `generator.c` 中实现了 `assert` 的 C 代码生成，支持失败时打印行号并退出（`exit(1)`）。
- [x] **生成器 Bug 修复**: 修复了数组定义时 C 语法括号位置重复且错误的 Bug（例如 `int64_t[5] arr[5]` 修正为 `int64_t arr[5]`）。
- [x] **测试用例升级**: 
    - 更新 `if_test.jiang`, `loop_test.jiang`, `func_test.jiang` 使用 `assert` 自动化验证。
    - 验证了 `assert` 失败时的错误输出准确性（`tests/assert_fail.jiang`）。
- [x] **定义 Module 结构**: 在 `src/semantic.c` 中定义了 `Module` 结构，并增加了符号可见性 `is_public`。
- [x] **解析器支持 Public**: 解析器现在可以识别 `public` 关键字并应用于声明。
- [x] **保留顶级作用域**: `semantic_check` 现在会将顶级作用域存入模块缓存。
- [x] **完善递归加载逻辑**: 在语义分析遇到 `AST_IMPORT` 时，触发递归调用 `get_or_analyze_module(path)`。
- [x] **符号导入**: 已实现将被导入模块的 `public` 符号合并到当前作用域。
- [x] **跨模块命名空间**: 支持 `alias.member` 语法。
- [x] **路径标准化**: 完善 `module_cache` 的绝对路径查找。
- [x] **结构体封装性**: 实现私有字段访问限制及强制构造函数初始化逻辑。
- [x] **自动化编译链**: 改进生成器，使其能自动生成 `Makefile` 或调用 `gcc` 编译所有相关的 `.c` 文件。
- [x] **更优雅的 Slice 定义**: 将 `Slice_int64_t` 等公共定义提取到单独的 `include/runtime.h` 中。
- [x] **全局头文件保护**: 为所有生成的 C 文件增加 `#ifndef` 保护。

## ✅ Stage0 完成条件（已达成）

### 0. 收口 Binding / Pattern 语法 (Highest Priority)
- [x] **定义 Binding 语法**: 统一绑定原子形式为 `type_expr identifier`。
    - 例: `Int x`, `Int! y`, `_ x`, `_! y`
- [x] **统一 For Binding**: `for` 语句只接受 binding 语法，不再接受裸标识符。
    - 例: `for Int i in 0..10 { ... }`
    - 例: `for (_ i, _ item) in list.indexed() { ... }`
- [x] **统一解构绑定**: 将元组解构、Union 变体绑定、`for` pattern 统一为 binding 列表。
    - 例: `(_ x, _! y) = foo()`
    - 例: `Shape.circle(_ x1)`
- [x] **补齐 Binding AST / 语义分析 / Codegen**: 让 parser、semantic、lowering、codegen 共享同一套 binding 节点与检查逻辑。
    - [x] 拆分独立 `AST_BINDING_LIST`，不再复用 `AST_BLOCK`
    - [x] 支持 tuple return 与 binding assignment
    - [x] `switch` / Union variant 绑定复用同一套 binding 检查
- [x] **补齐 Binding 测试**: 增加 `for`、赋值解构、Union 解构三类 binding 用例。

### 1. 完成 JIR 主链 (Do Not Defer)
- [x] **保留 JIR 入口**: `main` 通过 `lower_to_jir -> jir_generate_c` 走主链。
- [x] **定义 JIR 基础结构**: 已具备 `JirInst`、`JirLocal`、`JirFunction`、`JirModule` 基础骨架。
- [x] **补齐 Lowering 覆盖率**: 将主要 AST 节点稳定降级为 JIR。
    - [x] 变量声明与基础赋值
    - [x] tuple literal / binding assign
    - [x] struct / union 初始化与字段访问
    - [x] range / for-in / break / continue
    - [x] import 后的顶层初始化与跨模块引用
- [x] **在 Lowering 阶段消解语法糖**: 不把高层模式匹配和隐式语义继续留给 C generator。
    - [x] `for-in`
    - [x] Union pattern / `switch`
    - [x] tuple binding
    - [x] `$` 相关语法糖
- [x] **让符号绑定显式进入 JIR**: 局部变量与参数优先绑定到 JIR local，减少 Codegen 阶段的字符串查找与 AST 回溯。
- [x] **重构 JIR Codegen**: `jir_generate_c` 已以 JIR 为函数体主路径产出 C，AST 直出仅保留声明生成与兼容兜底。

### 2. 完成 Union 模式匹配
- [x] **Union 基础实现**: 支持 `union(Tag) Name { ... }` 语法及 C 代码生成。
- [x] **Switch 模式匹配**: 实现 `switch` 语句对 Union 变体的解构（Pattern Matching）。
- [x] **将 Union 绑定并入 Binding 体系**: `Shape.circle(_ x)` 与普通 binding list 使用一致检查逻辑。
- [x] **匿名 Union**: 支持不带显式 Tag Enum 的 Union。

### 3. 最低限度标准库 (Minimal Stdlib)
- [x] **建立标准库目录结构**: 约定 `std/` 或等价 sysroot 布局，避免继续依赖测试目录充当库。
- [x] **实现最小标准库模块**:
    - [x] `std/io`
    - [x] `std/debug`
    - [x] `std/fs`
- [x] **梳理 intrinsic 与标准库边界**: `std.io` / `std.debug` 已通过 `__intrinsic_*` 暴露底层能力，标准库调用不再直接依赖裸 `print` / `assert`。
- [x] **为 Stage1 预留编译器自举所需接口**: 文件读取、字符串处理、路径处理至少要能支撑词法器和 import 解析。

### 4. 标准库 Import 机制
- [x] **区分普通 import 与标准库 import**: 不再只依赖相对路径字符串查找。
- [x] **定义标准库查找规则**:
    - [x] 当前文件相对路径
    - [x] 项目根路径
    - [x] 标准库根路径
- [x] **加入标准库路径配置**: 例如 `--sysroot` 或 `--stdlib-dir`
- [x] **保证标准库模块参与完整编译链**: 与普通模块一样完成分析、生成、编译、链接。
- [x] **补标准库 import 集成测试**: 覆盖标准库模块间互相导入、用户模块导入标准库、名称冲突等情况。

### 5. Stage0 收尾 Bug 与一致性问题
- [x] **构造函数映射**: 修复 `Vault(...)` 没能正确翻译为 `Vault_new(...)` 的问题。
- [x] **多模块符号可见性**: 解决 `Alias.member` 在独立编译模式下产生的链接错误。
- [x] **切片语法兼容性**: 完善 `x[]` 在 C 语言层面的零索引占位生成。
- [x] **空元组返回处理**: 统一将 `() func()` 在 C 声明中映射为 `void func()`。
- [x] **补充真实项目型测试**: 用多模块 + 标准库 + Union + binding 的组合样例验证 Stage0 可用性。

## 📦 Stage1 前置条件（已完成）
- [x] **定义自举子集**: 明确 Stage1 第一版 Jiang 编译器允许依赖的语法与标准库范围。
- [x] **同步语言文档**: 更新 `README`、`doc/jiang.md`、`doc/ebnf.md`，确保与当前实现一致。
- [x] **准备标准库示例程序**: 至少能用 Stage0 编译器编译一个依赖标准库的简单工具程序。

## 🚧 Stage1 当前任务（Current Focus）

### 0. Stage1 后续主线：最小 Build System
- [x] **定义项目清单格式**: 第一版当前已固定为临时 `jiang.build`，作为 Stage2 前的过渡 manifest。
- [x] **定义模块图模型**: 当前已支持默认 root、命名模块映射、`import std;` 与 `import foo;` 的最小模块注入模型。
- [x] **实现统一构建入口**: 当前已提供 `jiang build` / `jiang run` / `jiang test` 与 `--manifest` / `--target`。
- [x] **接入现有 Stage1 编译链**: 当前 build system 已能驱动 `bootstrap/jiang.build` 下的稳定 `lexer` 入口。
- [x] **补 build system 回归测试**: 当前已覆盖默认 target、额外 target、命名模块、错误路径与 bootstrap smoke。

### 0.1 Stage1 后续主线：标准库与 Runtime 分层
- [x] **冻结 Stage1 runtime ABI**: 当前宿主 ABI 固定为 `__intrinsic_print`、`__intrinsic_assert`、`__intrinsic_read_file`、`__intrinsic_file_exists`、`__intrinsic_alloc_ints`、`__intrinsic_alloc_bytes`。
- [x] **固定最小标准库表面**: 当前对外继续收口为 `std.io`、`std.debug`、`std.fs`，`std/std.jiang` 仅负责 re-export。
- [x] **保留裸 `print` / `assert` 兼容入口**: 当前继续作为 Stage0 与测试兼容能力保留，但不再视为标准库 public API。
- [x] **限制 raw intrinsic 使用范围**: 当前面向用户的示例与标准库测试优先走 `std.*`，bootstrap 内部基础设施允许继续直连 intrinsic。
- [x] **增加 runtime 边界回归**: 当前已通过单独 smoke 固定 runtime ABI 集合、`std` 对外模块集合与 README 说明一致性。

### 0.2 Stage1 后续主线：LLVM Spike
- [x] **固定 spike 范围**: 只验证最小 `JIR -> LLVM IR` 可行性，不替换正式 `JIR -> C` 主线。
- [x] **建立实验性后端入口**: 已新增明确标记为实验性的 LLVM 输出路径，不影响现有默认构建链。
- [x] **覆盖最小语义子集**: 当前已支持 `Int` / `Bool`、局部变量、算术与比较、`if` / `while`、函数调用、`return`，以及最小 `UInt8[]` / `print` / `assert` 路径。
- [x] **复用现有 runtime ABI**: 当前未新增新的 `__intrinsic_*`，继续围绕冻结后的宿主入口验证后端可行性。
- [x] **补 LLVM spike 回归**: 当前已加入独立 smoke，证明最小样例与 `import std;` 样例可稳定落到 LLVM IR 与 LLVM 工具链。

### 0.3 Stage1 后续主线：接入 LLVM C API
- [x] **固定迁移目标**: 当前已将实验性 LLVM 路径收口到 LLVM C API，不改变默认 `JIR -> C` 主线。
- [x] **引入最小 LLVM C API 构建链**: 当前已在 CMake 中接入 `llvm-config` / LLVM C 头文件与最小链接库集合。
- [x] **建立实验性 API 后端入口**: 当前已保留 `--emit-llvm`，并新增 `--backend llvm` 作为可选完整后端入口。
- [x] **迁移最小已覆盖语义**: 当前已超出最小 spike 子集，支持完整模块图、聚合类型、`std.io` / `std.debug` / `std.fs` 与完整正向测试集。
- [x] **移除 textual fallback**: 当前仓库中已不再保留旧的 textual LLVM emitter / fallback 逻辑。
- [x] **补 API 级 smoke 与回归**: 当前 `script/llvm_spike_smoke.sh`、`script/test_llvm_backend.sh` 与 `script/test.sh` 都已覆盖 LLVM 后端路径。

### 0. 建立 Stage1 工程骨架
- [x] **创建 Stage1 源码目录**: 当前使用顶层 `bootstrap/` 目录承载 Jiang 版编译器源码。
- [x] **约定入口程序**: 第一阶段入口已经定为最小 `lexer` 可执行程序。
- [x] **补最小构建说明**: 已在 `bootstrap/README.md` 中写清如何用 Stage0 编译器编译和运行第一个程序。

### 1. 先做文件与模块基础设施
- [x] **实现文件读取封装**: 已在 `bootstrap/source_loader.jiang` 中用 `std.fs` 封装 Stage1 当前所需的源码加载接口。
- [x] **实现路径工具封装**: 已在 `bootstrap/path_utils.jiang` 中收口相对路径、源码后缀与标准库模块名判断。
- [x] **定义模块加载边界**: Stage1 第一版当前只接受 `import std;` 与相对 `.jiang` 路径导入，不复制 Stage0 的完整 import 复杂度。
- [x] **补跨模块枚举命名空间访问**: 已支持 `store.TokenKind.kw` 这类 `namespace.Enum.member` 形式稳定通过语义分析。

### 2. 实现最小 Lexer
- [x] **定义 Token 数据结构**: 已在 `bootstrap/token_store.jiang` 中定义 `Token` 与较完整的 `TokenKind`，覆盖关键字、标识符、字面量、分隔符、常用运算符与 `eof`。
- [x] **跑通源码扫描主循环**: 当前已支持空白符、注释、标识符、数字、字符串、单字符与关键双字符 token。
- [x] **输出可验证结果**: 当前 `lexer.jiang` 已能输出稳定的 token 文本，包含 kind、span 与转义后的 lexeme。

### 3. 实现最小 Parser
- [x] **抽出可复用 Lexer 模块**: 已将词法逻辑收口到 `bootstrap/lexer_core.jiang`，供后续前端阶段复用。
- [x] **建立 Parser 骨架**: 已加入 `bootstrap/parser_core.jiang`、`bootstrap/parser_store.jiang` 与 `bootstrap/parser.jiang`，使用整数 node id 保存并输出稳定 parse tree。
- [x] **补 Parser 冒烟测试**: 已加入 `bootstrap/parser_sample.jiang`、`bootstrap/parser.golden` 与 `script/stage1_parser_smoke.sh`，固定 parser 当前 parse dump。
- [x] **冻结 Parser v1 边界**: 当前已覆盖 `bootstrap-first` 子集，支持 import、struct、enum、函数、变量、block、if/else、while、for-in、switch-else、return、break/continue、调用、成员访问、索引、数组字面量、struct init 与基础类型修饰符。

### 4. 建立最小 HIR / Semantic 前端
- [x] **建立 HIR 骨架**: 已加入 `bootstrap/hir_store.jiang`、`bootstrap/type_store.jiang`、`bootstrap/symbol_store.jiang`，使用整数 id 保存 HIR、类型与符号。
- [x] **打通 AST -> HIR lowering**: 已加入 `bootstrap/hir_core.jiang`，覆盖当前真实 bootstrap 模块需要的顶层声明、语句、表达式与基础类型构造。
- [x] **实现最小模块图加载**: 已加入 `bootstrap/module_loader.jiang` 与 `bootstrap/module_paths.jiang`，递归加载当前真实 bootstrap 模块集合并缓存 HIR root。
- [x] **补 HIR 冒烟测试**: 已加入 `bootstrap/hir.jiang`、`bootstrap/hir.golden` 与 `script/stage1_hir_smoke.sh`，固定真实模块图的 HIR dump。

### 5. 打通最小 Stage1 后端
- [x] **建立最小 JIR 骨架**: 已加入 `bootstrap/jir_store.jiang`，使用整数 id 保存 Stage1 当前的 JIR 节点树。
- [x] **打通 HIR -> JIR lowering**: 已加入 `bootstrap/jir_lower.jiang`，覆盖当前 codegen fixture 需要的声明、语句与表达式 lowering。
- [x] **实现最小 C emitter**: 当前 `bootstrap/jir_lower.jiang` 已能把固定 fixture 输出成可由系统 C 编译器编译的 C 文本。
- [x] **增加 JIR 冒烟测试**: 已加入 `bootstrap/jir.jiang`、`bootstrap/jir.golden` 与 `script/stage1_jir_smoke.sh`，固定当前 JIR dump。
- [x] **增加 Stage1 codegen 冒烟测试**: 已加入 `bootstrap/compiler_core.jiang`、`bootstrap/compiler.jiang`、`bootstrap/codegen_sample.jiang` 与 `script/stage1_codegen_smoke.sh`，固定 `compiler -> C -> cc -> 可执行` 闭环。

### 6. 收口为真实入口编译器
- [x] **固定 `compile_entry(path, mode)` 为真实入口 API**: `bootstrap/compiler_core.jiang` 当前已支持给定入口模块的 `dump_ast`、`dump_hir`、`dump_jir`、`emit_c`。
- [x] **补真实入口 smoke**: `script/stage1_real_entry_smoke.sh` 当前已覆盖 `source_loader.jiang`、`parser_core.jiang`、`compiler_core.jiang`，并继续扩到 `lexer_core.jiang`、`hir_core.jiang`、`module_loader.jiang`、`jir_lower.jiang`、`parser_store.jiang`、`buffer_int.jiang`、`buffer_bytes.jiang`、`intern_pool.jiang`、`module_paths.jiang`、`token_store.jiang`、`hir_store.jiang`、`jir_store.jiang`、`symbol_store.jiang`、`type_store.jiang`。
- [x] **冻结数组 / slice ABI 规则**: 宿主 Stage0 与 Stage1 当前已统一固定数组、slice 和数组别名的 C 生成约定，真实入口 codegen 可稳定通过。
- [x] **完成核心 bootstrap 模块图覆盖**: 当前所有核心前端、store、driver 与模块图基础设施都已纳入正式 real-entry 回归；剩余 sample / demo / probe 文件不视为正式模块图契约。
- [x] **完成 growable store 主链迁移**: `buffer_int` / `buffer_bytes` / `intern_pool` 已接入，`token/parser/hir/symbol/type/jir` 当前都已切到 growable buffer；后续重点是继续压实真实模块图与宿主兼容性。
- [x] **收尾残余 Stage1 告警**: 当前正式回归路径中的 `stage1_codegen_smoke`、`stage1_real_entry_smoke` 与 `test.sh` 日志已无稳定复现 warning 命中，主链告警已收口。

### 7. 为 Stage1 补测试与样例
- [x] **增加 Stage1 样例源码**: 已加入 `bootstrap/sample.jiang` 与 `bootstrap/lexer.jiang`。
- [x] **增加 Stage1 冒烟测试脚本**: 已加入 `script/stage1_smoke.sh`，验证 Stage0 编译器可以编译并运行第一个 Stage1 程序。
- [x] **定义 golden 测试格式**: 已使用 `bootstrap/lexer.golden` 固定 lexer 输出，并在 `script/stage1_smoke.sh` 中做完整 diff。

### 8. 控制 Stage1 范围，避免失控
- [x] **冻结第一版自举子集**: 目前继续维持 bootstrap-first 语法边界，没有 Stage1 真实需要的语法不再扩展。
- [x] **避免提前引入复杂后端规划**: 当前继续维持 `AST -> HIR -> JIR -> C` 主线，不并行推进 LLVM / MIR / 原生后端。
- [x] **优先补标准库缺口**: 当前 Stage1 所需的最小 `std.io` / `std.debug` / `std.fs` 已足够支撑真实入口编译链，没有新的标准库阻塞缺口。

## 📝 架构笔记 (Architectural Notes)
- **File as a Struct**: 借鉴 Zig 的设计，将每个文件视为一个独立的命名空间，避免全局符号冲突。
- **Lazy Analysis**: 考虑未来引入惰性语义分析，仅在符号被引用时才进行深度类型检查。
- **Self-hosting Path**: `import` 的完整实现是编译器实现自举（用 Jiang 编写 Jiang）的关键前提。
