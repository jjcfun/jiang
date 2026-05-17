# Jiang Next 架构

Jiang Next 编译器应围绕稳定的阶段边界组织。

## 顶层流程

```text
driver -> api -> pipeline
                  |
                  v
source -> syntax -> resolve -> sema -> ir -> backend
                           \          /
                            v        v
                              query
```

## 边界

- `driver` 把进程参数转换成编译请求。
- `api` 向 CLI、测试和未来工具暴露编译请求/结果。
- `pipeline` 串联各阶段，并负责跨阶段错误处理。
- `source` 负责 package manifest、路径处理、文件读取和 source ID。
- `syntax` 只产生 token 和 AST；它不应理解类型语义。
- `diagnostic` 负责诊断数据结构、source map、终端输出和未来 LSP 位置转换。
- `resolve` 负责 symbol interning、keyword table、import、module、namespace 和
  declaration/reference 的名字解析。
- `sema` 负责类型检查、overload resolution、trait、generic 和类型转换。
- `ir` 包含 HIR/MIR 数据定义，以及从上一阶段 lower 到目标 IR 的逻辑。
  `ir/common.jiang` 放共享 IR 结构，`ir/hir.jiang` 放 HIR，`ir/mir.jiang` 放 MIR；
  `ir/ast_lower/` 放 AST 到 HIR 的 lowering，`ir/hir_lower/` 放 HIR 到 MIR 的 lowering。
- `backend` 把 MIR 转成产物。当前仅保留 target 骨架；后续如果引入 LLVM 后端，
  LLVM 细节放在 `backend/llvm`。
- `incremental` 负责 hashing、cache key、依赖图和复用策略。
- `query` 是跨阶段查询入口和全局事实表聚合点；普通阶段通过 API 查询，不直接依赖
  其他阶段的内部表。
- `support` 只放可复用容器和工具，不 import 编译阶段模块。

当前源码目录约定：

```text
src/
  driver/       CLI 参数和命令入口
  source/       package、source file、source manager
  syntax/       token、lexer、parser、flat AST
  diagnostic/   diagnostic、source map、reporter
  resolve/      symbol table、keyword table、import/module/name resolver
  sema/         type、trait、generic、overload、type check
  ir/           common、HIR、MIR、AST lower、HIR lower
  backend/      target 和后端入口
  incremental/  cache key、fingerprint、依赖图、symbol index
  query/        query system、cache、id、key、result
  support/      arena、list、table、hash、unicode 等通用工具
```

## Syntax AST

AST 使用 flat table 结构：

```text
AstFile.nodes    -> ArrayList<AstNode>
AstFile.children -> ArrayList<AstId>
AstId            -> 单个 AstFile 内部的有效 node index
```

`AstNode` 是固定大小节点，只包含 `span` 和 `AstData`。`AstData` 是 tagged union，
它本身就是节点种类，不再额外维护 `AstKind`。变长子节点通过 `AstRange` 指向
`children` 中的一段连续 `AstId`。例如函数参数、调用实参、结构体字段、block
statement 都使用各自语义字段名保存 `AstRange`，但底层共用 `children`。

`AstId` 本身不表示缺失值；所有 `AstId` 都必须指向有效节点，下标 `0` 合法。
可缺省字段使用 `AstId?`，不能用 `-1` 之类的哨兵值。`children` 中也不能加入
缺失节点。

`AstFile.source` 记录 AST 来源；它只能是普通 `SourceFileId` 或 virtual source。
测试、宏展开、REPL 片段使用 virtual source，不引入 none 状态。`AstId` 不属于
全局 query id。跨文件或跨阶段引用 AST 时应显式携带 `AstSource`/`SourceFileId`
和 `AstId`，不能只传裸 `AstId`。

## Support Table

编译器内部表结构统一沉淀在 `support`，阶段模块优先复用这些结构，而不是在
`resolve`、`sema`、`ir` 或 `query` 中各自实现同类容器。

