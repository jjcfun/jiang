# Jiang 语言设计草案

本文档记录 Jiang 语言本身的设计，不记录编译器源码目录结构和实现细节。编译器工程约定见
`doc/architecture.md`。

当前 release/0.5.3 在既有泛型/trait、JIL/backend、所有权、Task 与 Domain/Executor
基础上，用 payload enum 统一表示普通代数数据类型，并保持既有 layout、lifetime、
pattern 和 drop 规则。compiler service 保留为后续工具链的基础，LSP 与 Linux no-libc
仍属于后续版本。
本文档描述当前分支希望稳定下来的语言规则；
未定设计必须显式标注，避免 parser、resolve、sema 在隐含假设上继续扩展。

## 设计约定

本文档记录目标语义与架构决策，不追踪实现完成度或验证进度。标为“未定”的内容仍需讨论，
不能作为语义分析的既定前提；开发任务及实现差异由对应版本 TODO 维护。

## 设计目标

Jiang 是面向系统编程的语言，目标是在低层控制能力、工程可维护性和高层抽象之间取得平衡。

核心方向：

- 明确的值语义、指针语义和可变性语义。
- 可读的泛型和 trait 约束。
- AST 保留源码结构，语义信息进入 resolve/sema/Semantic Model。
- 字符串、数组、slice、指针等系统级类型有直接语法支持。
- 通过 lang package 支持场景化语法扩展；扩展必须回到 Jiang syntax tree，再进入普通语义检查。

## 命名规范

- 类型名使用 `PascalCase`。
- 函数名、变量名、字段名、枚举成员、模块别名使用 `snake_case`。
- 基本类型名不是关键字，词法阶段按 `ident` 处理，后续 resolver 解释为内建类型。

示例：

```jiang
struct SourceFile {
    UInt8[]& file_path;
    Int start_offset;
}

UInt8[]& read_source(UInt8[]& file_path) {
    return file_path;
}

alias store = import "token_store.jiang";
```

## 词法

Token 只表示词法事实，不承载语义类型。

已确定：

- identifier、关键字和基本类型名在 token 层统一为 `ident`；后续由 `SymbolStore`
  和 resolve/sema 解释。
- identifier 的 ASCII fast path 允许 ASCII 字母或 `_` 作为首字符，ASCII 字母、数字或 `_`
  作为后续字符。非 ASCII UTF-8 字符按 Unicode `XID_Start` / `XID_Continue` 判定；
  数字、组合标记等只能在 `XID_Continue` 允许的位置出现。Unicode punctuation 不属于
  identifier，lexer 应产生 `unicode_punctuation` 诊断。
- 保留关键字包括 `new`、`where`、`life`、`import`、`public`、`const`、`alias`、
  `extern`、`return`、`if`、`else`、`guard`、`while`、`for`、`in`、`is`、`enum`、
  `struct`、`trait`、`extend`、`associated`、`init`、`deinit`、`comptime`、
  `switch`、`try`、`catch`、`await`、`break`、`continue`、`defer`、`do`、`throw`、
  `true`、`false`、`null`。`ref`、`unsafe`、`async`、`sync` 是上下文关键字；
  `self`、`Self` 是特殊名字。
- 字符字面量使用单引号，例如 `'a'`。
- 字符串字面量使用双引号，文本按 UTF-8 字节序列处理。
- `Span` 使用字节偏移和字节长度；line/column 在诊断阶段计算。

`Self` 是类型位置的特殊名字。`self` 是类型内部实例函数、`init` 和 `deinit` 的显式参数名，
表示当前 receiver 或初始化目标。没有 `self` 参数的类型内部函数是类型函数。

## Lang Package / 自定义语法

Jiang 源码中的 lang invocation 使用 block 形式：

```jiang
User user = #sql {
    select * from User where id == \(id)
};
```

`#sql` 中的 `sql` 不是普通名字，也不通过 `import` / resolve 查找。它来自当前包配置的语言别名；
未单独配置 `lang` 别名时，默认使用 Lang 依赖别名：

```jiang
#package {
    name = "app";
    root = "main.jiang";
    dependencies { sql = "../sql-lang"; }
}
```

被调用 dependency 必须声明为 lang package：

```jiang
#package {
    name = "sql_lang";
    root = "lang.jiang";
    type = .lang;
}
```

lang package root 用 `@entry(lang)` 标记一个可无参数构造、满足 `std.jiang.syntax.Provider` 的具体类型。
入口类型名称任意，可保持私有；标记不能附着 alias／重导出，也不从导入文件继承。
编译器在 host 上把该 package 编译成 dynamic library，lexer/parser 在 syntax 阶段调用 provider。
Provider 通过关联类型 `LangContext` 和静态 `create_context(Session&)` 创建语言级状态。
无状态 Provider 默认使用 `EmptyLangContext`，可以省略关联类型与初始化方法；
自定义上下文应提供自己的 `create_context`。
Session 提供宿主符号表的 `intern`；该状态按实际语言身份在一个编译周期内共享，块实例独立创建。
`scan`、`parse` 接收 `SyntaxContext<LangContext>&`，通过只读 `lang` 引用访问状态，
通过 `syntax` 访问块级回调。共享对象不捕获 Session 借用，释放顺序为块、语言状态、动态库。

语言层规则（下列 invocation 限制适用于用户 lang package；内建 `#doc` provider 自己扫描
line/block header）：

- Jiang 源码内使用 block invocation：`#alias { ... }`；独立 Lang 文件按扩展名选择 Provider。
- 当前不支持 `#alias(...)`。
- 当前不支持源码内声明多个 parser 入口。
- 一个 lang package 只提供一个默认 provider。
- provider 返回 opaque `Ast` 根句柄。compiler 根据 invocation 位置验证根节点的实际语法角色；当前支持
  expression、statement、declaration/member、type、pattern 和 attribute 位置。
- provider 不能直接生成 Semantic Model、JIL、后端 IR，也不能绕过普通 resolve/type check。
- DSL 生成的节点和普通 Jiang 源码节点进入同一套 resolve/sema/JIL/backend。
- Provider 的 `import_expression()` 接收包名或文件路径字面量，构造返回 namespace 的表达式；
  可用于条件分支和 `alias_declaration()` 的表达式目标，构造时不加载导入模块。
- Provider 的 `declarations()` 只组合声明，不引入作用域或执行。`comptime_block()` 接收局部语句
  和可选尾表达式，遵循普通 comptime 的词法作用域与求值规则；条件使用普通 `if_expression()` 组合，
  不提供向外发布分支声明的专用 comptime-if 机制。

公共解析结果通过 `Ast.role()` 区分 Expr、Stmt、Decl、Type、Pattern、Attribute、Member、
声明序列和成员序列；`as_expr()`、`as_decl()`、`as_member()` 等类型化取出在类别不匹配时返回 null。
`declarations()`／`members()` 即使只有一个元素，也保留序列类别；空序列同样保留声明／成员的区别。
调用方通过 `parser.len(sequence)` 和 `parser.at(sequence, index)` 读取序列，不能把序列当单个声明。
顶层只接收声明类结果，成员位置只接收成员类结果；实际节点和序列内容由宿主验证。

默认扫描支持嵌套 RawBlock。默认 parser 的 `parse_raw_block()` 消费当前位置的块，返回 `Ast?`；
失败保留诊断并返回 null，非 RawBlock 不消费。表达式／类型等 fragment 解析共享当前块存储。
Attribute 结果由语言作者按自身语法暂存并通过 `with_attributes` 附着；不根据节点创建顺序猜测目标，
也不要求语言作者识别 #doc 的名字。`doc_attribute` 仅负责构造 Markdown Attribute。

自定义 `Tokenizer<K>` 在 `begin_token()` 后于 `#` 位置调用 `scan_raw_block()`，取得默认 RawBlock
token，再将块身份映射到自己的 K 并 `emit`。扫描不得越过 tokenizer bounds；失败保留诊断。
自定义 `Parser<K>` 可将保存的块身份和 span 重建为默认 RawBlock token，调用 `parse_raw_block(raw)`；
该重载不移动自定义 cursor，消费由调用方负责。块身份只在原编译调用的源码／块存储内有效。

独立 Lang 文件采用普通文件 import，也可直接作为生成输入。Provider 在 `#package` 中用
`lang { extensions = ["schema", "sch"]; }` 声明自身扩展名；使用方通过
`lang sql { package = sql; extensions = ["model"]; }` 整组覆盖。两边均未配置时默认使用语言别名。
映射只作用于文件所属包，`.jiang` 保留原生语法；无映射或映射冲突均报错，不猜测解析语言。
文件 invocation 的 `Input.delimiter` 为 `.none`，`body_start` 为 0，扫描范围必须覆盖整个文件，
结果必须是声明或声明集合。源码身份及位置保持原文件，不生成带有虚构前缀的包装源码。

provider 有两个阶段：

```text
scan(input, syntax) -> ScanResult
parse(input, syntax) -> Ast
```

lexer 看到 `#alias {` 后创建 per-block provider 实例并调用 `scan`。`scan` 负责判断 DSL block
边界，并可把私有 token/cache 保存在 provider 实例字段中。parser 后续读到 `raw_block` token 时
调用同一实例的 `parse`。provider 通过 `Parser<K>` 的 typed factory 直接把普通 Jiang syntax 写入
compiler-owned `AstUnit`，最后只返回根节点 `Ast`；不公开 compiler AST data，也不建立 mirror tree。
编译器内建 provider 包括 inline asm 和
API 文档：`#asm { ... }` / `#jiang.asm { ... }` 生成 inline asm；`#doc` /
`#jiang.doc` 生成声明 attribute，`#doc(module)` 指向 module semantic owner。短名
允许被用户的 lang dependency alias 覆盖，`#jiang.*` 始终选择 compiler builtin。

API 文档保存规范化的 Markdown，模块和声明分别拥有自身文档，具体实例沿用原始声明的文档。
反射支持读取自身文档及枚举同包范围内带文档的目标，不进入函数体或沿类型引用递归，跨包查询遵守公开边界。
文档不是类型或签名身份的一部分；实际读取文档的编译期计算及生成任务需要依赖其内容。
未准备的文档不能视为没有文档。页面组织、渲染和二进制分发规则不属于文档附着本身的职责。

这种机制的目标不是把 Jiang 变成文本宏语言，而是让不同领域可以使用更适合的表面语法，例如
SQL、shader 或 UI DSL，同时保持后续类型检查、借用检查、单态化和 backend 仍由 Jiang 编译器统一处理。

## 字面量

已确定字面量：

- integer literal
- float literal
- char literal
- string literal
- bool literal: `true` / `false`
- null literal: `null`

字符字面量用于表示单个字符。`UInt8 byte = 'a';` 这类初始化由 expected type 约束；非 ASCII 字符初始化 `UInt8` 应编译失败。

字符串字面量是 UTF-8 字节序列。字符串字面量的默认类型为 `UInt8[:0]&`；backing storage 会自动追加末尾 `0`，但该 sentinel 不计入 length。字符串字面量可用于 `UInt8[_]` / `UInt8[]&` / `UInt8[:0]&`，也可在 expected type 下转换为 `UInt8*` 或 `UInt8[N:0]`。

`UInt8*` 可以接收字符串字面量，用于 C ABI 的 NUL 结尾地址；类型本身不记录 sentinel。需要保留 length 和 sentinel 语义时使用 `UInt8[:0]&`。

## 类型系统

Jiang 类型语法遵循从左往右、从里到外的原则。类型后缀越靠右，包裹范围越大。

基础类型名例如 `Int`、`UInt8`、`Bool`、`Float`、`Double`、`Char` 都是普通名字，由 resolver 解析为内建类型。

目标规则：除 `Tuple` 和 `Fn` 暂时排除外，所有类型都拥有自己的 namespace。这里的 namespace
表示该类型可承载静态函数、实例方法、constructor、associated type、trait implementation
和 extension 成员；它不要求该类型一定是 `struct` 内存模型。`Int`、`Bool`、array、slice、
sentinel slice、pointer、reference、box、optional、result、user struct、enum、trait
self type 等都应统一作为 type namespace provider 参与 `Type.member` 和 `value.method`
lookup。

内建后缀类型也遵循同一条规则：`UInt8[]&` 的成员 lookup 会落到 borrowed slice
类型的 namespace，`Int[4]` 会落到 array 类型的 namespace，
`Int` 会落到 builtin integer type 的 namespace。backend 或 JIL 可以继续把 builtin 类型
lowering 成高效 ABI 表示，但 resolve/sema 层不应该因为类型是不是 nominal struct 而拆出
不同的成员查找路径。

已确定类型语法：

- `_`：推断类型。
- `Void`：Void type；它唯一的值写作 `()`。
- `T` / `foo.Bar`：命名类型。
- `T<A, B>`：泛型类型参数。
- `(A, B)`：tuple type。
- `T&!`：唯一可变引用。
- `T?`：optional 类型。
- `T^`：owning pointer；它不是 C 风格 raw pointer。
- `T&`：非 owning 引用，不表达释放职责。
- `T[]&`：borrowed slice view，layout 是 `{ data, length }`，不表达所有权。裸 `T[]` 是
  unsized array type，不能作为普通 value。
- `T[:S]&`：borrowed sentinel slice view，
  layout 与 `T[]&` 一样是 `{ data, length }`，并额外保证 `data[length] == S`。
  裸 `T[:S]` 是带 sentinel 的 unsized array type，不能作为普通 value。
- `T*`：raw pointer，供 FFI / ABI / 低层能力使用；可在 `unsafe` 中按下标读取，`T*!`
  还可在 `unsafe` 中按下标写入。
- `T[N]`：定长数组。
- `T[N:0]`：sentinel 定长数组。逻辑长度为 `N`，实际 storage 为 `N + 1` 个元素，末尾元素保存 sentinel；`T[N:0]$.size()` 包含 sentinel storage。
- `T[_]`：数组长度由初始化器推断。
- `T@E`：errorable result，只能出现在函数、方法和 callable 类型的返回位。
- 错误类型 `E` 顶层不能带 `?` 或 `!`。
- `T@E` 两侧不允许空白，避免与前缀 attribute 和普通表达式混淆。

