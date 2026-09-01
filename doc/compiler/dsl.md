# DSL / Lang Package

Jiang 的 DSL 是 syntax-stage provider expansion。lexer 看到 `#alias { ... }` 后创建该
invocation 独有的 provider 实例，调用 `scan` 确定 block 边界；parser 随后调用同一实例的
`parse`，取得普通 Jiang syntax expansion。生成节点继续进入既有 resolve、type check、JIL 和
backend，不允许 provider 直接生成语义模型或后端 IR。

## 调用形式

```jiang
User user = #sql {
    select * from User where id == \(id)
};
```

`sql` 来自当前 package manifest 的 lang dependency alias：

```ini
[dependencies]
sql = ../sql-lang
```

目标 package 必须声明 `type = lang`，并从 package root public 导出满足
`std.jiang.syntax.Provider` 的 `Lang`。当前只支持 block invocation，不支持 `#sql(...)`，一个 lang
package 只提供一个默认 provider。

编译器内建 inline asm provider 支持 `#asm { ... }` 和 `#jiang.asm { ... }`；内建文档
provider 支持 `#doc` / `#doc(module)` 以及完整路径 `#jiang.doc`。短名允许被用户
dependency alias 覆盖，完整路径始终指向内建 provider。`doc` 的 line/terminated-block header
是 compiler builtin 的固定入口，不扩展普通 lang package 的 invocation grammar。

## Public API

公开边界由以下类型组成：

- `Input`：当前 `Source`、provider 名字 span、body 起点和外层 delimiter。
- `SyntaxContext`：单次 invocation 的 opaque capability；compiler 传入，provider 只在调用期间借用。
- `Token<K>`、`Tokenizer<K>`：可选的通用词法 cursor、token storage、trivia 和 checkpoint。
- `Parser<K>`：token cursor、诊断、恢复和 typed Jiang syntax factory。
- `Expansion`：expression、statement、declarations、type syntax 或 pattern。

compiler AST data、node index、child range、arena 和 factory operation 不属于 public API。provider 通过
`Parser<K>` 的 typed method 创建节点，不能读取、遍历或手工组装 compiler AST。

```jiang
public struct Lang: std.jiang.syntax.Provider {
    public std.jiang.syntax.Expansion parse(
        Self&! self,
        std.jiang.syntax.Input input,
        std.jiang.syntax.ExpansionKind expected,
        std.jiang.syntax.SyntaxContext&! syntax
    ) {
        _ parser! = std.jiang.syntax.default_parser(syntax, input);
        std.jiang.syntax.Expr value = parser.int_literal(input.name_span, "0");
        return .expression(value);
    }
}
```

采用 Jiang 默认 lexical rule 的 provider 只需实现 `parse`。`Provider.scan` 的默认实现处理嵌套
delimiter、string、comment 和 EOF，并把连续 token storage 直接交给 `default_parser`。需要完全
自定义 token 的 provider 可以覆盖 `scan`，在实例字段中保存自己的 `Token<CustomKind>`，再在
`parse` 中构造 `Parser<CustomKind>`。

`expected` 表示 invocation 所在位置。provider 返回的 `Expansion` case 必须一致，否则 compiler
报告 syntax error。当前 parser 已接入 expression、statement、declaration/member、type 和 pattern
位置。

## Source、Token 与诊断

`Span` 是单个 `Source` 内的 byte range，只保存 `start/length`。custom token 文本按 `Source + Span`
取得，不要求 provider 复制文本。默认 Jiang `TokenKind` 是扁平 enum；identifier/literal case 直接携带
compiler symbol store 管理的 identity，custom `K` 完全归 provider 所有。

判断默认 token case 直接写 `token.kind is .ident`；需要 identity 时写
`guard token.kind is .ident(symbol) else { ... }`。`is_identifier()` 和 `is_literal()` 用于类别判断，
不存在第二套 `TokenTag`。

`Tokenizer<K>` 不解释 `K`。provider 决定何时 `emit(kind)`，并用 `finish(eof_kind)` 一次性交出
连续 storage。`checkpoint/rewind` 同时恢复 byte cursor、token storage 和 staged diagnostics；不会
恢复 provider 自己的 mode、nesting 或 side table。

诊断入口是 `Tokenizer.error*`、`Parser.expect*` 和 `Parser.error`。provider 通常不提供 diagnostic
code；compiler 根据内建 diagnostic kind 生成 stable code。raw message 是可选补充，不进入 Jiang
message catalog。

identifier 判定使用 ASCII fast path 和 Unicode `XID_Start` / `XID_Continue`。压缩表由
`script/gen_unicode_xid.js` 生成到 `src/std/jiang/text/generated/xid.jiang`。

## Compiler Boundary

```text
source
  -> compiler lexer scans provider block
  -> Provider.scan(Input, SyntaxContext)
  -> raw_block token
  -> Provider.parse(Input, ExpansionKind, SyntaxContext)
  -> typed Parser factory writes current AstUnit
  -> resolve / sema / JIL / backend
```

普通 Jiang lexer/parser 使用 compiler-private 静态调用路径。provider 的 typed factory 通过固定 ABI
callback 写同一个 `AstUnit`；两条路径复用同一 token、span、diagnostic 和 AST 语义，但普通热路径
不经过 `Provider.Any` 或 callback dispatch。builtin `asm` 和 `doc` 共用 tagged builtin dispatch 与
LangBlock 生命周期，但不经过 dynamic provider ABI。

每个 invocation 持有固定地址的 compiler-owned state。`scan` 期间只临时绑定 `CompilerStore`；
`parse` 期间再临时绑定目标 `AstUnit`。调用返回后立即解除绑定，因此 `SyntaxContext` 不能
逃逸，也不形成第二份 lifetime provenance。

## Registry 与动态库

编译器为 `type = lang` dependency 构建 host dynamic library，并把 dependency alias 注册到 lang
registry：

```text
dependency alias -> package id -> provider dylib -> Provider.Any
```

compiler-private wrapper 位于 `src/lang/`：

- `abi.jiang`：wrapper version 和固定入口符号。
- `wrapper_template.jiang`：生成 host wrapper package。
- `dylib_builder.jiang`：按需构建 provider dylib。
- `runtime.jiang`：加载 dylib 并调用 `scan/parse`。
- `registry.jiang`：dependency alias 与 dylib 生命周期。
- `block.jiang`：单个 invocation 的 provider、context、input 和 scan state。

wrapper 只导出 `jiang_lang_provider_create`，返回 `std.jiang.syntax.Provider.Any^`。provider dylib 是
本机 compiler cache 产物，不承诺跨 compiler ABI 版本复用；provider 源码仍是发布格式。

## Artifact Cache

lang provider dylib 与普通 package artifact 共用 package fingerprint 和 target cache key。provider
manifest、root/source closure、dependency source、compiler version、wrapper version、host target 或
mode 改变都会使 dylib key 失效。

cache 命中后若 dylib 无法加载、缺少固定符号或 ABI version 不匹配，应报告明确诊断；不能靠
静默重建掩盖损坏产物或 wrapper bug。

## 限制

- provider 不能生成 Semantic Model、JIL 或 backend IR。
- provider 不能返回 source string 要求 compiler 再解析。
- provider 可以维护私有 CST/AST，但不能访问 compiler AST data。
- builtin intrinsic、parser recovery node 和 compiler compatibility node 不进入 public factory schema。
- `SyntaxContext` 的 raw callback 模块不从 `std.jiang.syntax` package root 导出。
