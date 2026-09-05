# Resolve 设计

resolve 是 package root 入口之后的名字解析阶段。它负责 module graph、import 解析、
namespace 建立、declaration collection 和 reference resolution。当前 resolve 直接把
resolved AST lower 成 Semantic Model，不再产生 `ResolvedFile` 这种中间文件结果。

## 入口

`resolve/module_resolver.jiang` 是当前 resolve 入口。pipeline 把解析得到的 owned root AST
直接移交给 `ModuleResolver`。resolver 用私有 typed map 持有 root/import closure 中尚未完成
lowering 的 active AST，先构建 `ModuleGraph`，再把 graph
内可达 module 直接 lower 到 Semantic Model：

```jiang
SyntaxResult root! = syntax.parse_source(ctx, text, root_source_id)
ModuleResolver resolver! = ModuleResolver(ctx)
ModuleGraph^ graph = resolver.build_module_graph(root.take_unit())
resolver.lower_module_graph_to_model(graph$.ref())
```

`pipeline.compile(ctx, options)` 是当前 source/syntax/resolve 的路径入口：
如果 `input_path` 可直接读取为文件，就按单文件 root 编译；否则按 package 目录处理，
读取 `input_path/package.ini`，再编译 manifest 指定的 root source。

## Module Graph

module graph 只描述 root 可达 module 的 import closure。它不替代 namespace，也不保存
declaration/reference 的解析结果。

```text
build_module_graph(root_unit)
  -> move root AstUnit into ModuleResolver active AST map
  -> ensure_declarations(root_unit.source_id)
  -> create/reuse module shell, collect imports, publish declaration names
  -> seal_module_graph with a growing module/import worklist
  -> ensure each import target and parse/load newly reached AstUnit when needed
  -> add ModuleGraph edges and append newly discovered module shells to the worklist
  -> check package dependency cycle on cross-package edges
```

`ModuleGraph.package_id` 只记录入口 root package。`ModuleGraph.modules` 可以包含多个
package 的 module；后续 Semantic Model、type check、JIL、layout、borrow check 和 backend
都消费同一张 root import closure。

`ensure_module(source_id)` 保证一个 source 有稳定的 `ModuleId`：

- 如果 `source_modules` 中没有这个 `SourceId`，先根据 source 文件路径向上查找
  `package.ini`。找到 manifest 时创建或复用对应 package owner；找不到 manifest 或
  source 是 virtual/buffer 时，复用默认 root package。
- `PackageId` 表示 package owner，`ModuleId` 表示单个 source file module，`SourceId`
  表示输入源文件或虚拟文本。
- 一个 package 可以拥有多个 module，一个 source 当前对应一个 module。
- 如果已有 module 且 `source_revision` 未变化，直接复用原 `ModuleId`。
- 如果 source revision 变化，保持 `ModuleId` 不变，调用 `reset_module` 清空该 module
  的 imports，并创建新的 module namespace。

旧 namespace 和旧 def 暂时留在全局表中；后续如需长期增量会再引入 GC 或版本化策略。

## Resolve State

`ModuleResolver.module_states` 记录本次 resolver 的 module pass 进度：

- `unseen` / `parsing`：module shell 尚未处理，或正在解析对应 source。
- `collecting_imports` / `imports_collected`：正在或已经完成 import 收集。
- `collecting_declarations`：正在收集 top-level declaration names；重入只复用 module shell。
- `declarations_ready`：本文件 declaration skeleton 已发布，alias 和 extension 尚未完成。
- `declarations_collected`：当前 SCC 的 alias 和 extension declaration facts 已完成。
- `resolving` / `resolved`：正在或已经完成 reference resolve 和 Semantic Model lowering。
- `interface_loaded`：declaration/model facts 来自持久 interface，不拥有 source AST。
- `failed`：当前 module 的 parse、resolve 或 model lowering 已失败。

这些状态用于避免重复收集同一个 module，也用于 import cycle。如果 A 和 B 互相 import，
A 在递归 B 前已经发布本文件 declaration names；B 再回到 A 时，ModuleGraph 只复用已有 shell
并截断递归。alias、extension 和 body 仍在 SCC declarations 完整后处理。

## Name Resolver

`NameResolver` 是单个 AST file/module 的 resolver。它不负责创建 module，也不负责跨文件
加载；初始化时只拿当前 `AstResolveState`：

```text
NameResolver {
  AstResolveState
  lexical env for ReferenceResolver
}
```

当前 `NameResolver` 的 pass：

- `collect_imports`：扫描 top-level import，向 `ModuleRecord.imports` 写入 `ImportRecord`。
- `collect_declarations`：扫描 top-level declaration，创建 `DefId`，绑定到当前 module
  namespace，并按 visibility 记录到 namespace/export facts。
