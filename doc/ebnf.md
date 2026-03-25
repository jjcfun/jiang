# Jiang Language EBNF Grammar

基于 `jiang.md` 参考文档，以下为 Jiang 语言的 EBNF (Extended Backus-Naur Form) 语法说明：

```ebnf
(* ========================================== *)
(*        Jiang Language EBNF Grammar         *)
(* ========================================== *)

(* --- 1. Program & Statements (程序与语句) --- *)

program = { statement } ;

statement = import_decl
          | struct_decl
          | enum_decl
          | union_decl
          | function_decl
          | variable_decl
          | expression_statement
          | return_statement
          | break_statement
          | continue_statement
          | sudo_statement
          | block
          | if_statement
          | while_statement
          | for_statement
          | switch_statement
          ;

block = "{" { statement } "}" ;

import_decl = [ "public" ] "import" ( "std" | [ identifier "=" ] string_literal ) [ ";" ] ;

variable_decl = type_expr identifier "=" expression [ ";" ]
              | binding_list "=" expression [ ";" ]
              ;

binding = type_expr identifier ;
binding_list = "(" binding { "," binding } ")" ;

expression_statement = expression [ ";" ] ;
return_statement = "return" [ expression ] [ ";" ] ;
break_statement = "break" [ ";" ] ;
continue_statement = "continue" [ ";" ] ;
sudo_statement = "sudo" ( expression_statement | block ) ;

if_statement = "if" "(" expression ")" block [ "else" ( block | if_statement ) ] ;
while_statement = "while" "(" expression ")" block ;
for_statement = "for" for_pattern "in" expression block ;

for_pattern = binding
            | binding_list ;

switch_statement = "switch" "(" expression ")" "{" { switch_case } "}" ;

condition_expr = expression [ "==" union_match_pattern ] ;
union_match_pattern = identifier "." identifier [ "(" union_pattern_params ")" ] ;

switch_case = identifier "." identifier [ "(" union_pattern_params ")" ] ":" ( statement | block )
            | "else" ":" ( statement | block ) ;
union_pattern_params = binding { "," binding } ;

(* --- 2. Types (类型系统) --- *)

(* Jiang语言的类型遵循从左往右、从里到外的原则 *)
type_expr = base_type { type_modifier } ;

base_type = "Int" | "UInt8" | "UInt16" | "Float" | "Double" | "Bool"
          | "_" | identifier 
          | tuple_type | function_type 
          ;

type_modifier = "?" (* 可空类型 *)
              | "!" (* 可变类型 *)
              | "[" [ expression | "_" ] "]" (* 数组或切片，_表示推断 *)
              | "*" (* 指针 *)
              | "<" type_expr { "," type_expr } ">" (* 泛型参数 *)
              ;

tuple_type = "(" [ type_expr { "," type_expr } ] ")" ;

function_type = "Fn" "<" [ "async" ] type_expr [ "@" identifier ] { "," type_expr } ">" ;

(* --- 3. Functions (函数) --- *)

function_decl = [ "public" ] [ generic_params ] { alias_decl } [ "async" ] 
                type_expr [ "@" identifier ] identifier "(" parameter_list ")" block ;

generic_params = "@" "<" generic_param { "," generic_param } ">" ;
generic_param = identifier [ ":" identifier ] ;
alias_decl = "@alias" "(" identifier "=" type_expr ")" ;

parameter_list = [ parameter { "," parameter } ] ;
parameter = type_expr identifier ;

(* --- 4. Structs (结构体) --- *)

struct_decl = [ generic_params ]
            "struct" identifier "{"
                { struct_member }
            "}" ;

struct_member = type_expr identifier { "," identifier } [ ";" ]
              | function_decl
              | init_decl
              | struct_decl ;

init_decl = "init" "(" parameter_list ")" block ;

(* --- 5. Enums & Unions (枚举与联合体) --- *)

enum_decl = "enum" [ "(" type_expr ")" ] identifier "{" [ enum_member_list ] "}" ;
enum_member_list = enum_member { "," enum_member } [ "," ] ;
enum_member = identifier [ "=" expression ] ;

union_decl = "union" "(" identifier ")" identifier "{"
                 { union_member }
             "}" ;
union_member = type_expr identifier [ ";" ]
             | struct_decl ;

(* --- 6. Expressions (表达式) --- *)

expression = assignment_expr ;

assignment_expr = logical_or_expr { ( "=" | "+=" | "-=" | "*=" | "/=" ) logical_or_expr } ;

logical_or_expr = logical_and_expr { "||" logical_and_expr } ;
logical_and_expr = equality_expr { "&&" equality_expr } ;
equality_expr = relational_expr { ( "==" | "!=" ) relational_expr } ;
relational_expr = additive_expr { ( "<" | ">" | "<=" | ">=" ) additive_expr } ;
additive_expr = multiplicative_expr { ( "+" | "-" ) multiplicative_expr } ;
multiplicative_expr = unary_expr { ( "*" | "/" | "%" ) unary_expr } ;

unary_expr = [ "!" | "-" | "&" ] postfix_expr 
           | "new" expression ;

postfix_expr = primary_expr { postfix_op } ;
postfix_op = "[" expression "]"
           | [ "<" type_expr { "," type_expr } ">" ] "(" [ expression_list ] ")"
           | "." "cast" "(" type_expr ")"
           | "." identifier [ [ "<" type_expr { "," type_expr } ">" ] "(" [ expression_list ] ")" ] ;

primary_expr = literal
             | identifier
             | "(" [ expression_list ] ")"
             | closure_expr
             | struct_init_expr
             | "$" primary_expr (* 获取指针本身的代理如 $b.move() *)
             | "$" digit      (* 闭包内隐式参数如 $0, $1 *)
             | enum_or_union_init_expr
             | range_expr ;

range_expr = expression ".." expression ;

expression_list = expression { "," expression } ;

closure_expr = "{" [ closure_params "->" ] { statement | expression_statement } "}" ;
closure_params = closure_param { "," closure_param } ;
closure_param = [ type_expr ] identifier ;

struct_init_expr = [ type_expr ] "{" struct_init_fields "}" 
                 | [ type_expr ] "{" expression_list "}" ; (* 数组字面量初始化 *)
struct_init_fields = identifier ":" expression { "," identifier ":" expression } ;

enum_or_union_init_expr = [ identifier ] "." identifier [ "(" expression_list ")" ] ;

(* --- 7. Lexical Elements (词法元素) --- *)

identifier = letter { letter | digit | "_" } ;
literal = integer_literal | float_literal | string_literal | boolean_literal | "null" ;
integer_literal = [ "-" ] digit { digit } ;
float_literal = integer_literal "." digit { digit } ;
string_literal = '"' { any_printable_character_except_double_quote } '"' ;
boolean_literal = "true" | "false" ;

letter = "A" .. "Z" | "a" .. "z" ;
digit = "0" .. "9" ;
```
