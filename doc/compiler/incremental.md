# Incremental Compilation 设计

增量编译的目标是复用稳定语义事实和目标产物，不是复用一次编译过程里的内存对象。
`DefId`、`HirId`、`TypeId`、`MirFunctionId` 等 ID 都是 session-local handle，不能写入长期
cache，也不能作为跨次编译的身份。

## 阶段边界

一次 package 编译仍然从 root source 开始：

```text
root source
  -> module graph
  -> interface loading / decl graph
  -> resolve / HIR
  -> type_check
  -> monomorph
  -> MIR
  -> layout
  -> borrow_check
  -> backend
  -> link
```

增量层只负责在这些阶段之间记录稳定 key、semantic fingerprint 和 artifact path。各阶段的内部表
继续使用高效的 session-local ID。query dependency graph 等更细粒度的 invalidation 机制等
source artifact 边界稳定后再引入。

长期边界按职责拆分：

- `.ji` 是 semantic input/cache，用于渐进式构建 import graph、decl graph 和 type signature。
- `.o` 是 backend artifact，只表示某个 concrete codegen unit 的机器码可复用。
- HIR、type check 结果、layout 临时查询、单态化结果和 MIR 都是本次 compilation 的内存状态。

因此 `.o` 命中不能替代 `.ji`。即使某个 module 的 `.o` 可复用，其他 module 仍然需要它的
interface 做 name resolve 和 type check。

## 缓存产物

长期缓存分成三类：

```text
ImportSummary:
  用于 module graph discovery。

ModuleInterface:
  用于 decl / namespace / type signature graph。

ObjectArtifact:
  用于复用 concrete codegen output。
```

`ImportSummary` 和 `ModuleInterface` 在类型和 API 上必须分开，但物理上可以放在同一个 `.ji`
文件里。调用方通过不同 API 读取，不依赖磁盘布局：

```text
load_import_summary(source) -> ImportSummary
load_interface(source) -> ModuleInterface
load_generic_template(stable_id) -> GenericTemplate
```

`ModuleInterface` 只保存 public signature 和 template index；generic body payload 保存在 `.ji`
的独立 HIR template section。跨 package 泛型实例化不重新读取依赖源码，而是按 stable id 读取
generic HIR template。

`ModuleInterface.interface_hash` 是 public semantic surface 的 hash，不是 source hash：

- 使用 blake3 计算。
- 写入 symbol 文本和 stable id，不写 session-local `SymbolId` / `DefId` / `TypeId`。
- 写入 public declaration signature、public import、public alias 和 public generic template index。
- 不写 `SourceId`、`source_hash`、span/source offset。
- 函数 body 变化不应改变 interface hash，除非改变 public signature。

`.ji` 不保存：

- session-local ID。
- `TypeCheckStore`。
- `MonomorphStore`。
- concrete MIR。
- borrow check 临时状态。
- layout 临时状态。
- `.o` 的内部 codegen 细节。

`.o` 不保存 semantic interface。object cache key 只在 backend 层使用，需要包含所有影响
codegen 的稳定输入：

- codegen unit key。
- body fingerprint。
- type / layout fingerprint。
- monomorph instance key。
- target triple / ABI。
- compiler version。
- dependency interface hash。

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

## `.ji` 文件

`.ji` 是按 source/module 生成的 semantic artifact。它使用带 section table 的二进制容器，避免
module graph 阶段全量读取 interface。

推荐布局：

```text
JiHeader
  magic
  format_version
  compiler_version
  source_hash
  section_table_offset
  section_table_length

SectionTable
  ImportSummarySection: offset / length / hash
  InterfaceSection: offset / length / hash
  GenericTemplateSection: offset / length / hash
  SourceMapSection: offset / length / hash
```

读取策略：

```text
module graph:
  read header + section table + import summary

decl/type graph:
  read interface section

monomorph:
  read one generic template payload by stable owner id
```

编译阶段只依赖 `load_import_summary` / `load_interface` / `load_generic_template`，不直接依赖
section table 的物理布局。

`.ji` 文件不需要长期保持打开的文件指针。内存中只保存轻量索引：

```text
JiIndexEntry
  path
  file_fingerprint
  source_hash
  section_table
  loaded_import_summary?
  loaded_interface?
  loaded_generic_template_index?
```

读取 section 时临时 open -> read_at -> close。后续可以加小型 LRU file handle cache，但这只是
性能优化，不是架构依赖。

## Module Graph

`ModuleGraph` 本身是 session-local 内存对象，不作为核心长期 cache。长期稳定缓存单元是每个
source 的 `ImportSummary`。

构建流程：

```text
build_module_graph(root):
  queue = [root]
  while queue not empty:
    source = pop
    summary = load_import_summary(source)
    if summary miss:
      parse source
      collect imports
      write import summary
    add module node
    add import edges
    enqueue imported sources/packages
```

