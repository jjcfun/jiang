#include "parser.h"
#include "hashmap.h"
#include "vec.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* token_dup(const Token* token) {
    char* text = (char*)malloc(token->length + 1);
    if (!text) {
        return 0;
    }
    memcpy(text, token->start, token->length);
    text[token->length] = '\0';
    return text;
}

static char* string_token_dup(const Token* token) {
    char* text = 0;
    size_t length = 0;
    if (token->length < 2) {
        return 0;
    }
    length = token->length - 2;
    text = (char*)malloc(length + 1);
    if (!text) {
        return 0;
    }
    memcpy(text, token->start + 1, length);
    text[length] = '\0';
    return text;
}

static char* dup_join3(const char* left, const char* middle, const char* right) {
    size_t left_len = strlen(left);
    size_t middle_len = strlen(middle);
    size_t right_len = strlen(right);
    char* text = (char*)malloc(left_len + middle_len + right_len + 1);
    if (!text) {
        return 0;
    }
    memcpy(text, left, left_len);
    memcpy(text + left_len, middle, middle_len);
    memcpy(text + left_len + middle_len, right, right_len);
    text[left_len + middle_len + right_len] = '\0';
    return text;
}

static int token_equals(const Token* token, const char* text) {
    size_t n = strlen(text);
    return token->length == n && strncmp(token->start, text, n) == 0;
}

#define stmt_list_push(list, stmt) VEC_PUSH((list), (stmt))
#define expr_list_push(list, expr) VEC_PUSH((list), (expr))
#define param_list_push(list, param) VEC_PUSH((list), (param))
#define binding_pattern_list_push(list, pattern) VEC_PUSH((list), (pattern))
#define type_list_push(list, type) VEC_PUSH((list), (type))
#define function_list_push(list, fn) VEC_PUSH((list), (fn))
#define global_list_push(list, global) VEC_PUSH((list), (global))
#define enum_member_list_push(list, member) VEC_PUSH((list), (member))
#define struct_field_list_push(list, field) VEC_PUSH((list), (field))
#define struct_field_init_list_push(list, field) VEC_PUSH((list), (field))
#define switch_case_list_push(list, switch_case) VEC_PUSH((list), (switch_case))
#define import_list_push(list, import_decl) VEC_PUSH((list), (import_decl))
#define alias_list_push(list, alias_decl) VEC_PUSH((list), (alias_decl))
#define concept_list_push(list, concept_decl) VEC_PUSH((list), (concept_decl))
#define concept_method_list_push(list, method) VEC_PUSH((list), (method))
#define where_constraint_list_push(list, constraint) VEC_PUSH((list), (constraint))
#define struct_list_push(list, struct_decl) VEC_PUSH((list), (struct_decl))

static int is_known_type(Parser* parser, const Token* token);
#define enum_list_push(list, enum_decl) VEC_PUSH((list), (enum_decl))
#define union_variant_list_push(list, variant) VEC_PUSH((list), (variant))
#define union_list_push(list, union_decl) VEC_PUSH((list), (union_decl))
#define name_list_push(list, name) VEC_PUSH((list), (name))

static AstExpr* new_expr(AstExprKind kind, int line) {
    AstExpr* expr = (AstExpr*)calloc(1, sizeof(AstExpr));
    expr->kind = kind;
    expr->line = line;
    return expr;
}

static AstStmt* new_stmt(AstStmtKind kind, int line) {
    AstStmt* stmt = (AstStmt*)calloc(1, sizeof(AstStmt));
    stmt->kind = kind;
    stmt->line = line;
    return stmt;
}

static AstBindingPattern* new_binding_pattern(AstBindingPatternKind kind, int line) {
    AstBindingPattern* pattern = (AstBindingPattern*)calloc(1, sizeof(AstBindingPattern));
    pattern->kind = kind;
    pattern->line = line;
    return pattern;
}

static AstType* heap_type_copy(const AstType* type) {
    AstType* copy = (AstType*)malloc(sizeof(AstType));
    if (!copy) {
        return 0;
    }
    *copy = *type;
    return copy;
}

static void register_known_type(Parser* parser, const char* name) {
    hashmap_set(&parser->known_types, name, (void*)1);
}

static void advance(Parser* parser);

static int is_type_like_ident(const Token* token) {
    return token->kind == TOKEN_IDENT &&
           token->length > 0 &&
           token->start[0] >= 'A' &&
           token->start[0] <= 'Z';
}

static AstType parse_type(Parser* parser);
static int fail(Parser* parser, const char* error);
static int expect(Parser* parser, TokenKind kind, const char* error);
static int is_type_start(const Parser* parser);
static int parse_params(Parser* parser, AstParamList* params);
static AstExpr* parse_implicit_member(Parser* parser, int line, AstExpr* value_target);
static AstExpr* parse_type_implicit_expr(Parser* parser, int line);

static char* parse_qualified_name(Parser* parser) {
    char* name = 0;
    if (parser->current.kind != TOKEN_IDENT) {
        return 0;
    }
    name = token_dup(&parser->current);
    if (!name) {
        return 0;
    }
    advance(parser);
    while (parser->current.kind == TOKEN_DOT && parser->next.kind == TOKEN_IDENT) {
        char* right = 0;
        char* joined = 0;
        advance(parser);
        right = token_dup(&parser->current);
        if (!right) {
            free(name);
            return 0;
        }
        joined = dup_join3(name, ".", right);
        free(name);
        free(right);
        if (!joined) {
            return 0;
        }
        name = joined;
        advance(parser);
    }
    return name;
}

static int parse_type_arg_list(Parser* parser, AstTypeList* out) {
    if (!expect(parser, TOKEN_LT, "expected '<' to start type arguments")) {
        return 0;
    }
    if (!is_type_start(parser)) {
        return fail(parser, "expected type argument");
    }
    for (;;) {
        type_list_push(out, parse_type(parser));
        if (parser->current.kind == TOKEN_COMMA) {
            advance(parser);
            continue;
        }
        break;
    }
    return expect(parser, TOKEN_GT, "expected '>' after type arguments");
}

static int parse_named_generic_params(Parser* parser, AstNameList* out) {
    if (!expect(parser, TOKEN_LT, "expected '<' to start generic parameters")) {
        return 0;
    }
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, "expected generic parameter name");
    }
    for (;;) {
        name_list_push(out, token_dup(&parser->current));
        advance(parser);
        if (parser->current.kind == TOKEN_COMMA) {
            advance(parser);
            if (parser->current.kind != TOKEN_IDENT) {
                return fail(parser, "expected generic parameter name");
            }
            continue;
        }
        break;
    }
    return expect(parser, TOKEN_GT, "expected '>' after generic parameters");
}

static int parse_where_annotation(Parser* parser, AstWhereConstraintList* out) {
    if (!expect(parser, TOKEN_AT, "expected '@' before where")) {
        return 0;
    }
    if (parser->current.kind != TOKEN_IDENT || !token_equals(&parser->current, "where")) {
        return fail(parser, "expected 'where' after '@'");
    }
    advance(parser);
    if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after @where")) {
        return 0;
    }
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, "expected generic parameter name in @where");
    }
    for (;;) {
        AstWhereConstraint item;
        memset(&item, 0, sizeof(item));
        item.line = parser->current.line;
        item.param_name = token_dup(&parser->current);
        advance(parser);
        if (!expect(parser, TOKEN_COLON, "expected ':' in @where constraint")) {
            return 0;
        }
        if (parser->current.kind != TOKEN_IDENT) {
            return fail(parser, "expected concept name in @where");
        }
        item.concept_name = token_dup(&parser->current);
        advance(parser);
        where_constraint_list_push(out, item);
        if (parser->current.kind == TOKEN_COMMA) {
            advance(parser);
            if (parser->current.kind != TOKEN_IDENT) {
                return fail(parser, "expected generic parameter name in @where");
            }
            continue;
        }
        break;
    }
    return expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after @where");
}

static int parse_concept_decl(Parser* parser, AstProgram* out_program, int public_flag) {
    AstConceptDecl concept_decl;
    memset(&concept_decl, 0, sizeof(concept_decl));
    concept_decl.public_flag = public_flag;
    advance(parser);
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, "expected concept name");
    }
    concept_decl.name = token_dup(&parser->current);
    concept_decl.line = parser->current.line;
    advance(parser);
    if (parser->current.kind == TOKEN_SEMICOLON) {
        advance(parser);
        concept_list_push(&out_program->concepts, concept_decl);
        return 1;
    }
    if (!expect(parser, TOKEN_LEFT_BRACE, "expected ';' or '{' after concept declaration")) {
        return 0;
    }
    while (parser->current.kind != TOKEN_RIGHT_BRACE && parser->current.kind != TOKEN_EOF) {
        AstConceptMethod method;
        memset(&method, 0, sizeof(method));
        if (!is_type_start(parser)) {
            return fail(parser, "expected concept method return type");
        }
        method.return_type = parse_type(parser);
        if (method.return_type.kind == AST_TYPE_INFER) {
            return fail(parser, "concept method return type cannot be inferred");
        }
        if (parser->current.kind != TOKEN_IDENT) {
            return fail(parser, "expected concept method name");
        }
        method.name = token_dup(&parser->current);
        method.line = parser->current.line;
        advance(parser);
        if (!parse_params(parser, &method.params)) {
            return 0;
        }
        if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after concept method")) {
            return 0;
        }
        concept_method_list_push(&concept_decl.methods, method);
    }
    if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after concept declaration")) {
        return 0;
    }
    concept_list_push(&out_program->concepts, concept_decl);
    return 1;
}

static int looks_like_call_type_args(Parser* parser) {
    Parser probe = *parser;
    if (probe.current.kind != TOKEN_LT) {
        return 0;
    }
    if (!parse_type_arg_list(&probe, &(AstTypeList){0})) {
        return 0;
    }
    return probe.current.kind == TOKEN_LEFT_PAREN;
}

static int looks_like_type_implicit_suffix(Parser* parser) {
    Parser probe = *parser;
    if (!is_type_start(&probe)) {
        return 0;
    }
    (void)parse_type(&probe);
    return probe.current.kind == TOKEN_DOLLAR &&
           probe.next.kind == TOKEN_DOT;
}

