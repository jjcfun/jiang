# Jiang 编译器开发进度与计划

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

## 🎯 Stage0 完成条件 (Must Finish In Stage0)

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
- [ ] **重构 JIR Codegen**: 让 `jir_generate_c` 直接基于 JIR 产出 C，逐步淘汰 AST 直出路径。

### 2. 完成 Union 模式匹配
- [x] **Union 基础实现**: 支持 `union(Tag) Name { ... }` 语法及 C 代码生成。
- [x] **Switch 模式匹配**: 实现 `switch` 语句对 Union 变体的解构（Pattern Matching）。
- [x] **将 Union 绑定并入 Binding 体系**: `Shape.circle(_ x)` 与普通 binding list 使用一致检查逻辑。
- [x] **匿名 Union**: 支持不带显式 Tag Enum 的 Union。

### 3. 最低限度标准库 (Minimal Stdlib)
- [x] **建立标准库目录结构**: 约定 `std/` 或等价 sysroot 布局，避免继续依赖测试目录充当库。
- [x] **实现最小标准库模块**:
    - [x] `std/io`
    - [x] `std/assert`
    - [x] `std/string`
    - [x] `std/file`
    - [x] `std/path`
- [ ] **梳理 intrinsic 与标准库边界**: 将 `print`、`assert` 等能力从“编译器特殊对待”整理为“标准库导出 + 少量 intrinsic 支撑”。
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

## 📦 Stage1 前置条件 (Prepare During Stage0)
- [x] **定义自举子集**: 明确 Stage1 第一版 Jiang 编译器允许依赖的语法与标准库范围。
- [x] **同步语言文档**: 更新 `README`、`doc/jiang.md`、`doc/ebnf.md`，确保与当前实现一致。
- [x] **准备标准库示例程序**: 至少能用 Stage0 编译器编译一个依赖标准库的简单工具程序。

## 📝 架构笔记 (Architectural Notes)
- **File as a Struct**: 借鉴 Zig 的设计，将每个文件视为一个独立的命名空间，避免全局符号冲突。
- **Lazy Analysis**: 考虑未来引入惰性语义分析，仅在符号被引用时才进行深度类型检查。
- **Self-hosting Path**: `import` 的完整实现是编译器实现自举（用 Jiang 编写 Jiang）的关键前提。