- `HashTable<K, V>`：复杂 key 到 value 的 hash 映射；key 必须满足
  `Hashable`，而 `Hashable` 继承 `Equatable`；`get` 返回 `V?`，
  `get_ref` 返回 `V&?`。
- `ArrayTable<Id, T>`：append-only 实体主表；`next_id` 和 `append`
  通过 `Indexable.from_index` 返回强类型 Id。
- `SideTable<Id, T>`：已有 Id 到附加数据的直接索引表，适合半稠密 side data。
- `MultiTable<K, V>`：一个 key 对多个 value，适合同名符号链、依赖边和反向索引。
- `InternTable<K, V>`：去重驻留表，维护 `K -> index` 和 `index -> V`，
  `intern` 返回稳定 index，`from_index` 可通过 index 反查驻留值；调用方也可以
  把 index 包装成具体 Id。

`Indexable` 负责强类型 Id 和连续下标之间的转换：

```jiang
public trait Indexable {
    Int to_index();
    static Self from_index(Int index);
}
```

## 代码规范

这部分记录编译器源码本身的命名和组织规则。语言用户侧的语法设计放在
`language-design.md`。

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

### Resolve 与 Query

- `resolve/interner.jiang` 定义 `SymbolTable` 和 `KeywordTable`。`SymbolTable`
  用 `InternTable<SymbolText, SymbolId>` 维护源码文本驻留；`KeywordTable`
  用 `HashTable<SymbolId, Keyword>` 做关键字反查。
- `query/api.jiang` 的 `QuerySystem` 持有 `QueryCache`、`SymbolTable`、`KeywordTable`、
  `DefTable`、`TypeTable`、`ResolveStore` 等全局生命周期对象。长期事实表优先挂到
  `QuerySystem`，具体 record/key 类型仍由 owner 模块定义。
- `resolve/def.jiang` 定义 `DefKind`、`Visibility`、`NameDomain`、`DefRecord` 和
  `DefTable`。
- `resolve/namespace.jiang` 定义持久 namespace、namespace binding 和 lookup key。
- `resolve/store.jiang` 组合 package/module、import/export 和 namespace table 等名字解析组织结构。
- `ResolveStore.modules` 是 `ModuleId -> ModuleRecord` 主表。
- `ResolveStore.source_modules` 是 `SourceId -> ModuleId` 的 side table，只用于从 source
  快速找到当前 module；module 内容仍以 `modules` 为准。
- 编译阶段通过 `QuerySystem` 访问所属 store/table；不要绕过 `QuerySystem` 自行创建全局事实表。
- 后续需要缓存或依赖追踪的跨阶段问题，再在 `query/api.jiang` 增加高阶查询入口。
- `query/id.jiang` 放跨阶段共享 Id。
- `query/key.jiang` 放稳定 key、query key 和依赖 key。
- 阶段私有的局部 index 不放入 `query/id.jiang`；只有能跨阶段、跨缓存或被外部工具引用的
  Id 才进入 query 层。

### 当前 Resolve 流程

`resolve/module_resolver.jiang` 是当前 resolve 入口。外部创建 `ModuleResolver(ctx)`，调用
`resolve_file` 后拿到当前文件的 resolve 阶段产物：

```jiang
ModuleResolver! resolver = ModuleResolver(ctx)
ResolvedFile^ resolved = resolver.resolve_file(ast_file)
```

流程分为 module 级驱动和单文件名字解析两层：

```text
resolve_file(ctx, ast_file)
  -> parse_source has registered AstFile in QuerySystem.asts
  -> ensure_module(ast_file.source_id)
  -> create caller-owned ResolvedFile
  -> collect current module imports if needed
  -> mark current module as collecting declarations
  -> resolve import targets and collect registered target declarations
  -> collect current module declarations
  -> NameResolver.resolve_references()
  -> return ResolvedFile^
```

`ensure_module(source_id)` 保证一个 source 有稳定的 `ModuleId`：