static int looks_like_qualified_type_start(Parser* parser) {
    Parser probe = *parser;
    int seen_dot = 0;
    if (probe.current.kind != TOKEN_IDENT) {
        return 0;
    }
    advance(&probe);
    while (probe.current.kind == TOKEN_DOT && probe.next.kind == TOKEN_IDENT) {
        seen_dot = 1;
        advance(&probe);
        if (!is_type_like_ident(&probe.current)) {
            return 0;
        }
        advance(&probe);
    }
    return seen_dot || is_type_like_ident(&parser->current);
}

static int is_known_type(Parser* parser, const Token* token) {
    char* name = token_dup(token);
    int result = 0;
    if (!name) {
        return 0;
    }
    result = hashmap_contains(&parser->known_types, name);
    free(name);
    return result;
}

static void advance(Parser* parser) {
    parser->current = parser->next;
    parser->next = lexer_next(&parser->lexer);
}

static int fail(Parser* parser, const char* error) {
    parser->error = error;
    parser->error_line = parser->current.line;
    return 0;
}

static int expect(Parser* parser, TokenKind kind, const char* error) {
    if (parser->current.kind != kind) {
        return fail(parser, error);
    }
    advance(parser);
    return 1;
}

static int is_type_start(const Parser* parser) {
    if (parser->current.kind == TOKEN_KW_INT || parser->current.kind == TOKEN_KW_UINT8 || parser->current.kind == TOKEN_KW_BOOL || parser->current.kind == TOKEN_LEFT_PAREN) {
        return 1;
    }
    if (parser->current.kind == TOKEN_IDENT && token_equals(&parser->current, "_")) {
        return 1;
    }
    if (parser->current.kind == TOKEN_IDENT && looks_like_qualified_type_start((Parser*)parser)) {
        return 1;
    }
    return parser->current.kind == TOKEN_IDENT && is_known_type((Parser*)parser, &parser->current);
}

static int is_pattern_type_start(const Parser* parser) {
    if (parser->current.kind == TOKEN_KW_INT || parser->current.kind == TOKEN_KW_UINT8 || parser->current.kind == TOKEN_KW_BOOL) {
        return 1;
    }
    return parser->current.kind == TOKEN_IDENT && token_equals(&parser->current, "_");
}

static int pattern_has_explicit_type(Parser* parser) {
    if (parser->current.kind == TOKEN_KW_INT || parser->current.kind == TOKEN_KW_UINT8 || parser->current.kind == TOKEN_KW_BOOL) {
        return 1;
    }
    if (parser->current.kind == TOKEN_IDENT && token_equals(&parser->current, "_")) {
        if (parser->next.kind == TOKEN_IDENT) {
            return 1;
        }
        if (parser->next.kind == TOKEN_BANG) {
            Parser probe = *parser;
            advance(&probe);
            advance(&probe);
            return probe.current.kind == TOKEN_IDENT;
        }
    }
    return 0;
}

static int looks_like_var_decl(Parser* parser) {
    Parser probe = *parser;
    if (!is_type_start(&probe)) {
        return 0;
    }
    (void)parse_type(&probe);
    if (probe.error) {
        return 0;
    }
    return probe.current.kind == TOKEN_IDENT && probe.next.kind == TOKEN_ASSIGN;
}

static AstType parse_type_postfix(Parser* parser, AstType type) {
    while (parser->current.kind == TOKEN_LEFT_BRACKET || parser->current.kind == TOKEN_BANG || parser->current.kind == TOKEN_STAR || parser->current.kind == TOKEN_QUESTION) {
        if (parser->current.kind == TOKEN_BANG) {
            type.mutable_flag = 1;
            advance(parser);
            continue;
        }
        if (parser->current.kind == TOKEN_STAR) {
            AstType pointee = type;
            advance(parser);
            memset(&type, 0, sizeof(type));
            type.kind = AST_TYPE_POINTER;
            type.array_item = heap_type_copy(&pointee);
            continue;
        }
        if (parser->current.kind == TOKEN_QUESTION) {
            AstType wrapped = type;
            advance(parser);
            memset(&type, 0, sizeof(type));
            type.kind = AST_TYPE_OPTIONAL;
            type.array_item = heap_type_copy(&wrapped);
            continue;
        }
        if (parser->current.kind == TOKEN_LEFT_BRACKET) {
            AstType element = type;
            advance(parser);
            if (parser->current.kind == TOKEN_RIGHT_BRACKET) {
                memset(&type, 0, sizeof(type));
                type.kind = AST_TYPE_SLICE;
                type.array_item = heap_type_copy(&element);
                advance(parser);
                continue;
            }
            memset(&type, 0, sizeof(type));
            type.kind = AST_TYPE_ARRAY;
            type.array_item = heap_type_copy(&element);
            if (parser->current.kind == TOKEN_IDENT && token_equals(&parser->current, "_")) {
                type.array_length = -1;
                advance(parser);
            } else if (parser->current.kind != TOKEN_INT_LIT) {
                fail(parser, "expected integer array length");
                return type;
            } else {
                type.array_length = (int)strtol(parser->current.start, 0, 10);
                advance(parser);
            }
            if (!expect(parser, TOKEN_RIGHT_BRACKET, "expected ']' after array length")) {
                return type;
            }
        }
    }
    return type;
}

static AstType parse_type(Parser* parser) {
    AstType type;
    memset(&type, 0, sizeof(type));
    type.kind = AST_TYPE_INT;

    if (parser->current.kind == TOKEN_LEFT_PAREN) {
        AstType first;
        advance(parser);
        if (parser->current.kind == TOKEN_RIGHT_PAREN) {
            type.kind = AST_TYPE_VOID;
            advance(parser);
            return type;
        }
        if (!is_type_start(parser)) {
            parser->error = "expected tuple item type";
            parser->error_line = parser->current.line;
            return type;
        }
        first = parse_type(parser);
        if (parser->current.kind != TOKEN_COMMA) {
            if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after type")) {
                return type;
            }
            return parse_type_postfix(parser, first);
        }
        type.kind = AST_TYPE_TUPLE;
        type_list_push(&type.tuple_items, first);
        while (parser->current.kind == TOKEN_COMMA) {
            advance(parser);
            if (!is_type_start(parser)) {
                parser->error = "expected tuple item type after ','";
                parser->error_line = parser->current.line;
                return type;
            }
            type_list_push(&type.tuple_items, parse_type(parser));
        }
        if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after tuple type")) {
            return type;
        }
        return parse_type_postfix(parser, type);
    }

    if (parser->current.kind == TOKEN_KW_BOOL) {
        type.kind = AST_TYPE_BOOL;
        advance(parser);
        return parse_type_postfix(parser, type);
    }
    if (parser->current.kind == TOKEN_KW_INT) {
        type.kind = AST_TYPE_INT;
        advance(parser);
        return parse_type_postfix(parser, type);
    }
    if (parser->current.kind == TOKEN_KW_UINT8) {
        type.kind = AST_TYPE_UINT8;
        advance(parser);
        return parse_type_postfix(parser, type);
    }
    if (parser->current.kind == TOKEN_IDENT && token_equals(&parser->current, "_")) {
        type.kind = AST_TYPE_INFER;
        advance(parser);
        return parse_type_postfix(parser, type);
    }
    if (parser->current.kind == TOKEN_IDENT) {
        type.kind = AST_TYPE_NAMED;
        type.named_name = parse_qualified_name(parser);
        if (parser->current.kind == TOKEN_LT) {
            if (!parse_type_arg_list(parser, &type.type_args)) {
                return type;
            }
        }
        return parse_type_postfix(parser, type);
    }
    parser->error = "expected type";
    parser->error_line = parser->current.line;
    return type;
}

static int looks_like_destructure(Parser* parser) {
    Parser probe = *parser;
    if (probe.current.kind != TOKEN_LEFT_PAREN) {
        return 0;
    }
    advance(&probe);
    if (!is_type_start(&probe)) {
        return 0;
    }
    for (;;) {
        (void)parse_type(&probe);
        if (probe.error) {
            return 0;
        }
        if (probe.current.kind != TOKEN_IDENT) {
            return 0;
        }
        advance(&probe);
        if (probe.current.kind == TOKEN_COMMA) {
            advance(&probe);
            if (!is_type_start(&probe)) {
                return 0;
            }
            continue;
        }
        break;
    }
    return probe.current.kind == TOKEN_RIGHT_PAREN && probe.next.kind == TOKEN_ASSIGN;
}

static int parse_binding_list(Parser* parser, AstParamList* bindings) {
    if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' to start binding list")) {
        return 0;
    }
    for (;;) {
        AstParam binding;
        memset(&binding, 0, sizeof(binding));
        if (!is_type_start(parser)) {
            return fail(parser, "expected binding type");
        }
        binding.type = parse_type(parser);
        if (parser->current.kind != TOKEN_IDENT) {
            return fail(parser, "expected binding name");
        }
        binding.name = token_dup(&parser->current);
        binding.line = parser->current.line;
        advance(parser);
        param_list_push(bindings, binding);
        if (parser->current.kind == TOKEN_COMMA) {
            advance(parser);
            continue;
        }
        break;
    }
    return expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after binding list");
}

static AstBindingPattern* parse_binding_pattern(Parser* parser) {
    AstBindingPattern* pattern = 0;
    if (parser->current.kind == TOKEN_LEFT_PAREN) {
        int line = parser->current.line;
        advance(parser);
        pattern = new_binding_pattern(AST_BINDING_TUPLE, line);
        if (!pattern) {
            return 0;
        }
        for (;;) {
            AstBindingPattern* item = parse_binding_pattern(parser);
            if (!item) {
                return 0;
            }
            binding_pattern_list_push(&pattern->items, item);
            if (parser->current.kind == TOKEN_COMMA) {
                advance(parser);
                continue;
            }
            break;
        }
        if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after binding pattern")) {
            return 0;
        }
        return pattern;
    }
    pattern = new_binding_pattern(AST_BINDING_NAME, parser->current.line);
    if (!pattern) {
        return 0;
    }
    if (pattern_has_explicit_type(parser)) {
        pattern->type = parse_type(parser);
    } else {
        memset(&pattern->type, 0, sizeof(pattern->type));
        pattern->type.kind = AST_TYPE_INFER;
    }
    if (parser->current.kind != TOKEN_IDENT) {
        fail(parser, "expected binding name");
        return 0;
    }
    pattern->name = token_dup(&parser->current);
    advance(parser);
    return pattern;
}

