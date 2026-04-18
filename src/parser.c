#include "parser.h"
#include "hashmap.h"
#include "vec.h"

#include <errno.h>
#include <limits.h>
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
#define struct_list_push(list, struct_decl) VEC_PUSH((list), (struct_decl))
#define enum_list_push(list, enum_decl) VEC_PUSH((list), (enum_decl))
#define union_variant_list_push(list, variant) VEC_PUSH((list), (variant))
#define union_list_push(list, union_decl) VEC_PUSH((list), (union_decl))

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
    if (parser->current.kind == TOKEN_KW_INT || parser->current.kind == TOKEN_KW_BOOL || parser->current.kind == TOKEN_LEFT_PAREN) {
        return 1;
    }
    if (parser->current.kind == TOKEN_IDENT && token_equals(&parser->current, "_")) {
        return 1;
    }
    return parser->current.kind == TOKEN_IDENT && is_known_type((Parser*)parser, &parser->current);
}

static int is_pattern_type_start(const Parser* parser) {
    if (parser->current.kind == TOKEN_KW_INT || parser->current.kind == TOKEN_KW_BOOL) {
        return 1;
    }
    return parser->current.kind == TOKEN_IDENT && token_equals(&parser->current, "_");
}

static int pattern_has_explicit_type(Parser* parser) {
    if (parser->current.kind == TOKEN_KW_INT || parser->current.kind == TOKEN_KW_BOOL) {
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

static AstType apply_type_suffixes(Parser* parser, AstType type) {
    while (parser->current.kind == TOKEN_LEFT_BRACKET) {
        AstType element = type;
        advance(parser);
        if (parser->current.kind == TOKEN_IDENT && token_equals(&parser->current, "_")) {
            type.kind = AST_TYPE_ARRAY;
            type.array_item = heap_type_copy(&element);
            type.array_length = -1;
            advance(parser);
        } else if (parser->current.kind != TOKEN_INT_LIT) {
            fail(parser, "expected integer array length");
            return type;
        } else {
            type.kind = AST_TYPE_ARRAY;
            type.array_item = heap_type_copy(&element);
            type.array_length = (int)strtol(parser->current.start, 0, 10);
            advance(parser);
        }
        if (!expect(parser, TOKEN_RIGHT_BRACKET, "expected ']' after array length")) {
            return type;
        }
    }
    return type;
}

static AstType finalize_type(Parser* parser, AstType type) {
    type = apply_type_suffixes(parser, type);
    if (parser->current.kind == TOKEN_BANG) {
        type.mutable_flag = 1;
        advance(parser);
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
            return apply_type_suffixes(parser, first);
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
        return finalize_type(parser, type);
    }

    if (parser->current.kind == TOKEN_KW_BOOL) {
        type.kind = AST_TYPE_BOOL;
        advance(parser);
        return finalize_type(parser, type);
    }
    if (parser->current.kind == TOKEN_KW_INT) {
        type.kind = AST_TYPE_INT;
        advance(parser);
        return finalize_type(parser, type);
    }
    if (parser->current.kind == TOKEN_IDENT && token_equals(&parser->current, "_")) {
        type.kind = AST_TYPE_INFER;
        advance(parser);
        return finalize_type(parser, type);
    }
    if (parser->current.kind == TOKEN_IDENT) {
        type.kind = AST_TYPE_NAMED;
        type.named_name = token_dup(&parser->current);
        advance(parser);
        return finalize_type(parser, type);
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

static int looks_like_qualified_init_call(Parser* parser) {
    Parser probe = *parser;
    if (probe.current.kind != TOKEN_IDENT || !is_known_type(&probe, &probe.current)) {
        return 0;
    }
    advance(&probe);
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
    if (probe.current.kind != TOKEN_IDENT || !is_known_type(&probe, &probe.current)) {
        return 0;
    }
    advance(&probe);
    if (probe.current.kind != TOKEN_DOT) {
        return 0;
    }
    advance(&probe);
    if (probe.current.kind != TOKEN_IDENT) {
        return 0;
    }
    advance(&probe);
    return probe.current.kind == TOKEN_LEFT_PAREN;
}

static int looks_like_method_decl(Parser* parser) {
    Parser probe = *parser;
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

static AstExpr* parse_expr(Parser* parser);

static AstExpr* parse_variant_expr(Parser* parser, int pattern_flag) {
    AstExpr* expr = new_expr(AST_EXPR_VARIANT, parser->current.line);
    if (parser->current.kind == TOKEN_IDENT) {
        expr->as.variant.union_name = token_dup(&parser->current);
        advance(parser);
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

    if ((token.kind == TOKEN_KW_INT || token.kind == TOKEN_KW_BOOL ||
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

    if (token.kind == TOKEN_IDENT) {
        if (parser->next.kind == TOKEN_DOT &&
            is_known_type(parser, &token) &&
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
    while (expr) {
        if ((expr->kind == AST_EXPR_NAME ||
             (expr->kind == AST_EXPR_FIELD &&
              expr->as.field.base &&
              expr->as.field.base->kind == AST_EXPR_NAME)) &&
            parser->current.kind == TOKEN_LEFT_PAREN) {
            AstExpr* call = new_expr(AST_EXPR_CALL, expr->line);
            if (expr->kind == AST_EXPR_NAME) {
                call->as.call.callee = expr->as.name;
            } else {
                call->as.call.callee = dup_join3(expr->as.field.base->as.name, ".", expr->as.field.name);
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
        break;
    }
    return expr;
}

static AstExpr* parse_multiplicative(Parser* parser) {
    AstExpr* expr = parse_postfix(parser);
    while (expr && (parser->current.kind == TOKEN_STAR || parser->current.kind == TOKEN_SLASH)) {
        AstExpr* right = 0;
        AstExpr* bin = new_expr(AST_EXPR_BINARY, parser->current.line);
        bin->as.binary.left = expr;
        bin->as.binary.op = parser->current.kind == TOKEN_STAR ? AST_BIN_MUL : AST_BIN_DIV;
        advance(parser);
        right = parse_postfix(parser);
        if (!right) {
            return 0;
        }
        bin->as.binary.right = right;
        expr = bin;
    }
    return expr;
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

static AstExpr* parse_expr(Parser* parser) {
    AstExpr* expr = parse_equality(parser);
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

    if (is_type_start(parser)) {
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

    if (parser->current.kind == TOKEN_IDENT &&
        (parser->next.kind == TOKEN_ASSIGN || parser->next.kind == TOKEN_LEFT_BRACKET || parser->next.kind == TOKEN_DOT)) {
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

static int parse_struct_decl(Parser* parser, AstProgram* out_program) {
    AstStructDecl struct_decl;
    memset(&struct_decl, 0, sizeof(struct_decl));
    advance(parser);
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, "expected struct name");
    }
    struct_decl.name = token_dup(&parser->current);
    struct_decl.line = parser->current.line;
    register_known_type(parser, struct_decl.name);
    advance(parser);
    if (!expect(parser, TOKEN_LEFT_BRACE, "expected '{' after struct name")) {
        return 0;
    }
    while (parser->current.kind != TOKEN_RIGHT_BRACE && parser->current.kind != TOKEN_EOF) {
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

        if (parser->current.kind == TOKEN_KW_ENUM) {
            if (!parse_enum_decl(parser, out_program)) {
                return 0;
            }
            continue;
        }
        if (parser->current.kind == TOKEN_KW_STRUCT) {
            if (!parse_struct_decl(parser, out_program)) {
                return 0;
            }
            continue;
        }
        if (parser->current.kind == TOKEN_KW_UNION) {
            if (!parse_union_decl(parser, out_program)) {
                return 0;
            }
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

        if (parser->current.kind == TOKEN_LEFT_PAREN) {
            AstFunction fn;
            memset(&fn, 0, sizeof(fn));
            if (type.kind == AST_TYPE_INFER) {
                return fail(parser, "function return type cannot be inferred");
            }
            fn.return_type = type;
            fn.name = name;
            fn.line = parser->current.line;
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
            AstGlobal global;
            memset(&global, 0, sizeof(global));
            global.type = type;
            global.name = name;
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