- declaration collection 同时登记 `DefId -> 精确 declaration/member AST`；member 另记共享语法容器。extension、
  extern 和 type/trait member 按需 resolve/lower 时直接进入 context，只遍历被请求 member，不从 `DefId` 反向
  扫描整文件。嵌套 namespace 直接沿现有 `DefRecord.owner_def` 链逐层选择直属 AST，不保存额外路径对象。
- `resolve_references`：遍历当前 file 的 declaration body，校验基础 type reference、
  expression name、local binding 和 import alias path。

reference resolution 完成后，Semantic Model lowering 使用同一套 namespace/local lookup 直接写入
resolved Semantic Model。

## Import Target

每个 import 使用 `unresolved/resolving/resolved/failed` 状态，并由
`ensure_import_target(module, import_index)` 独立推进：

- 对普通 `import dep`，优先在当前 package 的 `[dependencies]` 中查找 `dep`。
- 命中 dependency 时读取依赖 package 的 manifest root 文件。
- dependency package 内部继续按它自己的 `package.ini` 解析 `[dependencies]`，因此
  `app -> util -> base` 这类递归源码依赖会进入同一编译 closure。
- 未命中 dependency 时，再按已登记 virtual/module 名称查 `SourceStore`。
- 找到 source 后调用 `ensure_module(source_id)`。
- 如果目标 source 的 `AstUnit` 已经登记到 resolver 的 active AST map，会递归推进目标 module pass。
- 非通配 import 在 target 解析前已经创建稳定 `import_alias_def` skeleton，并作为
  `.namespace_name` 绑定到当前 module namespace；解析成功后只补上目标 namespace。
- `import *` 在 target 解析成功后登记 lazy wildcard namespace edge。普通 lookup 可沿 private edge，
  qualified re-export lookup 只沿 public edge；沿 edge 查询时也只接受目标 namespace 的 public binding，
  private 同名声明不能遮住其他 public re-export；不为目标 declaration 批量创建 alias `DefId`。
- 对 string/file import，当前取字符串字面量的 symbol 文本，
  按当前源文件目录解析显式文件路径。
- file import 只能跨当前 package 内部 source。跨 package 必须使用 manifest dependency
  alias；如果 string import 解析到另一个 package，会报 `cross_package_file_import`。

module import cycle 允许。package dependency cycle 不允许：ModuleGraph 构建完成后会从
跨 package import 边汇总 package-level reachability，如果发现 package 闭环，报
`package_dependency_cycle`。

## Package Public Surface

跨 package 可见性只看 dependency package 的 root module public namespace：

- root module 的 public function/type/global 可作为 package API 访问。
- root module 的 `public import` 可以把目标 module namespace 作为 public API 的一个成员
  重新导出，但不会 flatten 目标 module declarations。
- root module 的 `public alias` 可以重新导出 public symbol。函数 alias 记录一个可见 overload
  anchor，调用时仍在目标原始 namespace/name 下执行 public overload resolution；不会暴露
  private 同名函数。
- 非 root module 中的 public declaration 不会自动成为 package API。
- private declaration、private alias 和非 root public declaration 跨 package lookup 都会诊断。

## Semantic Model Lowering

resolve 的最终输出是 Semantic Model facts：

```text
lower_module_graph_to_model(graph)
  -> collect declarations for all graph modules
  -> NameResolver.resolve_references()
  -> lower resolved AST nodes directly into CompilerStore.model
  -> mark module resolved and release its owned AstUnit
```

resolve 不输出 `ResolvedFile`，也不把 AST 持久化到 query。Semantic Model lowering 只在 resolve 阶段
使用 AST 和 resolve state。

## 0.5.4 渐进式改造基线

0.5.3 的实际调用顺序是：

```text
pipeline.compile_package_with_emit
  -> syntax.parse_source_with_lang(root)
  -> ModuleResolver.build_module_graph
       -> ModuleGraphBuilder.ensure_declarations(source)
       -> collect imports / parse newly reachable source
       -> publish module DefId and top-level declaration skeletons
  -> ModuleResolver.lower_module_graph_to_model
       -> schedule SourceGraph SCCs
       -> bind import aliases
       -> collect declaration names for every source in the SCC
       -> resolve alias fixed point and extension owners/members
       -> resolve every reference and lower every signature/body in each module
  -> type_check.check_model_package
  -> JIL lowering / borrow check / drop elaboration / backend
```

各步骤的事实 owner 固定如下：

| 步骤 | 读取 | 写入 |
| --- | --- | --- |
| parse/import discovery | source、provider、AST | active AST、`ModuleGraph`、`SourceDependencyGraph` |
| declaration scan | active AST、module namespace | `DefStore`、namespace/export、Semantic Model skeleton、`SourceMap` |
| alias/extension collection | skeleton、namespace | 同一 namespace binding 与同一 `sem.Def.members` |
| reference/model lowering | active AST、namespace、skeleton | Semantic Model signature/body/node、`SourceMap`，随后释放 AST |
| type check/const | Semantic Model、resolve facts | `TypeCheckStore`、`ComptimeStore` 与各 typed query cache |
| JIL | Semantic Model、type/const/layout facts、`ModuleGraph` | pipeline-owned `jil.Store` |

