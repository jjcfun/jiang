# Jiang Next 架构

Jiang Next 编译器围绕稳定的阶段边界组织。本文只保留整体架构、跨阶段边界和目录约定；
各阶段的详细设计放在对应阶段文档中。

## 顶层流程

```text
driver/cli -> pipeline.compile_with_options
                |
                v
        package source/AST -> HIR -> type facts -> MIR -> checked MIR -> backend output
                                      \      \          \          \
                                       ------ layout facts --------
```

流程中的几个块对应：

- `package source/AST`：package manifest、source、syntax、module graph 和 resolve。
- `HIR`：resolve 直接生成的未类型化语义树。
- `type facts`：`TypeCheckResults` 和 `MonomorphInstances`。
- `MIR`：HIR lowering 生成的 CFG。
- `checked MIR`：borrow check 后经过 drop elaboration 的 MIR。
- `backend output`：LLVM IR、object file 或 executable。
- `layout facts`：由 HIR、type facts 和 target layout 按需查询得到，供 MIR、borrow/drop 和
  backend 使用。

`layout` 是 query 层事实表，不从 MIR body 生成。它消费 HIR、`TypeCheckResults`、
monomorph `MonomorphInstances` 和 target layout。MIR lowering、borrow check、drop elaboration
和 backend 都可以按需查询 layout；各阶段不能绕过 `LayoutStore` 自己推导 field offset、size
或 ABI 表达。

`--check` 当前仍会跑到 MIR、borrow check 和 drop elaboration，保证源码级语言契约不只停在
type check。

## 阶段边界

- `driver` 把进程参数转换成编译请求，直接创建 `CompilerContext` 并调用 pipeline。
- `pipeline` 以 package root file 为入口串联各阶段，并负责跨阶段错误处理。目录入口读取
  `package.ini`，文件入口把该文件作为 root source。
- `source` 负责 package manifest、路径处理、文件读取和 source ID。
- `syntax` 只产生 token 和 AST；详见 [AST 设计](compiler/ast.md)。
- `diagnostic` 负责诊断数据结构、终端输出和未来 LSP 位置转换。
- `source_map` 属于 `source` 模块，保存 `DefId` / `HirId` 到源码 span 的定位事实。
- `resolve` 负责 import、module graph、namespace 和名字解析，并直接生成 HIR；
  详见 [Resolve 设计](compiler/resolve.md)。
- `hir` 包含 resolved、未类型化的语义树；详见 [HIR 设计](compiler/hir.md)。
- `sema` 负责类型检查、trait、generic、overload 和类型转换；
  详见 [Type Check 设计](compiler/type-check.md)。
- `monomorph` 运行在 type check 之后，负责收集 concrete generic instances；
  详见 [Monomorph 设计](compiler/monomorph.md)。
- `mir` 包含 MIR 数据定义、HIR -> MIR lowering 和 drop elaboration；MIR lowering 可以查询
  layout 做布局相关的 representation 决策，但 layout 仍由 `layout` 模块统一计算；
  详见 [MIR 设计](compiler/mir.md)。
- `layout` 负责 concrete type layout 查询和缓存；详见 [Layout 设计](compiler/layout.md)。
- `borrow_check` 消费 MIR、`TypeCheckResults` 和 layout；详见
  [Borrow Check 设计](compiler/borrow-check.md)。
- `backend` 把 elaborated MIR 和 layout 转成 LLVM IR、object file 或可执行产物；
  详见 [Backend 设计](compiler/backend.md)。
- `incremental` 负责 hashing、cache key、依赖图和复用策略；详见
  [Incremental Compilation 设计](compiler/incremental.md)。
- `query` 是跨阶段查询入口和全局事实表聚合点；普通阶段通过 API 查询，不直接依赖
  其他阶段的内部表。
- `support` 只放可复用容器和工具，不 import 编译阶段模块。

## Query 与 Store

`QuerySystem` 是跨阶段事实表的生命周期所有者。长期事实表优先挂到 `QuerySystem`，
具体 record/key 类型仍由 owner 模块定义。

- `query/api.jiang` 定义 `QuerySystem`。
- `resolve/interner.jiang` 定义 `SymbolTable` 和 `KeywordTable`。
- `resolve/def.jiang` 定义 `DefKind`、`Visibility`、`NameDomain`、`DefRecord` 和 `DefTable`。
- `resolve/namespace.jiang` 定义持久 namespace、namespace binding 和 lookup key。
- `resolve/store.jiang` 组合 package/module、import/export 和 namespace table。
- `ResolveStore.modules` 是 `ModuleId -> ModuleRecord` 主表。
- `ResolveStore.source_modules` 是 `SourceId -> ModuleId` 的 side table。
- `HirStore`、`TypeCheckResults`、`LayoutStore` 和 `IncrementalSymbolIndex` 都挂在 `QuerySystem`。
- `MonomorphInstances`、`MirStore`、`ModuleGraph` 和 `BorrowCheckResults` 是单次 pipeline
  调用中的阶段产物。
- `AstStore` 是一次 `compile_package` 的临时 AST cache，不挂到 `QuerySystem`。
- 0.3 再引入 cache-backed query dependency tracking；0.2 不保留未接入的 cache 骨架。
- 后续需要缓存或依赖追踪的跨阶段问题，再在 `query/api.jiang` 增加高阶查询入口。

## 源码目录