static int looks_like_variant_ref(Parser* parser) {
    if (parser->current.kind == TOKEN_DOT && parser->next.kind == TOKEN_IDENT) {
        return 1;
    }
    if (parser->current.kind == TOKEN_IDENT) {
        Parser probe = *parser;
        advance(&probe);
        return probe.current.kind == TOKEN_DOT && probe.next.kind == TOKEN_IDENT;
    }
    return 0;
}

static int looks_like_qualified_variant_ref(Parser* parser) {
    Parser probe = *parser;
    int consumed_type_segment = 0;
    if (probe.current.kind != TOKEN_IDENT) {
        return 0;
    }
    advance(&probe);
    while (probe.current.kind == TOKEN_DOT && probe.next.kind == TOKEN_IDENT) {
        if (!is_type_like_ident(&probe.next)) {
            break;
        }
        consumed_type_segment = 1;
        advance(&probe);
        advance(&probe);
    }
    return consumed_type_segment &&
           probe.current.kind == TOKEN_DOT &&
           probe.next.kind == TOKEN_IDENT;
}

static int looks_like_qualified_init_call(Parser* parser) {
    Parser probe = *parser;
    if (probe.current.kind != TOKEN_IDENT) {
        return 0;
    }
    advance(&probe);
    while (probe.current.kind == TOKEN_DOT && probe.next.kind == TOKEN_IDENT) {
        if (token_equals(&probe.next, "init")) {
            Parser tail = probe;
            advance(&tail);
            advance(&tail);
            return tail.current.kind == TOKEN_LEFT_PAREN;
        }
        advance(&probe);
        advance(&probe);
    }
    if (probe.current.kind != TOKEN_DOT) {
        return 0;
    }
    advance(&probe);
    if (probe.current.kind != TOKEN_IDENT || !token_equals(&probe.current, "init")) {
        return 0;
    }
    advance(&probe);
    return probe.current.kind == TOKEN_LEFT_PAREN;
}

static int looks_like_qualified_call(Parser* parser) {
    Parser probe = *parser;
    if (probe.current.kind != TOKEN_IDENT) {
        return 0;
    }
    advance(&probe);
    while (probe.current.kind == TOKEN_DOT && probe.next.kind == TOKEN_IDENT) {
        advance(&probe);
        advance(&probe);
    }
    return probe.current.kind == TOKEN_LEFT_PAREN;
}

static int looks_like_method_decl(Parser* parser) {
    Parser probe = *parser;
    if (probe.current.kind == TOKEN_KW_PUBLIC) {
        advance(&probe);
    }
    if (probe.current.kind == TOKEN_KW_STATIC) {
        advance(&probe);
    }
    if (!is_type_start(&probe)) {
        return 0;
    }
    (void)parse_type(&probe);
    if (probe.error || probe.current.kind != TOKEN_IDENT) {
        return 0;
    }
    advance(&probe);
    return probe.current.kind == TOKEN_LEFT_PAREN;
}

static int looks_like_typed_array_constructor(Parser* parser) {
    Parser probe = *parser;
    AstType type;
    if (!is_type_start(&probe) || (probe.current.kind == TOKEN_LEFT_PAREN && !is_known_type(parser, &parser->current))) {
        return 0;
    }
    type = parse_type(&probe);
    if (probe.error) {
        return 0;
    }
    if (type.kind != AST_TYPE_ARRAY) {
        return 0;
    }
    return probe.current.kind == TOKEN_LEFT_BRACE || probe.current.kind == TOKEN_LEFT_PAREN;
}

static int variant_args_are_patterns(Parser* parser) {
    Parser probe = *parser;
    if (!expect(&probe, TOKEN_LEFT_PAREN, "")) {
        return 0;
    }
    if (probe.current.kind == TOKEN_RIGHT_PAREN) {
        return 1;
    }
    for (;;) {
        AstBindingPattern* pattern = parse_binding_pattern(&probe);
        if (!pattern) {
            return 0;
        }
        if (probe.current.kind == TOKEN_COMMA) {
            advance(&probe);
            continue;
        }
        break;
    }
    return probe.current.kind == TOKEN_RIGHT_PAREN;
}

static int looks_like_variant_pattern_expr(Parser* parser) {
    Parser probe = *parser;
    if (!looks_like_variant_ref(&probe)) {
        return 0;
    }
    if (probe.current.kind == TOKEN_IDENT) {
        if (!is_known_type(parser, &parser->current) && !token_equals(&parser->current, "Option")) {
            return 0;
        }
        advance(&probe);
        if (probe.current.kind != TOKEN_DOT) {
            return 0;
        }
        advance(&probe);
    } else {
        advance(&probe);
    }
    if (probe.current.kind != TOKEN_IDENT) {
        return 0;
    }
    advance(&probe);
    if (probe.current.kind != TOKEN_LEFT_PAREN) {
        return 0;
    }
    return variant_args_are_patterns(&probe);
}

static AstExpr* parse_postfix(Parser* parser);
static AstExpr* parse_unary(Parser* parser);

static AstExpr* parse_expr(Parser* parser);

static char* expr_to_qualified_callee(const AstExpr* expr) {
    if (!expr) {
        return 0;
    }
    if (expr->kind == AST_EXPR_NAME) {
        return strdup(expr->as.name);
    }
    if (expr->kind == AST_EXPR_FIELD) {
        char* base = expr_to_qualified_callee(expr->as.field.base);
        char* out = 0;
        if (!base) {
            return 0;
        }
        out = dup_join3(base, ".", expr->as.field.name);
        free(base);
        return out;
    }
    return 0;
}

static AstExpr* parse_variant_expr(Parser* parser, int pattern_flag) {
    AstExpr* expr = new_expr(AST_EXPR_VARIANT, parser->current.line);
    if (parser->current.kind == TOKEN_IDENT) {
        expr->as.variant.union_name = token_dup(&parser->current);
        advance(parser);
        while (parser->current.kind == TOKEN_DOT && parser->next.kind == TOKEN_IDENT) {
            Parser probe = *parser;
            char* right = 0;
            char* joined = 0;
            advance(&probe);
            if (!is_type_like_ident(&probe.current)) {
                break;
            }
            advance(&probe);
            if (probe.current.kind != TOKEN_DOT) {
                break;
            }
            advance(parser);
            right = token_dup(&parser->current);
            joined = dup_join3(expr->as.variant.union_name, ".", right);
            free(expr->as.variant.union_name);
            free(right);
            expr->as.variant.union_name = joined;
            advance(parser);
        }
        if (!expect(parser, TOKEN_DOT, "expected '.' after union name")) {
            return 0;
        }
    } else if (!expect(parser, TOKEN_DOT, "expected '.' before variant name")) {
        return 0;
    }
    if (parser->current.kind != TOKEN_IDENT) {
        fail(parser, "expected variant name");
        return 0;
    }
    expr->as.variant.variant_name = token_dup(&parser->current);
    expr->as.variant.pattern_flag = pattern_flag;
    advance(parser);
    if (parser->current.kind != TOKEN_LEFT_PAREN) {
        return expr;
    }
    if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after variant name")) {
        return 0;
    }
    if (pattern_flag) {
        if (parser->current.kind != TOKEN_RIGHT_PAREN) {
            for (;;) {
                AstBindingPattern* binding = parse_binding_pattern(parser);
                if (!binding) {
                    return 0;
                }
                binding_pattern_list_push(&expr->as.variant.bindings, binding);
                if (parser->current.kind == TOKEN_COMMA) {
                    advance(parser);
                    continue;
                }
                break;
            }
        }
        if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after variant pattern")) {
            return 0;
        }
        return expr;
    }
    if (parser->current.kind != TOKEN_RIGHT_PAREN) {
        expr->as.variant.payload = parse_expr(parser);
        if (!expr->as.variant.payload) {
            return 0;
        }
    }
    if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after variant constructor")) {
        return 0;
    }
    return expr;
}

