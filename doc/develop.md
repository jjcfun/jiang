# 编译器开发说明

这个目录包含 Jiang Stage1 编译器实现。Stage1 的目标是成为自举编译器，因此模块划分要尽量小、明确、稳定。

当前顶层结构刻意保持平铺。只有边界非常清楚的一类实现才放入子目录，例如 `support/` 和 `llvm/`。

## 编译流程

当前 Stage1 frontend 的编译流程是：

```text
package input / source registry
  -> SourceManager
  -> ModuleGraph
  -> lexer / TokenBuffer
  -> parser / AstFile
  -> resolve / scope side tables
  -> type_check / TypeTable + type side tables
  -> lower_hir / HirModule
  -> lower_jir / JIR
  -> llvm codegen
```

当前边界：

- `SourceManager` 保存 source file registry，负责 `SourceId -> SourceFile` 和 path lookup。
- `ModuleGraph` 负责模块节点、import 边、cycle 检查、dependency-first resolve order；它不做语义检查。
- `lexer` 产出 `TokenBuffer`，`parser` 只消费 token buffer 并产出 `AstFile`。
- `resolve` 产出名称解析 side tables，例如 `BindingId`、`LocalBindingId`、`TypeRefId -> ResolvedTypePath`。
- `type_check` 产出 `TypeTable` 和类型 side tables，例如 `BindingId -> TypeId`、expression span -> `TypeId`。
- `lower_hir` 消费 AST、resolve result、type check result，产出携带 resolved ID 和 `TypeId` 的 `HirModule`。
- `lower_jir`、JIR 和 LLVM backend 是后续 lower/codegen 边界，不能重新依赖源码字符串做语义查找。

`HIR` 当前定位为类似 Rust 的 HIR 加 typed HIR/THIR：它保留足够的源码结构，方便诊断和语义 pass，同时在 name resolution 和 type checking 之后携带已解析名称和类型信息。当前 HIR 覆盖 parser/type_check 已产出的主要 declaration、statement、expression 和 pattern；import/module graph 元信息不作为普通 HIR declaration 进入可执行语义节点。

`JIR` 是更低层、面向代码生成的 IR。它应该移除大部分源码级语法糖，把控制流、值、存储和调用降到 LLVM backend 容易消费的形式。

## 名称解析和符号生成

Jiang Stage1 应参考 Rust 的内部表示方式：编译器内部使用结构化 ID 表达语义，最终到 codegen/link 阶段才生成符号名。

内部不要把方法、泛型实例、初始化函数等 lowering 成可再次被 parser 解释的字符串。例如不要长期使用类似下面的形式表示调用目标：

```text
self.arena.alloc_array__ast.AstType
```

这种字符串同时包含语义信息和语法分隔符，容易在后续阶段被错误拆分。

当前职责边界：

- `Symbol` 只表示 interned source text，例如 identifier 的文本。
- `BindingId` 表示当前模块顶层 binding，例如 function、struct、enum、union、trait、global、import alias。
- `LocalBindingId` 表示局部 binding，例如 function param、local var、pattern binding。
- `TypeId` 表示语义类型，不直接等同 AST type syntax。
- `HirDeclId` / `HirStmtId` / `HirExprId` / `HirPatternId` 表示 HIR flat node storage 中的节点位置。
- `HIR` 中的 name、decl、local var、expr type 应引用 resolved ID，不依赖合成字符串查找。
- `JIR` 和 codegen 后续也应继续使用结构化 ID，LLVM/codegen 阶段才生成最终 symbol name。

后续进入 overload、trait method lookup、generic monomorphization 后，需要补充更精确的语义身份类型，例如：

- `DefId`：跨模块唯一声明身份，不局限于当前模块 `BindingId`。
- `ResolvedCallee`：已解析调用目标，至少包含声明身份、receiver type、type args。
- `InstanceKey`：泛型实例身份，至少包含 declaration identity 和 type args。

```text
ResolvedCallee {
    def: DefId,
    receiver_type: TypeId?,
    type_args: TypeId[],
}

InstanceKey {
    def: ast.arena.Arena.alloc_array,
    type_args: [ast.AstType],
}
```

最终 symbol mangling 可以参考 Swift 或 Itanium ABI：使用不会被 Jiang parser 误解的编码。优先考虑长度前缀方案；如果暂时使用分隔符，也必须保证输出只包含安全字符，不包含 `.`、`#`、`[]`、`<>` 等仍有语法意义的字符。

示例：

