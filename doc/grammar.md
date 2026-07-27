# Jiang PEG 语法

本文档使用 PEG 风格描述 Jiang 语法。PEG 的选择是有序选择：`A / B`
表示先尝试 `A`，失败后再尝试 `B`。

记法约定：

- `rule <- expr` 表示规则定义。
- `A / B` 表示有序选择。
- `A?` 表示可选。
- `A*` 表示重复零次或多次。
- `A+` 表示重复一次或多次。
- `&A` 表示正向预判，不消耗 token。
- `!A` 表示负向预判，不消耗 token。
- 终结符使用双引号，例如 `"struct"`。
- `ident`、`int_lit`、`string_lit` 等为词法 token。

## 词法 token

```peg
ident       <- /* ASCII [_A-Za-z][_A-Za-z0-9]*，或符合 Unicode XID_Start / XID_Continue 的 UTF-8 标识符 */
escaped_ident
            <- "`" /* 任意非反引号字节序列 */ "`"
int_lit     <- decimal_int / binary_int / octal_int / hex_int
decimal_int <- digit ("_"? digit)*
binary_int  <- ("0b" / "0B") binary_digit ("_"? binary_digit)*
octal_int   <- ("0o" / "0O") octal_digit ("_"? octal_digit)*
hex_int     <- ("0x" / "0X") hex_digit ("_"? hex_digit)*
float_lit   <- decimal_int "." decimal_int exponent?
             / decimal_int exponent
exponent    <- ("e" / "E") ("+" / "-")? decimal_int
char_lit    <- /* 字符字面量 */
string_lit  <- /* UTF-8 字符串字面量 */
provider_path
            <- ident ("." ident)*
raw_block   <- /* `#provider_path { ... }` 中由 lang provider scan 确定边界的原始 block */

literal     <- int_lit
             / float_lit
             / char_lit
             / string_lit
             / "true"
             / "false"
             / "null"
```

## 源文件

```peg
file        <- top_level_item* eof

top_level_item
            <- compile_block
             / intrinsic_block
             / extern_block
             / top_level_decl

compile_block
            <- "comptime" "{" top_level_item* "}"

intrinsic_block
            <- intrinsic_attribute leading_annotation* "{" member_decl* "}"

intrinsic_attribute
            <- "@" "intrinsic" "(" intrinsic_receiver_kind "," type ")"

intrinsic_receiver_kind
            <- "value" / "type"

extern_block
            <- "extern" "{" extern_item* "}"

extern_item <- "public"? (extern_function_decl / extern_global_decl)

```

## 声明

```peg
top_level_decl
            <- leading_annotation* decl_modifier* top_level_decl_body

member_decl <- leading_annotation* member_modifier* member_decl_body

leading_annotation
            <- "@" "where" "(" where_constraints ")"
             / "@" "life" "(" life_constraints ")"
             / "@" "alias" "(" alias_attribute_bindings ")"

alias_attribute_bindings
            <- alias_attribute_binding ("," alias_attribute_binding)* ","?

alias_attribute_binding
            <- name "=" type

decl_modifier
            <- "public"
             / "extern" keyword_options?

member_modifier
            <- "public"

keyword_options
            <- "[" keyword_option ("," keyword_option)* ","? "]"

keyword_option
            <- name (":" expr)?
             / type

top_level_decl_body
            <- import_decl
             / alias_decl
             / nominal_decl
             / trait_decl
             / extend_decl
             / function_decl
             / const_global_decl
             / global_decl

member_decl_body
            <- alias_decl
             / nominal_decl
             / trait_decl
             / function_decl
