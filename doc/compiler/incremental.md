# Incremental Compilation 设计

增量编译的目标是复用稳定语义事实和目标产物，不是复用一次编译过程里的内存对象。
`DefId`、`sem.NodeId`、`TypeId`、`jil.FunctionId` 等 ID 都是 session-local handle，不能写入长期
cache，也不能作为跨次编译的身份。

## 阶段边界

一次 package 编译仍然从 root source 开始：

```text
root source
  -> module graph
  -> interface loading / decl graph
  -> resolve / Semantic Model
  -> type_check
  -> monomorph
  -> JIL
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
- Semantic Model、type check 结果、layout 临时查询、单态化结果和 JIL 都是本次 compilation 的内存状态。

因此 `.o` 命中不能替代 `.ji`。即使某个 module 的 `.o` 可复用，其他 module 仍然需要它的
interface 做 name resolve 和 type check。

## 缓存产物

长期缓存只跨进程保存三类内容：

```text
.ji
  ImportSummary、ModuleInterface、GenericTemplate 和 object closure。

object
  source unit、monomorph unit、整包单文件 object 和 lang provider dylib。

artifact cache index
  stable unit key 到本机相对 object path、校验 hash 和构建上下文的映射。
```

`ImportSummary` 和 `ModuleInterface` 在类型和 API 上必须分开，但物理上可以放在同一个 `.ji`
文件里。调用方通过不同 API 读取，不依赖磁盘布局：

```text
load_import_summary(source) -> ImportSummary
load_interface(source) -> ModuleInterface
load_generic_template(stable_id) -> GenericTemplate
```

`ModuleInterface` 保存跨 package type check 所需的声明面、generic template index 和 object
closure。generic body payload 保存在 `.ji` 的独立 Semantic Model template section。跨 package
泛型实例化不重新读取依赖源码，而是按 stable id 读取 generic Semantic Model template。

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
- concrete JIL。
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
- compiler build/schema fingerprint 与 language version。
- LLVM/toolchain 与 backend profile。
- dependency interface hash。

整包单文件 object 的身份由稳定排序的 unit key/hash closure 决定，和旧的 source 文本整包 hash
无关。`type = lang` provider dylib 仍使用 package artifact key，因为 provider discovery
发生在普通 module graph 之前；它的 key 同时包含 provider source、wrapper ABI、compiler、
target 和 profile。

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
- 可选的稳定 disambiguator。

函数重载共享基础路径，但不共享完整 stable key。Semantic Model signature lowering 会从 callable signature
生成 disambiguator；`init` / `deinit` 的隐式 receiver 不计入该值。函数的参数、返回类型和
generic signature 改变会形成新的 stable identity，函数 body 变化则只改变 body fingerprint。
`@life` 等不改变重载身份的 contract 信息进入 semantic fingerprint，而不伪装成函数名或
overload signature。

函数 alias 自身仍有独立 `DefId` 和 stable identity。它保存一个可见 overload anchor；调用解析
通过 anchor 回到目标的原始 namespace/name，枚举 public overload set。artifact 恢复 alias 时
也必须在目标 declarations 可用后重建该关系，不能把 alias 压缩成一份重载签名副本。

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
  ObjectClosureSection: offset / length / hash
```

读取策略：

```text
module graph:
  read header + section table + import summary

decl/type graph:
  read interface section

monomorph:
  read one generic template payload by stable owner id

codegen/link:
  validate object closure through the local artifact index
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
- `sem_store.Store`
- `TypeCheckStore`
- `LayoutStore`
- `jil.Store`

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
- `sem_store.Store`
- `TypeCheckStore`
- `LayoutStore`

因此同一个 context 可以进入下一轮 package 编译，但旧的 `DefId`、`sem.NodeId`、`TypeId`
不能继续使用。

例如：

- `ResolveStore` 可以重建当前 `DefId -> DefRecord`，长期层保存 stable id 与当前 def 的对齐结果。
- `sem_store.Store` 可以重建当前 Semantic Model，长期层保存 signature/body fingerprint。
- `TypeCheckStore` 是 Semantic Model -> JIL side table，不作为长期 cache artifact。
- `LayoutStore` 按 session 重建；后续如果要缓存 layout fact，key 不能包含 session-local `TypeId`。
- `jil.Store` 按 session 重建；长期层只保存 object artifact key 和 `.o` 路径。

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
compilation 的内存 `MonomorphStore` 中。public generic body 以 Semantic Model template payload 的形式
保存在 `.ji`，不保存成 concrete JIL。

跨 package 调用 public 泛型时，下游需要能恢复上游泛型 body：

```text
package util:
  public T id<T>(T value) { value }

package app:
  Int x = util.id<Int>(1)
