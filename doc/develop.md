# 编译器开发说明

这个目录包含 Jiang Stage1 编译器实现。Stage1 的目标是成为自举编译器，因此模块划分要尽量小、明确、稳定。

当前顶层结构刻意保持平铺。只有边界非常清楚的一类实现才放入子目录，例如 `support/` 和 `llvm/`。

## 编译流程

计划中的编译流程是：

```text
source files
  -> lexer/token
  -> parser/AST
  -> resolve/scope
  -> type_check/HIR
  -> lower_jir/JIR
  -> llvm/codegen
```

`HIR` 当前定位为类似 Rust 的 HIR 加 typed HIR/THIR：它需要保留足够的源码结构，方便诊断和语义检查，同时在 name resolution 和 type checking 之后携带已解析名称和类型信息。

`JIR` 是更低层、面向代码生成的 IR。它应该移除大部分源码级语法糖，把控制流、值、存储和调用降到 LLVM backend 容易消费的形式。

## 名称解析和符号生成

Jiang Stage1 应参考 Rust 的内部表示方式：编译器内部使用结构化 ID 表达语义，最终到 codegen/link 阶段才生成符号名。

内部不要把方法、泛型实例、初始化函数等 lowering 成可再次被 parser 解释的字符串。例如不要长期使用类似下面的形式表示调用目标：

```text
self.arena.alloc_array__ast.AstType
```

这种字符串同时包含语义信息和语法分隔符，容易在后续阶段被错误拆分。正确方向是把调用目标表示为结构化引用：

```text
ResolvedCallee {
    def: DefId,
    receiver_type: TypeId?,
    type_args: TypeId[],
}
```

推荐的职责边界：

- `Symbol` 只表示 interned source text，例如 identifier 的文本。
- `DefId` 表示唯一声明，例如 function、method、struct、enum、union、global。
- `TypeId` 表示语义类型，不直接等同 AST type syntax。
- `InstanceKey` 表示泛型实例，至少包含 `def_id` 和 `type_args`。
- `HIR/JIR` 中的 call 应引用 resolved callee 或 instance id，不依赖合成字符串查找。
- LLVM/codegen 阶段才把 `InstanceKey` 转成最终 symbol name。

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

当前 `ResolveResult` 已记录 `resolved_type_paths` side table：每个成功解析的 named type ref 会按 `TypeRefId` 记录为 builtin type、本模块 binding 或 imported binding。后续 type checker 应通过 `TypeRefId` 查询这个结果表，避免依赖遍历顺序，也避免重复实现 scope lookup。

resolve 应使用 `scope.jiang` 和 `interner.jiang`，但不做完整类型推导。

### `type.jiang`

编译器类型模型。

预期职责：
- primitive type
- nominal type
- pointer/reference/slice/array/tuple/optional/errorable/function type
- type equality 和 compatibility 辅助方法

类型表示要和 AST 语法节点分离。

### `type_check.jiang`

类型检查和语义验证。

预期职责：
- expression type checking
- 根据 expected type 处理 literal typing
- 本地类型别名在使用点展开为目标类型；struct/enum/union/trait 等声明保留 nominal type 句柄
- overload selection
- trait/concept constraint checking
- 生成 typed HIR

这个阶段负责把 AST/resolved syntax 转为 HIR。

### `hir.jiang`

高层 typed IR。

预期职责：
- typed item、statement、expression、pattern
- 已解析 declaration 和 binding
- 保留适合诊断和语义 pass 的源码结构

Stage1 中，HIR 暂时承担 resolved HIR 和 typed HIR/THIR 的角色。除非有明确需求，不要过早把它降成 CFG 形式。

### `jir.jiang`

Jiang backend lowering IR。

预期职责：
- 更低层的 expression 和 statement
- 显式 storage operation
- 更简单的控制流
- backend-friendly 表示

JIR 应避免源码级语法形态。它是进入 LLVM-specific lowering 前的边界。

### `lower_jir.jiang`

HIR 到 JIR 的 lowering。

预期职责：
- desugar 高层 HIR 构造
- 降低控制流表达式和 pattern-like 构造
- 为 backend emission 准备 call、temporary、storage

这里不要放 LLVM API 细节。

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

### `llvm/codegen.jiang`

LLVM backend。

预期职责：
- JIR 到 LLVM IR lowering
- LLVM type mapping
- function/global emission
- runtime intrinsic declaration

LLVM-specific 代码应放在 `llvm/` 内。

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