```

Jiang 统一把 `@where(...)`、`@region(...)`、`@life(...)`、`@intrinsic(...)` 这类
`@name(...)` 形式称为 attribute。`@intrinsic(value, T)` /
`@intrinsic(type, T)` 是编译器内部声明 `$` 内禀操作的 attribute block，
只允许编译器内部源码和标准库内部源码使用；普通用户源码写 `@intrinsic` 会报错。
`@life()` 是合法的空 attribute，表示显式空返回 lifetime 契约，不等同于省略 `@life`。
struct / union 的 `@region(a, b, b: a)` 由裸名称声明 public region，并用
`target: source` 表示 outlives 约束。单-slot 字段 binding 写作 `@life(a)`，多-slot
字段 binding 可以按位置写作 `@life(a, b)`，或按 type occurrence 的具名位置写作
`@life(left: a, right: b)`；两种模式不能混用，target 必须唯一且完整。callable
contract 同样使用 `target: source`，但只允许具名 target，不支持 `[0]` 位置路径。

同一个声明前的 attribute 按源码顺序应用，并且都作用在当前声明自己的 namespace 上。
当前声明的泛型参数会先进入这个 namespace；后面的 attribute 可以引用前面 attribute
引入的名字，前面的 attribute 不能引用后面的名字。attribute 引入的名字只在当前声明的
签名、约束、成员和函数体中可见，不泄漏到外层模块。

`@alias(Name = Type)` 是声明局部类型别名。一个 `@alias(...)` 可以包含多个逗号分隔的绑定；
这些绑定等价于按顺序拆成多个 `@alias`，因此后面的绑定可以引用前面引入的名字。

关键字 options 使用 `keyword [options] ...` 形式。`extern [builtin]` 当前只用于标准库、
core 或编译器内部源码，用来声明编译器内建常量和函数；普通用户源码不应依赖这个内部入口。
其中 builtin 函数定义按 public visibility 收集，无需额外书写 `public`。
旧的 `keyword(...)` options 写法不再作为 release 分支语法保留。

```peg
nominal_decl
            <- struct_decl
             / record_decl
             / enum_decl
             / union_decl

import_decl <- "import" (import_alias "=")? import_path ";"

import_alias
            <- name

import_path <- string_lit / ident

alias_decl  <- "alias" name ("=" alias_target)? ";"

alias_target
            <- escaped_ident
             / path
             / type

function_decl
            <- result_type name function_tail

const_global_decl
            <- "const" type name "=" expr ";"

global_decl <- type name global_tail

extern_function_decl
            <- result_type extern_symbol_name function_tail

extern_global_decl
            <- type extern_symbol_name global_tail

extern_symbol_name
            <- name
             / escaped_ident

global_tail <- ("=" expr)? ";"

function_tail
            <- generic_params? "(" param_list? ")" (";" / block)

param_list  <- param ("," param)* ","?

param       <- type binding_name ("=" expr)?

binding_name
            <- name "!"?
```

说明：顶层 `public`、`extern` 由 `decl_modifier` 统一解析。
因此 `struct_decl`、`record_decl`、`enum_decl`、`union_decl`、`trait_decl`
等规则本身不重复写 `"public"`。
`const_global_decl` 同样经由 `top_level_decl` 接受 modifier，因此 `public const Type name = expr;`
是合法顶层声明。
`comptime` block 是顶层 item，不经由 `decl_modifier`，只负责在编译期选择其中的顶层 item。
`global_decl` 只允许出现在 `top_level_decl` 和 `extern_item` 中；类型成员、trait/extend
成员等非顶层声明使用 `member_decl`，不允许定义全局变量。
顶层变量只使用普通 global declaration。独立 destructure 是函数体内的语句，
不能出现在顶层。
默认参数可以出现在参数列表任意位置。位置实参总是绑定最早尚未绑定的参数，不会按类型跳过
带默认值的参数；命名实参可以跳过或重排默认参数。第一个命名实参之后，其余普通实参也必须
使用命名形式。

## 泛型和 where 约束

```peg
generic_params
            <- "<" generic_param ("," generic_param)* ","? ">"

generic_param
            <- name ":" generic_param_bound
             / name

generic_param_bound
            <- type_bound
             / "const" type

where_constraints
            <- (where_constraint ("," where_constraint)* ","?)?

where_constraint
            <- projected_where_constraint
             / name ":" "const" type
             / name ":" type_bound
             / name "==" type
             / name "!=" type

projected_where_constraint
            <- name "." "[" path "]" "." name "==" type

life_constraints
            <- (life_constraint ("," life_constraint)* ","?)?

life_constraint
            <- life_name ">" life_name

life_name  <- name
             / "self"
             / "return"

type_bound  <- trait_bound ("&" trait_bound)*

trait_bound <- "!"? path ("<" trait_bound_arg_list? ">")?