```text
InstanceKey {
    def: ast.arena.Arena.alloc_array,
    type_args: [ast.AstType],
}

// 可接受的临时 mangling
__method_ast_d_arena_d_Arena_alloc_array__ast_d_AstType

// 更推荐的长期方向：长度前缀编码
M3ast5arena5Arena11alloc_arrayT3ast7AstType
```

Stage0 中可能仍需要字符串 mangling，但这只能作为过渡实现。Stage1 的 resolver、type checker、HIR、JIR 不应依赖字符串拼接来表达已解析语义。

## ID 与 Side Table 约定

Stage1 内部优先使用结构化 ID 和 side table 表达语义关系。ID 是某个 owner table 的索引句柄，不是源码文本，也不是跨阶段通用整数。除非明确说明，同一种 ID 只能在它所属的结果对象或表内查询。

通用规则：

- ID 类型要保持强类型区分，避免把 expression id、statement id、type id、binding id 混用。
- `Symbol` 只表示 interned text，相等比较快，但不代表声明身份。
- `BindingId`、`LocalBindingId`、`TypeId`、`Hir*Id` 才是后续阶段应传递的语义句柄。
- 不要把 fully-qualified string 当作语义身份；字符串只应出现在 source text、diagnostic message 或最终 backend mangling 中。
- 跨阶段查询必须走对应 result/table 的 helper，例如 `ResolveResult.lookup_resolved_type_path(...)`、`TypeCheckResult.lookup_binding_type(...)`、`HirModule.expr_at(...)`。

当前主要 ID：

| ID | Owner / 保存位置 | 作用 | 常用查询方式 |
| --- | --- | --- | --- |
| `Symbol` | `InternPool`，常见于 `ast.Name.symbol`、keyword symbols | interned 源码文本句柄，用于名字和关键字的快速相等比较 | 通过 `CompilerContext` / `InternPool` 比较或取回文本 |
| `SourceId` | `SourceManager.sources`，保存在 `SourceFile.id` | 源文件身份 | `SourceManager` 按 `SourceId` 取回 `SourceFile` |
| `TypeRefId` | parser 分配，保存在 `ast.NamedType.type_ref_id` | AST 中 named type reference 的解析 key | `ResolveResult.lookup_resolved_type_path(type_ref_id)` |
| `DeclId` | AST 顶层 declaration index，保存在 `scope.Binding.decl_id` | 指向 `AstFile.items` 中的顶层声明 | `AstFile.decl_at(binding.decl_id.id)` |
| `BindingId` | `ResolveResult.top_level` / `export_scope` 内的 binding table | 当前模块顶层声明、import alias、module alias 的稳定句柄 | `ResolveResult.lookup_top_level_id(symbol_id)`；type check 后用 `TypeCheckResult.lookup_binding_type(binding_id)` |
| `LocalBindingId` | `ResolveResult.local_bindings` | function param、local var、pattern binding、for binding 的稳定句柄 | `ResolveResult.lookup_local_binding_span(span)`；type check 后用 `TypeCheckResult.lookup_local_type(local_id)` |
| `TypeId` | `TypeTable.items` | 语义类型句柄，和 AST type syntax 分离 | `TypeTable.type_at(type_id)`、`type_equals(...)`、`compatible(...)` |
| `HirDeclId` | `HirModule.decls` | HIR declaration index | `HirModule.decl_at(id)` |
| `HirStmtId` | `HirModule.stmts` | HIR statement index | `HirModule.stmt_at(id)` |
| `HirExprId` | `HirModule.exprs` | HIR expression index | `HirModule.expr_at(id)` |
| `HirPatternId` | `HirModule.patterns` | HIR pattern index | `HirModule.pattern_at(id)` |

阶段职责：

- parser 只创建 AST 和 `TypeRefId`。它不解析 `TypeRefId` 指向哪个声明。
- resolver 创建 `BindingId` / `LocalBindingId`，并填充 `ResolveResult` side tables。
- type checker 创建 `TypeId`，并填充 `TypeCheckResult` side tables。
- HIR lowering 消费 `ResolveResult` 和 `TypeCheckResult`，把 AST 节点转换成携带 `BindingId` / `LocalBindingId` / `TypeId` 的 HIR 节点。
- JIR / codegen 后续应继续使用结构化语义身份，最终 symbol name 只在 backend 边界生成。

当前重要 side tables：

