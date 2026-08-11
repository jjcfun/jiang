# Resolve 设计

resolve 是 package root 入口之后的名字解析阶段。它负责 module graph、import 解析、
namespace 建立、declaration collection 和 reference resolution。当前 resolve 直接把
resolved AST lower 成 Semantic Model，不再产生 `ResolvedFile` 这种中间文件结果。

## 入口

`resolve/module_resolver.jiang` 是当前 resolve 入口。pipeline 先创建本次 package 编译的
临时 `syntax.Store`，放入 root AST 后按值移交给 `ModuleResolver`。resolver 是 root/import
closure active AST 的唯一 owner，先构建 `ModuleGraph`，再把 graph
内可达 module 直接 lower 到 Semantic Model：

```jiang
syntax_store.Store asts! = syntax_store.Store()
SyntaxResult root = syntax.parse_source(ctx, text, root_source_id)
asts.set_unit(root.unit, source_revision)
ModuleResolver resolver! = ModuleResolver(ctx, asts$.move())
ModuleGraph^ graph = resolver.build_module_graph_for_source(root_source_id)
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
  -> root AstUnit already lives in this compile_package syntax.Store
  -> ensure_declarations(root_unit.source_id)
  -> create/reuse module shell, collect imports, publish declaration names
  -> resolve import targets, parse/load target AstUnit into the same syntax.Store when needed
  -> add ModuleGraph import edges
  -> recursively ensure declarations for reachable source modules
  -> check package dependency cycle on cross-package edges
```

`ModuleGraph.package_id` 只记录入口 root package。`ModuleGraph.modules` 可以包含多个
package 的 module；后续 Semantic Model/type check/monomorph/JIL/layout/borrow/backend 都消费同一张
root import closure。

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
- `resolve_references`：遍历当前 file 的 declaration body，校验基础 type reference、
  expression name、local binding 和 import alias path。

reference resolution 完成后，Semantic Model lowering 使用同一套 namespace/local lookup 直接写入
resolved Semantic Model。

## Import Target

`resolve_import_targets` 在 imports 收集后运行：

- 对普通 `import dep`，优先在当前 package 的 `[dependencies]` 中查找 `dep`。
- 命中 dependency 时读取依赖 package 的 manifest root 文件。
- dependency package 内部继续按它自己的 `package.ini` 解析 `[dependencies]`，因此
  `app -> util -> base` 这类递归源码依赖会进入同一编译 closure。
- 未命中 dependency 时，再按已登记 virtual/module 名称查 `SourceStore`。
- 找到 source 后调用 `ensure_module(source_id)`。
- 如果目标 source 的 `AstUnit` 已经登记到本轮 `syntax.Store`，会递归推进目标 module pass。
- 解析成功后创建 `import_alias_def`，并把 alias 作为 `.namespace_name` 绑定到当前 module
  namespace。
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
```

resolve 不输出 `ResolvedFile`，也不把 AST 持久化到 query。Semantic Model lowering 只在 resolve 阶段
使用 AST 和 resolve state。

## 待完成

- package manifest 诊断还比较粗，只记录错误文本，没有 `package.ini` 的精确行列 span。
- reset 后旧 namespace/def 的回收或版本化。
- 长期增量需要 stable def key、source revision diff 和 query invalidation。