trait_bound_arg_list
            <- trait_bound_arg ("," trait_bound_arg)* ","?

trait_bound_arg
            <- name "=" type
             / type
```

`@where(N: const Int)` 是 const generic constraint 的 canonical 写法。声明列表中的
`N: const Int` 是等价简写，并 lower 到同一条 HIR predicate；两种写法重复出现时会去重，
约束类型不一致时报 `conflicting_const_constraint`。

## 类型

```peg
type        <- type_primary type_postfix*

result_type <- "unsafe"? async_effect? type ("@" type)?

async_effect
            <- "async" keyword_options?

type_primary
            <- "_"
             / path type_args?
             / "(" ")"
             / "(" type "," type ("," type)* ","? ")"
             / "(" type ")"

type_args   <- "<" type ("," type)* ","? ">"

type_postfix
            <- "?"
             / "!"
             / "&"
             / "^"
             / "*"
             / "[" "]"
             / "[" ":" int_lit "]"
             / "[" array_count "]"
             / "[" array_count ":" int_lit "]"

array_count <- "_" / int_lit

path        <- name ("." name)*

name        <- ident / "self"
```

约定：

- `T?` 表示 optional 类型层。
- `!` 作为类型后缀只允许直接修饰 reference 或 raw pointer 外层：`T&!` 和 `T*!`。
  `T!`、`T^!`、`T[]!`、`T?!` 和 `T!&` 都不合法。
- `T&` 表示引用外层。
- `T^` 表示 owning pointer 外层。
- `T*` 表示 raw pointer；主要用于 FFI / ABI / 低层 capability 场景，不参与自动解引用。
- `T[]` 表示 unsized array type，不能作为普通 value；`T[]&` 表示 borrowed slice view。
- `T[:0]` 表示 sentinel unsized array type，`T[:0]&` 表示 borrowed sentinel slice view，
  并额外记录 `data[length] == 0` 的类型语义。
- `T*` 表示只读能力的 raw pointer，`T*!` 表示可写 raw pointer；二者都不携带 length 或 sentinel。
- `T[*]` 和 `T[*:S]` 已移除；连续内存的低层地址使用 `T*` / `T*!`，带长度的安全 view
  使用 `T[]&` / `T[:S]&`。
- raw pointer 和 slice 可以按 C ABI 需要继续叠加，例如 `UInt8**`、`LLVMType**`。
- `T^` / `T&` 是语言级 ownership/reference handle，不能与其他 handle 叠加。
- `T[N]` 表示定长数组，`N` 只能是整数字面量。
- `T[N:S]` 表示 sentinel 定长数组语法；逻辑长度为 `N`，实际 storage 为 `N + 1`
  个元素，末尾元素保存 sentinel。
- `T@E` 表示 errorable result，只能出现在函数、方法和 callable 类型的返回位。
- `T@E` 不允许空白：`T @E`、`T@ E` 和 `T @ E` 都报 `unexpected_trivia`。
- `RawFn<unsafe T, ...>`、`RawFn<async T, ...>` 和 `RawFn<async [domain] T, ...>`
  表示带调用效果的函数指针类型。调用效果
  前缀写在 `RawFn<...>` 的返回类型前，但不修饰返回值类型本身，而是修饰外层函数指针类型。
  因此 `RawFn<async [UiDomain] Bool>[]&` 表示“元素为 UI domain 异步函数指针的切片引用”。
- `Fn<unsafe T, ...>`、`Fn<async T, ...>` 和 `Fn<async [domain] T, ...>`
  表示带调用效果的闭包值类型。调用效果前缀写在
  `Fn<...>` 的返回类型前，但不修饰返回值类型本身，而是修饰外层闭包值类型；因此
  `Fn<async [UiDomain] Bool>[]&` 表示“元素为 UI domain 异步闭包值的切片引用”。
- lambda 必须具有 expected callable type；其 async/unsafe/domain effect 由
  `Fn<...>` 或 `RawFn<...>` expected type 完整决定，lambda 表达式不重复书写 effect 前缀。
- async `Fn` / `RawFn` 动态调用复用普通 async 调用的 Domain 切换；跨 Domain 的参数、result 和
  capture 必须满足 Sendable，普通 borrow 不能跨不兼容 Domain 逃逸。
- `Fn<Ret, Args...>` 表示 Jiang 闭包值，可带 environment；`RawFn<Ret, Args...>` 表示裸函数
  指针，不带 environment，可用于 C ABI 函数指针边界。
- 函数声明只使用 `async` 表示 suspend function；函数前不保留 `sync` 修饰符。`async [domain]`
  可用于函数声明，表示 domain-bound async function。
- `sync [domain]` 是 `sync [domain: domain]` 的短写。`domain` 是实现 `Domain` 的编译期
  domain value；编译器通过 `Domain.kind` 区分 serial/concurrent 语义。
- 函数声明和 callable type 中的 `async [domain]` 只接受静态 domain type。
- 无 domain 的 `Task { ... }` / `sync { ... }` 只能在已有 current domain 的上下文中使用，
  并继承 current。
- 普通同步函数中的最外层 `sync [domain] {}` 阻塞进入 runtime；async context 中的
  `sync [domain] {}` 挂起当前 coroutine，结构化切换到目标 Domain，完成后回到原 Domain，不创建 Task。
- `Task { ... }` / `Task(domain: domain) { ... }` 使用尾随 closure 创建直接 Task；
  `new Task { ... }` / `new Task(domain: domain) { ... }` 创建 owner Task。
- Task creation 是 eager 的。`Task` 返回地址固定的 `Task<T>`，`new Task` 返回可移动、非复制的
  `Task<T>^` owner。`task.await()` 和 `task.cancel_and_await()` 消费一次 result；`task.cancel()`
  只同步发布幂等取消请求，不消费 result，也不等待 Task 退出。旧的 `async call()`、
  `async { ... }`、`new async { ... }`、`callee$().async()` 和 `await expr` 不属于当前语法。
- 需要进入调用效果上下文时使用 `unsafe { ... }` 或 `sync [domain] { ... }`；
  需要并发启动工作时使用 Task initializer。

`T?`、`T[N]`、`T[]&`、`T[:0]&`、`T^`、`T&`、`T*`、`T*!` 等内建后缀类型语法
不通过普通名字解析。compiler-owned constructor 名称不进入用户可见 namespace；用户声明
同名 nominal type 也不会改变这些语法的含义。

## struct

顶层可见性写在外层 `decl` 的 `decl_modifier` 中，例如
`public struct User { ... }`。
`struct` options 顺序无关；重复 option 会报错。`packed` 表示字段按 1 字节排列，
`align: N` 表示结构体整体对齐至少为 `N`，`N` 必须是 2 的幂。

```peg
struct_decl <- "struct" struct_options? name generic_params? trait_list? struct_body

