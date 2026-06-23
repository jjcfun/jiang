# DSL / Lang Package

Jiang 的 DSL 机制是 Jiang parse 之后、resolve 之前的 syntax expansion。普通 parser 先保留
`#alias { ... }` invocation，expansion 阶段再调用 lang provider。DSL provider 不生成 HIR、MIR
或 backend IR；它只把外部语法片段翻译成 Jiang 语法层能表示的 syntax tree。

## Goal

源码中的 lang invocation 形如：

```jiang
User user = #sql {
    select * from User where id == \(id)
};
```

`#sql` 中的 `sql` 只来自当前 package manifest 的 `[dependencies]` alias。目标 dependency
必须是 `type = lang` package，并在 package root 中公开默认入口 `Lang`。

lang invocation 使用 block 形式，不支持 `#sql(...)`，也不支持源码内声明多个 parser 入口。
编译器按 invocation 所在语法位置传入对应 entry kind，provider 需要返回同类 Jiang syntax tree。
每个 lang invocation 会创建一个 provider 实例，`scan` 和 `parse` 通过该实例共享 DSL 私有状态。

## Public Syntax Tree

`std/jiang/syntax/` 放 Jiang syntax 阶段共享 API，包括 source/span、symbol、syntax tree、
syntax builder trait、syntax diagnostic 和 provider protocol。`std/std.jiang` 导出 `jiang`
namespace，使用方通过 `std.jiang.syntax.*` 访问这些结构。Jiang 语言词法辅助放在
`std/jiang/lex/` 和 `std/jiang/text/`，通过 `std.jiang.Tokenizer`、`std.jiang.Token` 和
`std.jiang.ident` 复用。

`std.jiang.syntax.Tree` 是 lang provider 的 public Jiang syntax tree。它的定位是：

- 表达 Jiang 当前语法层能解析和继续语义检查的结构。
- 作为 DSL parser 的返回 ABI。
- 只保存 syntax facts、span 和文本名，不保存 resolver/sema/type/backend 结果。

它不是语言无关 AST，也不是编译器内部 `src/syntax/ast.jiang` 的逐字段暴露。公开 syntax tree
会尽量贴近 Jiang 内部 AST 的概念，例如 `TopLevelDeclaration`、`MemberDeclaration`、`Expression`、
`Statement`、`TypeReference`、`Pattern`、`Path`，但会避免公开内部 parser recovery、symbol id、
token range 和 compiler-only 节点。

`std.jiang.syntax.Span` 是单个 source 内的 byte range，只包含 `start/length`。source identity
放在 `std.jiang.syntax.Source.source_id` 上，不在每个 span 重复保存。syntax 阶段诊断使用
`std.jiang.syntax.Diagnostic`，避免和 compiler 内部包含 LSP/fix-it 信息的 rich `Diagnostic` 混用。

## Internal Boundary

短期边界如下：

```text
Jiang source
  -> Jiang lexer/parser
  -> lang expansion
  -> src/syntax/ast.jiang
  -> resolve/HIR/sema/MIR/backend

DSL source
  -> raw lang invocation node
  -> lang provider
  -> std.jiang.syntax.Tree
  -> validate/convert into src/syntax/ast.jiang
  -> resolve/HIR/sema/MIR/backend
```

也就是说，普通 Jiang parser 暂时继续产出内部 AST，并在遇到 `#alias { ... }` 时记录 invocation
和 raw block。parse 完成后、resolve 开始前，lang expansion 根据 registry 调用 provider；
provider 返回 `std.jiang.syntax.Tree` 后，编译器校验并转换成内部 AST，再交给既有 resolve/sema
流程。

长期可以让内部 parser 逐步向 `std.jiang.syntax.Tree` 靠拢，但不要求当前重写 parser 或 resolve。

## Lexer Behavior

Jiang lexer 默认按普通 Jiang token 处理。看到 `#ident { ... }` 时，输出：

```text
hash ident raw_block
```

动态库 provider 接入后，host 只需要识别 `#ident` 和 opening delimiter；完整 body 边界由
provider 的 `scan` 决定。
在 provider scan 接入前，`raw_block` 仍作为过渡 lexer 能力存在：span 覆盖完整 `{ ... }`，
内部只递归匹配 `{}` 边界，不按 Jiang token 展开。未闭合 raw block 产生 `unterminated_raw_block`
诊断。

公开 `std.jiang.Tokenizer` 不保存 token text 或 compiler 内部 symbol id。token 的文本由
`Token.span` 回到 `Source.bytes` 按需取得，identifier 的 intern 由调用方的 builder/compiler
上下文负责。`std.jiang.syntax.Tree` 中的 name 和 int/float/char/string literal 原始文本都保存为
public `SymbolId`，由 `Builder.intern_symbol` 创建。identifier 判定使用 ASCII fast path 加 Unicode
`XID_Start` / `XID_Continue`，底层压缩表由 `script/gen_unicode_xid.js` 生成到
`std/jiang/text/generated/xid.jiang`。

## Registry

解析 package manifest 后，编译器先加载 dependencies。对 `type = lang` package，编译器将其
编译为 host dynamic library，并把 dependency alias 注册到 lang registry：

```text
dependency alias -> provider handle for package root public Lang
```