```

如果 Jiang 不使用 runtime metadata 泛型，`app` 必须拿到 `id<T>` 的 body，生成 `id<Int>` 的
concrete JIL 和 `.o`。这个 body 来自 `.ji` 的 generic Semantic Model template section，不依赖上游源码。

## Object Cache

object cache 是 backend/codegen cache，不是 semantic cache。

每个 stable codegen unit 都有独立的分片 index record：

```text
ObjectIndexRecord
  format version
  object kind
  stable unit key
  compiler build/schema fingerprint
  language version
  target/ABI fingerprint
  LLVM/toolchain fingerprint
  backend profile fingerprint
  dependency/interface fingerprint
  relative object path
  expected object hash
```

record 不保存绝对路径、pointer、`DefId`、`TypeId` 或其他 session-local ID。相对路径只允许
直接位于 cache root 的 `objects/` 下，拒绝绝对路径、父目录和嵌套目录穿越。每个 stable key
对应一个独立 `.jai` record，因此两个进程写不同 key 不需要合并全量 index。

lookup 会先读取 record，再校验完整构建上下文、dependency fingerprint、预期相对路径和
object 文件的实际 hash。record 缺失返回 `missing`；record 存在但截断、损坏、版本不兼容、
上下文不匹配、文件缺失或 hash 不匹配返回 `stale`。内存中的 `ObjectArtifactCache`
只是当前进程的加速层，不能作为跨进程命中依据。

object path 由 backend artifact path planner 生成：

```text
cache_root/objects/source_<hash>.o
cache_root/objects/mono_<stable_instance_fingerprint>.o
cache_root/objects/package_<package_artifact_hash>.o
cache_root/lang/lang_<package_artifact_hash>.<dylib-ext>
cache_root/index/objects/source_<stable-unit-key>.jai
cache_root/index/objects/mono_<stable-unit-key>.jai
cache_root/index/objects/package_<closure-key>.jai
```

planner 是纯函数，不创建目录、不写文件。backend 把 object 先写入同一 cache root 下带进程
标识的临时路径，完成并可计算 hash 后用原子替换发布 object，最后原子发布 index record。
任何失败都会删除本进程的临时文件。相同 stable key 的并发编译允许重复生成，
但最终 object 内容和 record 必须一致；不能出现 index 指向半写入 object 的状态。

`CompileOptions.artifact_cache_dir` 是当前编译的 cache root，默认值为 `build/cache`。
命令行可用 `--artifact-cache-dir <path>` 为一次编译选择其他 cache root。测试 runner
会为每个 case 使用独立目录；日常编译未指定该选项时继续使用默认位置。
pipeline 后续只从这里取得 cache root，不在各阶段硬编码路径。

推荐分成两类：

```text
source object:
  同一 source module 的普通 concrete function、global 和 hosted entry wrapper。

monomorph object:
  一个 concrete generic instance，包括 type args 和 const args。
```

external declaration 不拥有 object。每个 definition 只能由一个 unit 发出；source/monomorph
unit 与 symbol 顺序都按 stable identity 排序，不能依赖 session-local ID。

每个 unit 只声明并 lower 自己拥有的 function body；遇到跨 unit 的直接调用时，backend 根据
JIL function reference 按需补充 LLVM declaration，不把被调用方 body 写入当前 `.o`。
只包含 global、没有 function 的 source module 也必须建立 source unit。通过已验证 interface
object closure 加载的 global 在当前 JIL 中只是 external declaration，其 definition 仍由恢复的
source object 提供，不能在当前 unit 重复发出。
`CodegenUnitKey` 只做本轮 JIL 分组；长期 object path 必须来自 source object key 或
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
  const_args
  body_hash
  layout_hash
  target / ABI
  compiler build / language version / toolchain
  backend_profile
```

当前 session 内的 `MonomorphStore` 仍然使用 `DefId + TypeId[] + ComptimeValue[]` 做内存去重。这个 key
不能写入本地 cache。`StableInstanceKey` 只用于 object cache，type args 必须先转换成不含
session-local `TypeId` 的 stable type key，const args 也必须转换成稳定值 fingerprint。

同一个 compilation 内可能因 session-local `TypeId` 不同而暂时出现语义等价的 concrete JIL
instance。codegen unit 分组必须用稳定 symbol identity 归一化这些重复项，只发出一个 definition
和一个 object cache entry。不同 owner 即使文本相同，也不做结构性去重。

### Link closure

executable 和 dylib 直接链接稳定排序后的 unit objects。link closure 包含：

- 当前编译的 source/monomorph units。
- 已通过本机 index 和实际 hash 验证的 interface-loaded units。
- package 的传递依赖 units。
- 在依赖 generic template 上生成的 concrete monomorph units。
- target runtime object。

link plan 按 object path 和 definition identity 去重。当前 compilation 已重新生成某个 definition
时，不再链接 interface closure 中该 definition 的旧 object。