struct_options
            <- "[" struct_option ("," struct_option)* ","? "]"

struct_option
            <- "packed"
             / "align" ":" int_lit

trait_list  <- ":" path ("," path)*

struct_body <- "{" struct_member* "}"

struct_member
            <- member_modifier* struct_member_body

struct_member_body
            <- deinit_decl
             / init_decl
             / assoc_type_impl
             / method_decl
             / field_decl

deinit_decl <- "deinit" "(" "self" ")" block

init_decl   <- "init" "?"? name? "(" param_list? ")" block

assoc_type_impl
            <- "associated" path "=" associated_item_value ";"

associated_item_value
            <- type
             / expr

method_decl <- result_type name function_tail

field_decl  <- type field_init ("," field_init)* ";"

field_init  <- name ("=" expr)?
```

## enum

顶层可见性写在外层 `decl` 的 `decl_modifier` 中，例如
`public enum Color { ... }`。

```peg
enum_decl   <- "enum" ("[" type "]")? name trait_list? "{" enum_member* "}"

enum_member <- member_modifier* method_decl
             / name ("=" expr)? ("," / ";")?
```

`enum [T]` 的 `T` 必须是具体整数类型；未写 `T` 时默认使用 `Int32`。
旧的 `enum(T)` options 形式不再保留。
未显式指定值的 enum case 从 `0` 开始递增；显式值目前只接受整数 literal，包括负整数字面量。

## union

顶层可见性写在外层 `decl` 的 `decl_modifier` 中，例如
`public union Value { ... }`。

```peg
union_decl  <- "union" ("[" name "]")? name generic_params? trait_list? union_body

