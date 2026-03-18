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
- [ ] **跨模块命名空间**: 支持 `alias.member` 语法在语义分析和生成阶段的符号查找（目前仅支持直接导入）。
- [ ] **路径标准化**: 完善 `module_cache` 的绝对路径查找，避免重复解析。

### 2. 生成器完善 (Generator Updates)
- [ ] **自动化编译链**: 改进生成器，使其能自动生成 `Makefile` 或调用 `gcc` 编译所有相关的 `.c` 文件，而不仅仅是靠 `#include`。
- [ ] **更优雅的 Slice 定义**: 将 `Slice_int64_t` 等公共定义提取到单独的 `jiang_runtime.h` 中。

## 📝 架构笔记 (Architectural Notes)
- **File as a Struct**: 借鉴 Zig 的设计，将每个文件视为一个独立的命名空间，避免全局符号冲突。
- **Lazy Analysis**: 考虑未来引入惰性语义分析，仅在符号被引用时才进行深度类型检查。
- **Self-hosting Path**: `import` 的完整实现是编译器实现自举（用 Jiang 编写 Jiang）的关键前提。