内建后缀类型语法不经过普通名字解析，compiler-owned constructor 名称也不进入用户可见
namespace。用户仍可声明同名 nominal type，但不会影响 `T?`、`T[N]`、`T[]&`、`T[:0]&`、
`T^`、`T&`、`T*` 等表面语法。后缀类型仍会参与对应内部 canonical owner 的
extension/member lookup。

sentinel value 使用 `S: const T` 语义，`S` 的类型来自元素类型 `T`。整数 literal 会根据
元素类型转换；非整数 constable 类型也可以作为 sentinel，只要元素类型是 Copyable。
例如 `UInt8[5:0]`、`Bool[1:true]`、`Char[3:'\0']` 和 enum/struct const sentinel 都是同一套规则。

示例：

```jiang
Int[2][3] matrix;
Int?[] values;
UInt8* raw;
Int@Error result;
Int?@Error maybe_result;
```

## 可变性

当前版本只支持两种类型级可变能力：

- `T&!`：唯一可变引用；在其存活期间，借用的 place 不能再被共享借用、可变借用或直接访问。
- `T*!`：可写 raw pointer；在 `unsafe` 中允许下标写入。

其他类型不支持 `!` 后缀。`Int!`、`T^!`、`T[]!`、`T?!` 和 `T!&` 都必须报错。
如果普通变量、参数、全局或字段需要可写，应把 `!` 放在绑定名上：

```jiang
Int value! = 1;
value = 2;

Int& shared = value$.ref();
Int&! borrowed = value$.mut_ref();
Int*! pointer = unsafe { value$.mut_ptr() };
```

绑定名后的 `!` 是 place metadata，不进入 `TypeId` 或函数签名。因此 `foo(Int value!)`
和 `foo(Int value)` 的签名相同；前者只允许函数体内重新赋值参数槽。相反，
`foo(Int&! value)` 的唯一可变引用能力属于参数类型，必须进入签名。
语言不提供额外的 `unique` 参数修饰符；`unique` 是普通标识符。

字段同样使用绑定名表达存储可写性：

```jiang
struct User {
    Int id;
    Int age!;
}
```

共享引用 `T&` 不授予写能力；通过共享引用也不能把字段或元素升级成 `T&!`。
需要修改借用目标时，必须从可写 place 创建 `ref!` / `$.mut_ref()`，并由 borrow checker
保证该可变引用唯一。raw pointer 不参与引用别名证明，写入仍受 `unsafe` 约束。

## 指针、引用、数组和 Slice

Jiang 对共享引用和唯一可变引用执行静态 borrow check，并同时检查 ownership、lifetime 和 drop safety。
这里先固定 pointer/reference 的目标语义：

- `T^`：owning pointer；它不是 C 风格 raw pointer。
- `T&`：shared non-owning reference，不表达释放职责，也不提供可变能力。
- `T&!`：唯一可变的 non-owning reference；存活期间排斥指向同一 place 的其他共享或可变引用。
- `T*`：裸指针，只用于 FFI / ABI / 低层 capability 场景；可在 `unsafe` 中下标读取，
  `T*!` 还可下标写入。
- `T[]`：unsized array type，必须通过 `T[]&` 形成 borrowed slice view，或通过 `T[]^`
  形成 owning handle。
- `T[:0]`：带 sentinel 的 unsized array type，必须通过 `T[:0]&` 形成 sentinel slice view，或通过 `T[:0]^` 形成 owning handle；sentinel view 保证 `data[length] == 0`。

旧的 `T[*]` / `T[*:S]` many pointer 类型已经移除。低层连续内存地址统一使用 `T*` / `T*!`；
需要 length 或 sentinel 保证时使用 slice reference。raw pointer 类型本身不携带这些元数据。

`Void*` / `Void*!` 只用于擦除类型后的传递、比较和转换，不能 `$.get()`、`$.set()` 或下标访问；
访问前必须先转换成具有具体元素类型的 raw pointer。

数组字面量的元素是普通 expression，并由 expected type 约束元素类型与长度。`new [expr, ...]`
可以直接按 expected `T[N]^`、`T[]^` 或 `T[:S]^` 在最终 owning storage 中初始化。借用 expected type
也会参与字面量类型检查，例如 `T[]& value = [...]` 会先完成元素检查，但随后因为引用了
临时 storage 而被 ownership/lifetime 检查拒绝。

标准库 `Vector<T>.slice()` 返回借用 view；`Vector<T>.into_slice(Self self)` 消耗 receiver，
并把 initialized 区间转移为 owning `T[]^`。

`T&`、`T&!` 和 `T[]&` 可以作为字段；它们不拥有目标对象，字段析构时不会释放目标对象。
存储引用字段时，目标对象的生命周期必须覆盖包含该字段的值；`T&!` 字段还会持续持有
其来源 place 的唯一借用。裸 `T[]` 是 unsized array type，
不能作为普通字段类型。

函数签名直接用 `T&!` 声明调用点必须提供唯一可变访问能力：

```jiang
Void swap(Int&! left, Int&! right) {
}
```

该能力属于类型并进入函数签名；`swap(Int&! left, Int&! right)` 不能把同一个 place 同时传给
两个参数。唯一可变能力完全由 `T&!` 类型表达，不存在额外的参数修饰关键字。

唯一可变引用会在单个 borrow-check 域内排斥重叠别名，但这不等于自动证明任意并发程序
没有 data race。跨 domain 传递受 domain borrow 规则约束；跨线程共享可变状态仍必须通过
标准库的 mutex、atomic 或其他显式同步协议表达。Channel 与 rwlock 尚未进入 0.5.3 公共 API。

`^` 和 `&` 会创建新的 language handle 外层。一个完整源码类型中最多只能出现一个 `^`
或 `&` 外层；源码中不允许写出 `^^`、`&&`、`^&` 或 `&^`。`T*` 和 `T[]&`
是 ABI/低层指针视图，可以按 C ABI 需要叠加。归一化阶段如果因为泛型替换得到重复同类
language handle，可以合并；如果得到 `^` 与 `&` 混合的 handle 层，必须保留错误状态并报告
diagnostic。

`T^` 在普通值上下文中默认自动解引用。`T&` 和 `T*` 不默认解引用，必须通过 `$.get()` 显式读取：

```jiang
Int^ foo();

Int^ a = foo();      // expected type 是 Int^，保留 owning pointer
_ b = foo();         // 推导上下文保留自然类型，b: Int^
Int c = foo();       // expected type 是 Int，自动解引用
_ d = foo() + 123;   // 算术上下文，自动解引用为 Int

Int&! ref = value$.mut_ref();
Int copied_value = ref$.get();
```

Jiang 没有前缀手动解引用语法，`*foo()` 这类写法不成立。需要显式取出 `T^` / `T&` / `T*`
指向的值时，使用隐式操作层的 `value$.get()`；需要通过 pointer/reference 写入目标对象时使用
`value$.set(new_value)`。写入 reference 需要 `T&!`，写入 raw pointer 需要 `T*!`。

```jiang
Int& ref = value$.ref();
Int copied = ref$.get();
unsafe {
    Int*! ptr = value$.mut_ptr();
    ptr$.set(42);
}
```

`$` 会阻止自动解引用，并进入隐式操作层。`$.ref()` 和 `$.ptr()` 分别投影到语言引用和裸指针：

```jiang
Int^ value = new Int(42);

_ ref = value$.ref(); // ref: Int&
unsafe {
    _ ptr = value$.ptr(); // ptr: Int*
    value$.dealloc();
}

Int sum = value + 100;        // 允许：value 自动解引用为 Int
Int bad = value$.ref() + 100; // 错误：Int& 不会在结果位置继续自动解引用
```

`T*` raw pointer 不参与默认自动解引用。它表示可在 `unsafe` 中按元素索引的地址：

```jiang
Int* ptr;
Int*! writable;

unsafe {
    _ raw = ptr;                  // raw: Int*
    Int value = ptr[0];
    writable[1] = 42;

    _ item_ref = ptr[1]$.ref();   // item_ref: Int&
    _ item_ptr = ptr[1]$.ptr();   // item_ptr: Int*
}
```

数组长度是类型的一部分；slice 长度是运行时值。
数组类型中的具名长度按普通词法绑定解析，可引用 const 泛型参数或已求值的 const，
结果必须是非负整数；不会绕过局部遮蔽去寻找外层同名常量。

## 所有权、implicit copy 和析构

当前 borrow check 是 JIL 后的必经阶段，用于检查 move/use-after-move、引用逃逸、drop safety
以及 `T&!` 的唯一可变借用。它按重叠 place 和引用的后续使用检查 alias 冲突；raw pointer
不参与这项别名证明，其访问由 `unsafe` 边界约束。

所有权类型：

- `T^` 是 owning pointer。它拥有指向的堆对象，并参与自动析构。
- `T&` 是非 owning 引用。它不拥有资源，不参与自动析构。
- `T*` / `T*!` 是低层指针；`T[]&` 是 slice reference。它们不表达所有权，不参与自动析构。裸 `T[]` 是 unsized array type，不是可独立存放的 reference value。

自动析构规则：

- 是否需要 runtime drop 由类型的 ownership、字段和自定义 `deinit` 决定，不再由 `Movable`
  标记代替。`!Movable` 值仍会在原 place 的生命周期结束时正常析构。
- `T^` 是 owning pointer，drop 时先 drop pointee，再释放其堆存储。
- nominal、tuple、array、optional、errorable 作为值拥有自己的字段、元素或 payload；
  如果内部类型需要 drop，外层按结构递归 drop。
- `T&`、`T*`、`T*!` 本身不拥有目标对象，不会因为 element type 是 `Movable`
  就自动 drop。
- `T[]&` 本身不拥有整段 buffer，drop slice reference 时不 drop 全部元素；但 `slice[i]`
  是一个已初始化 `T` place，覆盖该元素时按 `T` 的 drop 规则处理旧值。
- 经过 `T*!` 得到的 place 是低层裸指针派生 place，写入时是 raw write，
  不隐式 drop 旧值。
- 如果 nominal 有自定义 `deinit`，drop 该 nominal 时先执行自定义 `deinit`，再递归 drop 字段；
  该类型不能实现 `Copyable`。

示例：

```jiang
struct Node {
    Node^ next;      // 自动析构
    UInt8* bytes;  // 不自动析构
    Int length;

    deinit() {
        unsafe {
            bytes$.dealloc(); // raw pointer 不会自动释放，必须显式管理
        }
    }
}
```

析构顺序：

- 同一个 nominal 内，自动析构的字段按字段声明逆序执行。
- 自定义 `deinit` 发生在自动递归字段析构之前。
- 已经被显式 move 的局部变量不再参与析构。

implicit copy / Movable 规则：

- `Movable` 是默认 auto trait，表示初始化完成后值可以改变 storage address。nominal 可以用
  `!Movable` 显式退出；包含 `!Movable` 字段或 payload 的聚合也不可移动。
- `!Movable` 值不能按值传参、返回、赋给新 place、捕获、`$.move()` 或 `$.forget()`；它必须
  直接初始化到最终 place，并一直保留到该 place 析构。直接 `Task<T>` 和 `Mutex<T>` 使用这一规则
  保持地址稳定。
- `Copyable` 继承 `Movable`，决定普通值使用是复制还是移动。整数、浮点、Bool、Char、enum、
  shared reference、raw pointer 和 RawFn 默认 Copyable。
- `T&!` 是唯一 capability，不能自由复制成两个可同时使用的引用；按值传播必须转移它，
  或建立受 lifetime 约束的 reborrow，并在派生借用存活期间冻结原引用。
- tuple、array、optional 和 errorable 只有在所有组成类型都 Copyable 时才 Copyable。
- 用户定义的 struct 和 payload enum 不默认 Copyable；必须显式实现 `Copyable`，且所有字段或
  payload 都必须 Copyable。带自定义 `deinit` 的 nominal 不能 Copyable。
- `T^`、捕获环境的 `Fn` / `Fn^`、直接 `Task<T>` 和 `Task<T>^` 都不是 Copyable。
- 非 Copyable、但 Movable 的值在普通按值位置默认 move；不需要写 `$.move()`。Copyable 值
  默认 copy，也可以用 `$.move()` 强制转移并让源 place 失效。
- 泛型代码只有在 `T: Copyable` 约束下才能依赖隐式复制；无该约束的按值使用按 move 处理。
- 自定义 `copy()` / `clone()` 只是普通 API，不会让类型获得隐式 Copyable 语义。
- owned nominal 的 stored field 可以独立 move，嵌套字段沿同一 place path 处理。move 后该字段
  及其祖先 aggregate 不能作为完整值读取，但不重叠的兄弟字段仍可使用；重新初始化缺失字段后，
  祖先 aggregate 恢复完整。
- 部分移动沿 CFG 做保守合流：任一可达前驱移动过字段，合流后的父值都视为可能不完整。drop
  只析构该路径上仍然初始化的字段；分支内是否发生 move 由对应字段的运行时 drop state 保留。
- 普通 `T&`、`T&!`、raw pointer/index 派生 place 不能被部分移动。任一路径上的父 nominal
  自身声明自定义 `deinit` 时也禁止部分移动，保证 `deinit` 始终观察完整 `self`。

`T&!` 和 `T[]&!` 不属于 Copyable。binding、字段、返回值等普通按值传播会 move capability；
将已有可变引用传给 `T&!` 或 `T&` 参数时建立只持续到调用点的 reborrow，因此调用返回后
原引用仍可继续使用。
reference 的 ABI 表示即使只是一个地址，也不能据此授予第二份可变能力。

```jiang
struct Point: Copyable {
    Int x;
    Int y;
}

Point p2 = p1; // 允许：普通值类型

struct Buffer {
    UInt8^ data;
    Int length;
}

Buffer b2 = b1;         // 默认 move，b1 随后失效
Buffer b3 = b2$.move(); // 也允许显式 move，b2 随后失效
```

move：

- 非 Copyable 值在赋值、按值传参、返回和 capture 时默认 move；`value$.move()` 是保留的
  显式形式。
- 对 Copyable 值使用 `$.move()` 会强制 move，而不是 copy。
- move 后，源变量进入失效状态，后续不能读取、写入、调用方法或再次 move。
- move 后的源变量离开作用域时不会调用 `deinit`。
- move 的目标变量成为新的有效值，后续按普通局部变量规则参与析构。