union_body  <- "{" union_member* "}"

union_member
            <- member_modifier* method_decl
             / union_variants

union_variants
            <- type name ("," name)* ";"
```

## trait

顶层可见性写在外层 `decl` 的 `decl_modifier` 中，例如
`public trait Hashable: Equatable { ... }`。

```peg
trait_decl  <- "trait" name generic_params? (":" path ("," path)*)?
               (";" / trait_body)

trait_body  <- "{" trait_member* "}"

trait_member
            <- leading_annotation* member_modifier* associated_type_decl
             / leading_annotation* member_modifier* trait_method_decl

associated_type_decl
            <- "associated" name (":" associated_item_bound)? ";"

associated_item_bound
            <- type_bound
             / "const" type

trait_method_decl
            <- result_type name function_tail
```

## extend

```peg
extend_decl <- "extend" generic_params? result_type (":" path)? "{" extend_member* "}"

extend_member
            <- assoc_type_impl
             / member_decl
```

泛型 extension 必须在 `extend` 后显式声明模式参数，例如 `extend <T> Foo<T> {}`。推荐保留
`extend` 与 `<T>` 之间的空格，以兼容未来可读的 `extend [options] <T>` 形式；空格不是语法要求。
目标类型中的未声明名称按普通类型名解析，不会隐式引入模式参数；因此找不到类型 `T` 时，
`extend Foo<T> {}` 会报告 `unresolved_type`。`_` 仍表示不绑定名称的单个类型占位符。
extension binder 的数量不要求与 target constructor 的参数数量相同；target pattern 决定绑定关系。
例如 `extend <T> T[]^ {}` 从嵌套 owning slice 捕获 `T`。无法从 target/equality pattern 推导的
binder 会报告 `unbound_extension_parameter`。

## 语句和 block

```peg
block       <- "{" stmt* tail_expr? "}"

stmt        <- return_stmt
             / throw_stmt
             / break_stmt
             / continue_stmt
             / defer_stmt
             / block
             / lang_invocation_stmt
             / guard_stmt
             / while_stmt
             / for_stmt
             / if_stmt
             / switch_stmt
             / destructure_stmt
             / var_decl_stmt
             / assign_stmt
             / call_stmt

tail_expr   <- expr

return_stmt <- "return" expr? ";"

throw_stmt  <- "throw" expr ";"

break_stmt  <- "break" ";"

continue_stmt
            <- "continue" ";"

defer_stmt  <- "defer" (block / expr ";")

guard_stmt  <- "guard" expr_without_block "else" block

// guard 的 else block 必须非空，且最后一条语句必须是
// return、break、continue 或 throw。

var_decl_stmt
            <- type name ("=" expr)? ";"

destructure_stmt
            <- "(" (destructure_binding ("," destructure_binding)*)? ")"
               "=" expr ";"

destructure_binding
            <- tuple_pattern
             / "_"
             / type_pattern name "!"?
             / ref_binding_mode type_pattern? name "!"?

assign_stmt <- expr assign_op expr ";"

assign_op   <- "=" / "+=" / "-=" / "*=" / "/=" / "%="
             / "&=" / "|=" / "^=" / "<<=" / ">>="

call_stmt   <- postfix_expr ";"

lang_invocation_stmt
            <- lang_invocation

if_stmt     <- if_expr

switch_stmt <- switch_expr

while_stmt  <- "while" expr_without_block block

for_stmt    <- "for" binding_pattern "in" expr_without_block block

catch_binding
            <- type name?
             / name

switch_pattern_list
            <- "else"
             / match_pattern ("," match_pattern)*
```

说明：`stmt` 永远不贡献 `block` 的值；`block` 的值只来自最后一个不带分号的
`tail_expr`。没有 `tail_expr` 的 `block` 值为 `Unit`。普通表达式不能随意写成
`expr;`，只有调用语句、赋值语句、控制语句和声明等明确 statement 形态可以带
分号出现。这样可以避免 `T x;` 声明和任意表达式语句在 block 开头互相抢解析。
`call_stmt` 在语法上先解析为 `postfix_expr`，但要求最外层 postfix 必须是调用；
`foo();`、`value$.ref();` 合法，`foo.bar;`、`a + b;` 不合法。

## 表达式

表达式按优先级从低到高定义：

```peg
expr        <- lambda_expr
             / expr_with_block
             / expr_without_block