- `ResolveResult.resolved_type_paths`：`TypeRefId -> ResolvedTypePath`，用于查询 named type ref 被解析成 builtin、generic param、`Self`、本模块 binding 或 imported binding。
- `ResolveResult.resolved_value_paths`：按 name 使用点 span 保存 value resolution 结果，用于查询 expression name 是 local、top-level 还是 imported module path。
- `ResolveResult.local_bindings`：保存局部 binding 的 `LocalBindingId`、名字、span 和可变性。
- `TypeCheckResult.binding_types`：`BindingId -> TypeId`，保存顶层 binding 的类型。
- `TypeCheckResult.function_result_types`：`BindingId -> TypeId`，保存 function result type。
- `TypeCheckResult.local_types`：`LocalBindingId -> TypeId`，保存局部 binding 类型。
- `TypeCheckResult.expr_types`：按 expression span 保存 `TypeId`，供 HIR lowering 和后续诊断使用。
- `HirModule.decls/stmts/exprs/patterns`：HIR flat node storage，节点之间用 `HirDeclId` / `HirStmtId` / `HirExprId` / `HirPatternId` 引用。

查询示例：

```text
Name in type context
  Ast NamedType.type_ref_id
  -> ResolveResult.lookup_resolved_type_path(type_ref_id)
  -> binding / builtin / generic param / Self
  -> TypeChecker lowers to TypeId

Name in value context
  Ast Name.span
  -> ResolveResult.lookup_resolved_value_span(span)
  -> local binding or top-level binding
  -> TypeCheckResult.lookup_local_type(...) or lookup_binding_type(...)

HIR expression type
  HirExpr.type_id
  -> TypeTable.type_at(type_id)
```

## 顶层模块

### `compiler.jiang`

编译器顶层调度。

后续应负责高层编译入口：读取 package/module 输入，构建共享上下文，按顺序运行各个编译阶段，并选择输出模式。这里不应该放 lexer、parser、resolver、type checker 或 backend 的细节。

### `context.jiang`

编译器共享上下文。

当前职责：
- `InternPool` 和 keyword symbols
- `SourceManager`
- AST arena
- diagnostic arena 和 `DiagnosticBag`

它拥有跨 frontend 阶段共享的长期状态。阶段内部的临时数据仍应放在对应阶段或局部 arena 中，不要无条件塞进 `CompilerContext`。

### `source.jiang`

源文件和源码文本模型。

预期职责：
- 源文件身份
- 文件路径或模块路径
- 完整源码文本，通常是 `UInt8[]`
- span 构造辅助方法
- 诊断需要时的 line/column 查询

span 数据优先使用字节偏移和字节长度。line/column 应该在诊断时计算，不要存到每个 token 上。

### `source_manager.jiang`

编译期 source registry。

当前职责：
- 分配 `SourceId`
- 保存 `SourceFile`
- 按 `SourceId` 取回 source
- 按 path 查找已注册 source

它由 `CompilerContext` 持有，现在仍然是内存注册模型，不读取文件系统。后续 package 编译入口应在这里或它的上层接入 path normalization、文件读取、source 去重、source root 和 dependency root 查找。

`ModuleGraph` 不直接管理 source 列表，只通过 `SourceManager` 获取 import 目标。这样 graph 只保留依赖边、状态、cycle 和 resolve order。

### `diagnostic.jiang`

诊断和错误报告数据。

预期职责：
- error/warning 表示
- 关联 source span
- 诊断消息数据
- 后续支持 note、label、suggestion

这个模块应独立于 lexer/parser/type checker 的内部实现。

### `token.jiang`

Token 定义。

当前职责：
- `Kind` token/keyword 枚举
- `Token` 值类型
- literal/operator 等 token 分类辅助方法

Token 只表示词法事实。语义信息、已解析 symbol、类型信息都不应该放在这里。

### `lexer.jiang`

词法扫描器。

当前职责：
- 将 `UInt8[]` 源码扫描为 token
- 识别关键字
- 处理 operator、标点、注释、identifier、literal

lexer 应尽量保持 byte-oriented。UTF-8 校验和 Unicode 策略要在需要的位置显式处理，尤其是 identifier、string literal、character literal。

### `ast.jiang`

解析后的语法树。

预期职责：
- item/statement/expression/type 等语法节点
- 用于诊断的 source span
- name resolution 和 type checking 之前的语法形态

AST 应保留源码形状，不应该要求类型信息或已解析声明。

### `parser.jiang`

从 token stream 解析到 AST。

预期职责：
- 解析 package/module item
- 解析 declaration、type、statement、expression、pattern
- 报告语法错误，后续可加入错误恢复

