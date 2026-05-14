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
ident       <- /* ASCII [_A-Za-z][_A-Za-z0-9]*，或包含 UTF-8 非 ASCII 字节的标识符 */
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
            <- extern_block
             / global_destructure
             / top_level_decl

extern_block
            <- "extern" "{" extern_item* "}"

extern_item <- "public"? "static"? (function_decl / global_decl)

global_destructure
            <- "(" destructure_binding ("," destructure_binding)* ")"
               "=" expr ";"

destructure_binding
            <- type name
```

## 声明

```peg
top_level_decl
            <- leading_annotation* decl_modifier* top_level_decl_body

member_decl <- leading_annotation* member_modifier* member_decl_body

leading_annotation
            <- "@" "where" "(" where_constraints ")"

decl_modifier
            <- "public"
             / "extern"

member_modifier
            <- "public"
             / "static"

top_level_decl_body
            <- import_decl
             / alias_decl
             / nominal_decl
             / trait_decl
             / extend_decl
             / function_decl
             / global_decl

member_decl_body
            <- alias_decl
             / nominal_decl
             / trait_decl
             / function_decl

nominal_decl
            <- struct_decl
             / record_decl
             / enum_decl
             / union_decl

import_decl <- "import" (import_alias "=")? import_path ";"

import_alias
            <- name

import_path <- string_lit / ident

alias_decl  <- "alias" name "=" type ";"

function_decl
            <- result_type name function_tail

global_decl <- type name global_tail

global_tail <- ("=" expr)? ";"

function_tail
            <- generic_params? "(" param_list? ")" (";" / block)

param_list  <- param ("," param)* ","?

param       <- type name ("=" expr)?
```

说明：顶层 `public`、`extern` 由 `decl_modifier` 统一解析。
因此 `struct_decl`、`record_decl`、`enum_decl`、`union_decl`、`trait_decl`
等规则本身不重复写 `"public"`。
`global_decl` 只允许出现在 `top_level_decl` 和 `extern_item` 中；类型成员、trait/extend
成员等非顶层声明使用 `member_decl`，不允许定义全局变量。

## 泛型和 where 约束

```peg
generic_params
            <- "<" name ("," name)* ","? ">"

where_constraints
            <- (where_constraint ("," where_constraint)* ","?)?

where_constraint
            <- projected_where_constraint
             / name ":" type_bound
             / name "==" type

projected_where_constraint
            <- name "." "[" path "]" "." name "==" type

type_bound  <- trait_bound ("&" trait_bound)*

trait_bound <- path ("<" trait_bound_arg_list? ">")?

trait_bound_arg_list
            <- trait_bound_arg ("," trait_bound_arg)* ","?

trait_bound_arg
            <- name "=" type
             / type
```

## 类型

```peg
type        <- type_primary type_postfix*

result_type <- type ("@" type)?

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
             / "[" "]"
             / "[" "*" "]"
             / "[" array_count "]"

array_count <- "_" / int_lit

path        <- name ("." name)*

name        <- ident / "self"
```

约定：

- `T?` 表示 optional 类型层。
- `T!` 表示当前类型层级可变。
- `T&` 表示引用外层。
- `T^` 表示 owning pointer 外层。
- `T[]` 表示 slice。
- `T[*]` 表示 many pointer。
- `T[N]` 表示定长数组，`N` 只能是整数字面量。
- `T@E` 表示 errorable，只能出现在 `result_type`，也就是函数、方法和函数类型的返回位。
- `RawPointer<T>` 在语义上等价于裸指针类型；语法上仍按命名泛型类型解析。

## struct / record

顶层可见性写在外层 `decl` 的 `decl_modifier` 中，例如
`public struct User { ... }`。

```peg
struct_decl <- "struct" name generic_params? trait_list? struct_body

record_decl <- "record" name generic_params? trait_list? struct_body

trait_list  <- ":" path ("," path)*

struct_body <- "{" struct_member* "}"