```jiang
Buffer a = Buffer();
Buffer b = a;

a.length; // 编译错误：a 已经 move
// 作用域结束时只析构 b，不析构 a
```

`T&` 表达共享只读访问，`T&!` 表达唯一可变访问。borrow checker 会阻止仍活跃的 `T&!`
与重叠共享/可变引用并存，也会阻止通过来源 place 绕过该借用直接访问。引用最后一次
使用后，来源 place 可以恢复访问。

生命周期来源约束使用 `@life(...)` leading attribute 表达，并统一写成
`target: source`。例如 `@life(return: input)` 表示返回值的 lifetime shape 由 `input`
覆盖。每个 target 在一条 attribute 中必须唯一；同 Shape 的多个候选来源使用
`left & right` 逐位取得共同最短 region，多-slot source 使用 `(left, right)` 构造
product shape。
`@life` 与 `@where` 分离：`@where` 只描述类型、trait 和 associated type 约束，
`@life` 只描述 lifetime 来源覆盖。

常用 lifetime 名：

- `self`：方法或 trait 方法的 receiver lifetime。
- `return`：函数返回值 lifetime。
- 参数名：参数值的完整 lifetime shape。
- callable 的 result/参数契约名：对应 callable 位置的 lifetime shape。
- public region 名：nominal 类型公开 shape 中的具名位置。

struct / payload enum 只有显式 `@region` 才公开 lifetime shape。裸名称按源码顺序声明 public
region，`target: source` 在声明 target 的同时表示 source 覆盖 target。每个 target 在
attribute 中只出现一次，source 必须由同一 attribute 的其他 item 声明，但可以位于 target
之前或之后。coverage 可以成环，例如 `a: b, b: a` 表示两个 region 互相覆盖。每个 public
region 必须由字段或 enum payload 的实际 lifetime slot 直接使用，不支持 phantom region：

```jiang
@region(a, b: a)
struct Pair {
    @life(a)
    Int& first;

    @life(b)
    Int& second;
}
```

字段类型只有一个 lifetime slot 时使用 `@life(a)`。多-slot 字段可以按公开 Shape 顺序写
`@life(a, b)`，也可以按 type occurrence 提供的名称写 `@life(left: a, right: b)`。
named 模式的 target 必须唯一且完整，不能与位置模式混用。字段 binding 不支持 `self` source，
也不改变字段 `TypeId`、layout 或 ABI。

`@region` 的普通参数是固定单 slot；`value: T` 显式声明
`shape(value) = shape(T)`，其中 `T` 必须是同一 nominal 的类型泛型参数。region schema
不再从字段布局反向推导。`@region(r, value: T = r)` 还声明字段省略 `value` binding 时，
用 `r` 填充 `shape(T)` 的全部 slots；没有 `= r` 时必须显式提供完整 binding，实际 shape
为空时除外。shape-valued region 必须位于固定 region 之后。

region slot 不得与同一 nominal 的字段、泛型参数或其他 region slot 重名；函数、内部类型和
extension 成员不参与该冲突检查。

reference 使用专用 `reference(value)` Shape，不退化成普通 product，其 schema 在语义上等价于
`@region(r, value: T = r)`。外层 borrow 是第一个
逻辑位置，第二个逻辑位置保持完整 pointee Shape。reference 字段必须绑定外层 borrow；例如
`Pair` 的 Shape 为 `(a, b)` 时，可以只写 `@life(r)`，由 `r` 填充 pointee，也可以完整写作
`@life(r, (a, b))`，但不能扁平化或部分绑定。函数 contract 中，reference 参数根名表示外层
borrow，`input.a` 表示 pointee 的公开 region；`(T t)& input` 中的 `input.t` 表示完整
`shape(T)`。

函数没有显式 `@life` 且返回 Shape 非空时，readonly `self` / `Self&! self` reference receiver
的 Shape 非空则优先使用 receiver，等价于 `@life(return: self)`；即使还有其他非空参数 root，
也不产生歧义。没有该特例时，只有恰好一个用户可见参数 root 的 Shape 非空且与返回 Shape
兼容，才默认使用该完整 root。一个 product Shape 仍只算一个 root。`Self self` 按值 receiver
不享受优先级，只作为普通参数参与唯一 root 计数。

`Task<T>` 的公开 Shape 由一个 capture slot 和 `T` 的 result Shape 组成；`Task<T>^` 继承同一
Shape，owner handle 不增加新的 slot。capture slot 约束 Task closure 中的借用，result Shape
约束 `await()` 取出的值。Task 是直接值还是 owner、frame 位于栈还是 heap，都不改变这份契约。
`await()`、`cancel_and_await()` 或直接 Task 的结构化 join 结束执行后，capture loans 随 Task 根一起
结束。`Task<T>^` owner 析构不等待 coroutine，因此带 capture loans 的 owner 必须先消费，不能靠
离开作用域静默结束借用。

返回 Shape 非空但零个非空输入 root 时，必须显式写 `@life()` 确认返回值不携带参数 borrow；
存在两个或更多非空 root，或唯一 root Shape 不兼容时，也必须显式写出契约。默认契约只由公开
签名决定，不读取函数体。任意显式 `@life(...)` 都完全替换 implicit return contract；
只声明 `callback.result: callback.input` 之类的 callable 子契约不会继续补充外层默认来源。

`@life(return: input)` 约束的是 `input` 值携带进来的 loans，不是按值参数 binding 自身的栈槽。
因此包含引用字段的值可以传播已有 borrow，但不能对按值 `T` / `T^` 参数的字段临时取引用后返回。
不含 borrow 的参数对应空 loan 集合，约束自然成立。raw pointer 不携带语言级 lifetime。
带 `@region` 的 nominal 参数使用公开 region 名选择单个位置，例如 `input.left`。该名称解析为
类型声明的字段 binding，不是对同名字段的访问；private 字段名不能出现在公开 callable contract 中。

`Fn` / `RawFn` 的 result 和参数可以提供按需契约名，供外层函数约束 callback：

```jiang
@life(callback.result: callback.fallback, return: fallback)
Int& apply(
    Fn<Int& result, Int& value, Int& fallback> callback,
    Int& value,
    Int& fallback
);

@life(return.result: return.value)
Fn<T result, T value>^ make_identity<T>();
```

Fn 作为函数参数或返回值时使用同一套命名位置规则；根分别是参数名和 `return`。callable contract
只能引用 result/参数的声明名；需要参与 contract 的位置必须命名。
不支持 `callback[0]` 之类的位置路径。closure environment、receiver adapter 和 continuation
等 ABI 隐藏参数不能出现在公开 contract 中。

名称只在语法、接口和诊断中保留；声明检查会把它们一次性解析成参数索引。解析后的
`LifetimeContract` 属于 callable 的语义签名，调用传播、函数值兼容性和 lambda expected type
共用该 contract；
closure environment 等 ABI 隐藏参数不进入公开索引。trait object 动态派发和 RawFn/Fn adapter
同样必须把 contract 映射到 JIL 实参数；borrow checker 不再用“callee 加全部实参”猜测间接调用来源。

lifetime 契约参与约束检查，不参与类型身份；相同参数类型不能仅因 lifetime 契约不同构成重载，
这一规则同样适用于参数内部的 `Fn` / `RawFn` 契约。类型相同并不自动代表值满足目标契约：
函数值赋值、参数传递和 trait 实现仍检查契约兼容性。稳定类型 key 不包含 lifetime 契约；
需要观察契约内容的反射与缓存依赖使用独立的契约指纹。

裸 `Fn<R, Args...>` / `RawFn<R, Args...>` 的默认返回契约为空。它等价于把 `R` 固定在参数
lifetime 之外，因此 callback 不能把参数 borrow 作为 `R` 返回；这与普通函数的 signature
elision 不同。需要返回参数 borrow 的高阶接口必须用 callable 契约名显式声明来源。

跨函数调用、返回含引用字段的值、或把来源关系写入 public API 时，仍建议显式表达返回值不超过来源：

```jiang
@life(return: input)
UInt8& first(UInt8& input);

@life(return: buffer)
Slice make_slice(Buffer& buffer);
```

当前 lifetime 检查会阻止局部引用逃出来源 owner 的有效范围，并阻止 owner 在活跃借用期间被
move/drop/free。跨函数和存储到类型字段的来源关系通过 `@life` 检查；shared/mutable alias
冲突则由 loan 的种类、重叠 place 和最后一次使用共同判断。

返回聚合值时，`return` 必须作为完整 Shape 一次映射。source 可以由参数的具名 region
投影或 product expression 构造：

```jiang
@life(return: (left, right))
PairRef make_pair(Int& left, Int& right);

@life(return: value.second)
Int& take_second((Int& first, Int& second) value);
```

不允许把完整 target 拆成 `return.a`、`return.b` 多条映射，也不支持 tuple/Fn 的 `[0]`
位置式 lifetime path。源码中的具名位置会解析为稳定内部投影；array 的运行时下标不能建立
彼此独立的 lifetime 身份，必须保守地与同一 array 的其他元素别名。

`panic(message)` 是进程级不可恢复错误入口，而不是可捕获的 control flow。它先向标准错误输出
消息与换行，再立即 abort；不执行 unwind，也不保证运行局部析构。可恢复失败继续使用 `T@E`，
Task cancellation 不复用 panic。

`assert(condition[, message])` 在 debug/release mode 都保留。失败路径输出调用点 source path、行列
和可选 message 后 trap，不执行 unwind。`build.mode` 是 compiler 注入的只读 compile-time
`BuildMode`；库可以据此选择实现，但普通源码不能改变当前 compilation mode。

## 隐式操作层

`$` 用于进入值或类型的隐式操作层。

已确定操作：

- `value$.as(Type)`：强制类型转换，不保证类型安全。
- `value$.ref()`：阻止 receiver 自动解引用，并返回其指向值的 `T&`。
- `value$.mut_ref()`：从可写 place 创建唯一可变引用 `T&!`。
- `value$.ptr()`：阻止 receiver 自动解引用，并返回其指向值的 `T*`，需要 `unsafe`。
- `value$.mut_ptr()`：从可写 place 创建可写裸指针 `T*!`，需要 `unsafe`。
- `value$.get()`：显式解引用 `T^` / `T&` / `T*`，返回指向的值；raw pointer 也可在 `unsafe` 中使用下标访问。
- `value$.set(new_value)`：显式写入 `T*!` 指向的单个目标对象；`T*` 不允许写入。
- `value$.move()`：显式转交当前变量的值；Copyable receiver 也会被强制 move，源 place 随后失效。
- `value$.drop()`：立即结束当前值的生命周期并执行正常析构，需要 `unsafe`。
- `value$.forget()`：让 Movable 值失效但跳过析构，需要 `unsafe`；`!Movable` receiver 必须拒绝。
- `value$.addr()`：获取裸指针，需要 `unsafe`。
- `value$.dealloc()`：释放默认堆分配器上的对象，需要 `unsafe`。
- `optional$.some()`：强制解包 optional。
- `Type$.size()`：类型大小。
- `Type$.align()`：ABI 对齐。
- `Type$.max_align()`：默认分配器保证支持的最大对齐。
- `Type$.alloc()`：分配一个未初始化元素，返回 `Type*!`。
- `Type$.alloc(n)`：分配 `n` 个未初始化元素，返回 `Type*!`。

`Type$.alloc(n)` 要求 `n >= 0`。元素数量、sentinel slot 或 byte size 计算溢出，以及底层 allocator 对
非零大小返回 null，都会立即 trap。allocation failure 不可恢复，不执行 unwind，也不保证析构；需要
可恢复资源上限的 API 必须在调用 allocation 前自行验证业务限制，而不能把 OOM 当作 errorable 结果。

安全类型转换优先用类型初始化形式，例如 `Int(value)`；`$.as()` 保留为底层强制转换。

隐式操作层的低层操作会逐步接入 effect 检查。当前裸指针获取和显式释放需要放在 `unsafe`
中；语言引用仍由 borrow checker 检查 ownership、lifetime、drop safety 和唯一可变借用。

如果后续引入更细的 capability 系统，`$` 会成为受编译期 capability 约束的低层操作层。每个 `$`
操作都需要对应能力；缺少能力时编译失败。

初步分类：

- 总是安全或低风险的编译期查询：`Type$.size()`、`Type$.align()`、`Type$.max_align()`。
- 类型系统强制操作：`optional$.some()`，后续需要定义失败时的诊断、trap 或静态证明规则。
- 需要低层内存能力：`value$.ptr()`、`value$.mut_ptr()`、`value$.dealloc()`、`Type$.alloc()`、`Type$.alloc(n)`。
- 需要 unsafe/cast 能力：`value$.as(Type)`。

当前阶段不区分编译器源码包和普通 Jiang 包，普通 package 也默认拥有这些低层能力。最小能力集合和
显式授权规则推迟到 capability 系统设计时再固定。

## 声明

顶层声明包括：

- `import name;`
- `alias alias = import "path.jiang";`
- `public import name;`
- `public alias alias = import "path.jiang";`
- `alias Name = Type;`
- `alias Name;`
- `public alias exported = module.symbol;`
- `public alias exported;`
- const global declaration: `const Type name = expr;`
- public const global declaration: `public const Type name = expr;`
- global declaration: `Type name = expr;`
- function declaration / definition
- `struct`
- `enum`
- `trait`
- `extend`

目标语言支持 `public import`，用于 re-export 被导入模块的 public API。

顶层 `const` 是编译期常量声明。initializer 经语义检查后按需进入 JIL，完成借用检查和析构展开，
再由统一的 JIL 执行器求值；普通函数调用、局部存储和析构遵循与运行期相同的语言语义。
求值结果保存为可复用的不可变值，不携带执行器的临时地址。
零长度普通切片允许数据指针为 null，并可继续取 `[0..0]` 子切片；它不提供可访问的元素。
带 sentinel 的空切片仍需有效的哨兵存储；空分配被释放后，旧视图不能继续使用。

`public const` 是模块公开接口的一部分。编译器在 interface artifact 中保存最终实例化后的
declaration type 和 const payload；跨模块使用时由 importer 还原成 `ComptimeValue`，不重新执行
定义模块的 initializer。value path 会先解析出真实 value root，再由 type check 验证后续 member
chain，因此 `build.target.link_libc` 这类 public aggregate const 字段读取按普通字段访问处理。