当前 package 的 lang expansion 遇到 parser 留下的 `#alias { ... }` invocation 时只查这个 registry，
不查普通 import/name resolve。这样 DSL 机制不依赖 Jiang 普通名字解析。

lang dynamic library 是本机缓存产物。缓存 key 至少包含 provider source hash、dependency hash、
当前 `jiangc` 版本、std ABI 版本、lang ABI 版本和 host target。provider dylib 只暴露一个
compiler-private 入口符号，例如 `jiang_lang_entry`；`jiangc` 自动生成低层 ABI wrapper。

compiler-private wrapper scaffold 位于 `src/lang/`：

- `abi.jiang` 定义 ABI version、固定入口符号、request kind、status 和低层 request/response。
- `handle.jiang` 定义已加载 provider dylib 的 opaque handle。
- `registry.jiang` 定义 dependency name 到 provider handle 的 registry。

这层不是公开 std API。它后续才会接入 `dlopen` / `dlsym`、provider dylib cache 和 root `Lang`
签名校验。

## Provider Contract

provider 必须实现统一接口：

```text
Lang.scan(std.jiang.syntax.Input, std.jiang.syntax.Builder.Any&) -> std.jiang.syntax.ScanResult
Lang.parse(std.jiang.syntax.Input, std.jiang.syntax.Builder.Any&) -> std.jiang.syntax.NodeId
```

`Lang` 是 `type = lang` package root module 的 public 导出，并且必须满足
`std.jiang.syntax.Provider`。provider 可以直接在 root file 定义：

```jiang
public struct Lang: std.jiang.syntax.Provider {
    SqlToken[] tokens;

    public std.jiang.syntax.ScanResult scan(
        std.jiang.syntax.Input input,
        std.jiang.syntax.Builder.Any& builder
    ) {
        self.tokens = tokenize_sql(input, builder);
        return std.jiang.syntax.ScanResult.ok(...);
    }

    public std.jiang.syntax.NodeId parse(
        std.jiang.syntax.Input input,
        std.jiang.syntax.Builder.Any& builder
    ) {
        ...
    }
}
```

也可以把实现放在内部模块，再从 root file 重新导出固定入口：

```jiang
import internal = "internal.jiang";

public alias Lang = internal.SqlLang;
```

编译器只查 package root 的 public `Lang`，不查普通 import/name resolve，也不支持一个 lang
package 同时导出多个默认 parser。

`Lang` 必须是可构造类型。每个 `#alias { ... }` invocation 创建一个新的 `Lang` 实例，先调用
`scan`，scan 成功后再调用 `parse`，最后销毁实例。provider 私有 token、parser cache 或中间状态
放在实例字段里，compiler 不理解也不保存 DSL 私有 token。

`Input.delimiter` 表示 host envelope。当前 `#sql { ... }` 使用 `Delimiter.brace`；完整 DSL
文件使用 `Delimiter.none`，表示 provider 从 `body_start` 扫到 source 结束。`Delimiter.paren`
和 `Delimiter.bracket` 为后续语法保留。

`Input.name_span` 表示 invocation 名字的源码范围，例如 `#sql { ... }` 中的 `sql`。它主要用于
provider 诊断；registry 查找和 dependency 校验仍由 host 在调用 provider 前完成。

`Input.entry_kind` 直接使用 `std.jiang.syntax.Root.Kind`，可为 `file`、`top_level_declaration`、
`member_declaration`、`statement`、`expression`、`type_reference` 或 `pattern`。返回 syntax
root node 的 root kind 必须等于 `input.entry_kind`；编译器根据 invocation 所在位置传入 entry kind，
并拒绝不匹配的 tree。也就是说，DSL 输出需要落在 Jiang 当前语法层能表示的完整 syntax
entry 中。

`ScanResult` 返回 `status`、`body_span`、`full_span` 和 `end_offset`。scan 阶段负责判断 DSL
body 的结束位置，并通过 `builder` 报告词法或边界错误；`status = error` 时 compiler 不再调用
`parse`。

provider 需要保留 source span。对于从 DSL 原文生成的节点，应使用 `ScanResult.body_span` 内的
局部 span；对于插值表达式，provider 可以请求 host parser 解析 Jiang expression/type/path，并把解析
结果嵌入返回 tree。

## Excluded From Public Syntax Tree

公开 syntax tree 不包含：

- HIR/MIR/backend IR。
- `DefId`、`TypeId`、内部 token range。
- compiler-only intrinsic block。
- 已废弃或内部兼容用的 compile-if 节点。

如果 provider 需要按 target 或 build option 做条件选择，应直接在 provider 执行期返回最终 AST，
不要把条件编译节点暴露给后续阶段。

## Validation

DSL provider 返回 syntax tree 后，validator/converter 负责：

- 校验 root entry kind。
- 校验所有 `NodeId` / `NodeRange` 有效。
- 直接复用 public `SymbolId` 表示 name 和 literal 原始文本。
- 复用 public `Span` 作为内部 syntax token span。
- 把 public syntax node 转换成 `src/syntax/ast.jiang` 内部 AST。
- 对不支持或不合法的 public syntax tree 结构产生 parser/syntax 诊断。

后续 resolve、type check、MIR 和 backend 不区分这些节点来自 Jiang source 还是 lang provider。