static AstExpr* parse_primary(Parser* parser) {
    AstExpr* expr = 0;
    Token token = parser->current;
    char* end = 0;
    long long value = 0;
    if (is_type_start(parser)) {
        Parser probe = *parser;
        AstType probe_type = parse_type(&probe);
        if (!probe.error && probe.current.kind == TOKEN_LEFT_BRACE && probe_type.kind == AST_TYPE_SLICE) {
            AstExpr* call = new_expr(AST_EXPR_CALL, token.line);
            AstStructFieldInit field_init;
            memset(&field_init, 0, sizeof(field_init));
            call->as.call.callee = strdup("__slice_with_capacity");
            type_list_push(&call->as.call.type_args, parse_type(parser));
            if (!expect(parser, TOKEN_LEFT_BRACE, "expected '{' after slice type")) {
                return 0;
            }
            if (parser->current.kind != TOKEN_IDENT || !token_equals(&parser->current, "capacity")) {
                fail(parser, "slice initializer requires 'capacity'");
                return 0;
            }
            advance(parser);
            if (!expect(parser, TOKEN_COLON, "expected ':' after capacity")) {
                return 0;
            }
            field_init.value = parse_expr(parser);
            if (!field_init.value) {
                return 0;
            }
            expr_list_push(&call->as.call.args, field_init.value);
            if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after slice initializer")) {
                return 0;
            }
            return call;
        }
    }

    if (looks_like_type_implicit_suffix(parser)) {
        return parse_type_implicit_expr(parser, token.line);
    }

    if ((token.kind == TOKEN_KW_INT || token.kind == TOKEN_KW_UINT8 || token.kind == TOKEN_KW_BOOL ||
         (token.kind == TOKEN_IDENT && is_known_type(parser, &token))) &&
        looks_like_typed_array_constructor(parser)) {
        AstType array_type = parse_type(parser);
        if (array_type.kind != AST_TYPE_ARRAY) {
            fail(parser, "typed array constructor requires an array type");
            return 0;
        }
        if (parser->current.kind == TOKEN_LEFT_BRACE) {
            AstExpr* array = new_expr(AST_EXPR_ARRAY, token.line);
            advance(parser);
            if (parser->current.kind != TOKEN_RIGHT_BRACE) {
                for (;;) {
                    expr = parse_expr(parser);
                    if (!expr) {
                        return 0;
                    }
                    expr_list_push(&array->as.array.items, expr);
                    if (parser->current.kind == TOKEN_COMMA) {
                        advance(parser);
                        continue;
                    }
                    break;
                }
            }
            if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after array literal")) {
                return 0;
            }
            return array;
        }
        if (parser->current.kind == TOKEN_LEFT_PAREN) {
            AstExpr* array = new_expr(AST_EXPR_ARRAY, token.line);
            AstExpr* item = 0;
            int i = 0;
            advance(parser);
            item = parse_expr(parser);
            if (!item) {
                return 0;
            }
            if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after repeated array initializer")) {
                return 0;
            }
            if (array_type.array_length < 0) {
                fail(parser, "repeated array initializer requires a fixed length");
                return 0;
            }
            for (i = 0; i < array_type.array_length; ++i) {
                expr_list_push(&array->as.array.items, item);
            }
            return array;
        }
    }

    if (token.kind == TOKEN_IDENT && looks_like_qualified_type_start(parser)) {
        Parser probe = *parser;
        char* qualified_name = parse_qualified_name(&probe);
        if (probe.current.kind == TOKEN_LT) {
            if (!parse_type_arg_list(&probe, &(AstTypeList){0})) {
                free(qualified_name);
                qualified_name = 0;
            }
        }
        if (qualified_name && probe.current.kind == TOKEN_LEFT_BRACE) {
            AstExpr* struct_lit = new_expr(AST_EXPR_STRUCT, token.line);
            free(qualified_name);
            struct_lit->as.struct_lit.type_name = parse_qualified_name(parser);
            if (parser->current.kind == TOKEN_LT) {
                if (!parse_type_arg_list(parser, &struct_lit->as.struct_lit.type_args)) {
                    return 0;
                }
            }
            advance(parser);
            if (parser->current.kind != TOKEN_RIGHT_BRACE) {
                for (;;) {
                    AstStructFieldInit field_init;
                    memset(&field_init, 0, sizeof(field_init));
                    if (parser->current.kind != TOKEN_IDENT) {
                        fail(parser, "expected struct field name");
                        return 0;
                    }
                    field_init.name = token_dup(&parser->current);
                    field_init.line = parser->current.line;
                    advance(parser);
                    if (!expect(parser, TOKEN_COLON, "expected ':' after struct field name")) {
                        return 0;
                    }
                    field_init.value = parse_expr(parser);
                    if (!field_init.value) {
                        return 0;
                    }
                    struct_field_init_list_push(&struct_lit->as.struct_lit.fields, field_init);
                    if (parser->current.kind == TOKEN_COMMA) {
                        advance(parser);
                        continue;
                    }
                    break;
                }
            }
            if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after struct literal")) {
                return 0;
            }
            return struct_lit;
        }
        free(qualified_name);
    }

    if (token.kind == TOKEN_IDENT && is_known_type(parser, &token) && parser->next.kind == TOKEN_LEFT_BRACE) {
        AstExpr* struct_lit = new_expr(AST_EXPR_STRUCT, token.line);
        struct_lit->as.struct_lit.type_name = token_dup(&token);
        advance(parser);
        advance(parser);
        if (parser->current.kind != TOKEN_RIGHT_BRACE) {
            for (;;) {
                AstStructFieldInit field_init;
                memset(&field_init, 0, sizeof(field_init));
                if (parser->current.kind != TOKEN_IDENT) {
                    fail(parser, "expected struct field name");
                    return 0;
                }
                field_init.name = token_dup(&parser->current);
                field_init.line = parser->current.line;
                advance(parser);
                if (!expect(parser, TOKEN_COLON, "expected ':' after struct field name")) {
                    return 0;
                }
                field_init.value = parse_expr(parser);
                if (!field_init.value) {
                    return 0;
                }
                struct_field_init_list_push(&struct_lit->as.struct_lit.fields, field_init);
                if (parser->current.kind == TOKEN_COMMA) {
                    advance(parser);
                    continue;
                }
                break;
            }
        }
        if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after struct literal")) {
            return 0;
        }
        return struct_lit;
    }

    if (token.kind == TOKEN_INT_LIT) {
        errno = 0;
        value = strtoll(token.start, &end, 10);
        if (errno != 0 || end != token.start + token.length || value > INT64_MAX || value < INT64_MIN) {
            fail(parser, "invalid integer literal");
            return 0;
        }
        expr = new_expr(AST_EXPR_INT, token.line);
        expr->as.int_value = (int64_t)value;
        advance(parser);
        return expr;
    }

    if (token.kind == TOKEN_KW_TRUE || token.kind == TOKEN_KW_FALSE) {
        expr = new_expr(AST_EXPR_BOOL, token.line);
        expr->as.bool_value = token.kind == TOKEN_KW_TRUE;
        advance(parser);
        return expr;
    }

    if (token.kind == TOKEN_STRING_LIT) {
        expr = new_expr(AST_EXPR_STRING, token.line);
        expr->as.string_lit.text = string_token_dup(&token);
        expr->as.string_lit.length = (int)(token.length >= 2 ? token.length - 2 : 0);
        advance(parser);
        return expr;
    }

    if (token.kind == TOKEN_KW_NULL) {
        expr = new_expr(AST_EXPR_NULL, token.line);
        advance(parser);
        return expr;
    }

    if (token.kind == TOKEN_IDENT) {
        if (parser->next.kind == TOKEN_DOT &&
            (is_known_type(parser, &token) || is_type_like_ident(&token) || looks_like_qualified_variant_ref(parser)) &&
            !looks_like_qualified_init_call(parser) &&
            !looks_like_qualified_call(parser)) {
            return parse_variant_expr(parser, 0);
        }
        char* name = token_dup(&token);
        advance(parser);
        expr = new_expr(AST_EXPR_NAME, token.line);
        expr->as.name = name;
        return expr;
    }

    if (token.kind == TOKEN_DOT) {
        return parse_variant_expr(parser, 0);
    }

    if (token.kind == TOKEN_LEFT_PAREN) {
        advance(parser);
        if (parser->current.kind == TOKEN_RIGHT_PAREN) {
            fail(parser, "empty tuple literal is only supported in return statements");
            return 0;
        }
        expr = parse_expr(parser);
        if (!expr) {
            return 0;
        }
        if (parser->current.kind == TOKEN_COMMA) {
            AstExpr* tuple = new_expr(AST_EXPR_TUPLE, token.line);
            expr_list_push(&tuple->as.tuple.items, expr);
            while (parser->current.kind == TOKEN_COMMA) {
                advance(parser);
                expr = parse_expr(parser);
                if (!expr) {
                    return 0;
                }
                expr_list_push(&tuple->as.tuple.items, expr);
            }
            if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after tuple literal")) {
                return 0;
            }
            return tuple;
        }
        if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after expression")) {
            return 0;
        }
        return expr;
    }

    if (token.kind == TOKEN_LEFT_BRACE) {
        AstExpr* array = new_expr(AST_EXPR_ARRAY, token.line);
        advance(parser);
        if (parser->current.kind != TOKEN_RIGHT_BRACE) {
            for (;;) {
                expr = parse_expr(parser);
                if (!expr) {
                    return 0;
                }
                expr_list_push(&array->as.array.items, expr);
                if (parser->current.kind == TOKEN_COMMA) {
                    advance(parser);
                    continue;
                }
                break;
            }
        }
        if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after array literal")) {
            return 0;
        }
        return array;
    }

    fail(parser, "expected expression");
    return 0;
}

static AstExpr* parse_postfix(Parser* parser) {
    AstExpr* expr = parse_primary(parser);
    if (!expr && looks_like_type_implicit_suffix(parser)) {
        return parse_type_implicit_expr(parser, parser->current.line);
    }
    while (expr) {
        if (parser->current.kind == TOKEN_DOLLAR) {
            expr = parse_implicit_member(parser, expr->line, expr);
            continue;
        }
        if ((parser->current.kind == TOKEN_LEFT_PAREN ||
             (parser->current.kind == TOKEN_LT && looks_like_call_type_args(parser)))) {
            AstExpr* call = new_expr(AST_EXPR_CALL, expr->line);
            call->as.call.callee = expr_to_qualified_callee(expr);
            if (!call->as.call.callee) {
                free(call);
                break;
            }
            if (parser->current.kind == TOKEN_LT) {
                if (!parse_type_arg_list(parser, &call->as.call.type_args)) {
                    return 0;
                }
            }
            advance(parser);
            if (parser->current.kind != TOKEN_RIGHT_PAREN) {
                for (;;) {
                    AstExpr* arg = parse_expr(parser);
                    if (!arg) {
                        return 0;
                    }
                    expr_list_push(&call->as.call.args, arg);
                    if (parser->current.kind == TOKEN_COMMA) {
                        advance(parser);
                        continue;
                    }
                    break;
                }
            }
            if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after call arguments")) {
                return 0;
            }
            expr = call;
            continue;
        }
        if (parser->current.kind == TOKEN_LEFT_BRACKET) {
            AstExpr* index = new_expr(AST_EXPR_INDEX, expr->line);
            advance(parser);
            index->as.index.base = expr;
            index->as.index.index = parse_expr(parser);
            if (!index->as.index.index) {
                return 0;
            }
            if (!expect(parser, TOKEN_RIGHT_BRACKET, "expected ']' after index expression")) {
                return 0;
            }
            expr = index;
            continue;
        }
        if (parser->current.kind == TOKEN_QUESTION && parser->next.kind == TOKEN_LEFT_BRACKET) {
            AstExpr* index = new_expr(AST_EXPR_OPTIONAL_INDEX, expr->line);
            advance(parser);
            advance(parser);
            index->as.index.base = expr;
            index->as.index.index = parse_expr(parser);
            if (!index->as.index.index) {
                return 0;
            }
            if (!expect(parser, TOKEN_RIGHT_BRACKET, "expected ']' after optional index expression")) {
                return 0;
            }
            expr = index;
            continue;
        }
        if (parser->current.kind == TOKEN_DOT) {
            AstExpr* field = 0;
            if (parser->next.kind == TOKEN_IDENT && token_equals(&parser->next, "indexed")) {
                break;
            }
            advance(parser);
            if (parser->current.kind != TOKEN_IDENT) {
                return 0;
            }
            field = new_expr(AST_EXPR_FIELD, expr->line);
            field->as.field.base = expr;
            field->as.field.name = token_dup(&parser->current);
            advance(parser);
            expr = field;
            continue;
        }
        if (parser->current.kind == TOKEN_QUESTION_DOT) {
            AstExpr* field = 0;
            advance(parser);
            if (parser->current.kind != TOKEN_IDENT) {
                return 0;
            }
            field = new_expr(AST_EXPR_OPTIONAL_FIELD, expr->line);
            field->as.field.base = expr;
            field->as.field.name = token_dup(&parser->current);
            advance(parser);
            expr = field;
            continue;
        }
        break;
    }
    return expr;
}

