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
- `resolve` 负责 symbol interning、keyword table、import、module graph、namespace 和
  declaration 的名字解析。
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
- 数据/模型文件优先使用名词：`token.jiang`、`type.jiang`、`artifact.jiang`。

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
- 编译阶段通过 `QuerySystem` 访问所属 store/table；不要绕过 `QuerySystem` 自行创建全局事实表。
- 后续需要缓存或依赖追踪的跨阶段问题，再在 `query/api.jiang` 增加高阶查询入口。
- `query/id.jiang` 放跨阶段共享 Id。
- `query/key.jiang` 放稳定 key、query key 和依赖 key。
- 阶段私有的局部 index 不放入 `query/id.jiang`；只有能跨阶段、跨缓存或被外部工具引用的
  Id 才进入 query 层。

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