parser 的输出应只是 AST。名称查找、重载解析、类型推导都放到后续阶段。

### `interner.jiang`

编译器名称字符串驻留。

当前职责：
- `Symbol`：interned text 的小整数稳定句柄
- `InternKey`：对 interned bytes 做 hash/equality 的内部 key
- `InternPool`：拥有复制后的字符串存储，负责去重，并将文本映射到 `Symbol`

它适合用于需要稳定身份和快速相等比较的名字，例如 identifier、field name、module name、builtin name。它不是 scope table，不应该存 declaration 或 binding。

### `scope.jiang`

词法作用域和 item 作用域数据。

预期职责：
- 嵌套 scope
- scope 内可见声明
- binding lookup
- 适当位置的重复声明检查

`BindingId` 是当前模块内 binding 的稳定句柄，`DeclId` 指向对应的 AST 顶层声明位置。这个模块描述可见性和查找结构，不做类型检查。

### `resolve.jiang`

名称解析。

预期职责：
- 将 AST name 解析到 declaration 或 symbol
- 连接 import/module
- 发现 unresolved 或 ambiguous name
- 为 HIR/type checking 准备已解析数据

当前 `ResolveResult` 产出语义 side tables：

- `resolved_type_paths`：每个成功解析的 named type ref 会按 `TypeRefId` 记录为 builtin type、generic param、`Self`、本模块 binding 或 imported binding。
- `resolved_value_paths`：记录 value/name 使用点解析结果，包括 local binding、top-level binding 和 import module alias。
- `local_bindings`：记录 function params、block local vars、pattern bindings、for bindings。

resolve 采用顶层单命名空间：import alias、alias、function、global、struct、enum、union、trait 在同一模块顶层互相冲突。普通 import 不摊平导入声明，只引入模块命名空间；`public import` 用于 re-export module namespace。

resolve 应使用 `scope.jiang` 和 `interner.jiang`，但不做完整类型推导。

### `type.jiang`

编译器类型模型。

预期职责：
- primitive type
- nominal type
- type param
- pointer/reference/slice/array/tuple/optional/errorable/function type
- type equality 和 compatibility 辅助方法

类型表示要和 AST 语法节点分离。

当前 `TypeTable.type_equals(a, b)` 表示语义类型的结构等价：builtin/type param 以 `Symbol` 比较，nominal type 以 binding identity 比较，tuple/function/pointer/slice/array/layer/errorable 递归比较子类型。

当前 `TypeTable.compatible(expected, actual)` 只在严格等价基础上额外放行 `invalid` / `infer`，用于错误恢复和未定型占位。literal 的 expected-type 适配不属于 `compatible`，由 `type_check.jiang` 在表达式检查时处理。

### `type_check.jiang`

类型检查和语义验证。

预期职责：
- expression type checking
- 根据 expected type 处理 literal typing
- 本地类型别名在使用点展开为目标类型；struct/enum/union/trait 等声明保留 nominal type 句柄
- alias cycle 检测
- declaration / binding / function result / expression / local binding 的类型 side tables
- function signature、global initializer、function body、local var、assignment、return、基础 control-flow 的语义检查
- generic param 可作为 type param 使用；`@where` 的 trait/equality bound 在 resolve/type_check 层只做名称解析和基本合法性检查
- 二元表达式会检查左右操作数基础兼容性；逻辑表达式和条件表达式要求 `Bool`
- call、field、index、slice 会做第一版目标类型检查，错误时写入 diagnostics 并继续产出 `invalid` 类型
- generic type arg 目前只做 arity 检查，不做实例化或约束求解

当前边界是：type checker 消费 AST 和 `ResolveResult`，产出稳定 side tables 与 diagnostics；`lower_hir.jiang` 只消费这些 side tables，不重新做名字解析或类型推导。

暂不实现：

- overload resolution
- trait method lookup
- trait conformance solving
- generic monomorphization

### `hir.jiang`

高层 typed IR。

预期职责：
- typed item、statement、expression、pattern
- 已解析 declaration 和 binding
- 保留适合诊断和语义 pass 的源码结构

Stage1 中，HIR 暂时承担 resolved HIR 和 typed HIR/THIR 的角色。除非有明确需求，不要过早把它降成 CFG 形式。