static AstExpr* parse_multiplicative(Parser* parser) {
    AstExpr* expr = parse_unary(parser);
    while (expr && (parser->current.kind == TOKEN_STAR || parser->current.kind == TOKEN_SLASH)) {
        AstExpr* right = 0;
        AstExpr* bin = new_expr(AST_EXPR_BINARY, parser->current.line);
        bin->as.binary.left = expr;
        bin->as.binary.op = parser->current.kind == TOKEN_STAR ? AST_BIN_MUL : AST_BIN_DIV;
        advance(parser);
        right = parse_unary(parser);
        if (!right) {
            return 0;
        }
        bin->as.binary.right = right;
        expr = bin;
    }
    return expr;
}

static AstExpr* parse_named_member_target_expr(const char* callee, char** out_member, int line) {
    size_t callee_len = 0;
    char* prefix = 0;
    char* dot = 0;
    AstExpr* target = 0;
    if (!callee) {
        return 0;
    }
    callee_len = strlen(callee);
    dot = strrchr(callee, '.');
    if (!dot || dot == callee || !dot[1]) {
        return 0;
    }
    prefix = strndup(callee, (size_t)(dot - callee));
    if (!prefix) {
        return 0;
    }
    if (out_member) {
        *out_member = strdup(dot + 1);
    }
    dot = strrchr(prefix, '.');
    if (!dot) {
        target = new_expr(AST_EXPR_NAME, line);
        target->as.name = prefix;
        return target;
    }
    *dot = '\0';
    target = new_expr(AST_EXPR_FIELD, line);
    target->as.field.base = new_expr(AST_EXPR_NAME, line);
    target->as.field.base->as.name = strdup(prefix);
    target->as.field.name = strdup(dot + 1);
    free(prefix);
    return target;
}

static int parse_implicit_args(Parser* parser, AstExprList* out_args) {
    if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after implicit operation")) {
        return 0;
    }
    if (parser->current.kind != TOKEN_RIGHT_PAREN) {
        for (;;) {
            AstExpr* arg = parse_expr(parser);
            if (!arg) {
                return 0;
            }
            expr_list_push(out_args, arg);
            if (parser->current.kind == TOKEN_COMMA) {
                advance(parser);
                continue;
            }
            break;
        }
    }
    return expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after implicit operation arguments");
}

static int parse_implicit_cast_args(Parser* parser, AstExpr* expr) {
    if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after implicit operation")) {
        return 0;
    }
    if (!is_type_start(parser)) {
        fail(parser, "expected cast target type");
        return 0;
    }
    expr->as.implicit.has_type_arg = 1;
    expr->as.implicit.type_arg = parse_type(parser);
    if (parser->current.kind == TOKEN_COMMA) {
        fail(parser, "cast accepts exactly one type argument");
        return 0;
    }
    return expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after cast target type");
}

static int is_implicit_member_name(Token* token) {
    return token->kind == TOKEN_IDENT &&
           (token_equals(token, "cast") ||
            token_equals(token, "ref") ||
            token_equals(token, "addr") ||
            token_equals(token, "free"));
}

static AstExpr* parse_implicit_member(Parser* parser, int line, AstExpr* value_target) {
    AstExpr* expr = new_expr(AST_EXPR_IMPLICIT, line);
    expr->as.implicit.value_target = value_target;
    if (!expect(parser, TOKEN_DOLLAR, "expected '$' after implicit target")) {
        return 0;
    }
    if (!expect(parser, TOKEN_DOT, "expected '.' after '$'")) {
        return 0;
    }
    if (!is_implicit_member_name(&parser->current)) {
        fail(parser, "expected implicit operation name after '$.'");
        return 0;
    }
    expr->as.implicit.member = token_dup(&parser->current);
    advance(parser);
    if (strcmp(expr->as.implicit.member, "cast") == 0) {
        return parse_implicit_cast_args(parser, expr) ? expr : 0;
    }
    return parse_implicit_args(parser, &expr->as.implicit.args) ? expr : 0;
}

static AstExpr* parse_type_implicit_expr(Parser* parser, int line) {
    AstExpr* expr = new_expr(AST_EXPR_IMPLICIT, line);
    expr->as.implicit.target_is_type = 1;
    expr->as.implicit.type_target = parse_type(parser);
    if (!expect(parser, TOKEN_DOLLAR, "expected '$' after implicit type target")) {
        return 0;
    }
    if (!expect(parser, TOKEN_DOT, "expected '.' after '$'")) {
        return 0;
    }
    if (parser->current.kind != TOKEN_IDENT) {
        fail(parser, "expected implicit type operation name");
        return 0;
    }
    expr->as.implicit.member = token_dup(&parser->current);
    advance(parser);
    if (strcmp(expr->as.implicit.member, "cast") == 0) {
        fail(parser, "type implicit operation '.cast(...)' is unsupported");
        return 0;
    }
    return parse_implicit_args(parser, &expr->as.implicit.args) ? expr : 0;
}

static AstExpr* parse_unary(Parser* parser) {
    if (parser->current.kind == TOKEN_MINUS) {
        AstExpr* zero = new_expr(AST_EXPR_INT, parser->current.line);
        AstExpr* right = 0;
        AstExpr* bin = new_expr(AST_EXPR_BINARY, parser->current.line);
        zero->as.int_value = 0;
        advance(parser);
        right = parse_unary(parser);
        if (!right) {
            return 0;
        }
        bin->as.binary.left = zero;
        bin->as.binary.op = AST_BIN_SUB;
        bin->as.binary.right = right;
        return bin;
    }
    if (parser->current.kind == TOKEN_STAR) {
        AstExpr* expr = new_expr(AST_EXPR_DEREF, parser->current.line);
        advance(parser);
        expr->as.unary.value = parse_unary(parser);
        return expr->as.unary.value ? expr : 0;
    }
    if (parser->current.kind == TOKEN_KW_NEW) {
        AstExpr* expr = new_expr(AST_EXPR_NEW, parser->current.line);
        advance(parser);
        expr->as.unary.value = parse_postfix(parser);
        return expr->as.unary.value ? expr : 0;
    }
    return parse_postfix(parser);
}

static AstExpr* parse_additive(Parser* parser) {
    AstExpr* expr = parse_multiplicative(parser);
    while (expr && (parser->current.kind == TOKEN_PLUS || parser->current.kind == TOKEN_MINUS)) {
        AstExpr* right = 0;
        AstExpr* bin = new_expr(AST_EXPR_BINARY, parser->current.line);
        bin->as.binary.left = expr;
        bin->as.binary.op = parser->current.kind == TOKEN_PLUS ? AST_BIN_ADD : AST_BIN_SUB;
        advance(parser);
        right = parse_multiplicative(parser);
        if (!right) {
            return 0;
        }
        bin->as.binary.right = right;
        expr = bin;
    }
    return expr;
}

static AstExpr* parse_comparison(Parser* parser) {
    AstExpr* expr = parse_additive(parser);
    while (expr && (parser->current.kind == TOKEN_LT || parser->current.kind == TOKEN_LT_EQ || parser->current.kind == TOKEN_GT || parser->current.kind == TOKEN_GT_EQ)) {
        AstExpr* right = 0;
        AstExpr* bin = new_expr(AST_EXPR_BINARY, parser->current.line);
        bin->as.binary.left = expr;
        switch (parser->current.kind) {
            case TOKEN_LT: bin->as.binary.op = AST_BIN_LT; break;
            case TOKEN_LT_EQ: bin->as.binary.op = AST_BIN_LE; break;
            case TOKEN_GT: bin->as.binary.op = AST_BIN_GT; break;
            default: bin->as.binary.op = AST_BIN_GE; break;
        }
        advance(parser);
        right = parse_additive(parser);
        if (!right) {
            return 0;
        }
        bin->as.binary.right = right;
        expr = bin;
    }
    return expr;
}

static AstExpr* parse_equality(Parser* parser) {
    AstExpr* expr = parse_comparison(parser);
    while (expr && (parser->current.kind == TOKEN_EQ_EQ || parser->current.kind == TOKEN_BANG_EQ)) {
        AstExpr* right = 0;
        AstExpr* bin = new_expr(AST_EXPR_BINARY, parser->current.line);
        bin->as.binary.left = expr;
        bin->as.binary.op = parser->current.kind == TOKEN_EQ_EQ ? AST_BIN_EQ : AST_BIN_NE;
        advance(parser);
        if (looks_like_variant_pattern_expr(parser)) {
            right = parse_variant_expr(parser, 1);
        } else {
            right = parse_comparison(parser);
        }
        if (!right) {
            return 0;
        }
        bin->as.binary.right = right;
        expr = bin;
    }
    return expr;
}

static AstExpr* parse_coalesce(Parser* parser) {
    AstExpr* expr = parse_equality(parser);
    while (expr && parser->current.kind == TOKEN_QUESTION_QUESTION) {
        AstExpr* right = 0;
        AstExpr* out = new_expr(AST_EXPR_COALESCE, parser->current.line);
        out->as.coalesce.left = expr;
        advance(parser);
        right = parse_equality(parser);
        if (!right) {
            return 0;
        }
        out->as.coalesce.right = right;
        expr = out;
    }
    return expr;
}

static AstExpr* parse_expr(Parser* parser) {
    AstExpr* expr = parse_coalesce(parser);
    if (!expr) {
        return 0;
    }
    if (parser->current.kind == TOKEN_QUESTION) {
        AstExpr* ternary = new_expr(AST_EXPR_TERNARY, expr->line);
        advance(parser);
        ternary->as.ternary.cond = expr;
        ternary->as.ternary.then_expr = parse_expr(parser);
        if (!ternary->as.ternary.then_expr) {
            return 0;
        }
        if (!expect(parser, TOKEN_COLON, "expected ':' in ternary expression")) {
            return 0;
        }
        ternary->as.ternary.else_expr = parse_expr(parser);
        if (!ternary->as.ternary.else_expr) {
            return 0;
        }
        return ternary;
    }
    return expr;
}

static int parse_block(Parser* parser, AstBlock* out_block);