常量作为运行期值使用时，由 JIL 表达其值构造或只读静态存储。enum 与 error union 保留 tag
及所选分支的完整 payload；未选中分支不是有效值。取常量引用不会使它成为可变存储，引用的
生命周期不受某次函数调用限制；复制到局部变量后的存储遵循该局部变量的生命周期和可变性。
backend 只消费 JIL 事实，不重新执行源码初始化表达式。

const initializer 不能依赖运行时值，也不能执行 IO 或其他运行时副作用。递归 initializer 诊断为
`recursive_const_initializer`；编译期执行受递归深度和执行步数配额限制，循环与调用共同消耗本次求值的
步数额度。耗尽时诊断 `comptime_branch_quota_exceeded`，避免编译期执行失控。
整数与浮点互转按源/目标整数的符号性和目标精度执行。编译期浮点转整数向零截断；NaN、无穷或
截断后超出目标整数范围的值诊断为 `comptime_invalid_cast`，不执行未定义转换。
const generic 参数的 canonical 约束语法是 `@where(K: const Type)`，例如
`@where(N: const Int) struct Fixed<T, N>`。声明列表中的 `N: const Int` 是等价简写，lower
到同一条 Semantic Model predicate。这里 `const Type` 是一种约束 kind，不是 trait；const generic 名字
绑定在 value namespace，可在表达式中使用，不能作为类型名使用。重复的同类型约束会去重，
类型不一致时报 `conflicting_const_constraint`。
trait associated item 也可以使用同一形式表达编译期值约束，例如
`associated kind: const DomainKind`。

### Import

当前固定两种 import path：

```jiang
import dep;
alias dep = import "foo/bar.jiang";
```

`import dep;` 中的 `dep` 是 module/package 名称，不是文件路径。它会优先按当前 package
配置的 `dependencies` alias 解析到依赖 package root；未命中 dependency 时，再按当前
编译上下文中已登记的 module/package 名称解析。

`alias dep = import "foo/bar.jiang";` 中的字符串是显式文件路径。路径按 Zig 风格解析：相对路径以
当前 import 所在源文件的目录为基准，绝对路径按原路径规范化。编译器只加载字面路径
本身，不隐式补 `.jiang`，也不尝试目录入口 `mod.jiang`。

如果当前 source 是 virtual/buffer，没有真实文件路径，相对 file import 暂按字符串本身规范化；
后续如果需要 IDE buffer 的相对文件 import，需要给 virtual source 增加 base directory。

import 只引入一个模块命名空间 alias，不把目标模块的声明平铺到当前 namespace。被导入模块
的 public API 通过 `dep.Name` 访问。`public import` re-export 的也是这个模块命名空间 alias。

file import 省略 alias 时，默认使用路径 basename 去掉最后一个扩展名后的名字。因此下面两种写法
语义完全相同，都只在当前 namespace 中登记一个名为 `foo` 的 module namespace binding：

```jiang
import "foo.jiang";
alias foo = import "foo.jiang";
```

如果默认名字不是合法 Jiang identifier，必须显式提供 alias。显式和默认 alias 走相同的 import
target、可见性、循环检测、稳定身份和增量失效路径。

`alias * = import path;` 不创建默认 module namespace，也不为目标模块的每个 public declaration 复制
alias `DefId`。它登记一条 wildcard namespace edge；lookup 按需查询目标 public namespace，并原样
保留函数 overload candidates。extension member 也通过同一个 public namespace surface 可达。
`public alias * = import path;` re-export 同一条 edge。
wildcard alias 的目标可以是任意求得 namespace 的编译期表达式，包括已有 namespace 的限定路径
和条件选择；求值结果统一按 namespace 身份展开，路径访问仍遵守可见性规则。

本地 declaration binding 优先于 wildcard edge。多条 wildcard edge 按源码登记顺序查询；最终
namespace validation 会枚举直接目标的 public binding，使未引用的同名导出也产生稳定冲突诊断。
枚举会沿 public wildcard edge 继续读取传递 re-export surface。import cycle 只复用已经登记的 module
namespace skeleton，并由 import 状态截断，不复制或递归展开 wildcard alias。

file import 只允许引用当前 package 内的 source file。跨 package 源码依赖必须通过
`dependencies` 和 `import dep;` 进入；直接用字符串路径导入另一个 package 的 source 会报错。

### 包信息声明

内置 `#package { ... }`（也可写作 `#jiang.package`）生成公开的 `info` 常量，类型为
`std.jiang.PackageInfo`。它使用普通构造表达式、类型检查和编译期求值，不定义每包独有的 struct。

```jiang
#package {
    name = "app";
    version = "0.5.5";
    type = .bin;
    root = "src/main.jiang";
    dependencies {
        tools = "../tools";
    }
    lang schema {
        package = tools;
        extensions = ["schema", "model"];
    }
    generate docs {
        module = "src/docs.jiang";
    }
}
```

普通字段值使用 Jiang 表达式；依赖名和 `package = tools` 中的 `tools` 是配置别名，后者不执行
模块导入。扩展名使用字符串表达式列表。无别名的 `lang { extensions = [...] ; }` 描述 Provider
自身的扩展名。`type` 使用 `.lib`、`.bin` 或 `.lang`，默认 `.lib`；未填写的文本和列表为空。

`.lang` 依赖默认以依赖别名注册；Provider 未声明扩展名时使用该别名作为扩展名。宿主可用
`lang <name> { package = <dependency>; }` 指定语言别名，非空 `extensions` 列表覆盖 Provider 的
声明。扩展名须符合标识符规则，不得重复或使用保留的 `jiang`；合并后的映射也不能相互冲突。

`generate <name>` 必须指定 `package` 或 `module` 其中之一；前者引用已声明的依赖，后者为本包
内部相对文件路径。包加载阶段校验来源和重复名称，只有被选择的生成器源码才加入编译图。
`--name` 选择对应入口，生成器接收的反射输入仍为本包 root；不指定名称时使用 root 自身的入口。

把声明放在 `package.jiang` 后，其他源码可用 `import "../package.jiang";` 导入，再通过
`package.info.version` 读取版本，或把 `package.info` 整体传给接受 `std.jiang.PackageInfo` 的函数。
不提供默认导出或隐式的包信息全局绑定；`info` 与用户同名声明冲突时遵循普通重名诊断。
生成的数组使用私有常量提供只读存储，不进入公开导出面。

包信息初始化可使用基础类型、标准库及本包独立 helper；不能直接或间接导入正在声明的 root，
也不能先把依赖源码当成本包 helper 加载、再把它登记为另一个包。root 必须是本包内部文件。
依赖和 Provider 在包信息求值后准备，生成任务不会参与配置初始化。
标准库自身的包入口也使用这套流程；内置 `std` 入口预先提供 `PackageInfo`，允许在配置阶段加载，
不依赖标准库的包信息完成登记。这个基础入口不受上述 root 限制，实际常量循环仍按普通语义报错。

### Package

目录或依赖路径显式加载固定入口 `package.jiang`。`#package` 展开为普通 `PackageInfo`
常量，求值后登记依赖、选择 root，再构建源码模块图；不通过向上搜索配置来自动发现子包。

```jiang
#package {
    name = "frontend";
    root = "src/main.jiang";
    dependencies {
        util = "../util_pkg";
    }
}
```

`PackageInfo` 的字符串字段默认留空，包种类默认 `.lib`，配置列表默认为空。
加载时，空 name 使用配置所在目录名，空 root 使用 `<有效包名>.jiang`；这两个加载默认值
不回写普通常量。空 version 表示未指定，非空版本只允许 ASCII 字母、数字、`.`、`_`、`+`、`-`。
有效包名及依赖别名复用 Jiang lexer 的标识符规则，包括其 Unicode 标识符规则。

依赖路径相对声明它的包目录解释，递归加载后进入同一编译闭包。包依赖环必须诊断；
同一包内的模块导入环仍允许。已登记的包形成源码边界，跨包通过依赖别名导入 root，
不能使用相对源码路径绕过边界。普通导入与包初始化复用配置模块、声明和常量结果。

package 对外导出面固定为 root file 的 public namespace：

- root file 的 public declaration 是 package API。
- root file 的 `public import` 可以重新导出一个 module namespace，但不 flatten 目标 module
  的 declarations。
- root file 的 `public alias` 可以重新导出 public symbol；函数 alias 保留目标 overload family。
- 非 root module 的 public declaration 不会自动暴露为 package API。
- dependency package 中的 `main` 不参与当前 package 的 hosted entry wrapper 选择。

### Alias

alias 有两种目标：

```jiang
alias Name = Type;
alias name = module.symbol;
```

如果省略右侧目标，`alias Name;` 等价于 `alias Name = Name;`，通常用于同名 re-export：

```jiang
public alias Bool;
```

如果右侧解析为已有 namespace/type/value/member symbol，alias 会绑定到同一个 name domain。
非函数 alias 在 Semantic Model 中记录单一目标 `DefId`；函数 alias 记录一个可见 public overload anchor，
调用时在目标原始 namespace 和 name 下枚举 public overload set，再执行普通重载决议。目标模块
的 private 同名函数不会通过 alias 暴露。如果右侧不能解析为已有 symbol，则按 type alias
处理，右侧必须是类型语法。
带 `?`、`&`、`^`、`*`、可变性或 slice 等类型层的目标属于完整类型语法，alias 必须保留这些层，
不能只重导出其底层类型符号；链式 alias 继续按最终完整类型归一身份。

`public alias` 是 package public surface 的显式 re-export 机制。package 对外只暴露 root file
的 public namespace；root file 可以通过 `public import` 重新导出模块命名空间，也可以通过
`public alias` 重新导出符号或函数 overload family。非 root module 的 public 声明不会自动成为
package API。

未定事项：

- ambiguous re-export 的诊断和恢复策略。
- 版本求解、lockfile 和 registry 规则。

## 函数和方法

函数一定有返回类型。无返回值使用 `Void`，对应的值写作 `()`。`return;` 是 `return ();` 的简写，
函数也可以在末尾隐式返回 `()`：

```jiang
Void hello() {
    return;
}
```

函数声明示例：

```jiang
Int add(Int left, Int right) {
    return left + right;
}
```

函数参数支持默认值。默认参数可以出现在任意位置；当前默认值只支持 literal，并按参数的
expected type 检查：

```jiang
Int add(Int left = 1, Int right) {
    return left + right;
}
```

命名 keyword options 同样使用等号，例如 `async [domain = ui_domain]` 和 `struct [align = 8]`；
无名选项、`packed` 等独立 flag 及类型／lifetime 约束不受影响。

命名实参使用 `name = value`；`:` 保留给类型／trait 约束与 lifetime 关系。实参开头的
`name =` 表示参数绑定，不修改调用者同名变量；`name == value` 仍是普通位置表达式。
赋值仍然只允许作为语句，不支持把括号赋值或链式赋值作为实参。旧 `name: value` 调用给出迁移诊断。

位置实参总是绑定最早尚未绑定的参数，不会按类型跳过默认参数。命名参数可以重排，也可以跳过
带默认值的参数；第一个命名参数出现后，后续普通参数都必须使用命名形式：

```jiang
add(10, 20);
add(right = 20);
add(left = 10, right = 20);
draw(x = 1, y = 2);
```

type check 会把 call args 重排成函数签名顺序，并把缺失参数替换成默认值。这个结果写入
`TypeCheckStore.call_args`，JIL lowering 只消费重排后的参数列表，不重新做 overload
或默认参数匹配。

同名函数和同名方法允许 overload。默认参数参与 overload 检查：如果两个 overload
在同一调用点可能同时满足参数数量和参数类型，必须诊断为歧义，而不是依赖声明顺序选择其中一个。

泛型函数：

```jiang
@where(T: Numeric)
T add<T>(T left, T right);
```

`init` / `deinit` 是目标语言的一部分。`init(self, ...)` 定义构造函数，
`deinit(self)` 定义析构逻辑；构造 sugar 使用 `Type(...)`，堆分配构造使用 `new Type(...)`。

### 显式入口

`@entry(kind)` 标记 root file 中的直接声明。每个 root、每个 kind 至多一个入口；
标记不附着 alias／重导出，也不沿 import 递归选择。函数重载可以只标记其中一个。
入口身份不改变普通可见性，私有声明仍然私有。

`@entry(main)` 指定程序入口，函数须非泛型、同步、无参数且有函数体，返回整数类型或 `Void`。
没有显式标记时沿用普通 `main`；存在显式标记时优先采用该声明，不回退到同名函数。
`@entry(lang)` 指定语法 Provider 类型，其构造和方法由 Provider 契约约束。
`@entry(generate)` 指定独立生成任务的函数入口，签名为 `Void (reflect.Module)`。

### 类型化 metadata

`@meta(expr)` 为声明附着一个普通 Jiang 值；`@meta(module: expr)` 是独立顶层项，附着到当前源码模块。
模块附着不需要后续声明，可以重复出现；不能位于成员或函数 body 内，也不能再带前置 attribute。
`module:` 是目标前缀，`@meta(module)` 中的 `module` 仍按普通表达式名称解析。

expr 隐式执行普通编译期求值，函数不需要专门标记；结果必须能按普通 const 规则物化。
同一目标的多次附着保留次序，不覆盖、不去重；精确类型身份决定查询匹配，别名归一。
`@where`、`@entry` 等内建 attribute 的语义属性不混入 metadata 集合。

Lang factory 的 `declaration_meta(span, expr)` 返回附着到后续声明的 attribute；
`declaration_meta(decl, span, expr)` 向已有声明追加，`module_meta(span, expr)` 返回独立模块项，
可直接放入 Provider 的声明集合。它们与原生语法产生同一类附着，不在 Provider 进程内执行 metadata 表达式。

### 生成器来源

`jiang generate` 默认执行输入包 root 的 `@entry(generate)`；没有入口时报错，不自动选择第三方工具。
`#package` 的 `dependencies` 块统一注册包；`lang <name>` 和 `generate <name>` 的 `package` 字段引用依赖别名。
命名生成器也可用 `module` 指定本包内部文件，与 `package` 互斥。每个所选 root 只允许一个生成入口。
`--name` 选择别名，输入始终是本包 root；不提供 `--generator` 路径覆盖或任务输入 root 覆盖。
Lang 别名可复用同包的生成入口，显式同名 generate 配置优先；普通 build/check 不执行生成器。