当前 HIR 采用 flat arena list：`HirDeclId` / `HirStmtId` / `HirExprId` / `HirPatternId` 是强类型索引，节点内部引用 resolved `BindingId`、`LocalBindingId` 和 `TypeId`。HIR 覆盖当前 AST 的主要声明、语句、表达式和 pattern，包括 struct/record、enum、union、trait、extend、assign、if/switch/try/while/for/defer、coalesce、field/index/slice、struct literal、variant、tuple/array 和 optional pattern。import 只作为 module graph 输入，不生成普通 HIR declaration。

### `lower_hir.jiang`

AST 到 HIR 的 lowering。

预期职责：
- 消费 AST、`ResolveResult` 和 `TypeCheckResult`
- 把已解析的顶层 binding、local binding 和 expression type 写入 HIR 节点
- 保持源码 span，方便后续 diagnostics
- 不做新的 name resolution、type checking、desugar 或 backend lowering

### `jir.jiang`

Jiang backend lowering IR。

预期职责：
- 更低层的 expression 和 statement
- 显式 storage operation
- 更简单的控制流
- backend-friendly 表示

JIR 应避免源码级语法形态。它是进入 LLVM-specific lowering 前的边界。

当前第一版 JIR 仍保持 flat arena list，不直接降成 CFG。`JirDeclId` / `JirStmtId` / `JirExprId` / `JirPatternId` / `JirTempId` 是强类型索引，节点继续携带 `BindingId`、`LocalBindingId` 和 `TypeId`。JIR 已经能承接当前 HIR 的主要 declaration、statement、expression 和 pattern，包括 switch、try、for、pattern-bearing `is`、field/index/slice、struct literal、variant、tuple、array、optional pattern 和 variant pattern。全局 initializer 有独立 `initializer_block`，用于保存 initializer 表达式 lowering 产生的临时语句。`defer` 不再作为源码级 statement 保留，`lower_jir` 会在 block 退出点插入显式 `run_defer` statement；局部变量初始化中的 `?? return/break/continue` 会降成 `coalesce_control_local` statement，并为 left/result 保留独立 lowering block；普通 `??`、`catch` expression、block/if/switch/try expression 会拆成 statement + temporary/assignment；`while` 条件有独立 `cond_block`，用于在每轮条件判断前执行条件表达式 lowering 产生的临时语句；`value is some binding` 会降成 `optional_is_some` expression；普通 `is pattern`、switch case 和 for-each pattern 会携带显式 pattern test/bind 列表。JIR expression 层不再保留 `coalesce_control`、`coalesce`、`catch_handler`、`block`、`if_expr`、`switch_expr`、`try_expr` 这些源码级高层 case。

### `lower_jir.jiang`

HIR 到 JIR 的 lowering。

预期职责：
- desugar 高层 HIR 构造
- 降低控制流表达式和 pattern-like 构造
- 为 backend emission 准备 call、temporary、storage

这里不要放 LLVM API 细节。

当前实现只消费 `HirModule`，不重新做名称解析或类型推导。第一版先把 HIR 中的 resolved ID、`TypeId` 和高层结构稳定搬入 JIR，保证普通 HIR 节点不会落到 `unsupported`；其中 `defer` 已经降成显式退出前执行的 `run_defer`，`?? return/break/continue` 已经从表达式降成局部初始化 statement，普通 `??` 和 `catch` expression 也已经拆成 statement + temporary/assignment。后续如果继续降低控制流，应优先从当前 structured block 过渡到 CFG，而不是重新引入源码级 expression case。

### `module_graph.jiang`

Package/module 依赖图。

预期职责：
- module discovery
- import graph
- dependency order
- cycle diagnostics

这个模块是 frontend 各阶段之间的调度边界。parser 仍然只负责单个 source 的语法解析，resolve 仍然只负责单个 AST 加导入 scope 的名称解析；跨文件 import、依赖顺序和循环检测由 ModuleGraph 统一处理。

依赖图语义：

- 图节点是 module/source file。
- 有向边 `A -> B` 表示 `A` 直接 `import` 了 `B`，也就是 `A` 依赖 `B`。
- 成功编译时，参与当前 root 的依赖子图应当是 DAG。
- resolver/type checker/lowering/codegen 的执行顺序应是 dependency-first，也就是先处理 `B`，再处理 `A`。

当前实现先使用内存注册模型：

- `add_source(path, text)` 转发到 `SourceManager` 注册 source，并返回 `SourceId`。
- `parse(id)` 确保对应 module 被解析为 AST。
- `resolve_root(id)` 从 root module 开始递归解析 import，并按依赖优先顺序运行 resolve。
- `resolve_order_len()` / `resolve_order_at(index)` 暴露已经 resolved 的 dependency-first 顺序。
- 每条 import 边会记录目标 module 和源码 span；同一个 module 内重复 import 同一 source 时只保留一条边。
- import path 当前来自 `import "path"` 的 string literal span，去掉首尾引号后按 `path` 精确匹配已注册 source。

