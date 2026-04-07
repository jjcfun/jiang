# Jiang 编程语言

Jiang（江）是一门旨在成为编程领域“银弹”的现代静态类型编程语言。它的核心设计哲学是 `All in one`，结合了系统级语言的底层内存控制与现代高级语言的丰富表达能力，追求在性能、安全性与开发体验之间达到平衡。

## 前言
为什么设计这门语言？原因出自我全栈工程师的工作经历，以及我对于编程语言的爱好：在经历了各种语言的洗礼之后，我依旧没能找到一门完美的编程语言。它们或多或少存在着一些缺点，尽管现在流传着编程语言**没有银弹**的说法，我还是决定亲自设计一门语言。

在这之前，我先说一个**暴论**：编程语言的设计，特性什么的都是次要，设计哲学和品味很重要。

以`All in one`为目标，Jiang语言应该主要包含这些特性：

##### 1. 系统级编程语言

支持操作系统、编译器等的编写



##### 2. 静态类型和类型安全

支持类型推导、联合类型（Tagged Union Type）、模块（Module）、所有权等机制



##### 3. 原生支持协程和异步编程



##### 4. 杀手级特性：支持自定义子语言（自定义语法解析）

> 自定义语法的功能将会在**1.0版本的Stage2**与标准库一起实现

在大语言模型追求多模态的今天，文字也好，图片也罢，在模型内部以向量形式统一表示。 Jiang语言追求的是不同的语法下，用统一的AST结构来表示，而不丢失类型信息。  

有些人可能会问，我都自定义语法了，那和自己重新实现一个新语言有什么区别？有的，兄弟：

+ 在我开发编译器的经验中，即使是直接用LLVM实现了编译器后端的情况下，手写词法解析和语法解析在整个编译器的工作量也不足5%。真正需要花费大量时间和精力的是类型检查、语义分析、代码生成以及后续的标准库实现，包管理等等。

+ 虽然语法不同，但是生成的AST却是相同的，所以他们共享数据类型和结构，不同语法之间能互相调用各自声明的类型，不需要通过FFI。

+ 多种语法的需求是客观存在的。这些场景或多或少都涉及在宿主语言中使用DSL，如响应式布局：SwiftUI、Compose、React、Vue；着色器语言：HLSL、GLSL；数据库查询：SQL；并行计算：SIMD；甚至配置文件：TOML、XML、JSON等。

  以下就是在jiang语言中直接使用sql的例子，假设有哪位好心人实现了SQL子语言，就可以在Jiang语言内嵌入使用：

  ```c#
  struct School {
  	Int id;
    UInt8[] name;
  }
  
  struct User {
  	Int school_id;
    Int age;
  	UInt8[] name;
  }
  
  User user = {school_id: 100, age: 18, name: "jiang" }
  
  Database some_db = ...;
  
  /// 用类似 #[Lang] {xxx} 的形式调用SQL语言，大括号{}内就是子语言的语法
  /// sql中的'select', 'from', 'limit'是自定义关键字，拼写错误编译器直接报错
  /// 而'id', 'user.school_id'会受到jiang语言自带的类型检查限制，属性名写错了，编译也不会通过
  School?* school = #SQL(my_db) { select * where id == user.school_id from School limit 1 }
  ```



## 开发进展

> ⚠️ **注意**：Jiang 语言目前处于快速迭代的早期阶段。在 **1.0 正式版**发布之前，**语言语法是不稳定的**，随时可能发生重大调整。

好吧，它的**1.0正式版**将会经历以下几个阶段：

+ **Stage0**: 用 C 语言实现编译器的基本功能。该阶段已经完成：当前编译器已具备多模块、Union/Pattern、Binding、最小标准库与 `import std;` 支持，主链稳定为 `AST -> JIR -> C`，并已通过当前测试集验证