expr_with_block
            <- block_expr
             / effect_block_expr
             / async_call_expr
             / if_expr
             / switch_expr
             / try_catch_expr

expr_without_block
            <- range_expr

lambda_expr <- "{" lambda_capture_list? lambda_param_list? "=>" stmt* tail_expr? "}"

lambda_param_list
            <- lambda_param ("," lambda_param)* ","?

lambda_param
            <- name
             / "_"

lambda_capture_list
            <- "[" lambda_capture_item ("," lambda_capture_item)* ","? "]"

lambda_capture_item
            <- type name "=" expr
             / name "=" expr

range_expr  <- logic_or_expr ".." logic_or_expr
             / logic_or_expr

logic_or_expr
            <- logic_and_expr ("||" logic_and_expr)*

logic_and_expr
            <- coalesce_expr ("&&" coalesce_expr)*

coalesce_expr
            <- compare_expr ("??" compare_expr)*

compare_expr
            <- bit_or_expr ((compare_op bit_or_expr) / ("is" match_pattern))*

compare_op  <- "==" / "!=" / "<=" / "<" / ">=" / ">"

bit_or_expr <- bit_xor_expr ("|" bit_xor_expr)*

bit_xor_expr
            <- bit_and_expr ("^" bit_and_expr)*

bit_and_expr
            <- shift_expr ("&" shift_expr)*

shift_expr  <- add_expr (("<<" / ">>") add_expr)*

add_expr    <- mul_expr (("+" / "-") mul_expr)*

mul_expr    <- unary_expr (("*" / "/" / "%") unary_expr)*

unary_expr  <- ("-" / "!" / "~" / "&" / "new") unary_expr
             / postfix_expr

postfix_expr
            <- primary_expr postfix_tail*

postfix_tail
            <- type_args "(" call_args? ")"
             / "(" call_args? ")"
             / trailing_closure
             / "?." name
             / "." name
             / "?" "[" index_or_slice "]"
             / "[" index_or_slice "]"
             / "?" "$" implicit_call
             / "$" implicit_call

index_or_slice
            <- expr? ".." expr?
             / expr

implicit_call
            <- "." name "(" implicit_args? ")"

implicit_args
            <- type
             / call_args

call_args   <- call_arg ("," call_arg)* ","?

call_arg    <- (name ":")? expr

trailing_closure
            <- lambda_expr
             / block
```

`range_expr` 表示 range 这一优先级层；只有 `logic_or_expr ".." logic_or_expr`
这种形式会产生 `RangeExpr` AST，普通 `logic_or_expr` 直接向下传递。
`..` 的优先级低于逻辑或。
`expr_without_block` 用于 `guard`、`while`、`for` 等后面紧跟 `block` 的位置。
`paren_expr` 属于 `expr_without_block`，但括号内部重新进入完整 `expr`；
因此 block/struct/if/switch/try-catch 等 `expr_with_block` 可以通过括号出现在这些位置。

`lambda_expr` 必须有 expected callable type。参数类型由 expected type 提供；
参数列表只写绑定名。
`lambda_capture_list` 是可选的 environment 字段初始化列表，每一项形如 `field = expr` 或
`Type field = expr`。这些 initializer 在闭包创建时求值；未列入列表的外层 local 仍可按
默认捕获规则处理：只读 local 按共享引用捕获，写入外层 `!` storage 时按可变引用捕获。
`RawFn<...>` expected type 下不允许任何捕获，也不允许 capture list。

调用的最后一个 callable 参数可以写成尾随闭包：`run { work() }` 或
`map(values) { value => transform(value) }`。尾随位置中不含 `=>` 的 `{ ... }` 会转换成零参数
closure；普通表达式位置的同形语法仍是 block。当前一次调用只允许一个尾随闭包。

## primary expression

```peg
primary_expr
            <- lang_invocation
             / type "$" implicit_call
             / type "." name
             / literal
             / "self"
             / "$"
             / "." "(" call_args? ")"
             / path type_args "(" call_args? ")"
             / path
             / "." name
             / paren_expr
             / array_expr

