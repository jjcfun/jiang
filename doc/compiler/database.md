# Database：查询驱动的同步求值层

Database 是批处理编译器的同步求值机制，不是新的事实存储，也不假设编译器常驻。
调用方用 typed key 请求编译事实；对应 query wrapper 管理本轮 memo、重入语义和实际计算。

设计目标是：

- 同一事实只有一个 owner；
- 查询只缓存 Copyable ID、fingerprint 或小值；
- 每种查询保留自己的 key/value 类型和环语义；
- 不为没有真实复用价值的线性阶段包装 query。

0.5.2 不在 Database 中引入 task、job、single-flight 或并行调度。并行仍由 pipeline 外层负责。

## 1. 所有权边界

```text
CompilerContext
  store: CompilerStore
    AST 之后的跨阶段事实的唯一 owner
    DefStore / TypeStore / Semantic Model / TypeCheckStore / LayoutStore / ...

  query: QueryEngine
    各种 typed query wrapper
      wrapper 内部持有自己的 QueryCache<K, V>
    JIL Store identity allocator

阶段 local owner
  AstUnit / MonomorphStore / JIL Store / ObjectUnit / ...

持久层
  .ji / .o / .mono.o / .jbuild
```

`CompilerStore` 拥有跨阶段事实。query cache 不复制这些事实：例如 layout query 缓存
`LayoutId`，真正的 `Layout` 仍由 `LayoutStore` 拥有；JIL body query 缓存 `FunctionId`，
function/body 仍由当前 JIL Store 拥有。

`MonomorphStore^`、`jil.Store^` 和 `BorrowCheckStore^` 等 Movable 结果直接沿 pipeline 传递，
不通过 ID owner、callback 或第二张 cache 间接持有。

## 2. QueryCache

`QueryCache<K, V>` 是不知道业务的同步 memo 原语。`K` 和 `V` 都是 `Copyable`。

```text
entries: WyHashMap<K, V?>
```

一张表表示三种状态：

| entries | `QueryCacheState<V>` | 含义 |
| --- | --- | --- |
| 没有 key | `missing` | `state` 写入 `key -> null`，当前调用方负责计算 |
| `key -> null` | `evaluating` | 该 key 正在求值，由 wrapper 解释重入 |
| `key -> value` | `ready(value)` | 结果已发布 |

正常状态转换是：

```text
missing -> evaluating -> complete(value) -> ready
missing/ready -> publish(value) -> ready
```

`publish` 用于恢复已有 owner 事实的句柄或 fingerprint。普通递归查询使用
`state + complete`。

每个 wrapper 使用自己的 `Mutex<QueryCache<...>>`。锁只保护一次 cache 操作；compute 和
artifact I/O 必须在锁外执行。

## 3. Typed query wrapper

`QueryCache` 不知道业务。每种查询由一个轻量 wrapper 定义：

- typed key 和 typed value；
- L1 miss 后如何 compute 或 load；
- `evaluating` 是环、可恢复重入，还是应返回空结果；
- 结果何时发布；
- epoch reset。

```text
LayoutQuery
  QueryCache<Int, LayoutId>

ModuleInterfaceQuery
  QueryCache<StableSourceId, SourceId>

StableFingerprintQuery
  QueryCache<StableSymbolId, Fingerprint128>

TypeCheckDefQuery
  QueryCache<DefId, TypeId>
```

wrapper 不建立自己的 active set、完成值表或第二套状态机。

不使用统一 `QueryStack`。不同查询的 key 类型、环语义和错误恢复不同；全局栈会引入
第二套 active provenance，并迫使所有 key 转换为统一 enum。

## 4. QueryEngine

`QueryEngine` 是一次 compilation epoch 的求值容器。它只聚合：

- 各种 typed query wrapper；
- 本轮 declaration observation recorder；
- JIL Store identity allocator 等明确的 session 机制。

`QueryEngine.reset()` 清空每个 wrapper 的 L1 cache，并重置 session identity。进程结束后，
所有 L1 状态都消失。

`QueryEngine` 不拥有：

- AST、Semantic Model、TypeFacts 或 JIL；
- 线性 pipeline 阶段的完成标记；
- 稳定依赖图或没有消费者的失效候选集；
- 并行 job 和等待队列。

## 5. 查询类型

### 5.1 只有 L1 的查询

layout、type check、trait、lifetime shape、const definition 和 JIL body 等查询只在当前 epoch memo。
它们使用 `TypeId`、`DefId`、`NodeId` 或业务 key，不制造额外 stable key。

### 5.2 有真实 L2 value 的查询

source import summary 和 module interface 的 L2 value 由 `.ji` 与 `SourceArtifactCache` 拥有。
wrapper 只用 `StableSourceId` 缓存 owner 中的 `SourceId`。

declaration signature/body fingerprint 本身是 `.ji` 中的稳定值。加载 interface 时用
`StableSymbolId` 把 fingerprint 恢复到对应 L1 cache。

L2 I/O 不放进 `QueryCache`。wrapper 接收窄 callback，在 L1 miss 后由调用方加载 L2 或执行 L3。

### 5.3 不是 query 的阶段

borrow check、drop elaboration 和 generic instance collection 在批处理 pipeline 中各执行一次。
它们的 Movable owner 直接传给下一阶段，不包装为 query。

## 6. Declaration observations

外部 declaration 的真实读取由 type checker 记录到 importer source 的本轮 observation 集合。
记录包含 dependency source、stable declaration、读取 aspect 和 fingerprint，不保存 session-local ID。

除 declaration signature/body 外，精确名字 lookup 记录 `namespace_name`，flatten import 与 extension
枚举记录完整 `namespace` surface；空集合也必须记录，才能发现后续新增第一个可见声明或 extension。

recorder 属于 `QueryEngine` 的求值机制；完成集合由 `SourceArtifactCache` 拥有并写入 importer `.ji`。
只有前端或完整编译成功后才整组发布，失败分析不会覆盖上次成功集合。

不建立通用 `QueryDependencyGraph` 或统一 query key。source import closure 仍由
`SourceDependencyGraph` 负责；后续 importer 失效裁决直接验证 `.ji` observations。

## 7. 批处理流程

```text
source
  -> parse 为 owned AstUnit
  -> resolve / Semantic Model lowering
  -> 事实发布到对应 owner store
  -> AstUnit 释放
  -> typed queries 按需取得 type/layout/JIL 等事实
  -> 写出 .ji / object / .jbuild
  -> 进程退出，L1 cache 消失
```

AST 不进入 Database。`SourceId -> ModuleId` 是 `ResolveStore` 的索引，module progress 由
`ModuleResolver` 拥有。两者都不复制到 `QueryEngine`。

## 8. 新增 query 的规则

1. 明确事实的唯一 owner；
2. 使用业务本身的 typed `K` 和 Copyable `V`；
3. 只有存在重复读取、递归求值或真实 L2 复用时才新建 wrapper；
4. wrapper 内只有一个 `QueryCache<K, V>` 状态源；
5. 明确定义 missing/evaluating/ready 的业务语义；
6. compute 和 I/O 不跨 cache lock；
7. Movable owner 不塞进 `QueryCache`，不引入双 provenance；
8. 没有真实 L2 value 时，不预留 stable key、observation 或失效图。

## 9. 源码对应

| 位置 | 职责 |
| --- | --- |
| `src/db.jiang` | Database 对外模块入口 |
| `src/db/query_engine.jiang` | `QueryEngine` 与窄 facade |
| `src/db/query_cache.jiang` | 通用同步 memo 原语 |
| `src/db/*_query.jiang` 及其他 typed query 文件 | wrapper 与业务求值语义 |