+ **Stage1**: 用 Stage0 的 Jiang 编译器启动自举。当前 Stage1 主线固定在 `bootstrap/`：根目录保留可复用编译器模块，`bootstrap/entries/` 放 smoke driver 和工具入口。当前已经具备可工作的 `AST -> HIR -> JIR -> C` 真实入口链路，`compiler_core.compile_entry(path, mode)` 可对当前受支持的 Stage1 模块执行 `dump_ast` / `dump_hir` / `dump_jir` / `emit_c`；同时 `--backend llvm` 已经作为可选完整后端进入回归，核心 bootstrap 模块也已进入 LLVM smoke 覆盖，但默认后端当前仍保持 C

+ **Stage2**: 在 Stage1 自举编译器真正收口并可接管主职责之后，再用 Jiang 语言重构 Jiang 编译器，并实现自定义语法等高级功能。当前 `compiler/` 目录仅作为 Stage2 预留目录，不承载正式主线实现

+ **Stage3**: 包管理工具的实现

  


## 学习指南

有关详细的语法和特性说明，请参阅：
- **[Jiang 语言指南](doc/jiang.md)**

## 快速开始

### Stage1 自举子集（当前约定）

为了让 Stage1 的第一版自举实现尽量稳定，当前建议把可依赖能力收敛到下面这个子集：

- 模块与导入：`import "..."`、`import alias = "..."`、`import std;`
- 基础类型：`Int`、`UInt8`、`UInt16`、`Float`、`Double`、`Bool`
- 控制流：`if`、`while`、`for Int i in a..b`
- 绑定：普通 binding、tuple binding、Union variant binding
- 复合类型：`struct`、`enum`、`union`
- 最小标准库：`std.io`、`std.debug`、`std.fs`

当前 Stage0 里仍然建议避免把这些能力作为自举前提：

- 依赖复杂 JIR lowering 的高层语法糖组合
- 跨模块共享 tuple typedef 的复杂场景
- 超出最小标准库范围的运行时能力

### 命名规范（当前约定）

为了让 Stage1 自举代码和后续标准库实现保持一致，当前建议采用下面这套命名规则：

- 类型名使用 `PascalCase`
  例如 `TokenKind`、`SourceLoader`
- 函数名使用 `snake_case`
  例如 `read_source`、`push_token`
- 变量名与结构体字段名使用 `snake_case`
  例如 `token_count`、`start_offset`
- 枚举成员使用 `snake_case`
  例如 `kw`、`string_lit`、`left_paren`
- 模块别名优先使用 `PascalCase`
  例如 `import Store = "token_store.jiang";`

这套约定的目标是明确区分“类型”和“值”：`TokenKind` 是类型，`kw` / `string_lit` 是枚举值，避免把枚举成员写成看起来像类型名的 `SomeField`。

### 前提条件

- CMake
- C 编译器 (gcc, clang 等)

### 构建编译器

```bash
mkdir build
cd build
cmake ..
make
```

### 运行测试

```bash
./script/test.sh
```

当前仓库状态下，`script/test.sh` 会构建 `jiangc` 并运行全部测试用例；以仓库当前版本为准，测试集已全部通过。

### Stage1c CLI

当前可以通过下面的脚本构建正式的 Stage1 编译器二进制：

```bash
bash ./script/build_stage1.sh
```

生成产物：

```bash
./build/stage1c
```

当前 `stage1c` 支持最小正式 CLI：

```bash
./build/stage1c --help
./build/stage1c bootstrap/entries/lexer.jiang > out.c
./build/stage1c --mode emit-c bootstrap/entries/lexer.jiang > out.c
./build/stage1c --mode dump-ast bootstrap/entries/lexer.jiang
./build/stage1c --mode dump-hir bootstrap/entries/hir.jiang
./build/stage1c --mode dump-jir bootstrap/entries/jir.jiang
```

约定：

- `bootstrap/entries/` 是 Stage1 的入口层
- `bootstrap/` 根目录保留可复用的编译器模块
- `stage1c <entry>` 默认等价于 `stage1c --mode emit-c <entry>`