## Store 复用边界

这些表按 session 重建：

- `ModuleGraph`
- `ResolveStore`
- `HirStore`
- `TypeCheckStore`
- `LayoutStore`
- `MirStore`

这些表可以有对应的 stable key 或 artifact key，但长期层不能保存这些表的 session-local 内容。

`CompilerContext.begin_compilation` 是当前实现里的轮次边界。它保留：

- `SourceStore`
- `SymbolStore`
- `SourceArtifactCache`
- `ObjectArtifactCache`
- `IncrementalSymbolStore`
- `QueryDependencyGraph`

同时重建：

- `SourceMap`
- `DefStore`
- `TypeStore`
- `ResolveStore`
- `HirStore`
- `TypeCheckStore`
- `LayoutStore`

因此同一个 context 可以进入下一轮 package 编译，但旧的 `DefId`、`HirId`、`TypeId`
不能继续使用。

例如：

- `ResolveStore` 可以重建当前 `DefId -> DefRecord`，长期层保存 stable id 与当前 def 的对齐结果。
- `HirStore` 可以重建当前 HIR，长期层保存 signature/body fingerprint。
- `TypeCheckStore` 是 HIR -> MIR side table，不作为长期 cache artifact。
- `LayoutStore` 按 session 重建；后续如果要缓存 layout fact，key 不能包含 session-local `TypeId`。
- `MirStore` 按 session 重建；长期层只保存 object artifact key 和 `.o` 路径。

## Interface

interface 是 semantic boundary，不是完整编译产物。它用于把 package/module 的声明事实加载进
当前 compilation 的内存表。

interface 保存：

- exported / package-visible decl。
- namespace export、public import、public alias / reexport。
- function signature。
- 参数默认值表达式、where/lifetime 约束的 template/body 编码。
- nominal type 的字段、variant、associated type、method signature。
- trait / impl 的签名关系。
- 影响下游 resolve/type check 的 fingerprint。

interface 不保存单态化结果，也不直接内联完整 body payload。单态化结果只存在于当前
compilation 的内存 `MonomorphStore` 中。public generic body 以 HIR template payload 的形式
保存在 `.ji`，不保存成 concrete MIR。

跨 package 调用 public 泛型时，下游需要能恢复上游泛型 body：

```text
package util:
  public T id<T>(T value) { value }

package app:
  Int x = util.id<Int>(1)
```

如果 Jiang 不使用 runtime metadata 泛型，`app` 必须拿到 `id<T>` 的 body，生成 `id<Int>` 的
concrete MIR 和 `.o`。这个 body 来自 `.ji` 的 generic HIR template section，不依赖上游源码。

## Object Cache

object cache 是 backend/codegen cache，不是 semantic cache。

object cache lookup 分两步：

```text
lookup:
  只查 artifact index，返回 missing/hit。

validate:
  调用方读取 object 文件并计算 actual object hash。
  ObjectArtifactCache 只比较记录 hash 和 actual hash，返回 hit/stale/missing。
```

cache 层不直接读写文件，也不调用 LLVM emission。文件系统和 codegen unit 调度属于
driver/backend。

object path 由 backend artifact path planner 生成：

```text
cache_root/objects/source_<hash>.o
cache_root/objects/mono_<stable_instance_fingerprint>.o
cache_root/objects/release_pkg_<hash>.o
```

planner 是纯函数，不创建目录、不写文件。pipeline 负责在 object emit 前创建 cache 目录，
并在 cache lookup / validate 时使用 `artifact/object_hash` 计算实际 object hash。

`CompileOptions.artifact_cache_dir` 是当前编译的 cache root，默认值为 `build/cache`。
pipeline 后续只从这里取得 cache root，不在各阶段硬编码路径。

推荐分成两类：

```text
source object:
  普通 concrete 函数、global、hosted entry wrapper。

monomorph object:
  泛型函数实例。
  泛型 type 的 method / init / deinit 实例。
  泛型 trait impl method 实例。
```

`CodegenUnit` 是 backend-independent 的 MIR 分组，不是 LLVM module：

```text
source unit:
  同一 source module 的普通 concrete functions。

monomorph unit:
  一个泛型 concrete instance。
```

external declaration 不拥有 object unit。每个 backend emission 可以按需要在当前 object 内
materialize 外部声明。