struct_member
            <- "public"? struct_member_body

struct_member_body
            <- deinit_decl
             / init_decl
             / assoc_type_impl
             / "static"? method_decl
             / field_decl

deinit_decl <- "deinit" "(" ")" block

init_decl   <- "init" "?"? name? "(" param_list? ")" block

assoc_type_impl
            <- "associated" path "=" type ";"

method_decl <- result_type name function_tail

field_decl  <- type field_init ("," field_init)* ";"

field_init  <- name ("=" expr)?
```

## enum

顶层可见性写在外层 `decl` 的 `decl_modifier` 中，例如
`public enum Color { ... }`。

```peg
enum_decl   <- "enum" name trait_list? "{" enum_member* "}"

enum_member <- "public"? "static"? method_decl
             / name ("=" expr)? ("," / ";")?
```

## union

顶层可见性写在外层 `decl` 的 `decl_modifier` 中，例如
`public union Value { ... }`。

```peg
union_decl  <- "union" ("(" name ")")? name (":" name)?
               generic_params? trait_list? union_body

union_body  <- "{" union_member* "}"

union_member
            <- "public"? "static"? method_decl
             / typed_union_variants
             / union_variant ("," / ";")?

typed_union_variants
            <- type name ("," name)* ";"?

union_variant
            <- name ( "(" type ")" / ":" type )?
```

## trait

顶层可见性写在外层 `decl` 的 `decl_modifier` 中，例如
`public trait Hashable: Equatable { ... }`。

```peg
trait_decl  <- "trait" name generic_params? (":" path ("," path)*)?
               (";" / trait_body)

trait_body  <- "{" trait_member* "}"

trait_member
            <- associated_type_decl
             / trait_method_decl

associated_type_decl
            <- "associated" name (":" type_bound)? ";"

trait_method_decl
            <- "static"? result_type name function_tail
```

## extend

```peg
extend_decl <- "extend" type (":" path)? "{" extend_member* "}"

extend_member
            <- assoc_type_impl
             / member_decl
```

## 语句和 block

```peg
block       <- "{" stmt* "}"

stmt        <- return_stmt
             / throw_stmt
             / break_stmt
             / continue_stmt
             / defer_stmt
             / block
             / while_stmt
             / for_stmt
             / destructure_stmt
             / var_decl_stmt
             / assign_stmt
             / expr_stmt

return_stmt <- "return" expr? ";"

throw_stmt  <- "throw" expr ";"

break_stmt  <- "break" ";"

continue_stmt
            <- "continue" ";"

defer_stmt  <- "defer" (block / expr ";")

var_decl_stmt
            <- type name ("=" expr)? ";"

destructure_stmt
            <- "(" destructure_binding ("," destructure_binding)* ")"
               "=" expr ";"

assign_stmt <- expr assign_op expr ";"

assign_op   <- "=" / "+=" / "-=" / "*=" / "/=" / "%="
             / "&=" / "|=" / "^=" / "<<=" / ">>="

expr_stmt   <- expr ";"

while_stmt  <- "while" expr block

for_stmt    <- "for" for_binding "in" expr block

for_binding <- type name
             / pattern

catch_binding
            <- type name?
             / name

switch_pattern_list
            <- "else"
             / pattern ("," pattern)*
```

说明：`block` 内只允许 `stmt`，不允许独立的 tail expression。
`expr_stmt` 必须以 `;` 结束。`block` 的值由最后一条 `stmt` 决定；
空 `block` 的值为 `Unit`。

## 表达式

表达式按优先级从低到高定义：

```peg
expr        <- range_expr

range_expr  <- logic_or_expr ".." logic_or_expr

logic_or_expr
            <- logic_and_expr ("||" logic_and_expr)*

logic_and_expr
            <- coalesce_expr ("&&" coalesce_expr)*

coalesce_expr
            <- compare_expr ("??" coalesce_rhs)*