- 如果 `source_modules` 中没有这个 `SourceId`，创建 package/module/namespace/def，并写入
  `modules` 和 `source_modules`。
- 如果已有 module 且 `source_revision` 未变化，直接复用原 `ModuleId`。
- 如果 source revision 变化，保持 `ModuleId` 不变，调用 `reset_module` 清空该 module 的
  imports、exports、private_defs，并创建新的 module namespace。旧 namespace 和旧 def
  暂时留在全局表中，但不再通过当前 module 可达；后续如需长期增量会再引入 GC 或
  版本化策略。

`ModuleRecord.resolve_state` 记录 module 级 pass 进度：

- `unresolved`：module shell 已存在，但 import/declaration 还未收集。
- `collecting_imports` / `imports_collected`：正在或已经完成 import 收集。
- `collecting_declarations` / `declarations_collected`：正在或已经完成 top-level declaration 收集。

这些状态用于避免重复收集同一个 module，也用于 import cycle：如果 A 和 B 互相 import，
A 进入 `collecting_declarations` 后再从 B 回到 A，会直接停止递归，等 A 当前 pass 自己完成。

`NameResolver` 是单个 AST file/module 的 resolver。它不负责创建 module，也不负责跨文件
加载；初始化时只拿当前 `module_id` 和 `namespace_id`：

```text
NameResolver {
  query
  file
  module_id
  namespace_id
  resolved_file&
  lexical env
}
```

当前 `NameResolver` 的 pass：

- `collect_imports`：扫描 top-level import，向 `ModuleRecord.imports` 写入 `ImportRecord`。
- `collect_declarations`：扫描 top-level declaration，创建 `DefId`，绑定到当前 module namespace，
  并按 visibility 记录到 exports 或 private_defs。
- `resolve_references`：遍历当前 file 的 declaration body，解析基础 type reference、
  expression name、local binding 和 import alias path，并把结果写入调用方持有的
  `ResolvedFile`。

`resolve_import_targets` 在 imports 收集后运行。当前规则很窄：

- 对普通 `import dep`，取 import path symbol 的文本，构造 virtual `SourceKey` 查 `SourceStore`。
- 找到 source 后调用 `ensure_module(source_id)`。如果目标 source 的 `AstFile` 已经登记到
  `QuerySystem.asts`，会递归推进目标 module 的 import/declaration pass。
- 解析成功后创建 `import_alias_def`，并把 alias 作为 `.namespace_name` 绑定到当前 module namespace。
- 对 string import，当前取字符串字面量的 symbol 文本，构造 file `SourceKey` 查 `SourceStore`。

当前还未完成的部分：

- import path 到 source/package 的正式解析规则。
- import target 的自动加载还没有接正式 source loader；目前只调度 `QuerySystem.asts`
  中已登记的 `AstFile`。
- qualified path 的逐段 lookup。
- declaration/member namespace。
- duplicate definition、unresolved name、import not found 等诊断细节。
- reset 后旧 namespace/def 的回收或版本化。

### 代码风格

- 不为无效状态增加默认构造；确实可能缺失时使用 optional。
- 不用 `none`、`invalid`、`-1` 这类哨兵表达正常业务缺失。
- 注释解释结构意图和不变量，不复述代码。
- 新增抽象必须降低真实复杂度，不能只为了“看起来分层”。

## 导入纪律

优先保持单向 import。如果两个模块互相需要，应把共享数据结构抽到更底层的
所有者里，而不是引入循环依赖。

## 测试目录

- `test/smoke/`：当前 stage2 骨架的端到端 smoke，使用 stage1 编译器编译。
- `test/compiler/`：按编译阶段归档的测试目录，当前以 `.gitkeep` 保留结构。
- `test/compiler/fixture/`：编译器阶段测试的辅助输入。
- `test/lang/`：语言语义覆盖测试，按 `run/`、`check/`、`fail/`、`diagnostic/` 分组。
  后续 syntax、resolve、sema、query、backend、incremental 的专项测试放到对应目录。
