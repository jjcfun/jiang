# DSL / Lang Package

Jiang 的 DSL 机制是 syntax-stage provider expansion。lexer 看到 `#alias { ... }` 后创建
per-block provider 实例并调用 `scan` 决定 block 边界；parser 后续读到 `raw_block` token 时调用
同一 provider 的 `parse`，再把 public syntax tree 转成 compiler 内部 AST。DSL provider 不生成
Semantic Model、JIL 或 backend IR；它只把外部语法片段翻译成 Jiang 语法层能表示的 syntax tree。

## Goal

源码中的 lang invocation 形如：

```jiang
User user = #sql {
    select * from User where id == \(id)
};
```

`#sql` 中的 `sql` 通常来自当前 package manifest 的 `[dependencies]` alias。目标 dependency
必须是 `type = lang` package，并在 package root 中公开默认入口 `Lang`。编译器也可以提供
builtin provider；当前 builtin inline asm 同时支持短名 `#asm { ... }` 和完整内建路径
`#jiang.asm { ... }`。短名后续允许被用户 provider 覆盖，完整路径用于稳定指向编译器内建 provider。

lang invocation 使用 block 形式，不支持 `#sql(...)`，也不支持源码内声明多个 parser 入口。
当前 parser 已在 expression 和 statement 位置接入 `#alias { ... }`。provider 需要根据
`Input.entry_kind` 返回对应 root kind 的 syntax tree。public syntax tree 已保留 `file`、
declaration、type、pattern 等 root kind，用于后续继续扩展。每个 lang invocation 会创建一个
provider 实例，`scan` 和 `parse` 通过该实例共享 DSL 私有状态。

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
  -> Jiang lexer creates provider and scans raw block
  -> Jiang parser calls provider.parse at raw_block
  -> src/syntax/ast.jiang
  -> resolve/Semantic Model/sema/JIL/backend

DSL source
  -> raw_block token with block id
  -> lang provider
  -> std.jiang.syntax.Tree
  -> validate/convert into src/syntax/ast.jiang
  -> resolve/Semantic Model/sema/JIL/backend
```

也就是说，`#alias { ... }` 不会进入内部 AST 成为占位节点。lexer 只产出 provider path 和
`raw_block`，其中 `raw_block` 携带 compiler-private block id。parser 根据当前位置传入
`Root.Kind.expression` 或 `Root.Kind.statement` 并调用 provider parse。provider 返回
`std.jiang.syntax.Tree` 后，编译器校验并转换成内部 AST，再交给既有 resolve/sema 流程。

长期可以让内部 parser 逐步向 `std.jiang.syntax.Tree` 靠拢，但不要求当前重写 parser 或 resolve。

## Lexer Behavior

Jiang lexer 默认按普通 Jiang token 处理。看到 `#provider.path { ... }` 时，输出：

```text
hash provider_path raw_block
```

host 识别 provider path 和 opening delimiter 后，完整 body 边界由 provider 的 `scan` 决定。
`raw_block` token 携带 compiler-private block id，parser 通过该 id 找到 lexer 阶段创建的
provider、builder 和 scan state。找不到 provider 时 lexer 直接报告 syntax/package 错误，不继续
尝试恢复；provider 内部错误由 provider 自己报告，并负责把 `body_span` 推进到合适的结束位置。

公开 `std.jiang.Tokenizer` 不保存 token text 或 compiler 内部 symbol id。token 的文本由
`Token.span` 回到 `Source.bytes` 按需取得，identifier 的 intern 由调用方的 builder/compiler
上下文负责。`std.jiang.syntax.Tree` 中的 name 和 int/float/char/string literal 原始文本都保存为
public `SymbolId`，由 `Builder.intern_symbol` 创建。identifier 判定使用 ASCII fast path 加 Unicode
`XID_Start` / `XID_Continue`，底层压缩表由 `script/gen_unicode_xid.js` 生成到
`std/jiang/text/generated/xid.jiang`。

## Registry And Dynamic Library

解析 package manifest 后，编译器先加载 dependencies。对 `type = lang` package，编译器将其
编译为 host dynamic library，并把 dependency alias 注册到 lang registry：

```text
dependency alias -> provider handle for package root public Lang
```

lexer 读到 `#alias { ... }` 或 `#jiang.alias { ... }` 时先检查 builtin provider，再查这个 registry，不查普通
import/name resolve。这样 DSL 机制不依赖 Jiang 普通名字解析；如果 provider 不能加载，源码已经
不可解析，编译器直接报告 syntax/package 错误。

lang dynamic library 是本机缓存产物，不承诺跨 `jiangc` 版本复用。provider package 仍以源码发布；
当前 compiler 在 host 上为它生成 wrapper package，再编译成 dynamic library 并加载。

compiler-private wrapper scaffold 位于 `src/lang/`：