当前依赖解析使用 DFS 加 module state，等价于三色标记：

- `empty`：module 还未 parse。
- `parsed`：module 已经 parse，import 已收集，但还未 resolve。
- `resolving`：module 正在 DFS 栈中，用于发现 import cycle。
- `resolved`：module 及其直接/间接依赖已经 resolve。
- `failed`：module 的依赖解析失败，例如 import cycle。失败 module 不进入 `resolve_order`。

`resolve_root(root)` 的核心流程：

1. 确保 root module 存在。
2. parse root，并收集直接 import。
3. 对每个 import 递归执行 resolve。
4. 将已解析 import 作为模块命名空间加入当前 resolver；`public import` 同时加入当前模块的 public module namespace。
5. resolve 当前 module 的 AST。
6. 标记为 `resolved`。

这个算法的目标复杂度是 `O(V + E)`，其中 `V` 是 root 可达 module 数量，`E` 是 import 边数量。每个 module resolve 成功后会追加到 `resolve_order`，因此该列表天然是 dependency-first order，可供后续 type check、HIR lowering、JIR lowering 和 backend 调度复用。

当前限制：

- 只支持内存 source registry，不做 filesystem/package discovery。
- import path 不做规范化，也不解码 string escape。
- import cycle 会在触发循环的 import span 上报错，并对已知的循环边补充 note；后续应输出完整格式化 cycle path。
- import resolve 会为每条 import 绑定一个模块名；显式 alias 优先，否则从 import path 的文件名推导默认模块名。
- `module.Name` 只查询被导入模块的 export scope；private top-level declaration 不会跨模块可见。
- 普通 import 不会把被导入模块的 public name 平铺到当前模块。
- `public import` 会把被导入模块作为 public module namespace 重新导出，不会摊平被导入模块的声明。例如 `middle` 中 `public import "leaf";` 后，外部通过 `middle.leaf.Name` 访问，而不是 `middle.Name`。
- package dependency 和跨 package visibility 尚未接入。
- 多个 import 导出同名声明时，当前 resolve 还没有 ambiguity diagnostic。
- package manifest、module name、source root 和跨 package dependency 尚未接入。

### `package_manifest.jiang`

Package 元数据。

预期职责：
- `package.ini` 的解析模型
- package name/version/type
- 后续 source root 和 dependency metadata

### `llvm/api.jiang`

LLVM C API helper layer，内部直接声明 LLVM 21.1.x C API 的最小 extern surface。

职责：
- 在文件内部定义 LLVM opaque pointee types，例如 `LLVMContext`、`LLVMModule`、`LLVMBuilder`、`LLVMType`、`LLVMValue`、`LLVMBasicBlock`。
- 对外暴露 Jiang 风格 wrapper，例如 `Context`、`Module`、`Builder`、`Type`、`Value`、`Block`。
- 提供薄 helper，例如 `function_type(...)`、`const_int(...)`、`Builder.build_ret(...)`。
- 提供 module target helper：`Module.set_default_target()` 设置默认 target triple；object emission 路径会通过 `TargetMachine` 查询真实 data layout 并写回 module。
- 提供 object emission helper：`create_target_machine(...)`、`TargetMachine.data_layout_string()`、`TargetMachine.pointer_byte_size()`、`TargetMachine.abi_size_of(...)`、`TargetMachine.abi_align_of(...)`、`emit_object_file(...)`。
- 记录资源释放边界：`Context.dispose()`、`Module.dispose()`、`Builder.dispose()`、`TargetMachine.dispose()`、message / `dispose_message(...)`。
- 不记录 mock instruction，不重新建一套 LLVM facade IR。

命名规则：
- 内部 opaque pointee type 使用 `LLVMContext` 这类名字，函数签名写 `LLVMContext*`；不使用 `LLVMContextRef*` 这种双重指针语义命名。
- 普通 wrapper 类型不加 `LLVM` 前缀，因为模块路径已经表达了 LLVM 语境。
- 如果 wrapper 只是官方 C API 的薄转发，不应隐藏资源所有权；创建出来的 context/module/builder 仍由调用方显式 dispose。

