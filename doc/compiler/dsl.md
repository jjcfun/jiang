# DSL / Lang Package

Jiang 的 DSL 由 syntax-stage provider 生成 AST。lexer 看到 `#alias { ... }` 后创建该
invocation 独有的 provider 实例，调用 `scan` 确定 block 边界；parser 随后调用同一实例的
`parse`，取得普通 Jiang opaque `Ast` 根节点。生成节点继续进入既有 resolve、type check、JIL 和
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

目标 package 必须声明 `type = lang`，并在 package root 用 `@entry(lang)` 标记一个实现
`std.jiang.syntax.Provider`、可无参数构造的具体类型；类型名称任意，可以保持私有。当前只支持 block invocation，不支持 `#sql(...)`，一个 lang
package 只提供一个默认 provider。

编译器内建 inline asm provider 支持 `#asm { ... }` 和 `#jiang.asm { ... }`；内建文档
provider 支持 `#doc` / `#doc(module)` 以及完整路径 `#jiang.doc`。短名允许被用户
dependency alias 覆盖，完整路径始终指向内建 provider。`doc` 的 line/terminated-block header
由 doc provider 自己扫描；普通 lang package 当前仍只使用 block invocation envelope。

## Public API

公开边界由以下类型组成：

- `Input`：当前 `Source`、provider 名字 span、body 起点和外层 delimiter。
- `Session`：语言上下文初始化时的编译会话能力，提供宿主 symbol intern。
- `SyntaxContext<L>`：单次调用借用的上下文，`lang` 引用共享语言对象，`syntax` 引用块级回调。
- `Token<K>`、`Tokenizer<K>`：可选的通用词法 cursor、token storage、trivia 和 checkpoint。
- `Parser<K>`：token cursor、诊断、恢复和 typed Jiang syntax factory。
- `Ast`：单次 invocation 的 opaque AST 根句柄。

compiler AST data、node index、child range、arena 和 factory operation 不属于 public API。provider 通过
`Parser<K>` 的 typed method 创建节点，不能读取、遍历或手工组装 compiler AST。

```jiang
@entry(lang)
struct SqlProvider: std.jiang.syntax.Provider {
    public std.jiang.syntax.Ast parse(
        Self&! self,
        std.jiang.syntax.Input input,
        std.jiang.syntax.SyntaxContext<std.jiang.syntax.EmptyLangContext>& syntax
    ) {
        _ parser! = std.jiang.syntax.default_parser(syntax.syntax, input);
        std.jiang.syntax.Expr value = parser.int_literal(input.name_span, "0");
        return parser.ast(value);
    }
}
```

采用 Jiang 默认 lexical rule 的 provider 只需实现 `parse`。`Provider.scan` 的默认实现处理嵌套
delimiter、string、comment 和 EOF，并把连续 token storage 直接交给 `default_parser`。需要完全
自定义 token 的 provider 可以覆盖 `scan`，在实例字段中保存自己的 `Token<CustomKind>`，再在
`parse` 中构造 `Parser<CustomKind>`。

factory 创建节点时直接写入 compiler-owned `AstUnit`。`parse` 返回的 `Ast` 只标识本次生成结果的根节点；
compiler 根据 invocation 位置验证其实际语法角色。当前 parser 已接入 expression、statement、
declaration/member、type、pattern 和 attribute 位置。

Provider 可通过 `parser.provider_import(span, name)` 构造指向自身包入口的私有导入声明。
将声明加入返回的声明集合后，其他 factory 表达式通过该名字引用 Provider 导出的类型和函数；
编译器按实际 Provider 包定位入口，不要求使用方采用固定依赖别名。导入后的可见性、类型身份和
依赖关系遵循普通包导入规则。该能力只构造 AST，不在 `parse` 中触发语义检查或 metadata 求值。

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

## 语言共享状态

Provider 的关联类型 `LangContext` 由静态 `create_context(Session&)` 创建，宿主按语言的实际
产物身份缓存到编译周期结束。不同文件及不同别名共享同一个对象，各块 Provider 独立创建。
`SyntaxContext.lang` 为强类型只读引用，关键词 ID 在这里保存；它与默认 token 使用同一宿主符号表。
无共享数据时默认使用 `EmptyLangContext`，可省略关联类型和初始化方法。
默认初始化方法只适用于空上下文；自定义 `LangContext` 需要提供自己的工厂。

上下文工厂不得捕获 Session 借用。块先于语言对象销毁，语言对象先于动态库关闭；这保证
块内借用及动态析构代码的有效性。新的编译周期会重新创建语言上下文。