`.ji` 的 object closure 只保存 unit kind、stable unit key、definition key、dependency
fingerprint 和 package closure fingerprint，不保存本机路径。interface loader 只有在整条 closure
都能通过本机 index 验证时，才允许 codegen 模式跳过依赖 source 的 Semantic Model/JIL lowering。
源码仍可用时，缺失或 stale closure 会回退到正常源码编译；将来提供 interface-only 分发时，
缺少必需 object 必须成为不可恢复诊断。

### `--emit-obj`

`--emit-obj -o file.o` 对用户始终只产生一个 object。内部 unit object 不暴露为多个输出。
整包 object key 是稳定排序的 unit key/hash closure。

当前 backend 尚未对所有目标提供可靠的 relocatable object merge，因此 `--emit-obj` 采用完整
package lowering 后的整包 emission，并缓存最终 package object。它不会像 executable/dylib
那样跳过依赖 source body；这样冷/热构建的单文件拥有相同 symbol closure。目标平台具备可靠
merge 后，可以改为合并已验证 unit objects，但不能改变单文件契约。

### 可观测统计

`--artifact-stats` 在 stderr 输出 interface hit/miss/stale、object hit/miss/stale、
emitted/reused unit 和最终 linked object 数。统计只用于回归与性能分析，不进入 cache key，
也不改变构建结果。

## 当前实现边界

当前实现已经具备这些内存和 artifact 结构：

- `SourceArtifactCache`：保存 `ImportSummary`、`ModuleInterface` 和 `GenericTemplate`。
- `JiFileImage`：提供 `.ji` header / section table / read_at 的最小 API。
- `ModuleGraphBuilder`：从 `ImportSummary` 或 AST 构建 session-local `ModuleGraph`。
- `InterfaceLoader`：从 `ModuleInterface` 恢复 `DefRecord`、namespace binding 和 Semantic Model signature skeleton。
- `StableInstanceKey`：描述 monomorph object 的稳定输入。
- `ConcreteInstanceRegistry`：在同一 compilation 内按 stable instance 复用 concrete JIL body 位置。
- `ObjectArtifactCache`：区分 source、monomorph 和 package object，作为当前进程的 lookup 加速层。
- `ObjectIndexRecord`：按 stable key 分片持久化，并验证本机 object 和构建上下文。
- `ObjectReusePlan`：建立稳定 unit 所有权、lookup 状态和 package closure。
- `ObjectClosure`：让 `.ji` 恢复并验证依赖 unit objects。
- `PackageArtifactKey`：只负责需要在普通 module graph 前发现的 lang provider dylib。
- `QueryDependencyGraph`：记录 query dependency / reverse dependency，并提供 transitive invalidation。
- `CompilerSession`：持有可复用 `CompilerContext`，通过 `begin_compilation` 进入下一轮编译。

磁盘边界到此为止。长驻服务可以复用当前进程的 query dependency graph，但 0.5.0 不把
Semantic Model、type facts、layout 或 JIL 序列化到磁盘。

## Invalidation

source change 后先重新 parse 当前 source，并重建可达 import/module facts。resolve/Semantic Model 阶段用
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
  与有效 Interface/ObjectClosure 一起命中时，可以跳过依赖 source 的
  Semantic Model/type_check/JIL/codegen，但不能跳过 interface loading。

PackageArtifact 命中:
  `--emit-obj` 可以复用 unit closure 对应的整包单文件 object。
  lang provider dylib 可以复用 provider package key 对应的文件。
  两者都不能替代 `.ji` / interface loading；dylib 加载失败、缺少固定符号或 ABI 不匹配时
  应报告诊断，不自动重建来掩盖错误。
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

其他失效边界：

- compiler binary 的 `.build-id` 改变时，interface/object context 都失效；开发构建不能只依赖
  长期不变的 package version 字符串。
- debug/release、target/ABI、LLVM/toolchain 或影响 codegen 的 backend profile 改变时，
  object key 改变。
- private body 改变只重建定义所属 source unit；public interface 改变还会改变依赖 closure，
  使消费方 unit 重建。
- 损坏的 `.ji`、index 或 object 不触发进程崩溃；有源码时按 miss/stale 路径恢复。
- 并发发布后不保留临时文件，下一次热构建必须能得到完整命中。

## 不变量

- 长期 cache 不保存裸 pointer。
- 长期 cache 不保存 session-local ID。
- `StableSymbolId` 和长期 fingerprint 写入 symbol 文本；`SymbolId.to_index()` 只允许用于本轮内存表。
- fingerprint 输入不能包含 span/source offset 这类非语义位置数据。
- source map 可以随着 revision 更新；诊断位置不能作为语义 fingerprint 的一部分。
- 增量 query stack 和 lazy sema query guard 需要分层，避免 cycle 状态混用。