当前 declaration identity 已经先于 signature/body 建立，同名函数也保留多条 namespace binding。
`ModuleResolver.ensure_declaration_signature(graph, DefId)` 和 `ensure_declaration_model(graph, DefId)` 可以从普通
顶层或成员 `DefId` 定位回所属顶层语义单元。所有 declaration 都按 signature/body 分开 resolve/lower：init/deinit 与函数
一样先建立 callable signature；global initializer、field default、enum discriminant 和 associated const value
只在 body ensure 时生成。body pass 复用已有 signature、generic/parameter binding 和 owner header facts，不重复
创建这些事实。extension 只共享一次 target/generic/trait/where header，各 member 与 extern、type/trait namespace
member 一样独立推进；嵌套 namespace 由 `owner_def` 链恢复上下文。跨模块 extension target 遇到未连接的 import
alias 时会推进精确 import 并重试 member collection；comptime wrapper 和 module doc 顺序仍由最终 validation
sweep 完成。

pipeline 先调用 `begin_module_graph`，完成 root import scan 和 declaration skeleton 发布；这时 import target
仍是 unresolved，graph 尚未封闭。随后先准备 core/std 查询环境并 demand root：有 `main` 时推进 `main` body，
无 `main` 的 library/check root 则推进本 root 的源码 declaration。最后显式 `seal_module_graph` 才补齐未使用
import closure。因此 declaration skeleton 和入口 Sema 都不依赖完整 `ModuleGraph` 先构造完毕。

import target 也有独立的 unresolved/resolving/resolved/failed 状态，并可通过单条
`ensure_import_target(module, import_index)` 推进。`seal_module_graph` 使用显式增长 worklist：module shell
只收集 import，队列逐条 ensure target，新发现的 module 追加到同一 graph 后继续处理，直到 closure 稳定。
declaration 的 name/path lookup 遇到尚未连接 namespace 的 import alias 或未解析 wildcard 时，记录精确
module/import 下标并暂停当前 declaration；外层 ensure 解析该 import、发布目标 declaration skeleton 后重新解析
当前 declaration。命中 callable 名字时，同一 binding key 下尚未完成的 overload signature 也逐个走同一 ensure
入口。传入 ensure 的同一个 `ModuleGraph` 会立即吸收查询发现的 module/import edge；传递 public wildcard miss
会沿已连接的 namespace edge 找到下一条 unresolved import 后继续推进。重试复用 AST→Def 映射，不重复创建
generic、parameter 或 local `DefId`。CLI 的最终 seal 仍会遍历全部可达 import，作为 source graph 封闭和未使用
源码最终验证的一部分。

命名 import 在 target 尚未解析时已发布 namespace alias skeleton；file import 的默认 basename alias 与
显式同名 alias 走同一路径并复用同一个 DefId。普通 alias 也先登记自己的 `alias_name` skeleton，lookup 该
名字时才用同一 `DefId` 连接 target；精确 import ensure 不顺带解析目标 module 的其他 alias。`import *` 保存
wildcard namespace edge，lookup 时读取目标 namespace。namespace-local binding index 同时供 public surface
冲突和 observation fingerprint 使用；传递 surface 只接受 public binding，相同 `DefId` 的菱形重导出不冲突，
不同 `DefId` 的同名重导出稳定报错。最终 validation 仍观察目标完整 public namespace，因此新增未引用 export
会正确失效 artifact。

同一轮中一旦 module 已从 source 发布 declaration skeleton，artifact 调度就继续使用该 source 的 facts，
不会再把 `.ji` declaration 拼接到同一组 DefId。尚未扫描 declaration 的依赖 module 仍可直接恢复 interface。

`--jil-stats` 是本轮基线的固定采样入口。它输出 parse、graph、model、type check、完整 frontend、
JIL、borrow/drop 各阶段耗时，model 子阶段耗时，以及 source、module、DefId、源码函数、全部 JIL
function 和 JIL arena 用量。峰值 RSS 使用平台 `/usr/bin/time -l`（macOS）或 `/usr/bin/time -v`
（Linux）包住同一命令采集；冷/热结果必须使用独立且明确记录的 artifact cache 状态。

## 待完成

- package manifest 诊断还比较粗，只记录错误文本，没有 `package.ini` 的精确行列 span。
- reset 后旧 namespace/def 的回收或版本化。
- 跨轮复用 session-local semantic/query facts 仍需要版本化与精确 invalidation；持久 artifact identity
  已使用 `StableSymbolId`，不能把 `DefId` 直接写入缓存。
