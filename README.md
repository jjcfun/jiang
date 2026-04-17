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

+ **Stage1**: 用 Stage0 的 Jiang 编译器启动自举。该阶段已经完成：Stage1 主线固定在 `bootstrap/`，根目录保留可复用编译器模块，`bootstrap/entries/` 放 smoke driver 和工具入口；`compiler_core.compile_entry(path, mode)` 已稳定支持 `dump_ast` / `dump_hir` / `dump_jir` / `emit_c`，正式 `stage1c` CLI、manifest 与 build workflow 已收口，并通过统一 Stage1 完成验收与 selfhost 回归；`--backend llvm` 当前作为可选完整后端保留在回归中，但默认后端仍保持 C

+ **Stage2**: 在 Stage1 自举编译器真正收口并可接管主职责之后，再用 Jiang 语言重构 Jiang 编译器，并实现自定义语法等高级功能。该阶段当前已经完成首个正式主线目标：`compiler/` 是唯一继续演进的编译器主线目录，默认 `script/build_stage2.sh` 会优先使用已有旧版 `stage2c` 作为 bootstrap compiler，再由它重编自身产出当前 `build/stage2c`；当本地没有可用旧版 `stage2c` 时，仍可回退到 `Stage1 -> Stage2 bootstrap -> Stage2 self-rebuild`。当前已具备 `frontend -> HIR -> JIR -> C/LLVM`、多模块与 `public`/alias import、`struct` / `enum` / `UInt8` / `UInt8[]` / array / pointer 基础语义，以及 `emit-c` / `run` / `selfhost` / `error` / `llvm` / `llvm-error` / `complete` 回归；`bootstrap/` 继续保留为已完成的 Stage1 冻结基线。内部开发产物仍保持 `build/stage2c`，正式 release 对外分发的可执行文件名为 `jiang`

+ **Stage3**: 包管理工具的实现

  


## 学习指南

有关详细的语法和特性说明，请参阅：
- **[Jiang 语言指南](doc/jiang.md)**
- **[编译器开发说明](doc/develop.md)**

## 快速开始

### 前提条件

- CMake
- C 编译器 (gcc, clang 等)
- LLVM `21.1.x`

当前 `dev` 分支固定 LLVM minor 版本线为 `21.1.x`。脚本解析顺序是：

- 显式设置的 `LLVM_CONFIG`
- `JIANG_LLVM_ROOT/bin/llvm-config`
- `PATH` 中的 `llvm-config`

但无论从哪里解析到 `llvm-config`，版本都必须匹配 `21.1.x`，否则脚本和 CMake 都会直接失败。推荐方式有两种：

- Homebrew: `llvm@21`
- 自己安装到 `~/.jiang/toolchains/llvm-21.1`

```bash
/opt/homebrew/opt/llvm@21
```

### 构建编译器

```bash
mkdir build
cd build
cmake .. -DJIANG_LLVM_ROOT=/opt/homebrew/opt/llvm@21
make
```

### 运行测试

```bash
./script/test.sh
```

当前仓库状态下，`script/test.sh` 会构建 `jiangc` 并运行 Stage0、Stage1 基线以及 Stage2 主线验收链。

Stage1 的正式完成验收脚本是：

```bash
bash ./script/stage1_complete_smoke.sh
```

它会统一覆盖：

- Stage1 lexer / parser / HIR / JIR / codegen smoke
- real-entry smoke
- Stage1 manifest build/run 矩阵
- `stage1c` 构建、link、run、selfhost 验证

Stage2 的正式完成验收脚本是：

```bash
bash ./script/stage2_complete_smoke.sh
```

它会统一覆盖：

- `stage2c` CLI
- `emit-c`
- `run`
- `error`
- `emit-llvm`
- `llvm-error`

当前推荐的编译器主线是：

- `bootstrap/`: 冻结的 Stage1 bootstrap 基线
- `compiler/`: 唯一继续演进的 Stage2 编译器主线

当前的 Stage2 bootstrap 策略固定为：

- 显式指定时优先使用 `STAGE2_BOOTSTRAP_STAGE2`
- 否则优先使用本地 `dist/` 中当前平台可用的最新 `jiang-*.tar.xz`
- 再退到 `~/.jiang/bin/jiang`
- 最后才退到本地上一次成功构建留下的 `build/stage2c`

`stage1c` 现在只保留为历史兜底路径，不再是 Stage2 的默认主构建入口。

当前仓库状态下，可以把 Stage2 视为“已完成首个正式可用版本”；后续演进继续围绕 `compiler/` 展开，而不是继续扩张 `bootstrap/`。
当前仓库也应视为 **Stage2 主线仓库**：历史 C / Stage0、Stage1 和 bootstrap 资产仍然保留在仓库中，但它们的职责已经收敛到 bootstrap、冷启动和回归验证。后续如果需要进一步解耦冷启动体系，可以把 bootstrap orchestration 和历史阶段整理到单独的 `jiang-bootstrap` 仓库，而当前仓库继续只承担 Stage2+ 主线演进。

Stage2 的正式 bootstrap 合约见 [compiler/BOOTSTRAP.md](compiler/BOOTSTRAP.md)。

### Stage2 Release Packaging

当前 Stage2 的发布目录约定为：

- `build/`: 本地临时构建产物
- `dist/<version>/`: 可发布的 source tarball / binary tarball / checksum / release notes
- `releases/<version>.md`: 仓库内跟踪的 release notes 源文件

打包当前 release：

```bash
bash ./script/package_stage2_release.sh v0.2.0
```

安装当前 release 到推荐 toolchain 目录：

```bash
bash ./script/install_stage2_toolchain.sh v0.2.0
```

如果本机已安装并登录 `gh`，可直接发布 GitHub release：

```bash
bash ./script/publish_github_release.sh v0.2.0
```

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