### 模块反射

`reflect.Module` 是编译器发放的只读模块句柄，支持身份比较；`name()` 返回源码名称。
句柄及包含句柄的集合只在本轮编译期执行中有效，不能物化到运行期或持久化缓存中。

`reflect.modules(root)` 返回 `reflect.Modules` 视图，包含 root 及同包实际导入可达的模块。
顺序为 root 优先、源码导入顺序的深度优先遍历，按模块身份去重；导入环不会导致重复或无限遍历。
生成器自身额外加载的模块不自动进入该范围，外包目标不向其内部继续展开。

视图提供 `len()`、`get(index)` 和普通 `Sequence` 遍历；每次遍历拥有独立游标。
越界索引产生编译期诊断。查询复用已完成前端检查的模块图，不触发新的源码加载。

`module.imports()` 返回可重复遍历的 `reflect.Imports` 视图，同样提供 `len()` 和 `get(index)`。
每条 `Import` 提供 `source()`、`target()`、`binding()`、`visibility()` 和 `location()`：

- `ImportBinding` 区分 `.named(name)`、`.wildcard` 和 `.unbound`，重复绑定保留为独立边。
- `Visibility` 区分 `.public_visibility` 与 `.private_visibility`。
- `Location` 是普通来源数据，包含源码名称 `source`、字节 `offset`／`length`，以及从 1 开始的
  `line` 和 UTF-8 字节 `column`。来源数据可用于输出，句柄本身仍不可物化。

导入按源码顺序返回，只包含实际生效的关系，保留导入环和外包目标；允许读取外包目标的名称，
查询其内部导入或模块闭包则诊断。

`module.path()` 沿用编译器源码逻辑路径：包内文件返回相对包目录的路径，其他文件返回规范化源路径；
virtual 输入保留原名称。
`module.name()` 保留原始来源名称，`source_kind()` 区分 `SourceKind.file` 与 `.virtual`，
`location()` 返回整份输入的来源范围，空文件长度为 0。自定义 Lang 保留原始 DSL 文件及扩展名。
`module.package()` 返回 `reflect.Package`，支持 `name()` 和身份比较；同名包不等于同一包。
包句柄只在本轮有效，模块来源与包名称是可物化的普通数据；这些查询不开放外包的内部遍历权限。

### 公开导出

`reflect.exports(module)` 返回 `reflect.Exports`，提供 len／get／Sequence，只用于 generate。
每个 `Export` 提供 `name()`、`target()`、`source()` 和 `location()`；目标为 `Target.module` 或
`Target.declaration`。显式符号别名保留导出名并解析到实际目标，类型表达式别名仍为自身声明。
函数别名展开目标名字绑定中的全部公开重载，私有重载不进入导出面。

先按声明顺序返回模块自身的公开绑定，再深度优先展开公开通配边；本地公开名字优先，重复路径和环去重。
通配导出的来源指向实际命名声明，显式别名的来源指向别名声明；导入路径由 `module.imports()` 查询。
允许查询外包模块的公开导出，仍不能借此枚举其私有定义或内部导入闭包。视图可重复遍历，句柄不可物化。

### 声明反射

`module.declarations()` 与 `decl.members()` 返回直接拥有的声明视图 `reflect.Declarations`，
支持 `len()`、`get(index)` 与重复遍历，保留源码顺序。自定义 Lang 的同位置声明按最终 AST 输出
顺序返回，允许普通声明、公开别名与 extension 交错，工厂创建节点的先后不决定输出次序。
函数 body 中的局部定义不作为成员返回，
也不沿字段或参数的类型引用递归。输入包内可查询私有定义，外包模块及类型只返回公开声明／成员。

`reflect.declarations(root)` 返回同包可达声明的递归 `Declarations` 视图，仅用于 generate。
模块顺序与 `reflect.modules(root)` 一致，每个模块内先返回声明自身，再返回泛型参数和直接成员，
均保留声明顺序；按声明身份去重。模块本身不作为 Decl 返回，函数的普通参数由签名 API 查询，
不进入函数 body，也不沿类型引用展开。外包模块不能作为递归查询的根。

元素为带类别 payload 的 `reflect.Decl`，例如 `.function(Function)`、`.struct_decl(Struct)`、
`.enum_decl(Enum)`、`.field(Field)` 和 `.variant(Variant)`；类别句柄均定义在 `reflect` 中。
共同操作 `name()`、`module()`、`visibility()`、`location()`、`members()` 位于 Decl 上。
`==` 比较声明身份及成员替换环境，同名重载是不同声明；payload 支持普通模式匹配，不使用强制转换。
声明及类别句柄仅在本轮有效，包含句柄的值不能物化到运行期或持久化。

每个 extension 独立表示为 `.extension(Extension)`，由其声明模块枚举；无泛型参数的扩展同样有独立身份。
扩展没有用户命名，`name()` 返回空文本，使用身份和 `location()` 区分；`visibility()` 来自扩展声明本身。
扩展的 `members()`、`generic_parameters()` 与 `documentation()` 仅描述自身，不与目标类型的定义合并。
`Extension.target()` 返回 TypePattern，保留声明目标的实参、通配符和类型层，不枚举符合扩展条件的实例。
`trait_type()` 返回该扩展声明实现的 optional Type，纯成员扩展为空；关联绑定从 constraints 查询。
`is_unsafe()` 读取扩展声明的 unsafe 标记。
类型的直接成员排除 extension 方法；跨包只枚举公开扩展及其公开成员，`with_doc` 保留这个归属边界。

`Function.signature()` 返回 `reflect.FunctionSignature`，字段为 `parameters`、`result`、`receiver`、
`is_async`、`is_unsafe`、`has_body` 和 `domain`。参数保持不含 receiver 的只读视图，支持名称、类型、
默认值存在性及身份比较；receiver 区分 `.none`、`.borrowed`、`.mutable_borrowed`、`.owned`。
`has_body` 只表示声明提供实现，与 body 是否被加载或执行无关。调用方可读取一次签名，再访问多个字段。
签名与 lifetime 契约分别查询；返回结构体后的字段访问不改变查询的依赖粒度。

`decl.generic_parameters()` 按声明顺序返回泛型参数，不枚举单态实例。`GenericParameter.is_const()` 区分
值参数与类型参数；`type()` 对类型参数返回符号类型本身，对 const 参数返回所声明的值类型。
`Field.type()`／`Variable.type()` 返回定义的类型，`Variable.is_const()` 读取常量声明属性。

`reflect.Type` 是本轮语义类型的只读句柄；`reflect.type_of<T>()` 将已知类型接入反射，也可用于普通 comptime。
类型身份比较归一别名，保留泛型参数所属声明、泛型实参及可变性；`is_mutable()` 读取最外层可变性，
`has_parameters()` 判断类型中是否仍含待替换参数。含类型句柄的结果不能物化为运行期常量。

`Type.shape()` 返回 `TypeShape`：primitive、nominal、parameter、optional、error_union、handle、array、
slice、tuple、function 和 task 等结构采用 enum payload。整数提供目标位宽及符号性，浮点数提供位宽；
子类型保持只读 Type 句柄，不复制语义类型树。外包私有名义类型与编译器内部环境返回 opaque。

`NominalType.definition()` 返回原始声明；`ArrayType` 提供 element／length，长度区分 known 与符号
GenericParameter；`HandleType` 提供 reference／owner／raw／slice 类别及 element。
ErrorUnionType 提供 value／error，SliceType 提供 element；元组元素和函数参数使用可重复遍历的 Types 视图。
FunctionType 提供 result、raw／closure、调用 ownership 和 async／unsafe 标记。视图越界产生编译期诊断。
结构中的纯值可物化，包含类型或声明句柄的结构不可物化。

`Function.signature().domain` 和 `FunctionType.domain()` 返回 optional `reflect.Domain`。域身份由规范 const 绑定决定，
别名共享身份，相同类型和初始化值的不同绑定仍是不同域。Domain 提供 type、binding 和身份比较；
外包私有绑定的 binding 返回 null，其类型仍遵循 opaque 规则。查询不求值初始化器或创建 executor，
域句柄不能物化，未指定域的普通或 async 函数返回 null。符号域保留 const 泛型参数身份，
具体类型成员中的域按类型实参替换。

`Decl.regions()` 和 `NominalType.regions()` 返回声明顺序的 Regions 只读视图，支持 len／get／Sequence。
Region 提供 name、shape_source、default_source 和 sources；固定 region 的 shape_source 为空，
shape-valued region 返回对应的 Type，具体名义类型按泛型实参替换。schema 不因实例的空 shape 删除 region。
default_source 返回同一 schema 中的默认 region，sources 只列 `target: source` 的直接覆盖来源，
保留环而不计算传递闭包。region 身份包含所属声明及类型替换环境；region 和视图句柄均不能物化。

`Decl.lifetime_bindings()` 返回显式 `@life` 绑定的 LifetimeBindings 只读视图，支持 len／get／Sequence。
省略 attribute 与 `@life()` 都返回空集合，使用 is_explicit 区分；查询保留声明形式，不补入推导契约。
LifetimeBinding 的 target 为空表示位置绑定；source 返回 LifetimeExpression，其 shape 为
empty／path／product／meet。组合表达式提供 left／right；路径提供 receiver／result／named 根和
有序投影片段，保留公开 region 名称，不展开为私有字段。绑定、表达式和路径句柄不能物化。
具体类型成员上的声明绑定仍使用原声明的名称；有效契约与类型替换独立处理。

`Function.lifetime_contract()` 和 `FunctionType.lifetime_contract()` 返回规范 LifetimeContract 关系视图，
提供 len／get／Sequence。每条 LifetimeFlow 提供 source／target BorrowPath，包含根和有序投影。
Function 将 receiver 与显式参数索引分开；FunctionType 按自身参数列表编号。callback 参数的调用契约
通过该参数的 FunctionType 查询。契约包含省略写法的默认规则及规范化关系，不执行被查询函数。
投影包括字段原始 Decl、元组位置和 pointee；遇到外包不可见字段，以 opaque 截断余下路径。
契约、关系和路径句柄不能物化；查询当前已检查并完成类型替换的函数类型，不复制契约事实。

`Struct.lifetime_contract()`、`Enum.lifetime_contract()` 和 `NominalType.lifetime_contract()`
查询已检查名义声明 schema 的字段关系，复用 LifetimeContract／BorrowPath，根为 value。
`Region.paths()` 返回该 region 绑定的有序 BorrowPaths 视图；路径可经过字段、variant、元组和 pointee，
沿用相同的可见性截断与不可物化规则。泛型保留声明 schema，不按具体 shape 展开或删除路径，
字段身份指向原始声明。关系保留 schema 中的直接边，不另算传递闭包，也不递归展开引用类型。

`Decl.constraints()` 返回直接语义约束的 Constraints 视图，包含行内泛型约束和 `@where`，
按语义展开顺序读取并沿用编译器的去重规则；extension 从自身约束集合读取。
Constraint 区分 const_type、type_bound、negative_type_bound、equal、not_equal 和 associated_equal，
分别提供参数／类型、subject／bound、subject／value 或 subject／trait_type／name／value。
ConstraintValue 区分 Type、Constant、TypePattern 和 LiteralPattern；常量复用只读 Constant 接口，不触发新的求值。
const 参数类型依赖通配实参时，LiteralPattern.value() 保留未定型字面量的整数位模式及符号标记、浮点、
Bool、Char、字符串、null 或 unit；实际匹配时再按 const 参数类型校验。已知的前置类型实参用于替换参数类型，
已定型值返回 Constant；具名常量保留其自身类型。LiteralPattern 句柄不能物化，提取的普通字面量值可以物化。
类型模式通过 TypePatternShape 保留通配符、完整类型、名义类型实参、类型层、数组和错误联合形状。
ErrorUnionTypePattern 提供 value 和 optional error，省略的错误类型保持空值。
PatternExtent 保留长度或 sentinel 的 absent／wildcard、字面量种类或声明中的符号名称。
查询成员约束时沿用其类型替换环境；模式视图和约束句柄不能物化为运行期常量。

`NominalType.arguments()` 返回 `GenericArguments` 只读视图，按绑定顺序混合返回
`GenericArgument.type(Type)` 和 `.constant(Constant)`。Constant 提供 type／parameter／binding；
符号 const 参数保留其声明，命名绑定仅在当前可见范围内返回，计算出的值可以没有命名绑定。
`read<T>()` 在值已求出且类型精确匹配时返回 `T&?` 的只读借用，否则返回 null；不隐式转换类型。
借用的数据不可写，读取出的普通值可用于后续计算，Constant 句柄本身不能物化。
ArrayType／SliceType／HandleType 的 `sentinel()` 返回 optional Constant，保留具体值或符号参数。

`Type.members()` 返回该类型直接拥有的成员，并按类型实参替换字段、参数和返回类型。
方法自己的泛型参数保留符号身份；不合并 extension，也不沿指针或字段类型递归展开。
无声明成员的类型返回空视图；外包私有类型只保留不透明身份，其成员不可查询。
具体成员的比较包含语义类型环境，别名归一；`decl.definition()` 去除环境，回到保留符号参数的原始声明。
名称、位置和归属始终来自原声明。

已知类型的成员、成员签名及其来源可在普通 `comptime`／const 求值中查询；语义准备由既有按需编译流程完成，
执行器只消费就绪事实。普通求值以所属包为可见范围，外包私有类型保持不透明；
由成员取得的模块句柄可读取名称或比较身份，不授予枚举全模块声明／imports／模块闭包的权限。
这些范围查询要求 generate 上下文。反射参与常量或类型依赖循环时沿用编译期依赖诊断。

## 函数指针和闭包

Jiang 区分裸函数指针和闭包值。

- `RawFn<Ret, Args...>` 是裸函数指针。它只保存函数入口，不携带捕获环境，不需要 drop，
  可用于 C ABI 函数指针边界。
- `Fn<Ret, Args...>` 是 erased callable view。它可以表示捕获 lambda，运行时模型是
  `{ receiver, vtable }`；`receiver` 指向编译器合成的 closure object，`vtable` 提供
  call/drop 槽。
