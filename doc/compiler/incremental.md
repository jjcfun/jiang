# Incremental Compilation 设计

增量编译的目标是复用稳定语义事实和目标产物，不是复用一次编译过程里的内存对象。
`DefId`、`HirId`、`TypeId`、`MirFunctionId` 等 ID 都是 session-local handle，不能写入长期
cache，也不能作为跨次编译的身份。

## 阶段边界

第一版只做同进程增量。磁盘 cache、跨进程 artifact reuse 和 cache versioning 放到后续阶段。

一次 package 编译仍然从 root source 开始：

```text
source -> AST snapshot -> resolve/HIR -> type_check -> monomorph -> MIR -> layout -> borrow_check -> backend
```

增量层只负责在这些阶段之间记录稳定 key、fingerprint 和 query dependency。各阶段的内部表
继续使用高效的 session-local ID。

## AST

AST 是某个 `SourceId + revision` 的临时语法快照。它可以在一次 package 编译中作为 parse
cache 存在，但不进入长期 cache。

源文件变化时，当前 source 的 AST 重新 parse。后续阶段通过 stable symbol key 和 fingerprint
判断哪些语义事实可以复用，而不是试图复用旧 AST node。

## Stable Key

稳定身份必须来自源码语义路径，而不是数组下标。

`StableSymbolKey` 至少包含：

- package identity。
- module path 或 source path。
- owner stable id。
- name。
- name domain。
- symbol kind。

local/pattern binding 默认不跨 session 缓存。需要 body 级复用时，可以在 body fingerprint 内使用
def-local ordinal 或语法结构 hash，但它们不升级成 package 级 stable symbol。

## Store 复用边界

这些表按 session 重建：

- `ModuleGraph`
- `ResolveStore`
- `HirStore`
- `TypeCheckResults`
- `LayoutStore`
- `MirStore`

这些表可以有对应的长期索引或 cache entry，但长期层只能保存 stable key、fingerprint、
query dependency 和可验证的 artifact 路径。

例如：

- `ResolveStore` 可以重建当前 `DefId -> DefRecord`，长期层保存 stable id 与当前 def 的对齐结果。
- `HirStore` 可以重建当前 HIR，长期层保存 signature/body fingerprint。
- `TypeCheckResults` 可以重算局部 side table，长期层保存 query result fingerprint。
- `LayoutStore` 可以按 concrete layout key 复用 layout fact，但 key 不能包含 session-local `TypeId`。
- `MirStore` 可以重建 concrete MIR，长期层保存 concrete function key 和 MIR fingerprint。

## Invalidation

source change 后先重新 parse 当前 source，并重建可达 import/module facts。resolve/HIR 阶段用
stable key 对齐新旧 def：

- 新 stable id：新增 def。
- 旧 stable id 不再出现：deleted def。
- stable id 存在但 signature fingerprint 改变：依赖 signature 的 query 失效。
- signature 不变但 body fingerprint 改变：只失效依赖 body 的 query。

query dependency graph 需要维护 reverse edge。删除 def 时，所有依赖该 stable id 的 query 必须
失效并重新诊断 unresolved/reference error。

## 不变量

- 长期 cache 不保存裸 pointer。
- 长期 cache 不保存 session-local ID。
- fingerprint 输入不能包含 span/source offset 这类非语义位置数据。
- source map 可以随着 revision 更新；诊断位置不能作为语义 fingerprint 的一部分。
- 增量 query stack 和 lazy sema query guard 需要分层，避免 cycle 状态混用。