static AstStmt* parse_stmt(Parser* parser) {
    AstStmt* stmt = 0;

    if (parser->current.kind == TOKEN_KW_RETURN) {
        stmt = new_stmt(AST_STMT_RETURN, parser->current.line);
        advance(parser);
        if (parser->current.kind == TOKEN_SEMICOLON) {
            stmt->as.ret.expr = 0;
        } else if (parser->current.kind == TOKEN_LEFT_PAREN && parser->next.kind == TOKEN_RIGHT_PAREN) {
            advance(parser);
            advance(parser);
            stmt->as.ret.expr = 0;
        } else {
            stmt->as.ret.expr = parse_expr(parser);
            if (!stmt->as.ret.expr) {
                return 0;
            }
        }
        if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after return")) {
            return 0;
        }
        return stmt;
    }

    if (parser->current.kind == TOKEN_KW_BREAK) {
        stmt = new_stmt(AST_STMT_BREAK, parser->current.line);
        advance(parser);
        if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after break")) {
            return 0;
        }
        return stmt;
    }

    if (parser->current.kind == TOKEN_KW_CONTINUE) {
        stmt = new_stmt(AST_STMT_CONTINUE, parser->current.line);
        advance(parser);
        if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after continue")) {
            return 0;
        }
        return stmt;
    }

    if (parser->current.kind == TOKEN_KW_IF) {
        stmt = new_stmt(AST_STMT_IF, parser->current.line);
        advance(parser);
        if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after if")) {
            return 0;
        }
        stmt->as.if_stmt.cond = parse_expr(parser);
        if (!stmt->as.if_stmt.cond) {
            return 0;
        }
        if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after if condition")) {
            return 0;
        }
        if (!parse_block(parser, &stmt->as.if_stmt.then_block)) {
            return 0;
        }
        if (parser->current.kind == TOKEN_KW_ELSE) {
            stmt->as.if_stmt.has_else = 1;
            advance(parser);
            if (!parse_block(parser, &stmt->as.if_stmt.else_block)) {
                return 0;
            }
        }
        return stmt;
    }

    if (parser->current.kind == TOKEN_KW_SWITCH) {
        stmt = new_stmt(AST_STMT_SWITCH, parser->current.line);
        advance(parser);
        if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after switch")) {
            return 0;
        }
        stmt->as.switch_stmt.value = parse_expr(parser);
        if (!stmt->as.switch_stmt.value) {
            return 0;
        }
        if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after switch value")) {
            return 0;
        }
        if (!expect(parser, TOKEN_LEFT_BRACE, "expected '{' after switch")) {
            return 0;
        }
        while (parser->current.kind != TOKEN_RIGHT_BRACE && parser->current.kind != TOKEN_EOF) {
            AstSwitchCase switch_case;
            AstStmt* case_stmt = 0;
            memset(&switch_case, 0, sizeof(switch_case));
            if (parser->current.kind == TOKEN_KW_ELSE) {
                switch_case.is_else = 1;
                advance(parser);
            } else {
                if (looks_like_variant_pattern_expr(parser)) {
                    switch_case.pattern = parse_variant_expr(parser, 1);
                } else {
                    switch_case.pattern = parse_expr(parser);
                }
                if (!switch_case.pattern) {
                    return 0;
                }
            }
            if (!expect(parser, TOKEN_COLON, "expected ':' after switch case")) {
                return 0;
            }
            if (parser->current.kind == TOKEN_LEFT_BRACE) {
                if (!parse_block(parser, &switch_case.body)) {
                    return 0;
                }
            } else {
                case_stmt = parse_stmt(parser);
                if (!case_stmt) {
                    return 0;
                }
                stmt_list_push(&switch_case.body.stmts, case_stmt);
            }
            switch_case_list_push(&stmt->as.switch_stmt.cases, switch_case);
        }
        if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after switch")) {
            return 0;
        }
        return stmt;
    }

    if (parser->current.kind == TOKEN_KW_WHILE) {
        stmt = new_stmt(AST_STMT_WHILE, parser->current.line);
        advance(parser);
        if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after while")) {
            return 0;
        }
        stmt->as.while_stmt.cond = parse_expr(parser);
        if (!stmt->as.while_stmt.cond) {
            return 0;
        }
        if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after while condition")) {
            return 0;
        }
        if (!parse_block(parser, &stmt->as.while_stmt.body)) {
            return 0;
        }
        return stmt;
    }

    if (parser->current.kind == TOKEN_KW_FOR) {
        stmt = new_stmt(AST_STMT_FOR_RANGE, parser->current.line);
        advance(parser);
        if (parser->current.kind == TOKEN_LEFT_PAREN) {
            stmt->kind = AST_STMT_FOR_EACH;
            stmt->as.for_each.pattern = parse_binding_pattern(parser);
            if (!stmt->as.for_each.pattern) {
                return 0;
            }
            if (!expect(parser, TOKEN_KW_IN, "expected 'in' in for-each")) {
                return 0;
            }
            stmt->as.for_each.iterable = parse_expr(parser);
            if (!stmt->as.for_each.iterable) {
                return 0;
            }
            if (parser->current.kind == TOKEN_DOT) {
                advance(parser);
                if (parser->current.kind != TOKEN_IDENT || !token_equals(&parser->current, "indexed")) {
                    fail(parser, "expected 'indexed' after '.'");
                    return 0;
                }
                advance(parser);
                if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after indexed")) {
                    return 0;
                }
                if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after indexed()")) {
                    return 0;
                }
                stmt->as.for_each.indexed_flag = 1;
            }
            if (!parse_block(parser, &stmt->as.for_each.body)) {
                return 0;
            }
            return stmt;
        }
        if (parser->current.kind == TOKEN_IDENT && token_equals(&parser->current, "_")) {
            stmt->kind = AST_STMT_FOR_EACH;
            stmt->as.for_each.pattern = parse_binding_pattern(parser);
            if (!stmt->as.for_each.pattern) {
                return 0;
            }
            if (!expect(parser, TOKEN_KW_IN, "expected 'in' in for-each")) {
                return 0;
            }
            stmt->as.for_each.iterable = parse_expr(parser);
            if (!stmt->as.for_each.iterable) {
                return 0;
            }
            if (parser->current.kind == TOKEN_DOT) {
                advance(parser);
                if (parser->current.kind != TOKEN_IDENT || !token_equals(&parser->current, "indexed")) {
                    fail(parser, "expected 'indexed' after '.'");
                    return 0;
                }
                advance(parser);
                if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after indexed")) {
                    return 0;
                }
                if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after indexed()")) {
                    return 0;
                }
                stmt->as.for_each.indexed_flag = 1;
            }
            if (!parse_block(parser, &stmt->as.for_each.body)) {
                return 0;
            }
            return stmt;
        }
        if (parser->current.kind == TOKEN_IDENT && !is_known_type(parser, &parser->current) && parser->next.kind == TOKEN_KW_IN) {
            stmt->as.for_range.type.kind = AST_TYPE_INFER;
            stmt->as.for_range.name = token_dup(&parser->current);
            advance(parser);
            if (!expect(parser, TOKEN_KW_IN, "expected 'in' in for range")) {
                return 0;
            }
            stmt->as.for_range.start = parse_expr(parser);
            if (!stmt->as.for_range.start) {
                return 0;
            }
            if (parser->current.kind == TOKEN_DOT) {
                advance(parser);
                if (parser->current.kind != TOKEN_IDENT || !token_equals(&parser->current, "indexed")) {
                    fail(parser, "expected 'indexed' after '.'");
                    return 0;
                }
                advance(parser);
                if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after indexed")) {
                    return 0;
                }
                if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after indexed()")) {
                    return 0;
                }
                stmt->kind = AST_STMT_FOR_EACH;
                stmt->as.for_each.indexed_flag = 1;
                stmt->as.for_each.pattern = new_binding_pattern(AST_BINDING_NAME, stmt->line);
                stmt->as.for_each.pattern->type.kind = AST_TYPE_INFER;
                stmt->as.for_each.pattern->name = stmt->as.for_range.name;
                stmt->as.for_each.iterable = stmt->as.for_range.start;
                if (!parse_block(parser, &stmt->as.for_each.body)) {
                    return 0;
                }
                return stmt;
            }
            if (parser->current.kind != TOKEN_DOT_DOT) {
                stmt->kind = AST_STMT_FOR_EACH;
                stmt->as.for_each.pattern = new_binding_pattern(AST_BINDING_NAME, stmt->line);
                stmt->as.for_each.pattern->type.kind = AST_TYPE_INFER;
                stmt->as.for_each.pattern->name = stmt->as.for_range.name;
                stmt->as.for_each.iterable = stmt->as.for_range.start;
                if (!parse_block(parser, &stmt->as.for_each.body)) {
                    return 0;
                }
                return stmt;
            }
            advance(parser);
            stmt->as.for_range.end = parse_expr(parser);
            if (!stmt->as.for_range.end) {
                return 0;
            }
            if (!parse_block(parser, &stmt->as.for_range.body)) {
                return 0;
            }
            return stmt;
        }
        if (!is_type_start(parser)) {
            fail(parser, "expected loop variable type after for");
            return 0;
        }
        stmt->as.for_range.type = parse_type(parser);
        if (parser->current.kind != TOKEN_IDENT) {
            fail(parser, "expected loop variable name");
            return 0;
        }
        stmt->as.for_range.name = token_dup(&parser->current);
        advance(parser);
        if (!expect(parser, TOKEN_KW_IN, "expected 'in' in for range")) {
            return 0;
        }
        stmt->as.for_range.start = parse_expr(parser);
        if (!stmt->as.for_range.start) {
            return 0;
        }
        if (!expect(parser, TOKEN_DOT_DOT, "expected '..' in for range")) {
            return 0;
        }
        stmt->as.for_range.end = parse_expr(parser);
        if (!stmt->as.for_range.end) {
            return 0;
        }
        if (!parse_block(parser, &stmt->as.for_range.body)) {
            return 0;
        }
        return stmt;
    }

    if (parser->current.kind == TOKEN_LEFT_PAREN && looks_like_destructure(parser)) {
        stmt = new_stmt(AST_STMT_DESTRUCTURE, parser->current.line);
        if (!parse_binding_list(parser, &stmt->as.destructure.bindings)) {
            return 0;
        }
        if (!expect(parser, TOKEN_ASSIGN, "expected '=' in destructure declaration")) {
            return 0;
        }
        stmt->as.destructure.init = parse_expr(parser);
        if (!stmt->as.destructure.init) {
            return 0;
        }
        if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after destructure declaration")) {
            return 0;
        }
        return stmt;
    }

    if (looks_like_var_decl(parser)) {
        stmt = new_stmt(AST_STMT_VAR_DECL, parser->current.line);
        stmt->as.var_decl.type = parse_type(parser);
        if (parser->current.kind != TOKEN_IDENT) {
            fail(parser, "expected identifier after type");
            return 0;
        }
        stmt->as.var_decl.name = token_dup(&parser->current);
        advance(parser);
        if (!expect(parser, TOKEN_ASSIGN, "expected '=' in variable declaration")) {
            return 0;
        }
        stmt->as.var_decl.init = parse_expr(parser);
        if (!stmt->as.var_decl.init) {
            return 0;
        }
        if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after variable declaration")) {
            return 0;
        }
        return stmt;
    }

    if (parser->current.kind == TOKEN_IDENT || parser->current.kind == TOKEN_STAR || parser->current.kind == TOKEN_LEFT_PAREN) {
        AstExpr* target = parse_expr(parser);
        if (!target) {
            return 0;
        }
        if (parser->current.kind == TOKEN_ASSIGN) {
            stmt = new_stmt(AST_STMT_ASSIGN, target->line);
            stmt->as.assign.target = target;
            advance(parser);
            stmt->as.assign.value = parse_expr(parser);
            if (!stmt->as.assign.value) {
                return 0;
            }
            if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after assignment")) {
                return 0;
            }
            return stmt;
        }
        stmt = new_stmt(AST_STMT_EXPR, target->line);
        stmt->as.expr_stmt.expr = target;
        if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after expression")) {
            return 0;
        }
        return stmt;
    }

    stmt = new_stmt(AST_STMT_EXPR, parser->current.line);
    stmt->as.expr_stmt.expr = parse_expr(parser);
    if (!stmt->as.expr_stmt.expr) {
        return 0;
    }
    if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after expression")) {
        return 0;
    }
    return stmt;
}