paren_expr  <- "(" ")"
             / "(" expr "," expr ("," expr)* ","? ")"
             / "(" expr ")"

array_expr  <- "[" (expr ("," expr)* ","?)? "]"

lang_invocation
            <- "#" provider_path raw_block

block_expr  <- block

effect_block_expr
            <- effect_keywords block

async_call_expr
            <- async_effect postfix_expr

effect_keywords
            <- "unsafe" (("async" / "sync") keyword_options?)?
             / ("async" / "sync") keyword_options?

if_expr     <- "if" expr_without_block block "else" block

switch_expr <- "switch" expr_without_block "{" switch_expr_case* "}"

switch_expr_case
            <- switch_pattern_list "=>" switch_expr_body ","?

switch_expr_body
            <- block / expr_without_block / if_expr / switch_expr / try_catch_expr

try_catch_expr
            <- "try" expr catch_clause

catch_clause
            <- "catch" "(" catch_binding? ")" "=>" catch_body

catch_body  <- block / expr
```

## pattern

```peg
match_pattern
            <- literal
             / variant_pattern
             / tuple_pattern

pattern     <- match_pattern
             / binding_pattern

binding_pattern
            <- "_"
             / ref_binding_mode type_pattern? name "!"?
             / type_pattern name "!"?
             / name

ref_binding_mode
            <- "ref" "!"?

type_pattern
            <- type
             / "_"

variant_pattern
            <- variant_name ("(" pattern_list? ")")?

tuple_pattern
            <- "(" pattern_list? ")"

variant_name
            <- "." name
             / qualified_name

qualified_name
            <- name "." name ("." name)*

pattern_list
            <- pattern ("," pattern)* ","?
```

说明：

- `match_pattern` 用于 `is` 和 `switch` 分支根，接受 optional、variant、tuple 和 literal。
- 单段 `path` 只作为 binding 子 pattern 使用，不能作为 `is` 或 `switch` 的分支根。
- `ref T name` 创建共享借用；`ref! T name` 创建唯一可变借用，结果类型为 `T&!`。
  `ref` / `ref!` 是绑定模式，不是类型名。
- 所有借用 binding 都可省略类型位：`ref name` 等价于 `ref _ name`，`ref! name`
  等价于 `ref! _ name`。这适用于 match payload、独立解构和 lambda capture。
- 普通变量定义不能使用左侧 `ref`。定义引用变量时使用 RHS 借用表达式，例如
  `Int& shared = value$.ref();` 或 `Int&! unique = value$.mut_ref();`。
- 独立解构必须以括号为语法入口；即使只有一个 binding，也写 `(ref name) = value;`。
  by-value 解构不能省略类型位置，例如 `(_ left, Int right) = pair;`。
- type pattern 可以具体、完整推导或局部推导，例如 `Int value`、`_ value`、
  `Int[_] values`、`_[3] values`。
- 绑定名后的 `!` 表示该绑定可重新赋值，例如 `ref T name!`；它不会把共享借用变成可变借用。
- `_! name` 不合法；payload 共享借用使用 `ref T name`，唯一可变借用使用 `ref! T name`。
- Tuple pattern 递归保留括号层级。union/optional 的 Tuple payload 直接展开一层：
  `.pair(left, right)` 匹配 `(Int, Int)` payload；嵌套结构写作
  `.nested((left, right), tail)`。
- 单元素 Tuple 与元素等价，所以 `(ref name) = value;` 不会对 `value` 额外执行索引投影。

## 说明

- 这份 PEG 描述的是 Jiang 语言语法本身，不以旧编译器内部结构为边界。
- 少数语义限制不在 PEG 中表达，例如：
  - type suffix 的规范顺序是 `?` 再 `!`。
  - `return` 只能返回 `result_type` 中 `@` 左侧的成功值类型；错误值只能通过 `throw` 返回。
  - `@` 右侧类型是否可作为 error payload 由 type check 判断。
  - `switch` exhaustiveness、trait bound、visibility 等由 resolve/sema 阶段检查。
  - `struct_lit` 与 `block` 在语法上都使用 `{ ... }`，parser 依赖上下文和有序选择区分。
  - 泛型类型参数中的 `>>` 可能由 lexer 合并为一个 token，
    parser 在类型参数上下文中会按两个 `>` 处理。