当前由于 stage0 对“public 方法体引用本模块 private extern 依赖”的导出模型仍有限制，`api.jiang` 内部的 LLVM opaque pointee type 和 LLVM extern 声明暂时保持 `public`，但这不是目标 API 边界。测试和后续 codegen 应只使用 wrapper API，不应直接使用 `LLVMContext*`、`LLVMType*` 或 `LLVMContextCreate()` 这类 raw handle / raw extern。

stage1 实现自举后需要回收这层技术债：

- `LLVMContext`、`LLVMModule`、`LLVMBuilder`、`LLVMType`、`LLVMValue`、`LLVMBasicBlock` 等 raw opaque pointee type 改回 `api.jiang` 内部私有。
- LLVM extern 声明改回 private，只允许 `api.jiang` 内部 wrapper 方法调用。
- `codegen.jiang` 不直接引用 raw LLVM handle 和 extern，只通过 `api.Context`、`api.Module`、`api.Builder`、`api.Type`、`api.Value`、`api.Block` 工作。
- 如果 stage0 仍需维护，应先修复“public 方法体依赖 private helper/extern 被导入后不可见”的问题，再同步收回这些 `public`。

当前 target/data layout 还有一个过渡限制：`Module.set_default_target()` 会优先通过 `TargetMachine` 写入真实 data layout，失败时才回退到固定 64-bit data layout。compiler tests 通过 `lli` 执行，而当前 `lli` 不暴露 native target initialization 符号，所以 `api.jiang` 暂不声明 `LLVMInitializeNativeTarget` 这类入口；后续切到原生 stage1c 后应补回显式 target initialization，并把 `Int` / `UInt` / pointer-sized layout 和所有 aggregate layout 统一接到 target data。

### `llvm/linker.jiang`

最小 external linker driver。

当前职责：
- 用系统 `cc` 把 object file 链接成 executable。
- 提供 `run_executable_file(...)` 作为 compiler smoke test 的临时执行 helper。

当前限制：
- 第一版只拼接简单 `cc <object> -o <output>` 命令，路径不能包含空格或 shell 特殊字符。
- linker driver 只是打通 object -> executable 闭环，不代表最终 CLI 设计。
- 未来需要支持 linker 配置、额外 object/runtime、library search path、目标平台参数，以及 `clang -fuse-ld=lld` / `lld` 路径。

### `llvm/codegen.jiang`

LLVM backend。

预期职责：
- JIR 到 LLVM IR lowering
- LLVM type mapping
- function/global emission
- runtime intrinsic declaration

LLVM-specific 代码应放在 `llvm/` 内。

Backend 只消费 JIR、`TypeTable` 和必要的 module/codegen 配置，不重新读取 AST/HIR，也不重新做 resolve/type check。JIR 中的 `BindingId`、`LocalBindingId`、`TypeId`、`JirTempId` 是 backend 查表和生成 storage 的主要入口；最终 LLVM symbol name 只在 backend 边界按这些结构化 ID 做 mangling。

当前 `codegen.jiang` 已删除旧 facade `emit_module(...)`，主要 public 边界是：

- `emit_minimal_main_ir()`：直接构造最小 LLVM module，作为 FFI/LLVM 链路 smoke test。
- `emit_jir_module_ir(...)`：消费 JIR 和 `TypeTable`，返回 `LLVMPrintModuleToString` 生成的 IR 字符串。
- `emit_jir_module_object(...)`：消费 JIR 和 `TypeTable`，通过 LLVM target machine 写出 object file。它复用 `emit_jir_module_body(...)`，因此 object emission 和 IR emission 不应分叉实现语义。

当前真实 LLVM lowering 已覆盖的 JIR：

- declaration：function、method、global、type declaration metadata。
- storage：local、temp local、assign、name/temp expr。
- primitive expr：literal、unary、binary、call、tuple、fixed array、pointer index、fixed array index、field/index/slice、struct literal、variant constructor、`self`。
- compound ABI：struct/record、tuple、fixed array、optional、errorable、union variant payload 的基础 layout；union/errorable payload 使用 byte buffer，并由 resolved type layout 决定 payload 大小。
- pattern expr：`optional_is_some`、普通 `is_expr` 的 primitive test/bind 列表，包括 literal test、optional some test、variant tag test、tuple item bind、variant payload bind 和 binding materialization。
- structured stmt：block、if、switch branch chain、try success/error branch、while、for-range、fixed array/slice for-each、return、throw/unreachable、break、continue、run-defer。
- early exit：`coalesce_control_local` 已能生成 optional test 和 return/break/continue control flow。