- `Fn<Ret, Args...>^` 是 owned heap closure。`new { [captures] args => body }` 会直接构造
  heap closure object；移动 `Fn^` 只移动 owner handle，drop 时通过 closure vtable
  销毁 environment。
- owned closure 只拥有 closure object 和 by-value capture；borrow capture 仍然受来源 lifetime
  约束。所有 capture loans 合并到一个 callable environment lifetime slot，因此多个 borrow
  capture 使 `Fn^` 受其中最短来源限制。
- callable 类型可写成 `Fn<R result, A value, B fallback>` 或对应的 `RawFn` 形式。result 和参数
  可以按需命名，已提供的名称必须唯一；这些名称只为 lifetime contract、文档和诊断提供稳定引用，不参与 TypeId、
  ABI、重载或调用参数匹配。

`RawFn` 适合顶层函数、类型函数、未绑定实例方法和非捕获 lambda：

```jiang
Int inc(Int value) {
    value + 1
}

RawFn<Int, Int> raw = inc;
RawFn<Int, Int> also_raw = { value => value + 1 };
```

`Fn` 可以捕获外层 local。lambda 必须出现在有 expected callable type 的位置，参数类型由
expected type 下推：

```jiang
Int base = 10;
Fn<Int, Int> add_base = { value => value + base };
```

lambda 可以用 `[...]` 显式选择已有 local 的捕获方式；未列出的普通 value、owner 和 reference local
按共享引用/view 捕获，raw pointer 按值捕获：

```jiang
Fn<Int, Int> add_snapshot = { [base] value => value + base };
Fn<Int> read = { [ref base] => base$.get() };
```

`[name]` 按值捕获，遵守 Copyable/move 规则；`[ref name]` / `[ref! name]` 遵守普通 borrow 和
lifetime 规则。capture list 不声明变量、类型或 initializer，也不接受表达式；需要快照或表达式
结果时先声明普通 local。隐式捕获不能写入外层 storage；`T*!` 仍可在 `unsafe` 中写入 pointee。
逃逸的 owned `Fn^` 不能保存指向已结束栈帧的 borrow。

`[ref name]` 和 `[ref! name]` 分别复用 `name$.ref()` 与 `name$.mut_ref()` 的类型和 lowering 语义。
对已有 reference handle 的 reborrow 是幂等操作；例如 `T&!` 经 `[ref! name]` 后仍为 `T&!`，
不会产生嵌套 reference。

`RawFn` 不允许捕获。`RawFn` 可以通过 `Fn(raw)` 显式包装成同签名 `Fn`，但 `Fn` 不会隐式或
显式退回 `RawFn`：

```jiang
Fn<Int, Int> callable = Fn(raw);
RawFn<Int, Int> bad = callable; // fail
```

方法值不会自动绑定 receiver。`self.method` 或 `Type.method` 作为值时得到的是带显式
receiver 参数的 `RawFn`；如果调用点需要不带 receiver 的 `Fn`，必须写 lambda 显式捕获并调用：

```jiang
struct Meter {
    Int value;

    Int add(self, Int extra) {
        self.value + extra
    }

    Int call(self, Fn<Int, Int> callback) {
        callback(1)
    }

    Int ok(self) {
        self.call { extra => self.add(extra) }
    }
}
```

`Fn` 和 trait object 都是 erased value：调用者不直接知道具体实现类型，而是通过运行时表间接
调用。区别是：trait object 的 vtable 来自 trait requirement，receiver 指向满足 trait 的具体
值；closure object 的 vtable 来自某个 lambda/callable 签名，receiver 指向该闭包的 environment。
trait object 表达“某个类型实现了某个 trait”，closure object 表达“某段代码加上它捕获的环境”。

## 异步、Task 和并发同步

函数声明可以带 `unsafe`、`async` 和静态 Domain effect。effect 也进入 `RawFn` / `Fn` 的函数类型：

```jiang
unsafe Int read_raw(Int* pointer);
async [global_domain] Int load(Int id);

RawFn<unsafe Int, Int*> reader = read_raw;
Fn<async [global_domain] Int, Int> loader = { id => load(id) };
```

lambda 自身不增加 effect 前缀；async/unsafe/domain effect 必须由完整 expected callable type
下推。async `Fn` 与 async `RawFn` 使用普通 async 函数相同的 start/completion ABI，只额外携带 closure
environment。动态调用可以在相同或不同 Domain 间切换；跨 Domain 的参数、result 和 capture
按值 transfer 必须满足 Sendable；共享 borrow `T&`（含 borrowed slice）在 `T: Sendable` 时可以
跨 Domain，并继续由同一套 lifetime/borrow check 约束。

`Sendable` 只描述“值能否安全进入另一个 Domain”，不改变值原有的 ownership、地址稳定性或
lifetime：

- 直接转移 `T` 仍要求 `T` 可移动，复制 `T` 仍要求 `T` 可复制；两种操作还必须满足
  `T: Sendable`。`Sendable` 不隐含 `Movable` 或 `Copyable`，反向也不成立。
- 转移 `T^` 只移动 owner handle，不移动 heap pointee。因此地址固定的
  `T: Sendable + !Movable` 可以通过 `T^` 进入另一个 Domain，不能直接按值转移。
- `T&` 可以在 `T: Sendable` 时作为参数、result 或 capture 跨 Domain。引用可以保存或返回，但其
  使用期限不能超过来源 owner；跨 Domain 不会放宽 owner move、drop 或 reborrow 约束。
- `T&!` 不直接跨 Domain。调用需要 `T&` 时可以从 `T&!` 自动建立 shared reborrow；新借用存活期间
  原 `T&!` 保持冻结。
- tuple、定长 array、optional、errorable result、Task 和用户声明的 aggregate 会逐层检查其
  payload。任一组成部分不满足 `Sendable`，外层也不满足。
- raw pointer 不携带 ownership 或 lifetime 证明，不会自动满足 `Sendable`。低层共享必须留在
  显式 `unsafe` 边界内，或封装进具有明确同步契约的类型。

`unsafe extend T: Sendable;` 表示实现者显式承担 `T` 的跨 Domain 安全责任。该形式只允许用于
`Sendable`，并跳过 aggregate 字段的递归 Sendable 验证；普通 conformance 的结构验证保持不变。
它适用于内部使用 raw pointer、但已自行保证同步、ownership、地址稳定性和释放顺序的 handle。
该声明不会改变字段类型本身的 conformance，也不会改变普通 borrow 的 lifetime。

`Atomic<T>` 在 `T` 是受支持的原子值时提供同步边界。`Mutex<T>` 在 `T: Sendable` 时可以作为
跨 Domain 的同步对象，但 `Mutex<T>` 本体地址固定，应通过 `Mutex<T>^` 转移 owner handle：

```jiang
Mutex<Int>^ counter = new Mutex<Int>(0);
Task(domain = global_domain) {
    counter.with_lock { value =>
        value$.set(value$.get() + 1);
    };
};
```

domain-bound owned closure `Fn<async [domain] (...)>^` 在构造时检查全部 capture。值 capture
遵守相同的 move/copy 与 `Sendable` 规则；`T&` capture 的有效期由 closure environment 的 lifetime
shape 传播，不能活过来源 owner。

普通 async 调用是隐式挂起点，表达式类型仍是函数声明的返回类型。要提前启动并获得
handle，使用 Task initializer：

```jiang
async [main_domain] Int render() {
    Task<Int> first = Task(domain = global_domain) { load(1) };
    Task<Int> second = Task(domain = global_domain) { load(2) };
    first.await() + second.await()
}
```

Task creation 是 eager 的。`Task { ... }` 创建地址固定的直接 `Task<T>`；`new Task { ... }`
在 heap 上原地初始化同一 Task 布局并返回 `Task<T>^` owner：

- 直接 `Task<T>` 是 `!Movable`、非 Copyable 的结构化子任务，可放入 struct、tuple 或固定数组的
  静态 place；包含它的聚合也不可移动、按值传参、返回或捕获。
  optional/errorable/payload enum 等动态变体暂不承载直接 Task。
- `Task<T>^` 是 Movable、非 Copyable 的一等 owner，可以按值传参、返回、存入字段、容器和
  泛型实例。
  move 只转移 owner pointer，不移动 heap 上的 Task/TaskState。
- Task 的公开类型只包含 result type，不包含 Domain 类型参数；Domain 是创建点和 runtime
  元数据。
- `await()` 消费一次 result；第二个可能消费同一 result 的源码位置会被诊断。
- `cancel()` 同步、幂等地发布取消请求，不等待、不消费 result；取消后仍可 `await()`。
- `cancel_and_await()` 发布取消请求并异步等待目标退出，消费 result ownership，但不取消 caller。
- 直接 Task 离开作用域前若仍活跃，compiler 先向同一路径的全部 child 发布取消，再逐个等待
  结束。`Task<T>^` owner 析构不阻塞、也不隐式取消，由 owner/coroutine 双方交接完成最终回收。

取消是协作式的：resume/suspend boundary 会观察请求，长时间不挂起的 async 代码可调用
`coroutine.check_cancelled()` 建立显式检查点。普通 `await()` 发现 child 已取消且没有 result
时，当前 parent 进入 cancellation cleanup，并取消、等待其余 sibling。

`coroutine.sync(Domain) { ... }` 接受必填 Domain 目标和普通尾随 closure。目标可以是命名
`const` Domain，也可以是普通 Domain value 的共享引用。在 async context 中，它挂起
当前 coroutine，结构化切换到目标 Domain，完成后回到原 Domain；它不创建用户可见 Task。普通同步函数
用最外层 `coroutine.sync(Domain)` 进入 runtime 时，会阻塞当前线程等待 closure 完成。
`Task { ... }` 可以继承已有 current Domain；`Task(domain = D) { ... }` 显式选择 execution Domain。

`main_domain` 是绑定进程启动线程的标准串行 Domain，`global_domain`
是进程共享的标准并发 Domain。
Domain 是 execution identity；Executor 是该身份采用的排队策略。每个 canonical `const`
Domain binding 恰好懒创建一个程序级共享 Executor：

```jiang
struct InlineExecutor: Executor {
    Void enqueue(Self& self, ExecutorJob job) {
        job.run();
    }
}

struct InlineDomain: Domain<kind = .serial> {
    associated ExecutorType = InlineExecutor;

    InlineExecutor make_executor(Self& self) {
        InlineExecutor()
    }
}

const InlineDomain inline_domain = InlineDomain();
```

普通 Domain value 则是拥有者管理的运行时身份。它与 `const` Domain 使用同一个 `Domain`
trait、`Executor` contract、serial gate 和 Task ABI，不引入第二套协程模型。它支持 move、
参数、返回值、字段和 generic 流转；Task 和 `coroutine.sync` 通过共享引用选择它：

```jiang
SceneDomain domain = SceneDomain(config = config);
Task<Int> task = Task(domain = domain$.ref()) { load_scene() };
Int value = coroutine.sync(domain$.ref()) { update_scene() };
```

`async [D]` 和 domain-bound callable type 的 `D` 是静态 effect identity，仍只接受 canonical
`const` Domain binding。普通 Domain value 不进入函数类型，因此不需要 dependent effect 或把
Domain 类型参数加入 `Task<T>`。

普通 Domain owner drop 是 non-blocking 的，不广播取消已启动的 Task。每个已接受的 Task
持有 execution lease：即使 Task 还没开始执行，Domain owner 也可以先离开作用域；Executor
只在 owner 和最后一个 lease 都释放后销毁。Task 不因此携带 Domain value 的 borrow lifetime。
`coroutine.sync(domain$.ref())` 则保持普通共享借用直到 closure 及其结构化子协程全部完成；
调用返回后借用结束，结果不携带 Domain lifetime，也不需要为 Executor 建立 execution lease。
`make_executor` 的结果必须自包含，不能保存对 Domain receiver 或其配置字段的引用。

普通 Domain 当前可以放在 local、参数、返回值、aggregate 字段和 generic value 中，
不能直接放入 global storage。长期全局身份应使用 canonical `const` Domain binding。
命名 `const` Domain 具有稳定的程序级 identity，调度开销低于普通 runtime Domain；长期共享身份
应优先使用它。普通 Domain value 为独立 identity 和确定资源生命周期支付少量动态开销，适合页面、
场景或会话等有限生存期资源。选择应以所有权语义为主，而不是把需要及时释放的 Executor 改成常驻值。

`Executor.enqueue` 是同步方法，但可以把 move-only `ExecutorJob` 放入自己的队列后再运行。
它可能从多个线程并发调用，因此 Executor 的可变状态必须使用显式同步。
Job 被接收后必须最终恰好运行一次；未运行、重复运行或在仍有 pending Job 时销毁均违反
contract。Domain 的 `.serial`
保证由 runtime 的 per-domain gate 维护，即使多个串行 Domain 复用同一种并发 Executor，
它们也拥有各自独立的执行身份和串行序列。

跨线程共享简单标量状态使用 `Atomic<T>`。`get()`、`set()`、`get_and_set()` 和
`compare_and_set()` 默认使用 sequential order；同名重载接受 `MemoryOrder.relaxed`、`acquire`、
`release`、`acquire_release` 或 `sequential` 中对该操作合法的顺序。Atomic 只支持后端保证 lock-free
的整数、Bool 和 raw pointer 标量；它是显式内部可变性入口，写操作不要求外部 binding 带 `!`。

同步临界区使用 `Mutex<T>.with_lock<R>(Fn<R, T&!>)`。Mutex 将 lock 与受保护值绑定，只在同步
callback 期间提供 `T&!`，callback 返回后自动解锁。公开 API 不提供 guard，锁的作用域只能由
`with_lock` callback 表达；callback 返回值受生命周期约束，不能让受保护值的引用活过锁。
`Mutex<T>` 是 `!Movable`，当前不提供 poison 状态或公共 Channel/RwLock API。

后续最小 Channel 采用 unbounded async MPMC，而不是建立新的 Task 或 scheduler。Channel 本体地址固定，
通过 owner handle 共享；同步 send 成功时转移 value，closed 时把未发送 value 交还调用方；async receive
返回 optional，close 后先 drain buffer 再返回 null。receive 对 Channel 的 borrow 覆盖整个挂起期，
因此仍有 waiter 时不能析构 Channel。pending receive 的取消与 send 在同一 lock 下线性化，保证 value
和 waiter 都只被一方取得。实现使用 amortized O(1) ring buffer 和可 O(1) 移除的 FIFO waiter；不把
Task queue 当作消息队列，也不引入第二套 cancellation。该设计尚未进入 0.5.3 public API。