### Stage1 Runtime 边界（当前冻结）

build system 收口之后，Stage1 当前主线不再直接切 LLVM，也不直接进入 Stage2，而是先固定 runtime 与标准库边界。

当前对普通 Jiang 项目公开的最小标准库表面只有：

- `std.io`
- `std.debug`
- `std.fs`

其中：

- `std.io` 负责最小输出能力
- `std.debug` 负责断言与调试辅助
- `std.fs` 负责文件存在性、读文件与少量路径辅助

当前 Stage1 宿主 runtime ABI 只冻结为下面这 6 个 intrinsic：

- `__intrinsic_print`
- `__intrinsic_assert`
- `__intrinsic_read_file`
- `__intrinsic_file_exists`
- `__intrinsic_alloc_ints`
- `__intrinsic_alloc_bytes`

这些 intrinsic 主要供标准库实现、bootstrap 编译器内部基础设施和编译器生成的过渡 glue 使用，不作为普通 Jiang 项目的推荐 public surface。面向用户的代码应优先使用 `std.*`。

当前并不追求完全移除 libc；现阶段只要求把 libc 依赖集中在宿主 C 层，主要集中在 `include/runtime.h` 与编译器宿主实现内部。当前已经固定的 libc 接触点包括：

- 文件读取：`fopen` / `fread` / `fclose`
- 内存分配：`malloc` / `calloc` / `free`
- 文本与打印：宿主 `printf` 路径及等价能力

### Stage1 UTF-8 语义（当前约定）

当前 Stage1 只支持 UTF-8 的字节层正确性，不引入完整 Unicode 文本语义。

- 源码按 UTF-8 字节流读取
- 字符串字面量按 UTF-8 字节序列保留
- `UInt8[]` 继续表示字节切片
- `length` 按字节数计算
- `s[i]` 返回第 `i` 个字节

当前仍然不支持：

- Unicode 标识符
- code point / rune 类型
- 按字符计数或按字符索引
- grapheme cluster 语义

### 最小 Build System（实验中）

当前已经加入第一版实验性 project manifest 与子命令入口：

```bash
./build/jiangc build --manifest ./examples/build_demo/jiang.build
./build/jiangc run --manifest ./examples/build_demo/jiang.build
./build/jiangc build --manifest ./examples/build_demo/jiang.build --target alt
./build/jiangc test --manifest ./examples/build_demo/jiang.build --target alt
./build/jiangc test --manifest ./examples/build_demo/jiang.build
```

当前 manifest 采用最小 `jiang.build` 形式，示例见 `examples/build_demo/jiang.build`。第一版只覆盖：

- `name`
- `root`
- `stdlib_dir`
- `test_dir`
- `module.<name> = <path>`
- `target.<name>.root = <path>`
- `target.<name>.test_dir = <path>`

其中 `module.<name>` 会把模块名映射到入口文件，允许项目内直接使用：

```jiang
import greet;
```

当前目标是先统一项目入口、标准库路径与 `jiang -> C -> cc` 构建链，不在这一版中直接引入完整包管理与可编程构建 DSL。

### LLVM 后端（当前状态）

当前仓库已经提供实验完成态的可选 LLVM 后端：

```bash
./build/jiangc --backend llvm tests/std_import_test.jiang
./build/jiangc --emit-llvm tests/std_import_test.jiang
./build/jiangc build --backend llvm --manifest ./examples/build_demo/jiang.build
```

当前状态：

- `JIR -> C` 仍是默认后端
- `--backend llvm` 已通过当前正向测试集
- bootstrap 关键入口已经进入 LLVM smoke
- 当前下一步是评估 LLVM 是否已达到 Stage1 默认后端候选，而不是立刻切换默认后端

## 许可证

本项目采用 **Apache License 2.0** 许可证。详情请参阅 [LICENSE](LICENSE) 文件。