- `abi.jiang` 定义 wrapper version 和固定入口符号。
- `wrapper_template.jiang` 生成固定 ABI 入口。
- `dylib_builder.jiang` 负责按需编译 provider wrapper package。
- `runtime.jiang` 负责 `dlopen` / `dlsym`、调用 `scan` / `parse` 和 provider 生命周期。
- `handle.jiang` 定义已加载 provider dylib 的 runtime handle。
- `registry.jiang` 定义 dependency alias 到 lang package id 的 registry，并持有该 dependency edge 的 dylib handle。
- `resolve/store.jiang` 的 `PackageRecord.info` 使用 union 保存 package-specific info；lang package 的 dylib path、
  source path、wrapper version 和 cache 标记保存在 `PackageInfo.lang`。
- `LangBlock` 只保存单个 DSL block 的 `Provider.Any^?` 实例和 syntax builder/input/scan result。

wrapper 当前只导出一个 compiler-private 符号：

```text
jiang_lang_provider_create
```

`jiang_lang_provider_create` 返回 `std.jiang.syntax.Provider.Any^`。compiler 持有 provider
生命周期，在 lexer 阶段创建实例并通过 `Provider.scan` 扫描 DSL block，在 parser 阶段通过
`Provider.parse` 生成 syntax tree。provider 实例由 owner pointer 自动释放，普通用户代码不直接
调用这个符号。

## Package Artifact Cache

Lang provider dylib 和普通 target package 的 package-level 产物共用同一套 cache key/path 抽象：

- `src/artifact/package_fingerprint.jiang` 计算 package 指纹。
- `src/artifact/package_artifact.jiang` 计算 package-level artifact key 和路径。

package fingerprint 汇总：

- package manifest。
- package root 和 file import source closure。
- manifest dependency package 的 manifest/source closure。
- 已经进入 module graph 的 package modules source hash。

package artifact key 在 package fingerprint 外还包含：

- compiler/source artifact version hash。
- wrapper version，lang provider dylib 使用；普通 package object 为 0。
- target cache key。
- compile mode。
- artifact kind。

因此 provider root、provider internal import、provider dependency 源码、manifest、compiler version、
wrapper version、host target 或 mode 变化都会让 provider dylib path 变化并触发重建。target 源码
变化不混入 provider dylib key；target 自身通过普通 package/source/object artifact 失效，并重新
scan/parse DSL block。

缓存命中但 dylib 加载失败、缺少固定符号或 ABI 不匹配是产物损坏或 compiler/wrapper bug，
应报告诊断，不应靠“坏 dylib 自动重建”掩盖。

## Provider Contract

provider 必须实现统一接口：

```text
Lang.scan(std.jiang.syntax.Input, std.jiang.syntax.Builder.Any&!) -> std.jiang.syntax.ScanResult
Lang.parse(std.jiang.syntax.Input, std.jiang.syntax.Builder.Any&!) -> std.jiang.syntax.NodeId
```

`Lang` 是 `type = lang` package root module 的 public 导出，并且必须满足
`std.jiang.syntax.Provider`。provider 可以直接在 root file 定义：

```jiang
public struct Lang: std.jiang.syntax.Provider {
    SqlToken[] tokens;

    public std.jiang.syntax.ScanResult scan(
        std.jiang.syntax.Input input,
        std.jiang.syntax.Builder.Any&! builder
    ) {
        self.tokens = tokenize_sql(input, builder);
        return std.jiang.syntax.ScanResult.ok(...);
    }

    public std.jiang.syntax.NodeId parse(
        std.jiang.syntax.Input input,
        std.jiang.syntax.Builder.Any&! builder
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

`Input.entry_kind` 直接使用 `std.jiang.syntax.Root.Kind`。当前实现会传入 `expression` 或
`statement`；public tree 同时定义了 `file`、`top_level_declaration`、`member_declaration`、
`type_reference` 和 `pattern`，用于后续把 lang invocation 扩展到其他语法位置。返回 syntax root
node 的 root kind 必须等于 `input.entry_kind`，否则 compiler 拒绝该 tree。也就是说，DSL 输出
需要落在 Jiang 当前语法层能表示的完整 syntax entry 中。

`Input.body_start` 是 opening delimiter 后第一个字节的位置。`ScanResult` 只返回 `status` 和
`body_span`。`body_span` 是 raw block 的完整源码范围，包含 opening delimiter 和 closing delimiter；
scan 阶段负责判断 DSL body 的结束位置，并通过 `builder` 报告词法或边界错误。lexer 根据
`body_span` 直接创建 `raw_block` token，不再让公共 tokenizer 重新解析 raw block。
`status = error` 时 compiler 不再调用 `parse`。

provider 需要保留 source span。对于从 DSL 原文生成的节点，应使用 `ScanResult.body_span` 内的
局部 span；对于插值表达式，provider 可以请求 host parser 解析 Jiang expression/type/path，并把解析
结果嵌入返回 tree。

## Excluded From Public Syntax Tree

公开 syntax tree 不包含：

- Semantic Model/JIL/backend IR。
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

后续 resolve、type check、JIL 和 backend 不区分这些节点来自 Jiang source 还是 lang provider。