## Compiler Boundary

```text
source
  -> compiler lexer scans provider block
  -> Provider.scan(Input, SyntaxContext)
  -> raw_block token
  -> Provider.parse(Input, SyntaxContext) -> opaque Ast root
  -> compiler validates the root identity and syntax role
  -> resolve / sema / JIL / backend
```

普通 Jiang lexer/parser 使用 compiler-private 静态调用路径。provider 的 typed factory 通过固定 ABI
callback 写同一个 `AstUnit`；两条路径复用同一 token、span、diagnostic 和 AST 语义，但普通热路径
不经过 `Invocation.Any` 或 callback dispatch。builtin `asm`、`doc` 与第三方 lang 都通过统一的
`Invocation.Any` invocation 路径。

每个 invocation 持有固定地址的 compiler-owned state。`scan` 期间只临时绑定 `CompilerStore`；
`parse` 期间再临时绑定目标 `AstUnit`。调用返回后立即解除绑定，因此 `SyntaxContext` 不能
逃逸，也不形成第二份 lifetime provenance。

## Registry 与动态库

编译器为 `type = lang` dependency 构建 host dynamic library，并把 dependency alias 注册到 lang
registry：

```text
language alias -> registered dependency -> package id -> provider dylib -> Language factory -> Invocation.Any
```

宿主层负责 Provider 的入口适配、按需构建、加载和生命周期管理；语法调用通过统一的 Provider 契约完成。
Provider 在宿主目标上编译，其缓存与编译器 ABI 绑定；发布形式为源码，不承诺动态库跨编译器版本复用。
Provider root 可以使用其他 Lang，也可以是独立 Lang 源文件；宿主入口适配保持原模块的可见性和来源。

## 独立源文件扩展名

Lang package 可以声明独立源文件的扩展名；未声明时使用引入该 Provider 的语言别名：

```ini
[lang]
extensions = schema, sch
```

使用方通过语言别名引用注册依赖，并可覆盖整组扩展名；覆盖只作用于当前包：

```ini
[dependencies]
tools = ../schema_lang

[lang.schema]
package = tools
extensions = model, schema
```

扩展名使用逗号分隔，不带前导点。空项、重复配置和多个 Provider 的有效映射冲突均报错；
`.jiang` 保留原生解析。`#schema` 使用语言别名，普通 `import tools` 使用依赖别名。
省略 package 时沿用同名依赖；未显式配置的 Lang 依赖继续使用其依赖名作为默认语言名。

普通文件 import 和生成输入都使用文件所属包的有效映射，例如 `import "models.schema"`。
整份文件传给同一个 Provider：`Input.delimiter = .none`、`body_start = 0`，`Source` 保留原始文件内容和身份。
`scan` 必须覆盖完整文件；`parse` 返回单个声明或 `parser.declarations(...)` 组合的声明集合，允许空集合。
表达式结果不能作为独立文件的模块根。返回的定义继续参与普通语义检查，诊断位置仍对应原文件。
扫描范围不完整时停止该 invocation；依赖源码请求构建链中尚未准备完成的 Provider 时报告加载循环。

映射配置与 Provider 实现闭包属于解析依赖。普通源码编辑按源码内容失效，Provider 实现变化则同时
使其生成的语法结果失效；这些依赖复用编译器的 source/package artifact 管理。

## Artifact Cache

lang provider dylib 与普通 package artifact 共用 package fingerprint 和 target cache key。provider
manifest、实际 source/import 闭包、compiler build/version、LLVM version、linker 路径、wrapper version、
host target 或 mode 改变都会使 dylib key 失效。依赖闭包复用普通导入求值和前端检查，包含导入表达式
与其他 Lang 源文件；未选中的导入分支不加载。Provider 始终按宿主目标准备，其缓存位置不随嵌套
构建的层数或用户源码的解析缓存目录改变。

cache 命中后若 dylib 无法加载、缺少固定符号或 ABI version 不匹配，应报告明确诊断；不能靠
静默重建掩盖损坏产物或 wrapper bug。

## 限制

- provider 不能生成 Semantic Model、JIL 或 backend IR。
- provider 不能返回 source string 要求 compiler 再解析。
- provider 可以维护私有 CST/AST，但不能访问 compiler AST data。
- builtin intrinsic、parser recovery node 和 compiler compatibility node 不进入 public factory schema。
- `SyntaxContext` 的 raw callback 模块不从 `std.jiang.syntax` package root 导出。
