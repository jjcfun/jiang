# Jiang Stage1 Build System Plan

## 目标

当前 Stage1 的真实入口编译器已经基本可工作，但项目构建入口仍然分散在 `CMakeLists.txt`、`script/*.sh`、`bootstrap/*.jiang` 与命令行参数约定里。下一阶段的主线目标不是直接开始 Stage2，也不是先切换 LLVM，而是先把 Jiang 自身的构建模型收口成一个最小但正式的 build system。

这个 build system 的职责是：

- 描述一个 Jiang project 的 root module 与模块图
- 为 compilation 注入 `std`、`root` 等特殊模块
- 统一 `jiang -> C -> cc -> exe` 这条当前过渡构建链
- 统一 `build` / `run` / `test` 的用户入口
- 为后续标准库分层、runtime 收口、原生后端与 Stage2 重构提供稳定地基

## 为什么现在先做这个

当前最缺的不是新的编译器能力，而是稳定的工程组织能力。

如果现在直接开始 Stage2，会在没有固定项目模型、模块映射和测试入口的前提下重写编译器，后续极易返工。

如果现在直接移除 C、接 LLVM，会把后端复杂度提前到“构建模型尚未稳定”的阶段，收益低，风险高。

如果现在优先推进“完全移除 libc 依赖”，也会受到 target、sysroot、runtime 边界和项目入口未统一的限制。

因此顺序应当是：

1. 先稳定 build system
2. 再收口标准库与 runtime 边界
3. 再评估后端替换
4. 最后再正式进入 Stage2

## 设计原则

- 外部体验优先简单，参考 Cargo：`jiang build`、`jiang run`、`jiang test`
- 内部模型参考 Zig：围绕 module、root module、artifact 和 import name mapping
- 第一版优先声明式 manifest，不急于做完整可编程 DSL
- 第一版只覆盖 Jiang 当前真实需要，不提前做包管理器
- 必须能直接接入当前 Stage0/Stage1 过渡链，避免推倒重来
- 当前 manifest 明确定位为 bootstrap 过渡方案，未来允许被 `build.jiang` 或自定义 DSL 整体替换

## 第一版不做什么

- 不实现完整的 `build.jiang` 可编程构建 DSL
- 不实现远程依赖下载或包版本解析
- 不以 LLVM / 原生后端替换 C 过渡后端为目标
- 不把标准库重写或完全去 libc 化作为本阶段验收条件
- 不为了 build system 重新设计语言语法
- 不对临时 manifest 做长期兼容承诺

## 当前执行策略

由于 Stage2 之后大概率会切到 Jiang 自身的构建脚本或自定义 DSL，当前第一阶段不应投入到完整 JSON / TOML 解析器上。当前策略应当是：

- 使用一个极简、廉价、易删除的临时 manifest
- 只支持当前 build system 真正需要的少量字段
- 把重点放在统一 CLI、项目入口、标准库路径与测试入口，而不是配置格式本身

当前推荐的临时文件名：

- `jiang.build`

当前推荐的临时语法：

```txt
name = build_demo
root = main.jiang
stdlib_dir = ../../std
test_dir = tests
module.greet = greet.jiang
target.alt.root = alt.jiang
target.alt.test_dir = alt_tests
```

约束：

- 每行一个 `key = value`
- 允许空行
- 允许 `#` 注释
- 第一版只支持字符串型值，不支持数组、对象与条件逻辑
- 模块映射使用 `module.<name> = <path>`
- 最小多 target 入口使用 `target.<name>.root = <path>`
- target 级测试入口使用 `target.<name>.test_dir = <path>`
- 这是 bootstrap manifest，不是长期公开标准

## 推荐参考

- 核心编译模型参考 Zig：module、root module、`std` 注入、artifact
- 项目与 target 组织可参考 Swift Package Manager
- CLI 体验参考 Cargo

第一版的产品定位应当是：

“外部像 Cargo，内部像 Zig。”

## 分阶段计划

### Phase 1: 固定最小项目模型

目标：

- 定义第一版 bootstrap manifest 形态，优先使用 `jiang.build`
- 定义单项目下的 root module、target、artifact 概念
- 明确默认目录约定与可覆盖项

建议最小字段：

- `name`
- `root`
- `stdlib_dir`
- `test_dir`

验收标准：

- 能用一个 `jiang.build` 描述当前单模块程序与最小测试集
- 能稳定支持默认项目入口与标准库路径覆盖
- 不依赖隐式扫描整个目录来猜测项目主入口