coalesce_rhs
            <- "return" compare_expr?
             / "break"
             / "continue"
             / "throw" compare_expr
             / expr

compare_expr
            <- bit_or_expr ((compare_op bit_or_expr) / ("is" pattern))*

compare_op  <- "==" / "!=" / "<=" / "<" / ">=" / ">"

bit_or_expr <- bit_xor_expr ("|" bit_xor_expr)*

bit_xor_expr
            <- bit_and_expr ("^" bit_and_expr)*

bit_and_expr
            <- shift_expr ("&" shift_expr)*

shift_expr  <- add_expr (("<<" / ">>") add_expr)*

add_expr    <- mul_expr (("+" / "-") mul_expr)*

mul_expr    <- unary_expr (("*" / "/" / "%") unary_expr)*

unary_expr  <- ("-" / "!" / "~" / "&" / "*" / "new") unary_expr
             / postfix_expr

postfix_expr
            <- primary_expr postfix_tail*

postfix_tail
            <- type_args "(" call_args? ")"
             / "(" call_args? ")"
             / catch_expr
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
```

`range_expr` 只表示 `logic_or_expr ".." logic_or_expr` 这种形式；
普通 `logic_or_expr` 不归入 `range_expr`。AST 使用独立的 `RangeExpr` 节点，
`..` 的优先级低于逻辑或，高于条件表达式。

## primary expression

```peg
primary_expr
            <- type "$" implicit_call
             / literal
             / "self"
             / "$"
             / path type_args "(" call_args? ")"
             / path type_args? struct_lit
             / path
             / "." name
             / paren_expr
             / array_expr
             / block
             / if_expr
             / switch_expr

paren_expr  <- "(" ")"
             / "(" expr "," expr ("," expr)* ","? ")"
             / "(" expr ")"

array_expr  <- "[" (expr ("," expr)* ","?)? "]"

struct_lit  <- "{" (field_init_expr ("," field_init_expr)* ","?)? "}"

field_init_expr
            <- (name ":")? expr

if_expr     <- "if" expr block "else" block

switch_expr <- "switch" expr "{" switch_expr_case* "}"

switch_expr_case
            <- switch_pattern_list "=>" switch_expr_body

switch_expr_body
            <- block / stmt

catch_expr  <- "catch" "(" catch_binding? ")" "=>" catch_body

catch_body  <- block / expr
```

## pattern

```peg
pattern     <- optional_pattern
             / wildcard_binding_pattern
             / literal
             / implicit_variant_pattern
             / tuple_pattern
             / variant_or_binding_pattern

optional_pattern
            <- "some" pattern

wildcard_binding_pattern
            <- "_" ("!"? name)?

variant_or_binding_pattern
            <- path ("(" pattern_list? ")")?

implicit_variant_pattern
            <- "." name ("(" pattern_list? ")")?

tuple_pattern
            <- "(" pattern_list? ")"

pattern_list
            <- pattern ("," pattern)* ","?
```

说明：

- 单段 `path` 在 pattern 中通常先按 binding 解释；多段 `path` 或带 payload 的形式用于 variant。
- `_` 是 wildcard；`_ name` 和 `_! name` 是显式推导 binding。

## 说明

- 这份 PEG 描述的是 Jiang 语言语法本身，不以旧编译器内部结构为边界。
- 少数语义限制不在 PEG 中表达，例如：
  - type suffix 的规范顺序是 `?` 再 `!`。
  - `return` 只能返回 `result_type` 中 `@` 左侧的成功值类型；错误值只能通过 `throw` 返回。
  - `@` 右侧类型是否可作为 error payload 由 type check 判断。
  - `switch` exhaustiveness、trait bound、visibility 等由 resolve/sema 阶段检查。
  - `struct_lit` 与 `block` 在语法上都使用 `{ ... }`，parser 依赖上下文和有序选择区分。
  - 泛型类型参数中的 `>>` 可能由 lexer 合并为一个 token，parser 在类型参数上下文中会按两个 `>` 处理。