static int parse_block(Parser* parser, AstBlock* out_block) {
    if (!expect(parser, TOKEN_LEFT_BRACE, "expected '{' to start block")) {
        return 0;
    }
    while (parser->current.kind != TOKEN_RIGHT_BRACE && parser->current.kind != TOKEN_EOF) {
        AstStmt* stmt = parse_stmt(parser);
        if (!stmt) {
            return 0;
        }
        stmt_list_push(&out_block->stmts, stmt);
    }
    return expect(parser, TOKEN_RIGHT_BRACE, "expected '}' to close block");
}

static int parse_params(Parser* parser, AstParamList* params) {
    if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after function name")) {
        return 0;
    }
    if (parser->current.kind != TOKEN_RIGHT_PAREN) {
        for (;;) {
            AstParam param;
            memset(&param, 0, sizeof(param));
            if (!is_type_start(parser)) {
                return fail(parser, "expected parameter type");
            }
            param.type = parse_type(parser);
            if (param.type.kind == AST_TYPE_INFER) {
                return fail(parser, "parameter type cannot be inferred");
            }
            if (parser->current.kind != TOKEN_IDENT) {
                return fail(parser, "expected parameter name");
            }
            param.name = token_dup(&parser->current);
            param.line = parser->current.line;
            advance(parser);
            param_list_push(params, param);
            if (parser->current.kind == TOKEN_COMMA) {
                advance(parser);
                continue;
            }
            break;
        }
    }
    return expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after parameters");
}

static int parse_method_decl(Parser* parser, AstProgram* out_program, const char* owner_type_name) {
    AstFunction fn;
    memset(&fn, 0, sizeof(fn));
    if (parser->current.kind == TOKEN_KW_PUBLIC) {
        fn.public_flag = 1;
        advance(parser);
    }
    if (parser->current.kind == TOKEN_KW_STATIC) {
        fn.static_method_flag = 1;
        advance(parser);
    }
    if (!is_type_start(parser)) {
        return fail(parser, "expected method return type");
    }
    fn.return_type = parse_type(parser);
    if (fn.return_type.kind == AST_TYPE_INFER) {
        return fail(parser, "method return type cannot be inferred");
    }
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, "expected method name");
    }
    fn.name = token_dup(&parser->current);
    fn.method_flag = 1;
    fn.owner_type_name = (char*)owner_type_name;
    fn.line = parser->current.line;
    advance(parser);
    if (!parse_params(parser, &fn.params)) {
        return 0;
    }
    if (!parse_block(parser, &fn.body)) {
        return 0;
    }
    function_list_push(&out_program->functions, fn);
    return 1;
}

static int parse_import_decl(Parser* parser, AstProgram* out_program) {
    AstImportDecl import_decl;
    memset(&import_decl, 0, sizeof(import_decl));
    import_decl.line = parser->current.line;
    advance(parser);
    if (parser->current.kind == TOKEN_IDENT && parser->next.kind == TOKEN_ASSIGN) {
        import_decl.alias_name = token_dup(&parser->current);
        advance(parser);
        advance(parser);
    }
    if (parser->current.kind != TOKEN_STRING_LIT) {
        return fail(parser, "expected import path string");
    }
    import_decl.path = string_token_dup(&parser->current);
    advance(parser);
    if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after import")) {
        return 0;
    }
    import_list_push(&out_program->imports, import_decl);
    return 1;
}

static int parse_alias_decl(Parser* parser, AstProgram* out_program, int public_flag) {
    AstAliasDecl alias_decl;
    memset(&alias_decl, 0, sizeof(alias_decl));
    alias_decl.public_flag = public_flag;
    alias_decl.line = parser->current.line;
    advance(parser);
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, "expected alias name");
    }
    alias_decl.name = token_dup(&parser->current);
    if (is_type_like_ident(&parser->current)) {
        register_known_type(parser, alias_decl.name);
    }
    advance(parser);
    if (!expect(parser, TOKEN_ASSIGN, "expected '=' in alias declaration")) {
        return 0;
    }
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, "expected alias target");
    }
    alias_decl.target_name = parse_qualified_name(parser);
    if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after alias declaration")) {
        return 0;
    }
    alias_list_push(&out_program->aliases, alias_decl);
    return 1;
}

static int parse_enum_decl(Parser* parser, AstProgram* out_program) {
    AstEnumDecl enum_decl;
    memset(&enum_decl, 0, sizeof(enum_decl));
    advance(parser);
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, "expected enum name");
    }
    enum_decl.name = token_dup(&parser->current);
    enum_decl.line = parser->current.line;
    register_known_type(parser, enum_decl.name);
    advance(parser);
    if (!expect(parser, TOKEN_LEFT_BRACE, "expected '{' after enum name")) {
        return 0;
    }
    while (parser->current.kind != TOKEN_RIGHT_BRACE && parser->current.kind != TOKEN_EOF) {
        if (parser->current.kind == TOKEN_KW_STATIC || looks_like_method_decl(parser)) {
            if (!parse_method_decl(parser, out_program, enum_decl.name)) {
                return 0;
            }
            continue;
        }
        AstEnumMember member;
        memset(&member, 0, sizeof(member));
        if (parser->current.kind != TOKEN_IDENT) {
            return fail(parser, "expected enum member");
        }
        member.name = token_dup(&parser->current);
        member.line = parser->current.line;
        advance(parser);
        if (parser->current.kind == TOKEN_ASSIGN) {
            char* end = 0;
            long long value = 0;
            advance(parser);
            if (parser->current.kind != TOKEN_INT_LIT) {
                return fail(parser, "expected integer enum value");
            }
            errno = 0;
            value = strtoll(parser->current.start, &end, 10);
            if (errno != 0 || end != parser->current.start + parser->current.length || value > INT64_MAX || value < INT64_MIN) {
                return fail(parser, "invalid integer enum value");
            }
            member.has_value = 1;
            member.value = (int64_t)value;
            advance(parser);
        }
        enum_member_list_push(&enum_decl.members, member);
        if (parser->current.kind == TOKEN_COMMA) {
            advance(parser);
        }
    }
    if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after enum declaration")) {
        return 0;
    }
    enum_list_push(&out_program->enums, enum_decl);
    return 1;
}

static int parse_struct_decl(Parser* parser, AstProgram* out_program, AstNameList* generic_params) {
    AstStructDecl struct_decl;
    memset(&struct_decl, 0, sizeof(struct_decl));
    if (generic_params) {
        struct_decl.type_params = *generic_params;
        memset(generic_params, 0, sizeof(*generic_params));
    }
    advance(parser);
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, "expected struct name");
    }
    struct_decl.name = token_dup(&parser->current);
    struct_decl.line = parser->current.line;
    register_known_type(parser, struct_decl.name);
    advance(parser);
    if (parser->current.kind == TOKEN_LT) {
        if (struct_decl.type_params.count != 0) {
            return fail(parser, "duplicate generic parameter list");
        }
        if (!parse_named_generic_params(parser, &struct_decl.type_params)) {
            return 0;
        }
    }
    if (!expect(parser, TOKEN_LEFT_BRACE, "expected '{' after struct name")) {
        return 0;
    }
    while (parser->current.kind != TOKEN_RIGHT_BRACE && parser->current.kind != TOKEN_EOF) {
        if (parser->current.kind == TOKEN_KW_PUBLIC &&
            parser->next.kind == TOKEN_IDENT &&
            token_equals(&parser->next, "deinit")) {
            return fail(parser, "deinit must not be public");
        }
        if (parser->current.kind == TOKEN_KW_PUBLIC &&
            parser->next.kind == TOKEN_IDENT &&
            token_equals(&parser->next, "init")) {
            advance(parser);
        }
        if (parser->current.kind == TOKEN_KW_STATIC &&
            parser->next.kind == TOKEN_IDENT &&
            token_equals(&parser->next, "deinit")) {
            return fail(parser, "static deinit not allowed");
        }
        if (parser->current.kind == TOKEN_IDENT && token_equals(&parser->current, "init") && parser->next.kind == TOKEN_LEFT_PAREN) {
            if (struct_decl.has_init) {
                return fail(parser, "duplicate struct init");
            }
            struct_decl.has_init = 1;
            struct_decl.init_line = parser->current.line;
            advance(parser);
            if (!parse_params(parser, &struct_decl.init_params)) {
                return 0;
            }
            if (!parse_block(parser, &struct_decl.init_body)) {
                return 0;
            }
            continue;
        }
        if (parser->current.kind == TOKEN_IDENT && token_equals(&parser->current, "deinit") && parser->next.kind == TOKEN_LEFT_PAREN) {
            if (struct_decl.has_deinit) {
                return fail(parser, "duplicate struct deinit");
            }
            struct_decl.has_deinit = 1;
            struct_decl.deinit_line = parser->current.line;
            advance(parser);
            if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after deinit")) {
                return 0;
            }
            if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after deinit")) {
                return 0;
            }
            if (!parse_block(parser, &struct_decl.deinit_body)) {
                return 0;
            }
            continue;
        }
        if (parser->current.kind == TOKEN_KW_STATIC || looks_like_method_decl(parser)) {
            if (!parse_method_decl(parser, out_program, struct_decl.name)) {
                return 0;
            }
            continue;
        }
        AstStructField field;
        memset(&field, 0, sizeof(field));
        if (!is_type_start(parser)) {
            return fail(parser, "expected struct field type");
        }
        field.type = parse_type(parser);
        if (parser->current.kind != TOKEN_IDENT) {
            return fail(parser, "expected struct field name");
        }
        field.name = token_dup(&parser->current);
        field.line = parser->current.line;
        advance(parser);
        if (parser->current.kind == TOKEN_ASSIGN) {
            advance(parser);
            field.default_value = parse_expr(parser);
            if (!field.default_value) {
                return 0;
            }
        }
        if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after struct field")) {
            return 0;
        }
        struct_field_list_push(&struct_decl.fields, field);
    }
    if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after struct declaration")) {
        return 0;
    }
    struct_list_push(&out_program->structs, struct_decl);
    return 1;
}

