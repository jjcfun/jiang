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

#####  5. 面向AI的编程语言（2.0版本实现）

如果你了解了第4点子语言的概念，那么不难推断：子语言可以面向人类设计，当然可以面向AI设计。我只要将词法解析生成的Token扩展到大语言模型的Token...💡



## 开发进展

> ⚠️ **注意**：Jiang 语言目前处于快速迭代的早期阶段。在 **1.0 正式版**发布之前，**语言语法是不稳定的**，随时可能发生重大调整。

好吧，它的**1.0正式版**将会经历以下几个阶段：

+ **Stage0**: 用 C 语言实现编译器的基本功能。当前已经具备多模块、Union/Pattern、Binding、最小标准库与 `import std;` 支持，主链保持为 `AST -> JIR -> C`，仍在继续收口剩余 lowering/codegen 细节

+ **Stage1**: 用Stage0的Jiang语言编译器实现自举，并实现标准库的最小集合，此阶段也将借助AI实现

+ **Stage2**: 用Jiang语言重构Jiang语言编译器，并实现自定义语法等高级功能，此阶段追求代码质量和性能，并使用人工实现

+ **Stage3**: 包管理工具的实现

  


## 学习指南

有关详细的语法和特性说明，请参阅：
- **[Jiang 语言指南](doc/jiang.md)**

## 快速开始

### Stage1 自举子集（当前约定）

为了让 Stage1 的第一版自举实现尽量稳定，当前建议把可依赖能力收敛到下面这个子集：

- 模块与导入：`import "..."`、`import std;`
- 基础类型：`Int`、`UInt8`、`UInt16`、`Float`、`Double`、`Bool`
- 控制流：`if`、`while`、`for Int i in a..b`
- 绑定：普通 binding、tuple binding、Union variant binding
- 复合类型：`struct`、`enum`、`union`
- 最小标准库：`std.io`、`std.assert`、`std.string`、`std.file`、`std.path`

当前 Stage0 里仍然建议避免把这些能力作为自举前提：

- 依赖复杂 JIR lowering 的高层语法糖组合
- 跨模块共享 tuple typedef 的复杂场景
- 超出最小标准库范围的运行时能力

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

## 许可证

本项目采用 **Apache License 2.0** 许可证。详情请参阅 [LICENSE](LICENSE) 文件。