未来 async sleep 只由 `std.time` 提供 cancellable timer source；timer 到期后恢复 Continuation，runtime
仍负责把 coroutine 调度回其 current Domain。timer 不拥有 Executor，也不建立专用 Task 或 scheduler。
在 macOS/Linux provider 都能保证注册与取消竞态安全前，不公开阻塞线程或无法取消的占位 sleep。

## Struct 与 Enum

`struct` 用于普通名义类型，支持类型函数、实例函数、`init` 和 `deinit`。

`enum` 是统一的 nominal sum type。无 payload enum 表示有限命名整数集合；variant 可以写成
`case(T)` 或 `case(T name, U other)` 并携带 payload。

payload enum 使用统一的 tagged-sum layout、move、borrow、drop、pattern 和 JIL 语义。
enum variant 和普通类型函数/实例函数共用 `Type.member` 访问面，不能同名。
enum variant 的外部可见性由外层类型是否 public 控制。

当前命名空间规则：

- module/package/import alias 使用 namespace domain。
- 顶层类型、trait 和 associated type 使用 type namespace。
- 函数、全局变量、builtin value 和普通方法使用 value namespace。
- 全局变量使用静态初始化；有初始化器时必须产生可物化的编译期值，共用 JIL 求值及 borrow/drop
  语义，不引入运行期模块初始化顺序，也不允许初始化器读取运行期全局状态。存储在运行期是否可变由声明决定。
- 字段和 enum case 使用 member namespace。
- 每个 type namespace provider 拥有自己的 member/type/value 子 namespace，供 `Type.member`
  路径继续解析；`struct`、`enum`、builtin type 和大部分语法糖类型都属于
  type namespace provider。
- enum variant 虽然底层在 member namespace，仍会和 method 的 value namespace 做额外同名冲突检查。
- `Tuple` 和 `Fn` 暂时不作为可扩展 namespace provider；后续如果需要 tuple method 或函数类型
  method，再单独冻结 lookup 和 ABI 规则。

示例：

```jiang
enum Value<T> {
    none,
    some(T) = 2,
    pair(T value, Bool enabled),
}
```

enum 使用 variant-first 语法，无 payload variant 不需要写 `Void`。case 必须位于成员之前；
存在 method 或嵌套 nominal 成员时，用 `;` 分隔。
payload variant 保留整数 enum 的 underlying type、隐式递增值和显式 discriminant；variant 上的
`@life` 等语义注解使用统一的 lowering 和检查管线。
显式 discriminant 是编译期整数表达式，可引用 const 或调用编译期可执行函数；求值结果仍须满足
底层整数范围且不能重复。未指定的后续 variant 从前一个判别值递增。
值到整数的转换使用目标整数类型构造表达式；无 payload 整数 enum 保留
`Type.init?(integer)` 查找已声明 case 的能力。

## Trait 和 Extend

`trait` 描述行为约束。

`extend` 给已有类型增加实现或方法。

泛型 extension 使用独立且显式的模式参数列表：`extend <T> Foo<T> {}`。推荐在 `extend` 和 `<T>`
之间保留空格，为未来的 `extend [options] <T>` 形式保留清晰结构，但该空格不是语法强制要求。
目标类型中的名称不会隐式成为模式参数；未在 `extend <...>` 中声明的名称按普通类型名解析。
`extend Foo<T> {}` 只有在
作用域中确实存在类型 `T` 时才合法，否则报告 `unresolved_type`。`_` 是匿名类型占位符，不创建绑定。

具体 extension target 会在 Semantic Model lowering 时归一化为 canonical owner pattern 和相等约束。例如
`extend Int?` 等价于 `@where(T == Int) extend <T> T?`，`extend Int[4]` 等价于对 array element 和
count 分别添加 `T == Int`、`N == 4`。member lookup 只执行统一的 extension where predicate 匹配。

extension binder 是独立的语义参数 owner，不按位置复用 target owner 的 generic DefId，也不要求两边
参数数量相同。target/equality pattern 在 lookup 时递归生成 `GenericBindings`，并将绑定统一应用到
where predicates、成员参数、返回类型和函数值。`extend <T> T[]^` 可直接捕获嵌套的 `T`；
无法从 target/equality pattern 推导的 binder 报 `unbound_extension_parameter`。

extension 不使用全局 orphan 禁令，用户模块可以扩展 builtin 或其他模块公开的类型。普通 extension 只在
声明模块可见；`public extend` 通过 import graph 传播，`public import` 可以继续 re-export。member lookup
只考虑使用点可见且 pattern/where predicates 满足的 extension；concrete pattern 优先于 generic pattern，
同 specificity 的多个可调用候选报告 ambiguity，不按声明顺序静默选择。

示例：

```jiang
trait Equatable {
    Bool equal(Self& lhs, Self& rhs);
}

trait Hashable: Equatable {
    Void hash<H: Hasher>(self, H&! hasher);
}

trait Indexable {
    Int to_index(self);
    Self from_index(Int index);
}
```

当前规则：

- method 不进入模块顶层命名空间；它们挂到对应 type namespace provider 的成员集合中。
- `extend` 的目标只要求是可扩展 type namespace provider，不要求目标是源码中的 nominal
  `struct`。因此 builtin type、array、slice、sentinel slice、pointer/reference/box 等类型
  都可以通过 core 或 std 源码挂载方法和 trait implementation；`Tuple` 和 `Fn` 暂不支持。
- 第一个参数是 `self` 的类型内部函数是 instance method，`self` 的类型为 `Self&`。
- 第一个参数是 `Self self` 的类型内部函数是 move receiver method，调用会消耗 receiver。
- 没有 receiver 参数的类型内部函数是类型函数，函数体中不能使用 `self`。
- `init(self, ...)` 是 unnamed constructor，通过 `Type(...)` / `new Type(...)` 调用；
  `init name(self, ...)` 是 named constructor，通过 `Type.name(...)` / `new Type.name(...)`
  调用。两者都拥有初始化中的 `self` 目标，直接写入调用方提供的 `Self` storage，不先产生临时值。
  named init 只参与同名构造调用，不作为普通函数值暴露；泛型 owner 参数写在类型上，例如
  `Box<Int>.make(...)`。
- 字段能否被赋值由字段名后的 `!` 和访问路径的可写能力共同决定。修改 receiver 需要 `Self&! self`。
- 默认 `value.method(args...)` 等价于 `Type.method(value$.ref(), args...)`；`Self self`
  方法等价于传入 `value$.move()`，调用后原 receiver 失效。
- 如果 receiver 已经是 pointer/reference，`ref.method(args...)` 也等价于 `Type.method(ref, args...)`。
- `Type.method(receiver, args...)` 是显式方法调用形式；第一个实参必须匹配 receiver 类型。
- instance method 作为函数值时，显式 receiver 保留为第一个参数。例如 `Int get(self)`
  的函数值类型是 `RawFn<Int, Self&>`；`Int take(Self self)` 的函数值类型是 `RawFn<Int, Self>`。
  类型函数没有 receiver 参数。
- trait 可以声明没有 receiver 参数的类型函数 requirement，通过 `Type.method(args...)`
  调用，也可以在泛型约束中通过 `T.method(args...)` 调用。
  带 `self` 参数的 trait function requirement 是实例函数 requirement。
- trait 本身不是普通值类型；动态 trait view 通过 compiler-provided companion type
  表达：`Trait.Any` 和 `Trait.Receiver`。`Trait$.ref(value)` 生成 borrowed dynamic view，
  不移动原值；`Trait$.new(value)` 生成 owning dynamic view，返回 `Trait.Any^`。
  `Trait.VTable` 是 compiler-private 方法表类型，用户源码不能直接命名或传参。当前实现支持
  shared/mutable ref receiver method 的动态分派和 owning trait object drop，暂不支持 move receiver
  trait object dispatch。
- 泛型 receiver 的实例方法签名必须用实际 receiver type args 实例化后再检查。例如
  `Holder<T>.get() -> T` 在 `Holder<Int*!>` 上调用时，结果类型为 `Int*!`。
- enum variant name 和同一 enum 的类型函数/显式 method name 共享类型成员命名空间，
  不能重名，避免 `Enum.member(...)` 歧义。
- 同名函数和同名方法允许 overload；参数数量、参数类型或默认参数可接受范围必须
  能区分调用。
- `extend Type: Trait { ... }` 当前做基础 conformance 检查：trait 必须存在，required method 必须有同名、同参数、同返回类型实现。
- `Hashable` 继承 `Equatable`；可作为 hash key 的类型必须同时定义 hash 和相等比较。
- `Movable`、`Copyable`、`Mutable`、`Sendable`、`Domain`、`Contiguous`、`Hashable`、
  `Equatable` 等属于 compiler core trait。std prelude 只导出同一个 DefId；即使后续启用 no-std，
  它们仍然是语言核心约束。

当前已经支持 trait parent 继承和循环诊断、required method 签名检查、associated type/const 实现、
associated type bound、显式 projection、trait-list associated binding 和 where-bound member lookup。
这仍不是通用逻辑程序式 trait solver；候选歧义、递归约束和更复杂 higher-ranked 规则需要保持
显式限制。

未定事项：

- generic associated type 与 higher-ranked bound。
- move receiver trait object dispatch。
- 更复杂递归约束的终止与歧义规则。

## 泛型和 Type Bound

泛型参数写在声明名之后：

```jiang
T id<T>(T value);
```

约束使用 leading attribute：

```jiang
@where(T: Hashable)
T id<T>(T value);
```

支持多个约束：

```jiang
@where(T: Hashable, U: Equatable)
```

支持 intersection bound：

```jiang
@where(T: Hashable & Equatable)
```

支持类型相等/不等约束、negative trait bound，以及 associated type equality constraint；相等关系使用 `==`，不使用赋值语义的 `=`。associated type projection 需要显式写出 trait，以避免短投影歧义：

```jiang
@where(T: Sequence, T.[Sequence].Element == Int, T: !Mutable, T != _^)
```

类型相等/不等约束中的右侧类型可以作为形状 pattern 使用，`_` 匹配单个 type argument。
例如 `@where(T == _^)` 匹配任意 owning pointer，`@where(T != _?)` 排除 optional。
内建后缀类型在 pattern 中直接按 canonical type 处理。

`@where(T: Copyable)` 表示泛型 body 可以隐式复制 `T`；只有 `T: Movable` 时，按值使用仍是 move。
`@where(T: !Movable)` 是真正的 negative trait bound，表示 `T` 地址固定，不能把它当作旧版的
“允许复制”约束。negative bound 对其他 trait 也统一表示“不实现该 trait”。

当前 AST 使用：

- `WhereConstraint`：一条泛型约束。
- `TypeBound`：约束右侧的 bound 表达式。
- `TypeBoundIntersection`：`A & B`。

后置 `T id<T> @where(...)` 不支持。

## Optional 和 Errorable

Optional 只使用 `T?` 表示。`Int?` 表示 `Int` 值可能为空。Optional 不再幂等：
`T??` 表示两层 optional；optional 类型层不支持 `!`。

已确定表达式能力：

- optional chaining: `value?.field`
- coalesce: `value ?? fallback`
- guard: `guard value is .some(payload) else { return; }`
- 强制解包: `value$.some()`
- 条件解包 pattern: `value is .some(payload)`
- 可重赋值条件解包 binding: `value is .some(Int payload!)`
- 借用解包 pattern: `value is .some(ref Int payload)`
- 唯一可变借用解包 pattern: `value is .some(ref! Int payload)`

`.some(...)` / `.none` 是 optional 的 pattern 写法。`some` 是普通标识符，
不再作为 optional pattern 关键字。`ref` 是绑定模式，不是类型名；
`ref T payload` 创建共享借用；`ref! T payload` 创建唯一可变借用并得到 `T&!`。
如果只需要让新绑定可重新赋值，写 `ref T payload!`；绑定名上的 `!` 不改变借用能力。

示例：

```jiang
if value is .some(payload) {
    // payload: T
}

if value is .some(Int payload!) {
    // payload: T；binding 可重新赋值
}

if value is .some(ref Int payload) {
    // payload: T&
}

switch value {
    .some(payload) => ...
    .some(ref Int payload) => ...
    .none => ...
}
```

同一个 optional match/switch 层级中，不同 `.some(...)` 分支匹配范围相同，只是绑定形式不同，因此只能出现一个 `.some(...)` 分支，同时出现是编译错误。

目标设计偏向显式 optional handling。是否支持 `x == null` / `x != null`
分支窄化仍未定；在定稿前，sema 不应依赖该能力。

Errorable 只使用 `T@E` 表示。`T` 是成功值类型，`E` 是错误类型。错误类型顶层不能带
`?` 或 `!`。

合法：

```jiang
Int@Error parse();
Int?@Error parse_optional();
RawFn<Int@(T1?, T2)> parse_tuple_error;
```

非法：

```jiang
Int@Error? parse_optional_error();
Int@Error! parse_mutable_error();
```

errorable 函数调用在同错误类型的函数中透明投影为成功类型 `T`，失败时自动向上一层传播；成功值可
直接参与表达式。`try call() catch error { ... }` 对单个调用点阻止自动传播并处理 error 分支；
不需要错误值时写作 `catch { ... }`。
`throw expr` 只能出现在返回 errorable type 的函数中，`expr` 必须可赋给该函数的 error type。
catch binding 只在 catch body 内可见，类型来自被处理 errorable value 的 error type。

## 控制流

语句：

- `return`
- `throw`
- `break`
- `continue`
- `defer`
- `guard`
- `if`
- `switch`
- `while`
- `for`
- block
- assignment
- expression statement
- local variable declaration

表达式：

- if expr
- switch expr
- try catch expr
- binary/unary expr
- call/field/index/slice/postfix expr

同一个 `switch` 分支可以用逗号列出替代模式。各模式必须绑定相同的名字，且对应绑定的类型、
可变性与借用方式一致；payload 位置可以不同。匹配成功的模式初始化公共 body 的同一组绑定。