`emit_object_for_unit` 仍然可以先声明完整 `MirStore` 里的函数，再只 lower 当前 unit 的 body。
这样跨 unit 调用只需要 LLVM declaration，不会把被调用方 body 一起写进当前 `.o`。
`CodegenUnitKey` 只做本轮 MIR 分组；长期 object path 必须来自 source object key 或
stable monomorph instance key。
`artifact/object_key_builder.jiang` 负责把当前 session 的 module/type 信息转换成稳定 object key
输入；pipeline 不应直接读取 `ResolveStore` / `SourceStore` 拼 cache key。
target/compiler fingerprint 由调用方传入真实 target triple 和 compiler version，builder 只做
domain-separated hash，不提供临时默认值。
backend profile fingerprint 同样由 compile mode 派生，当前编码 mode、LLVM codegen opt level 和
LLVM pass pipeline。任何会改变 object 语义的 backend 选项都必须进入这个 profile。
interface/body/layout 等多段 fingerprint 通过 `artifact/object_key.jiang` 的组合 helper 聚合，
组合顺序必须稳定，不能使用 session-local id 排序。

泛型定义本身不直接生成 `.o`。只有 concrete instance 生成 object：

```text
StableInstanceKey
  generic_owner_stable_id
  type_args
  comptime_args
  body_hash
  layout_hash
  target / ABI
  compiler_version
  backend_profile
```

当前 session 内的 `MonomorphStore` 仍然使用 `DefId + TypeId[]` 做内存去重。这个 key
不能写入本地 cache。`StableInstanceKey` 只用于 object cache，type args 必须先转换成不含
session-local `TypeId` 的 stable type key。当前转换入口是 `artifact/object_key_builder.jiang`。

同一个 compilation 内，`StableInstanceKey` 相同就复用同一个 concrete MIR / object cache entry。
不同 owner 即使文本相同，也不做结构性去重。

## 当前实现边界

当前 0.4.1 实现已经具备这些内存和 artifact 结构：

- `SourceArtifactCache`：保存 `ImportSummary`、`ModuleInterface` 和 `GenericTemplate`。
- `JiFileImage`：提供 `.ji` header / section table / read_at 的最小 API。
- `ModuleGraphBuilder`：从 `ImportSummary` 或 AST 构建 session-local `ModuleGraph`。
- `InterfaceLoader`：从 `ModuleInterface` 恢复 `DefRecord`、namespace binding 和 HIR signature skeleton。
- `StableInstanceKey`：描述 monomorph object 的稳定输入。
- `ConcreteInstanceRegistry`：在同一 compilation 内按 stable instance 复用 concrete MIR body 位置。
- `ObjectArtifactCache`：区分 source object 和 monomorph object，不保存 semantic interface。
- `QueryDependencyGraph`：记录 query dependency / reverse dependency，并提供 transitive invalidation。
- `CompilerSession`：持有可复用 `CompilerContext`，通过 `begin_compilation` 进入下一轮编译。

这些结构中，source/interface artifact 和 object artifact 仍主要以内存 index 描述长期身份；
pipeline 已接入 object cache 目录创建、object lookup/validate、缺失 object emission 和命中
object 复制。长驻服务、磁盘 artifact index 持久化和更细粒度 invalidation 仍在后续阶段。

## Invalidation

source change 后先重新 parse 当前 source，并重建可达 import/module facts。resolve/HIR 阶段用
stable key 对齐新旧 def：

- 新 stable id：新增 def。
- 旧 stable id 不再出现：deleted def。
- stable id 存在但 signature fingerprint 改变：依赖 signature 的 query 失效。
- signature 不变但 body fingerprint 改变：只失效依赖 body 的 query。

query dependency graph 需要维护 reverse edge。删除 def 时，所有依赖该 stable id 的 query 必须
失效并重新诊断 unresolved/reference error。

缓存命中边界：

```text
ImportSummary 命中:
  可以跳过 parse imports。

Interface 命中:
  可以跳过 collect decls/signatures。
  root source 已有 AST 时仍继续编译 body。
  非 root module 如果只是 import discovery 临时 parse 过，仍可加载 interface skeleton。

ObjectArtifact 命中:
  可以跳过 HIR/type_check/MIR/codegen，但不能跳过 interface loading。
```

只改 private function body 时，通常：

```text
ImportSummary 不变
Interface 不变
source object 失效
下游 package 不失效
```

改 public signature 时：

```text
Interface 改变
依赖该 signature 的下游 query 失效
相关 object 失效
```

新增泛型实例时：

```text
Interface 不变
新增 InstanceKey
新增 monomorph object
```

## 不变量

- 长期 cache 不保存裸 pointer。
- 长期 cache 不保存 session-local ID。
- `StableSymbolId` 和长期 fingerprint 写入 symbol 文本；`SymbolId.to_index()` 只允许用于本轮内存表。
- fingerprint 输入不能包含 span/source offset 这类非语义位置数据。
- source map 可以随着 revision 更新；诊断位置不能作为语义 fingerprint 的一部分。
- 增量 query stack 和 lazy sema query guard 需要分层，避免 cycle 状态混用。