```text
src/
  driver/       CLI 参数和命令入口
  source/       package、source file、source manager、source map
  syntax/       token、lexer、parser、flat AST
  diagnostic/   diagnostic、reporter
  resolve/      symbol table、keyword table、import/module/name resolver
  sema/         type、trait、generic、overload、type check、monomorph
  hir/          HIR 数据结构和 HIR store
  mir/          MIR 数据结构和 HIR -> MIR lowering
  layout/       concrete type layout 查询层
  borrow_check/ ownership、loan、lifetime 和 drop safety 检查
  backend/      target 和后端入口
  incremental/  cache key、fingerprint、依赖图、symbol index
  query/        query system、cache、id、key、result
  support/      arena、list、table、hash、unicode 等通用工具
```

## Support Table

编译器内部表结构统一沉淀在 `support`，阶段模块优先复用这些结构，而不是在
`resolve`、`sema`、`query` 中各自实现同类容器。

- `HashTable<K, V>`：复杂 key 到 value 的 hash 映射；key 必须满足 `Hashable`，
  而 `Hashable` 继承 `Equatable`；`get` 返回 `V?`，`get_ref` 返回 `V&?`。
- `ArrayTable<Id, T>`：append-only 实体主表；`next_id` 和 `append`
  通过 `Indexable.from_index` 返回强类型 Id。
- `SideTable<Id, T>`：已有 Id 到附加数据的直接索引表，适合半稠密 side data。
- `MultiTable<K, V>`：一个 key 对多个 value，适合同名符号链、依赖边和反向索引。
- `InternTable<K, V>`：去重驻留表，维护 `K -> index` 和 `index -> V`。

`Indexable` 负责强类型 Id 和连续下标之间的转换：

```jiang
public trait Indexable {
    Int to_index();
    static Self from_index(Int index);
}
```

## 代码规范

这部分记录编译器源码本身的命名和组织规则。语言用户侧的语法设计放在
[语言设计](language-design.md)。

### 命名

- 类型、trait、enum、union、struct 使用 `UpperCamelCase`。
- 函数、方法、局部变量、字段、模块文件名使用 `lower_snake_case`。
- 常量使用 `SCREAMING_SNAKE_CASE`。
- 缩写词按普通单词处理，只首字母大写：
  - 使用 `QuerySystem`，不要用 `QuerySYSTEM`。
  - 使用 `ModuleId`、`DefId`、`AstId`、`MirId`。
  - 使用 `LspServer`、`Utf8`、`Utf16`。
- 文件名使用 lower snake case：`source_manager.jiang`、`type_check.jiang`。
- 数据/模型文件优先使用名词：`token.jiang`、`type.jiang`。

### 后缀语义

- `*Id`：轻量句柄，通常实现 `Indexable`。
- `*Record`：实体表中的存储行。
- `*Entry`：lookup bucket、链表或临时索引中的条目。
- `*Key`：可 hash、可缓存或可持久化的查询键。
- `*Table`：具体数据表或通用容器。
- `*Index`：由多张表组成的查询索引。
- `*System`：跨阶段状态聚合和生命周期所有者。

### 代码风格

- 不为无效状态增加默认构造；确实可能缺失时使用 optional。
- 不用 `none`、`invalid`、`-1` 这类哨兵表达正常业务缺失。
- 注释使用中文，解释结构意图和不变量，不复述代码。
- 单行不超过 120 个字符。
- 新增抽象必须降低真实复杂度，不能只为了“看起来分层”。

## 导入纪律

优先保持单向 import。如果两个模块互相需要，应把共享数据结构抽到更底层的
所有者里，而不是引入循环依赖。

## 测试目录

- `test/smoke/`：当前 stage2 骨架的端到端 smoke，使用 stage1 编译器编译。
- `test/compiler/`：按编译阶段归档的测试目录，当前以 `.gitkeep` 保留结构。
- `test/compiler/fixture/`：编译器阶段测试的辅助输入。
- `test/lang/`：源码级语言语义用例，和 `test/smoke` 的内部模块 API 测试分开。
  目录按语言功能优先组织，每个功能目录内部再按测试结果类型分组；覆盖策略见
  [Language Testing 设计](compiler/lang-testing.md)。

`test/lang` 当前按语言功能组织，每个功能目录内部再按结果类型组织：

```text
test/lang/
  aggregate/
    check/
    fail/
  control_flow/
    check/
    fail/
  error_handling/
    check/
    fail/
  function/
    check/
    fail/
    run/
  generic/
    check/
    fail/
    run/
  import/
    check/
    fail/
  package/
    check/
    fail/
    run/
  lifetime/
    check/
  literal/
    check/
    fail/
    run/
  nominal/
    check/
    fail/
  ownership/
    check/
    fail/
    run/
  runtime/
    fail/
    run/
  type/
    check/
    fail/
  diagnostic/
```

- `check/`：期望 `jiangc --check` 成功。
- `fail/`：期望 `jiangc --check` 失败，可用 `// expected: diagnostic_code` 精确匹配诊断。
- `emit/`：期望 `jiangc --emit-llvm` 成功。
- `run/`：后续用于需要生成并运行目标程序的端到端用例。
- `diagnostic/`：后续用于精确检查多条 diagnostic、span 和消息的用例。

运行方式：

```bash
JIANGC=/path/to/jiangc ./script/lang_check.sh
```

`lang_check.sh` 递归扫描 `*/check/*.jiang`、`*/fail/*.jiang`、`*/emit/*.jiang` 和
`*/run/*.jiang`，每个用例都会打印通过/失败状态。