static int parse_union_decl(Parser* parser, AstProgram* out_program) {
    AstUnionDecl union_decl;
    memset(&union_decl, 0, sizeof(union_decl));
    advance(parser);
    if (parser->current.kind == TOKEN_LEFT_PAREN) {
        advance(parser);
        if (parser->current.kind != TOKEN_IDENT) {
            return fail(parser, "expected union tag enum name");
        }
        union_decl.tag_name = token_dup(&parser->current);
        advance(parser);
        if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after union tag enum")) {
            return 0;
        }
    }
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, "expected union name");
    }
    union_decl.name = token_dup(&parser->current);
    union_decl.line = parser->current.line;
    register_known_type(parser, union_decl.name);
    advance(parser);
    if (!expect(parser, TOKEN_LEFT_BRACE, "expected '{' after union name")) {
        return 0;
    }
    while (parser->current.kind != TOKEN_RIGHT_BRACE && parser->current.kind != TOKEN_EOF) {
        if (parser->current.kind == TOKEN_KW_STATIC || looks_like_method_decl(parser)) {
            if (!parse_method_decl(parser, out_program, union_decl.name)) {
                return 0;
            }
            continue;
        }
        AstUnionVariant variant;
        memset(&variant, 0, sizeof(variant));
        if (!is_type_start(parser)) {
            return fail(parser, "expected union variant type");
        }
        variant.type = parse_type(parser);
        if (parser->current.kind != TOKEN_IDENT) {
            return fail(parser, "expected union variant name");
        }
        variant.name = token_dup(&parser->current);
        variant.line = parser->current.line;
        advance(parser);
        if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after union variant")) {
            return 0;
        }
        union_variant_list_push(&union_decl.variants, variant);
    }
    if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after union declaration")) {
        return 0;
    }
    union_list_push(&out_program->unions, union_decl);
    return 1;
}

void parser_init(Parser* parser, const char* source) {
    lexer_init(&parser->lexer, source);
    parser->error = 0;
    parser->error_line = 1;
    hashmap_init(&parser->known_types);
    parser->current = lexer_next(&parser->lexer);
    parser->next = lexer_next(&parser->lexer);
}

int parser_parse_program(Parser* parser, AstProgram* out_program) {
    memset(out_program, 0, sizeof(*out_program));
    while (parser->current.kind != TOKEN_EOF) {
        AstType type;
        char* name = 0;
        int public_flag = 0;
        AstNameList generic_params;
        AstWhereConstraintList where_constraints;
        memset(&generic_params, 0, sizeof(generic_params));
        memset(&where_constraints, 0, sizeof(where_constraints));

        if (parser->current.kind == TOKEN_KW_IMPORT) {
            if (!parse_import_decl(parser, out_program)) {
                return 0;
            }
            continue;
        }
        while (parser->current.kind == TOKEN_AT) {
            if (parser->next.kind == TOKEN_IDENT && token_equals(&parser->next, "where")) {
                if (!parse_where_annotation(parser, &where_constraints)) {
                    return 0;
                }
                continue;
            }
            return fail(parser, "only @where(...) annotations are supported here");
        }
        if (parser->current.kind == TOKEN_KW_PUBLIC) {
            public_flag = 1;
            advance(parser);
        }
        if (parser->current.kind == TOKEN_KW_ALIAS) {
            if (where_constraints.count != 0) {
                return fail(parser, "@where(...) requires a generic function or struct");
            }
            if (!parse_alias_decl(parser, out_program, public_flag)) {
                return 0;
            }
            continue;
        }
        if (parser->current.kind == TOKEN_KW_CONCEPT) {
            if (where_constraints.count != 0) {
                return fail(parser, "@where(...) requires a generic function or struct");
            }
            if (!parse_concept_decl(parser, out_program, public_flag)) {
                return 0;
            }
            continue;
        }

        if (parser->current.kind == TOKEN_KW_ENUM) {
            if (where_constraints.count != 0) {
                return fail(parser, "@where(...) requires a generic function or struct");
            }
            if (generic_params.count != 0) {
                return fail(parser, "generic enum is not supported");
            }
            if (!parse_enum_decl(parser, out_program)) {
                return 0;
            }
            out_program->enums.items[out_program->enums.count - 1].public_flag = public_flag;
            continue;
        }
        if (parser->current.kind == TOKEN_KW_STRUCT) {
            if (!parse_struct_decl(parser, out_program, &generic_params)) {
                return 0;
            }
            out_program->structs.items[out_program->structs.count - 1].where_constraints = where_constraints;
            if (where_constraints.count != 0 &&
                out_program->structs.items[out_program->structs.count - 1].type_params.count == 0) {
                return fail(parser, "@where(...) requires generic parameters");
            }
            out_program->structs.items[out_program->structs.count - 1].public_flag = public_flag;
            continue;
        }
        if (parser->current.kind == TOKEN_KW_UNION) {
            if (where_constraints.count != 0) {
                return fail(parser, "@where(...) requires a generic function or struct");
            }
            if (generic_params.count != 0) {
                return fail(parser, "generic union is not supported");
            }
            if (!parse_union_decl(parser, out_program)) {
                return 0;
            }
            out_program->unions.items[out_program->unions.count - 1].public_flag = public_flag;
            continue;
        }

        if (parser->current.kind == TOKEN_LEFT_PAREN && looks_like_destructure(parser)) {
            AstParamList bindings;
            AstExpr* init = 0;
            int i = 0;
            memset(&bindings, 0, sizeof(bindings));
            if (!parse_binding_list(parser, &bindings)) {
                return 0;
            }
            if (!expect(parser, TOKEN_ASSIGN, "expected '=' in global destructure declaration")) {
                return 0;
            }
            init = parse_expr(parser);
            if (!init) {
                return 0;
            }
            if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after global destructure declaration")) {
                return 0;
            }
            if (bindings.count == 1) {
                AstGlobal global;
                memset(&global, 0, sizeof(global));
                global.type = bindings.items[0].type;
                global.name = bindings.items[0].name;
                global.init = init;
                global.line = bindings.items[0].line;
                global_list_push(&out_program->globals, global);
                continue;
            }
            if (init->kind != AST_EXPR_TUPLE) {
                return fail(parser, "global destructure currently requires a tuple literal initializer");
            }
            if (init->as.tuple.items.count != bindings.count) {
                return fail(parser, "global destructure arity mismatch");
            }
            for (i = 0; i < bindings.count; ++i) {
                AstGlobal global;
                memset(&global, 0, sizeof(global));
                global.type = bindings.items[i].type;
                global.name = bindings.items[i].name;
                global.init = init->as.tuple.items.items[i];
                global.line = bindings.items[i].line;
                global_list_push(&out_program->globals, global);
            }
            continue;
        }

        if (!is_type_start(parser)) {
            return fail(parser, "expected top-level type declaration");
        }
        type = parse_type(parser);
        if (parser->error) {
            return 0;
        }
        if (parser->current.kind != TOKEN_IDENT) {
            return fail(parser, "expected identifier after top-level type");
        }
        name = token_dup(&parser->current);
        advance(parser);
        if (parser->current.kind == TOKEN_LT) {
            if (generic_params.count != 0) {
                return fail(parser, "duplicate generic parameter list");
            }
            if (!parse_named_generic_params(parser, &generic_params)) {
                return 0;
            }
        }

        if (parser->current.kind == TOKEN_LEFT_PAREN) {
            AstFunction fn;
            memset(&fn, 0, sizeof(fn));
            if (type.kind == AST_TYPE_INFER) {
                return fail(parser, "function return type cannot be inferred");
            }
            fn.public_flag = public_flag;
            fn.return_type = type;
            fn.name = name;
            fn.type_params = generic_params;
            fn.where_constraints = where_constraints;
            fn.line = parser->current.line;
            if (where_constraints.count != 0 && fn.type_params.count == 0) {
                return fail(parser, "@where(...) requires generic parameters");
            }
            if (!parse_params(parser, &fn.params)) {
                return 0;
            }
            if (!parse_block(parser, &fn.body)) {
                return 0;
            }
            function_list_push(&out_program->functions, fn);
            continue;
        }

        if (parser->current.kind == TOKEN_ASSIGN) {
            if (where_constraints.count != 0) {
                return fail(parser, "@where(...) requires a generic function or struct");
            }
            if (generic_params.count != 0) {
                return fail(parser, "generic global is not supported");
            }
            AstGlobal global;
            memset(&global, 0, sizeof(global));
            global.type = type;
            global.name = name;
            global.public_flag = public_flag;
            global.line = parser->current.line;
            advance(parser);
            global.init = parse_expr(parser);
            if (!global.init) {
                return 0;
            }
            if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after global declaration")) {
                return 0;
            }
            global_list_push(&out_program->globals, global);
            continue;
        }

        return fail(parser, "expected function or global declaration");
    }
    return 1;
}