### Phase 2: 固定模块导入与注入规则

目标：

- 定义模块名到入口文件的映射规则
- 明确 `import std;` 与普通模块导入的编译时注入方式
- 约定 `root` / `std` / `builtin` 的语义边界

建议规则：

- `root` 指向当前 artifact 的 root module
- `std` 指向标准库根模块
- 用户模块后续通过 manifest 中的 `modules` 显式注册
- 相对路径 import 继续保留，用于同一模块内局部文件组织

验收标准：

- 可以稳定支持 `import std;`
- 第一阶段先不要求完整模块名导入；后续阶段再把 `modules` 真正接进编译链
- 模块解析失败时能给出明确错误，而不是回退成模糊路径查找

### Phase 3: 实现统一 CLI

目标：

- 提供统一命令入口，替代散落脚本和手工步骤

第一版建议命令：

- `jiang build`
- `jiang run`
- `jiang test`

最小行为：

- `build`：生成 C 并调用系统 C 编译器产出可执行文件
- `run`：构建后执行默认产物
- `test`：编译并运行项目测试

验收标准：

- 当前仓库至少能从单条命令构建并运行一个 Jiang 程序
- `bootstrap/` 下最小样例可以通过统一命令触发编译
- 示例项目的最小测试集可以通过统一入口下执行

### Phase 4: 接入当前 Stage1 自举链

目标：

- 让 build system 能描述当前 bootstrap-first 编译器的构建入口
- 收口 `script/stage1_*` 中重复的路径、入口和参数约定

建议接入范围：

- `bootstrap/compiler.jiang`
- `bootstrap/compiler_core.jiang`
- `bootstrap/lexer.jiang`
- `bootstrap/parser.jiang`

验收标准：

- 可用统一配置描述 bootstrap artifact
- 至少一个 Stage1 real-entry 场景通过 build system 触发
- 不破坏现有脚本回归，允许新旧入口并行一段时间

### Phase 5: 为标准库与 runtime 分层做准备

目标：

- 在 build system 中固定 `stdlib_dir`、sysroot、target 等边界
- 为后续标准库去 libc 依赖和 runtime 分层创造条件

验收标准：

- 标准库根目录不再依赖散落命令行手工传参
- 后续 runtime 替换或平台分层时，不需要重新设计项目模型

## 推荐落地顺序

1. 先写清 `jiang.build` 的临时规范
2. 再实现 `build/run/test` CLI 与默认 manifest 查找
3. 再接入 `std` 路径与示例项目测试入口
4. 再把 `bootstrap/` 的至少一个入口接进统一入口
5. 完成后，再进入模块映射、标准库/runtime 收口阶段

## 当前里程碑拆分

### Milestone A: Bootstrap CLI 落地

目标：

- `jiangc build`
- `jiangc run`
- `jiangc test`

验收：

- 默认读取 `jiang.build`
- 支持 `--manifest` 指定其他路径
- 不破坏现有单文件编译模式

### Milestone B: 示例项目与回归接入

目标：

- 仓库内有一个最小示例项目
- 通过 smoke 脚本验证 `build/run/test`

验收：

- 示例项目可构建
- 示例项目可运行
- 示例项目自带最小 `tests/` 并可通过 `jiangc test`

### Milestone C: Stage1 入口接入

目标：

- 至少一条 `bootstrap/` 入口可从 build system 触发

验收：

- 允许和现有 `script/stage1_*` 并行
- 证明 build system 已不只是 demo，而是真能驱动 Stage1 资产

## 完成信号

达到下面这些条件时，可以认为 Stage1 后续的 build system 目标基本完成：

- Jiang 项目有统一的项目清单与默认入口
- `import std;`、root module、用户模块映射全部由构建系统统一描述
- `jiang build` / `jiang run` / `jiang test` 可以覆盖当前最主要的使用路径
- `bootstrap` 至少一条真实入口链可通过新系统驱动
- 现有脚本开始退居回归补充，而不是主入口

## 后续衔接

当 build system 稳定后，下一阶段建议转向：

1. 标准库与 runtime 分层，逐步减少对 libc 的直接依赖
2. 评估后端演进路线，包括是否继续走 C 过渡后端或引入 LLVM / 原生后端
3. 在工程骨架稳定的前提下，正式启动 Stage2 的 Jiang 版编译器重构