当前还需要继续真实 lowering 的 JIR：

- 更完整的 ABI：跨 module nominal type layout、泛型实例 layout、trait object/receiver ABI、按目标平台 data layout 校准 size/align。
- 可执行输出闭环：object file emission 已有最小路径；executable smoke test 已可通过 `llvm/linker.jiang` 调用系统 `cc` 链接运行；正式 CLI、linker 参数模型和 runtime 链接仍是后续工作。

当前明确不应进入 LLVM backend 的源码级结构：

- JIR expression 层不应包含 `coalesce`、`catch_handler`、`select`、`block`、`if_expr`、`switch_expr`、`try_expr`。
- 如果 backend 看到 JIR `.unsupported` 或无法处理的 expression fallback，应优先在 JIR lowering 阶段消除，或在 LLVM backend 中补齐真实 lowering；不要重新引入 mock instruction model。

暂不进入第一版 backend 的内容：

- overload resolution、trait solving、trait method lookup。
- generic monomorphization 和跨 module instance cache。
- 完整 CFG IR。当前可以从 structured JIR 直接 emit LLVM block；如果后续控制流复杂度升高，再新增 CFG 层。
- LLVM exception model。Jiang `try/catch` 先按普通 tagged/errorable value lowering。

第一批 backend 测试从只验证结构和可生成性开始：

- 空 module / 简单 function / global initializer。
- local/temp/assign/return 的基本 emission。
- if/while/block/run-defer 的 block 生成顺序。
- switch + pattern test/bind 的 branch 形状。
- optional coalesce、try/catch 的最小 success/error branch。

## Support 模块

Support 模块是编译器内部工具，不应依赖 AST/HIR/JIR。

### `support/arena.jiang`

Arena allocator。

当前职责：
- block-based allocation
- reset 时将 block 标记为未使用
- 按 alignment 分配 bytes、单个值、数组

适合用于生命周期清晰的编译器数据，例如某个阶段或某个上下文统一释放的数据。

### `support/list.jiang`

List 容器。

当前职责：
- `ArrayList<T>`：heap-backed growable list
- `ArenaList<T>`：arena-backed growable list

需要独立生命周期和可释放 buffer 时使用 `ArrayList`。列表生命周期绑定到 arena 时使用 `ArenaList`。

### `support/map.jiang`

Hash map 容器。

当前职责：
- open-addressed hash map
- `set`、`get`、`get_ptr`、`remove`、`reserve`、`clear`
- key 通过 `Hashable` 和 `Equatable` 约束

在需要更专门的数据结构之前，这是编译器内部默认 hash map。

### `support/hash.jiang`

Hash 工具。

当前职责：
- `WyHasher`
- 写入 bytes、单个 byte、`UInt64`
- 通过 `finish()` 返回 `UInt64` hash

编译器里的 `Hashable` 类型应返回 `UInt64`，和 hasher 结果保持一致。

### `support/string.jiang`

Byte-string 辅助方法。

当前职责：
- `substring`
- byte-wise equality

API 中优先使用 slice 语法，例如 `text[start..end]`。不要轻易增加 `*_range` 之类 helper，除非它真的减少复杂度。

### `support/bitset.jiang`

Bitset 工具占位。

预期职责：
- 紧凑 boolean set
- 后续用于编译器分析的 first-set、union、intersection 等辅助方法

## 当前命名约定

- 使用 `Symbol` 表示稳定的 interned-name handle。
- 暂时保留 `InternPool` 命名；它拥有 interned string 存储，并把文本映射到 `Symbol`。
- 调用方能传 `slice[start..end]` 时，避免添加 `*_range` API。
- 安全的基础数值转换使用 `Type(value)`。
- `$.as(Type)` 保留给 unsafe reinterpretation 或底层转换。
- struct 相关 helper 如果不能被其他模块复用，尽量放在 struct 内部。

## 所有权边界

- `InternPool` 通过自己的 `Arena` 拥有 interned string bytes。
- AST/HIR/JIR 节点后续应由所属 compiler context 或 phase 的 arena 分配。
- support 容器默认拥有自己的内部 buffer，除非它明确是 arena-backed。
- backend 模块不应拥有 frontend semantic data。

## 暂时不要添加

- LSP-specific 文件或 API。
- 为每个编译阶段建立过深的子目录。
- 在 AST/HIR/JIR 形状稳定前，为 IR node 过早抽象。
- 没有被编译器代码使用的通用 utility API。