`stmt` 和 `expr` 在语法上保持分离。`block` 的语法以 `doc/grammar.md` 为准：
`block <- "{" stmt* tail_expr? "}"`。`stmt` 不贡献 `block` 的值；`block`
作为表达式使用时，其值只来自最后一个不带分号的 `tail_expr`。没有
`tail_expr` 的 `block` 值为 `Void`。Jiang 没有通用表达式语句，只有
`call_stmt`、赋值、控制语句和声明等明确 statement 形态可以在语句位置出现。
因此：

```jiang
Int x = {
    foo();
    1
};
```

上面的 `block` 类型为 `Int`。如果没有最后的 `tail_expr`，或者最后一个
源码元素是赋值、局部变量声明、`defer` 等语句，则 `block` 类型为 `Void`。

语句 result type 规则：

| 语句 | result type |
| --- | --- |
| `call_stmt` | `Void` |
| `block` | block 的 tail expr result type；无 tail expr 时为 `Void` |
| `return expr?;` | `Never` |
| `throw expr;` | `Never` |
| `break;` | `Never` |
| `continue;` | `Never` |
| `var_decl_stmt` | `Void` |
| `destructure_stmt` | `Void` |
| `assign_stmt` | `Void` |
| `defer_stmt` | `Void` |
| `guard_stmt` | `Void` |
| `while_stmt` | `Void` |
| `for_stmt` | `Void` |

`Never` 表示该语句不会正常继续执行，可以在分支类型统一时转换为任意目标类型。
`return`、`throw`、`break`、`continue` 仍然是语句，不属于普通表达式语法。

`guard expr else { ... }` 用于提前退出并把 pattern 绑定带到后续作用域。
`else` block 必须非空，且最后一条语句必须是 `return`、`break`、`continue`
或 `throw`。更复杂的“所有分支都退出”由后续控制流分析处理。

`defer` 在当前块退出时按 LIFO 顺序执行。`defer` 内不支持 `return`、`break`、`continue`。

### For-in、Sequence 和 Iterator

`for pattern in expr` 根据值提供的协议选择遍历方式。`Sequence.Element` 是 iterator 实际产生的
类型，既可以是新产生的 owned value，也可以是绑定 source 的 reference。`Collection` 继承
`Sequence`，进一步表示有限、可重复遍历并可查询长度的集合。

当前内建 iterable：

- `start..end`：range，左闭右开，item type 是 `Int`。
- `T[]&`：slice view，item type 是 `T`。
- `T[N]`：定长数组，item type 是 `T`。
- `T*` / `T*!`：raw pointer 不是 iterable；需要遍历时使用 range 产生 index，再在 `unsafe` 中用 `p[i]` 访问。

自定义遍历协议分为两层：

```jiang
trait Iterator {
    associated Element;
    @life(return: self)
    Element? next();
}

trait Sequence {
    associated Element;
    associated Iter: Iterator<Element = Element>;
    Iter make_iterator();
}

trait Collection: Sequence {
    Int length();
    Bool is_empty() { return length() == 0; }
}

trait Contiguous {
    associated Element;
    Element* ptr();
    Int length();
}
```

`Iterator` 是有状态游标，`next()` 每次返回下一个元素，`none` 表示结束。
`Sequence.make_iterator()` 产生游标，`Collection` 增加 `length()` 和默认 `is_empty()`。
`Contiguous` 是正交的 storage capability，不是所有 collection 的前提；它的 `Element` 表示底层连续
元素类型。`Vector<T>` 同时满足 `Collection` 和 `Contiguous`：前者继承的
`Sequence.Element == T&`，后者的 `Contiguous.Element == T`。`HashMap` / `HashSet` 是
`Collection`，但不是 `Contiguous`。type check 负责选择具体 iteration plan，并把 pattern 的 expected
type 设为 iterator element；JIL lowering 只消费这个 plan，不重新做 trait lookup。

HashMap 的 hash seed 属于实例内部状态，不进入 public identity、equality 或 artifact contract。有系统
entropy 时，每个实际获得 storage 的 map 生成一次 seed，rehash 保持该 seed；lookup 热路径只做纯 hash
计算。缺少 entropy 的 target 使用内部 fallback 保持 collection 可用，但不承诺抗恶意碰撞。

`Path` 直接拥有 native byte slice；`PathBuilder.finish()`、lexical normalize 和 extension replacement
都把已有 storage 转移给结果，不额外建立文本表示。0.5.3 只冻结 macOS/Linux POSIX separator 语义；
Windows drive/UNC path grammar 留到对应 hosted provider 完成时统一设计，不让当前 API 假装跨平台等价。

## Pattern Matching

pattern 目前包括：

- literal
- variant
- optional
- tuple

binding/wildcard 只作为子 pattern 使用，不能作为 `is` 或 `switch` 分支根。
tuple pattern 可以作为分支根，并递归保留括号层级。
payload enum/optional 的 Tuple payload 直接展开一层，因此 `(Int, Int)` payload 使用
`.case(left, right)`，不是 `.case((left, right))`。

binding 的统一语义形态是 `binding_mode? type_pattern name binding_mutability?`。
match payload 中可以省略 type pattern；所有 `ref` / `ref!` binding 也可以省略，
例如 `ref item` 等价于 `ref _ item`。独立 by-value 解构必须保留类型位置，并且
整个解构必须写在括号内。type pattern 支持 `_` 以及 `Int[_]`、`_[3]` 这类局部推导。

普通变量定义不接受左侧 `ref`；引用变量通过 `value$.ref()` / `value$.mut_ref()`
作为 RHS 初始化。

`is` 用于 pattern matching，不再使用 `==` 表达 pattern 解构。

示例方向：

```jiang
if value is .some(payload) {
}

if block is .some(Int dead!) {
}
```

普通 enum variant 和 optional 都使用 dot case pattern；optional 不再支持旧 `some payload` pattern。

## Module 和 Visibility

目标规则：

- `import` 只导入当前模块使用，不做 re-export。
- `import dep;` 按 module/package 名称解析，并绑定模块命名空间 `dep`。
- `alias alias = import "path.jiang";` 按当前文件目录相对路径解析，并绑定模块命名空间 `alias`。
- file import 必须显式写出目标文件路径，不隐式补扩展名或目录入口。
- 被导入模块的 public API 通过 `module.Name` 访问，不默认平铺到当前模块。
- `public import` 导入当前模块使用，并将被导入模块作为当前模块 public API 中的一个模块
  命名空间重新导出。它不摊平被导入模块的声明；例如 `middle` 中 `public import leaf;` 后，
  外部通过 `middle.leaf.Name` 访问，而不是 `middle.Name`。
- `public alias name = target;` 将 symbol 重新导出到当前模块 public namespace；函数目标保留
  目标 namespace/name 下的 public overload family。
- `public` 标记声明对外可见。
- 基本类型不是关键字，由名字解析绑定到内建声明。

ambiguous re-export 仍需后续完善；package dependency 第一版只支持本地源码路径。

当前 resolver 已区分 module、type、value 和 member domain；字段和 enum case 使用 member
domain，associated type 使用 type domain。`foo.Bar` 根据左侧已解析的 module/type/value root 继续查找，
不会仅凭文本在 JIL 或 backend 重判。`import dep;` 优先查当前 package dependency alias。

仍需继续收口的是跨多个 public re-export 路径的 ambiguity 诊断，以及更细的 shadowing policy；
这些规则
不能改变已经建立的 namespace domain 和 package visibility 边界。

## 编译期执行

目标规则：

- `comptime { ... }` 是语言内建编译期 block，表示 block 内 Jiang 代码在编译期执行。
- `comptime` 使用普通关键字入口，不占用后续 `#sql { ... }`、`#asm { ... }` 这类 custom syntax
  namespace；`@` 保留给 attribute。
- `comptime` 唯一形式为 `comptime { ... }`，不接受方括号；`eval` 与 `generate` 都是普通标识符。
- comptime 在语义分析需要结果时执行；生成任务由独立入口和命令承载。
- `comptime` block 不生成 runtime code。
- `comptime` block 内使用普通 Jiang 语法。`if`、布尔表达式、字段访问、枚举比较等都复用普通
  parser、resolve、type check 和 const eval，不引入 `#if` 小语言，也不维护第二套 compile-only
  AST/type system。
- `comptime` block 内的普通分支遵循常规名字解析和类型检查；未执行分支不执行副作用，
  其中的 import 表达式也不加载目标模块。不能借由未执行分支隐藏普通名字或类型错误。
- 完整 parse `comptime` block，未执行分支里的语法错误仍然诊断。
- comptime 中 `if` 的 condition 是普通表达式，必须能在编译期求值为 `Bool`。先登记当前 source 的
  普通声明，再按需检查并执行条件的依赖，求值结果决定需要发现的 import source；
  不要求先封闭整个 Sema，也不要求 LLVM/linker 同时渐进式化。
- eval 可以读取已经发现 source 中的普通 const，并按需调用符合 comptime 安全边界的普通
  函数。依赖尚未由当前 source-selection 路径选中的 declaration 时，不猜测分支；对应查询状态输出
  不可达或依赖循环诊断。
- eval 不隐式执行 IO，不读取运行期变量；在编译期执行内部声明、初始化和修改的局部变量不属于运行期状态。
- comptime 在各个位置使用统一的处理和求值流程，并具有自己的普通词法作用域；块内 const
  和局部变量不发布到外部 namespace。结果通过块的返回值带出，外部声明决定绑定位置。
  声明位置由统一语义检查判断，不为 import 和 alias 分别维护作用域解析流程。
- alias 声明必须直接位于 namespace 中，初始化表达式隐式在编译期求值。
  `import "path"` 表达式产生 namespace 值，可作为 if 分支或 comptime block 的结果。
  独立的 `import "foo.jiang";` 默认绑定名字 foo，额外要求直接位于 module namespace；
  该规则不限制初始化表达式内部的 import 求值。独立 import 不因位于 comptime 中而穿透局部作用域。
- `const foo = expr` 要求初始化结果是 comptime value；允许由初始化结果推导类型，也可显式
  写出类型。整个初始化表达式处于隐式 comptime 上下文，函数调用和包含局部变量、赋值、
  循环的 block 表达式也在编译期执行，不要求额外写 comptime。
  依赖泛型参数的初始化表达式在具体实例中求值，不同实例不共享一个具体结果。
  普通运行期变量不能因被 const 初始化器引用就自动变成编译期值。
- comptime block 可以返回值，外部 const 通过初始化表达式接收该结果，名字归属由外部声明
  的位置决定。局部计算使用同一次执行中的存储和生命周期；结果不得保留对已结束局部存储的引用。
- const 声明只绑定单个名字，不支持 const 解构；聚合常量可通过成员读取参与后续常量计算。
- import 的位置要求独立于 const 和 comptime 块的词法作用域规则；不能用 import 的限制
  禁止局部编译期计算。
- eval、普通 const、数组长度、const generic 和 enum discriminant 共用 JIL 求值，generate 也复用
  同一执行语义，不维护独立的 AST 或 Semantic Model 表达式解释语义。
  JIL 执行遵守 borrow/drop、target 布局及执行配额，编译器回收内存不代替语言 deinit，host 地址不得逃逸。

示例：

```jiang
import build;

alias provider = if (build.target.os == .macos) {
    import "os/macos.jiang"
} else if (build.target.os == .linux) {
    import "os/linux.jiang"
} else {
    import "os/unsupported.jiang"
};
```

这里 `if` 是普通 Jiang 表达式；alias 初始化隐式编译期求值，condition 必须能求得 Bool。
只有选中的导入路径进入模块依赖图。分支末尾加分号会形成独立导入声明，因所在位置是局部
block 而诊断，不把这种写法解释成静默丢弃 namespace 值。

编译器提供 `build` virtual package 承载本次构建的编译期信息。`build` 下直接平铺常用 facts，
不引入 `BuildInfo` 总结构。目标形态包括 `build.target`、后续的 `build.mode`、
`build.compiler`、`build.features` 等。当前 `build.target` 以
`public const TargetInfo target` 的形式暴露，包含 `os`、`arch`、`abi` 和 `link_libc`。
普通源码、`comptime` 条件和 public const aggregate 字段读取都通过同一套 value path / const value
机制访问这些 facts。

## 自定义语法补充规则

当前自定义语法固定为 syntax-stage lang package 机制，入口见本文前面的
“Lang Package / 自定义语法”章节。

补充原则：

- `#` 保留给 custom syntax / lang invocation；`comptime {}` 是核心语言语法，`@` 用于 attribute。
- host lexer 只识别 provider path 和 raw block envelope；DSL body 的内部 token/cache 由 provider 自己维护。
- 自定义语法必须返回 Jiang syntax tree，不能通过字符串拼接回灌 host parser 来隐藏错误位置。
- 自定义语法不能绕过基础语言的错误恢复、诊断 span 和 IDE/LSP 能力。
- Semantic Model-level、JIL-level 或 backend-level plugin 不属于当前 Lang Package 设计。

## 后续提案

### API Effect

第一版不实现通用 API effect system。当前函数是否能写入某个 place，由 binding 名后的 `!`、
receiver 类型和 `T&!` / `T*!` capability 决定；方法不需要额外 `mutating` 标记。现有
`unsafe`、`async` 和
Domain effect 是调用上下文与 ABI 的语言规则，不属于下面设想的行为摘要。

后续可以引入 `@effect(...)` 作为 API 行为契约，而不是借用类型系统的一部分。例如：

```jiang
@effect(read)
Int get(self) {
    return self.value;
}

@effect(write(self))
Void set(Self&! self, Int value) {
    self.value = value;
}

@effect(write(self), io, alloc)
Void save(self, File& file) {
}
```

候选 effect 包括：

- `read`：不修改 receiver、参数或 global 可见状态，不调用 unknown/write/io 函数。
- `write(self)` / `write(arg)` / `write(global)`：可能修改对应对象或状态。
- `io`：执行输入输出。
- `alloc`：分配内存。

未标注函数在该提案中默认为 `unknown` / impure，不强制第一版代码全量标注。若未来启用检查，`@effect(read)` 函数中写入 `self` 的 `!` 字段应编译失败；trait requirement 也可以携带 effect，要求实现不比 requirement 更“脏”。
