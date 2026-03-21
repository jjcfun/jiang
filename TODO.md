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

## 🚀 待办事项 (TODO) - 模块化系统与 Import 重构

### 1. 架构升级：一文件一符号表 (Module-based Symbol Table)
- [x] **完善递归加载逻辑**: 在语义分析遇到 `AST_IMPORT` 时，触发递归调用 `get_or_analyze_module(path)`。
- [x] **符号导入**: 已实现将被导入模块的 `public` 符号合并到当前作用域。

## 🚀 待办事项 (TODO) - 模块化系统与 Import 重构

### 1. 架构升级：进阶功能
- [x] **跨模块命名空间**: 支持 `alias.member` 语法。
- [x] **路径标准化**: 完善 `module_cache` 的绝对路径查找。
- [x] **结构体封装性**: 实现私有字段访问限制及强制构造函数初始化逻辑。

### 2. 生成器完善 (Generator Updates)
- [x] **自动化编译链**: 改进生成器，使其能自动生成 `Makefile` 或调用 `gcc` 编译所有相关的 `.c` 文件。
- [x] **更优雅的 Slice 定义**: 将 `Slice_int64_t` 等公共定义提取到单独的 `include/runtime.h` 中。
- [x] **全局头文件保护**: 为所有生成的 C 文件增加 `#ifndef` 保护。

## 🚀 待办事项 (TODO) - 核心架构升级

### 0. Binding 语法统一 (Highest Priority)
- [ ] **定义 Binding 语法**: 统一绑定原子形式为 `type_expr identifier`。
    - 例: `Int x`, `Int! y`, `_ x`, `_! y`
- [x] **统一 For Binding**: `for` 语句只接受 binding 语法，不再接受裸标识符。
    - 例: `for Int i in 0..10 { ... }`
    - 例: `for (_ i, _ item) in list.indexed() { ... }`
- [ ] **统一解构绑定**: 将元组解构与 Union 变体绑定统一为 binding 列表。
    - 例: `(_ x, _! y) = foo()`
    - 例: `Shape.circle(_ x1)`
- [ ] **补齐 Binding AST / 语义分析**: 让 parser、semantic、codegen 共享同一套 binding 节点与检查逻辑。
    - [x] 拆分独立 `AST_BINDING_LIST`，不再复用 `AST_BLOCK`
- [x] **补齐 Binding 测试**: 增加 `for`、赋值解构、Union 解构三类 binding 用例。

### 1. 引入 JIR (Jiang Intermediate Representation)
- [ ] **定义 JIR 结构**: 实现基于指令流的线性 IR（类似于 Zig 的 ZIR 或三地址码）。
    - 定义 `JirInstruction` (OpCode, dest, src1, src2) 和 `JirLocal`。
- [ ] **实现 Lowering 流程**: 将 AST 降级为 JIR 序列。
    - **表达式扁平化**: 将嵌套表达式拆解为中间变量赋值。
    - **语法糖消解**: 在 IR 层展开 `$` 操作符、Union 模式匹配和 `for-in` 循环。
    - **显式符号绑定**: 终结 Codegen 阶段的字符串查找，每个引用直接关联 `Symbol`。
- [ ] **重构 Codegen**: 将 `generator.c` 切换为基于 JIR 生成 C 代码，消除现有的状态管理混乱。

### 2. Union 类型与模式匹配 (Union & Pattern Matching)
- [x] **Union 基础实现**: 支持 `union(Tag) Name { ... }` 语法及 C 代码生成。
- [ ] **Switch 模式匹配**: 实现 `switch` 语句对 Union 变体的解构（Pattern Matching）。
- [ ] **匿名 Union**: 支持不带显式 Tag Enum 的 Union。

## 🐞 待修复的 Bug (Bug Fixes)
- [x] **构造函数映射**: 修复 `Vault(...)` 没能正确翻译为 `Vault_new(...)` 的问题。
- [x] **多模块符号可见性**: 解决 `Alias.member` 在独立编译模式下产生的链接错误。
- [x] **切片语法兼容性**: 完善 `x[]` 在 C 语言层面的零索引占位生成。
- [ ] **空元组返回处理**: 统一将 `() func()` 在 C 声明中映射为 `void func()`。

## 📝 架构笔记 (Architectural Notes)
- **File as a Struct**: 借鉴 Zig 的设计，将每个文件视为一个独立的命名空间，避免全局符号冲突。
- **Lazy Analysis**: 考虑未来引入惰性语义分析，仅在符号被引用时才进行深度类型检查。
- **Self-hosting Path**: `import` 的完整实现是编译器实现自举（用 Jiang 编写 Jiang）的关键前提。
