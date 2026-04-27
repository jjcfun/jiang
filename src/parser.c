#include "parser.h"
#include "hashmap.h"
#include "vec.h"

#include <ctype.h>
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

static char* token_slice_dup(const Token* token) {
    char* text = (char*)malloc(token->length + 1);
    if (!text) {
        return 0;
    }
    memcpy(text, token->start, token->length);
    text[token->length] = '\0';
    return text;
}

static int decode_single_unicode_scalar_text(const char* raw_text, int length, int64_t* out_value) {
    const unsigned char* text = (const unsigned char*)raw_text;
    int64_t value = 0;
    if (!text || !out_value || length <= 0) {
        return 0;
    }
    if ((text[0] & 0x80) == 0) {
        if (length != 1) {
            return 0;
        }
        *out_value = text[0];
        return 1;
    }
    if ((text[0] & 0xE0) == 0xC0) {
        if (length != 2 || (text[1] & 0xC0) != 0x80) {
            return 0;
        }
        value = ((int64_t)(text[0] & 0x1F) << 6) |
                (int64_t)(text[1] & 0x3F);
        if (value < 0x80) {
            return 0;
        }
        *out_value = value;
        return 1;
    }
    if ((text[0] & 0xF0) == 0xE0) {
        if (length != 3 || (text[1] & 0xC0) != 0x80 || (text[2] & 0xC0) != 0x80) {
            return 0;
        }
        value = ((int64_t)(text[0] & 0x0F) << 12) |
                ((int64_t)(text[1] & 0x3F) << 6) |
                (int64_t)(text[2] & 0x3F);
        if (value < 0x800 || (value >= 0xD800 && value <= 0xDFFF)) {
            return 0;
        }
        *out_value = value;
        return 1;
    }
    if ((text[0] & 0xF8) == 0xF0) {
        if (length != 4 || (text[1] & 0xC0) != 0x80 || (text[2] & 0xC0) != 0x80 || (text[3] & 0xC0) != 0x80) {
            return 0;
        }
        value = ((int64_t)(text[0] & 0x07) << 18) |
                ((int64_t)(text[1] & 0x3F) << 12) |
                ((int64_t)(text[2] & 0x3F) << 6) |
                (int64_t)(text[3] & 0x3F);
        if (value < 0x10000 || value > 0x10FFFF) {
            return 0;
        }
        *out_value = value;
        return 1;
    }
    return 0;
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

static char* make_static_field_name(const char* owner, const char* field) {
    return dup_join3(owner, ".#", field);
}

static char* dup_text(const char* text) {
    size_t n = strlen(text);
    char* out = (char*)malloc(n + 1);
    if (!out) {
        return 0;
    }
    memcpy(out, text, n + 1);
    return out;
}

static int token_equals(const Token* token, const char* text) {
    size_t n = strlen(text);
    return token->length == n && strncmp(token->start, text, n) == 0;
}

static int parser_current_column(const Parser* parser) {
    const char* cursor = 0;
    int column = 1;
    if (!parser || !parser->source || !parser->current.start) {
        return 1;
    }
    cursor = parser->current.start;
    while (cursor > parser->source && cursor[-1] != '\n') {
        cursor -= 1;
        column += 1;
    }
    return column;
}

static void parser_set_error(Parser* parser, const char* error) {
    parser->error = error;
    parser->error_line = parser->current.line;
    parser->error_column = parser_current_column(parser);
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
#define switch_expr_case_list_push(list, switch_case) VEC_PUSH((list), (switch_case))
#define try_catch_list_push(list, try_catch) VEC_PUSH((list), (try_catch))
#define expr_try_catch_list_push(list, try_catch) VEC_PUSH((list), (try_catch))
#define import_list_push(list, import_decl) VEC_PUSH((list), (import_decl))
#define alias_list_push(list, alias_decl) VEC_PUSH((list), (alias_decl))
#define concept_list_push(list, concept_decl) VEC_PUSH((list), (concept_decl))
#define concept_method_list_push(list, method) VEC_PUSH((list), (method))
#define assoc_type_decl_list_push(list, assoc_type_decl) VEC_PUSH((list), (assoc_type_decl))
#define assoc_type_binding_list_push(list, assoc_type_binding) VEC_PUSH((list), (assoc_type_binding))
#define struct_init_decl_list_push(list, init_decl) VEC_PUSH((list), (init_decl))
#define where_constraint_list_push(list, constraint) VEC_PUSH((list), (constraint))
#define struct_list_push(list, struct_decl) VEC_PUSH((list), (struct_decl))

static int is_known_type(Parser* parser, const Token* token);
#define enum_list_push(list, enum_decl) VEC_PUSH((list), (enum_decl))
#define union_variant_list_push(list, variant) VEC_PUSH((list), (variant))
#define union_list_push(list, union_decl) VEC_PUSH((list), (union_decl))
#define name_list_push(list, name) VEC_PUSH((list), (name))

static Parser* g_active_parser = 0;

static AstExpr* new_expr(AstExprKind kind, int line) {
    AstExpr* expr = (AstExpr*)calloc(1, sizeof(AstExpr));
    expr->kind = kind;
    expr->line = line;
    expr->column = g_active_parser ? parser_current_column(g_active_parser) : 0;
    return expr;
}

static AstStmt* new_stmt(AstStmtKind kind, int line) {
    AstStmt* stmt = (AstStmt*)calloc(1, sizeof(AstStmt));
    stmt->kind = kind;
    stmt->line = line;
    stmt->column = g_active_parser ? parser_current_column(g_active_parser) : 0;
    return stmt;
}

static AstBindingPattern* new_binding_pattern(AstBindingPatternKind kind, int line) {
    AstBindingPattern* pattern = (AstBindingPattern*)calloc(1, sizeof(AstBindingPattern));
    pattern->kind = kind;
    pattern->line = line;
    pattern->column = g_active_parser ? parser_current_column(g_active_parser) : 0;
    return pattern;
}

static AstType* heap_type_copy(const AstType* type);

static AstType ast_type_copy_local(const AstType* type) {
    AstType out;
    int i = 0;
    memset(&out, 0, sizeof(out));
    if (!type) {
        return out;
    }
    out = *type;
    out.named_name = type->named_name ? dup_text(type->named_name) : 0;
    memset(&out.type_args, 0, sizeof(out.type_args));
    memset(&out.tuple_items, 0, sizeof(out.tuple_items));
    out.array_item = 0;
    out.error_type = 0;
    for (i = 0; i < type->type_args.count; ++i) {
        type_list_push(&out.type_args, ast_type_copy_local(&type->type_args.items[i]));
    }
    for (i = 0; i < type->tuple_items.count; ++i) {
        type_list_push(&out.tuple_items, ast_type_copy_local(&type->tuple_items.items[i]));
    }
    if (type->array_item) {
        out.array_item = heap_type_copy(type->array_item);
    }
    if (type->error_type) {
        out.error_type = heap_type_copy(type->error_type);
    }
    return out;
}

static AstType* heap_type_copy(const AstType* type) {
    AstType* copy = (AstType*)malloc(sizeof(AstType));
    if (!copy) {
        return 0;
    }
    *copy = ast_type_copy_local(type);
    return copy;
}

static void register_known_type(Parser* parser, const char* name) {
    hashmap_set(&parser->known_types, name, (void*)1);
}

static int ast_name_list_contains_text(const AstNameList* list, const char* name) {
    int i = 0;
    for (i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void push_scoped_type_name(Parser* parser, char* name) {
    if (!ast_name_list_contains_text(&parser->scoped_type_names, name)) {
        name_list_push(&parser->scoped_type_names, name);
    }
}

static void advance(Parser* parser);

static int is_name_token(const Token* token) {
    return token->kind == TOKEN_IDENT || token->kind == TOKEN_KW_SELF;
}

static int is_type_like_ident(const Token* token) {
    return token->kind == TOKEN_KW_SELF ||
           (token->kind == TOKEN_IDENT &&
            token->length > 0 &&
            token->start[0] >= 'A' &&
            token->start[0] <= 'Z');
}

static int text_is_ident_name(const char* text) {
    const unsigned char* p = (const unsigned char*)text;
    if (!text || !*text) {
        return 0;
    }
    if (!(isalpha(*p) || *p == '_')) {
        return 0;
    }
    p += 1;
    while (*p) {
        if (!(isalnum(*p) || *p == '_')) {
            return 0;
        }
        p += 1;
    }
    return 1;
}

static AstType parse_type(Parser* parser);
static int fail(Parser* parser, const char* error);
static int expect(Parser* parser, TokenKind kind, const char* error);
static int is_type_start(const Parser* parser);
static int parse_params(Parser* parser, AstParamList* params);
static AstExpr* parse_implicit_member(Parser* parser, int line, AstExpr* value_target);
static AstExpr* parse_type_implicit_expr(Parser* parser, int line);

static int ast_type_equals(const AstType* left, const AstType* right) {
    int i = 0;
    if (left->kind != right->kind || left->mutable_flag != right->mutable_flag) {
        return 0;
    }
    if (left->kind == AST_TYPE_NAMED) {
        if ((!left->named_name) != (!right->named_name)) {
            return 0;
        }
        if (left->named_name && strcmp(left->named_name, right->named_name) != 0) {
            return 0;
        }
    }
    if (left->kind == AST_TYPE_ARRAY && left->array_length != right->array_length) {
        return 0;
    }
    if (left->kind == AST_TYPE_POINTER ||
        left->kind == AST_TYPE_MANY_POINTER ||
        left->kind == AST_TYPE_ARRAY ||
        left->kind == AST_TYPE_SLICE ||
        left->kind == AST_TYPE_OPTIONAL) {
        if ((!left->array_item) != (!right->array_item)) {
            return 0;
        }
        if (left->array_item && !ast_type_equals(left->array_item, right->array_item)) {
            return 0;
        }
    }
    if (left->type_args.count != right->type_args.count ||
        left->tuple_items.count != right->tuple_items.count) {
        return 0;
    }
    for (i = 0; i < left->type_args.count; ++i) {
        if (!ast_type_equals(&left->type_args.items[i], &right->type_args.items[i])) {
            return 0;
        }
    }
    for (i = 0; i < left->tuple_items.count; ++i) {
        if (!ast_type_equals(&left->tuple_items.items[i], &right->tuple_items.items[i])) {
            return 0;
        }
    }
    return 1;
}

static AstExpr* make_range_expr(AstExpr* start, AstExpr* end, int line) {
    AstExpr* range = new_expr(AST_EXPR_STRUCT, line);
    AstStructFieldInit field_init;

    range->as.struct_lit.type_name = dup_text("Range");

    memset(&field_init, 0, sizeof(field_init));
    field_init.name = dup_text("start");
    field_init.value = start;
    field_init.line = line;
    struct_field_init_list_push(&range->as.struct_lit.fields, field_init);

    memset(&field_init, 0, sizeof(field_init));
    field_init.name = dup_text("end");
    field_init.value = end;
    field_init.line = line;
    struct_field_init_list_push(&range->as.struct_lit.fields, field_init);

    return range;
}

static char* parse_qualified_name(Parser* parser) {
    char* name = 0;
    if (!is_name_token(&parser->current)) {
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

static const AstConceptDecl* find_parsed_concept(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->concepts.count; ++i) {
        if (strcmp(program->concepts.items[i].name, name) == 0) {
            return &program->concepts.items[i];
        }
    }
    return 0;
}

static const AstStructDecl* find_parsed_struct(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->structs.count; ++i) {
        if (strcmp(program->structs.items[i].name, name) == 0) {
            return &program->structs.items[i];
        }
    }
    return 0;
}

static const AstUnionDecl* find_parsed_union(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->unions.count; ++i) {
        if (strcmp(program->unions.items[i].name, name) == 0) {
            return &program->unions.items[i];
        }
    }
    return 0;
}

static int push_concept_visible_assoc_names(Parser* parser,
                                            const AstProgram* program,
                                            const AstConceptDecl* concept,
                                            AstNameList* seen_concepts) {
    int i = 0;
    if (!concept) {
        return 1;
    }
    if (ast_name_list_contains_text(seen_concepts, concept->name)) {
        return 1;
    }
    name_list_push(seen_concepts, concept->name);
    for (i = 0; i < concept->concept_names.count; ++i) {
        const AstConceptDecl* parent = find_parsed_concept(program, concept->concept_names.items[i]);
        if (parent) {
            if (!push_concept_visible_assoc_names(parser, program, parent, seen_concepts)) {
                return 0;
            }
        }
    }
    for (i = 0; i < concept->assoc_types.count; ++i) {
        push_scoped_type_name(parser, concept->assoc_types.items[i].name);
    }
    return 1;
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
        if (parser->current.kind == TOKEN_COLON) {
            advance(parser);
            if (parser->current.kind != TOKEN_IDENT) {
                return fail(parser, "expected trait name in @where");
            }
            for (;;) {
                memset(&item.equal_type, 0, sizeof(item.equal_type));
                item.kind = AST_WHERE_CONCEPT;
                item.concept_name = token_dup(&parser->current);
                where_constraint_list_push(out, item);
                advance(parser);
                if (parser->current.kind != TOKEN_AMP) {
                    break;
                }
                advance(parser);
                if (parser->current.kind != TOKEN_IDENT) {
                    return fail(parser, "expected trait name after '&' in @where");
                }
            }
        } else if (parser->current.kind == TOKEN_ASSIGN) {
            item.kind = AST_WHERE_EQUAL;
            advance(parser);
            if (!is_type_start(parser)) {
                return fail(parser, "expected type in @where equality");
            }
            item.equal_type = parse_type(parser);
            where_constraint_list_push(out, item);
        } else {
            return fail(parser, "expected ':' or '=' in @where constraint");
        }
        if (parser->current.kind == TOKEN_COMMA) {
            advance(parser);
            if (parser->current.kind != TOKEN_IDENT) {
                return fail(parser, "expected constraint target name in @where");
            }
            continue;
        }
        break;
    }
    return expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after @where");
}

static int parse_decl_concept_names(Parser* parser, AstNameList* out);

static int parse_concept_assoc_decl(Parser* parser,
                                    AstProgram* out_program,
                                    AstConceptDecl* concept_decl,
                                    AstWhereConstraintList* where_constraints) {
    AstAssocTypeDecl assoc_type;
    memset(&assoc_type, 0, sizeof(assoc_type));
    advance(parser);
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, "expected associated type name");
    }
    assoc_type.name = token_dup(&parser->current);
    assoc_type.line = parser->current.line;
    push_scoped_type_name(parser, assoc_type.name);
    advance(parser);
    assoc_type.where_constraints = *where_constraints;
    memset(where_constraints, 0, sizeof(*where_constraints));
    if (parser->current.kind == TOKEN_COLON) {
        AstWhereConstraint item;
        advance(parser);
        if (parser->current.kind != TOKEN_IDENT) {
            return fail(parser, "expected trait name after ':'");
        }
        for (;;) {
            memset(&item, 0, sizeof(item));
            item.kind = AST_WHERE_CONCEPT;
            item.param_name = dup_text(assoc_type.name);
            item.line = parser->current.line;
            item.concept_name = token_dup(&parser->current);
            where_constraint_list_push(&assoc_type.where_constraints, item);
            advance(parser);
            if (parser->current.kind != TOKEN_AMP) {
                break;
            }
            advance(parser);
            if (parser->current.kind != TOKEN_IDENT) {
                return fail(parser, "expected trait name after '&' in associated type bound");
            }
        }
    }
    if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after associated type")) {
        return 0;
    }
    assoc_type_decl_list_push(&concept_decl->assoc_types, assoc_type);
    (void)out_program;
    return 1;
}

static int parse_concept_decl(Parser* parser, AstProgram* out_program, int public_flag, AstWhereConstraintList* leading_where_constraints) {
    AstConceptDecl concept_decl;
    int scoped_type_base = parser->scoped_type_names.count;
    memset(&concept_decl, 0, sizeof(concept_decl));
    concept_decl.public_flag = public_flag;
    concept_decl.where_constraints = *leading_where_constraints;
    memset(leading_where_constraints, 0, sizeof(*leading_where_constraints));
    advance(parser);
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, "expected trait name");
    }
    concept_decl.name = token_dup(&parser->current);
    concept_decl.line = parser->current.line;
    advance(parser);
    if (!parse_decl_concept_names(parser, &concept_decl.concept_names)) {
        return 0;
    }
    if (parser->current.kind == TOKEN_SEMICOLON) {
        advance(parser);
        concept_list_push(&out_program->concepts, concept_decl);
        return 1;
    }
    if (!expect(parser, TOKEN_LEFT_BRACE, "expected ';' or '{' after trait declaration")) {
        return 0;
    }
    {
        AstNameList seen_concepts;
        int i = 0;
        memset(&seen_concepts, 0, sizeof(seen_concepts));
        for (i = 0; i < concept_decl.concept_names.count; ++i) {
            const AstConceptDecl* parent = find_parsed_concept(out_program, concept_decl.concept_names.items[i]);
            if (parent) {
                if (!push_concept_visible_assoc_names(parser, out_program, parent, &seen_concepts)) {
                    return 0;
                }
            }
        }
    }
    while (parser->current.kind != TOKEN_RIGHT_BRACE && parser->current.kind != TOKEN_EOF) {
        AstWhereConstraintList where_constraints;
        AstConceptMethod method;
        memset(&where_constraints, 0, sizeof(where_constraints));
        memset(&method, 0, sizeof(method));
        while (parser->current.kind == TOKEN_AT) {
            if (parser->next.kind == TOKEN_IDENT && token_equals(&parser->next, "where")) {
                if (!parse_where_annotation(parser, &where_constraints)) {
                    return 0;
                }
                continue;
            }
            return fail(parser, "only @where(...) annotations are supported here");
        }
        if (parser->current.kind == TOKEN_KW_TYPE) {
            if (!parse_concept_assoc_decl(parser, out_program, &concept_decl, &where_constraints)) {
                return 0;
            }
            continue;
        }
        if (!is_type_start(parser)) {
            return fail(parser, "expected trait method return type");
        }
        method.return_type = parse_type(parser);
        if (method.return_type.kind == AST_TYPE_INFER) {
            return fail(parser, "trait method return type cannot be inferred");
        }
        if (parser->current.kind != TOKEN_IDENT) {
            return fail(parser, "expected trait method name");
        }
        method.name = token_dup(&parser->current);
        method.line = parser->current.line;
        method.where_constraints = where_constraints;
        advance(parser);
        if (!parse_params(parser, &method.params)) {
            return 0;
        }
        if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after trait method")) {
            return 0;
        }
        concept_method_list_push(&concept_decl.methods, method);
    }
    if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after trait declaration")) {
        return 0;
    }
    parser->scoped_type_names.count = scoped_type_base;
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
    int final_is_type = 0;
    if (!is_name_token(&probe.current)) {
        return 0;
    }
    final_is_type = is_type_like_ident(&probe.current);
    advance(&probe);
    while (probe.current.kind == TOKEN_DOT && probe.next.kind == TOKEN_IDENT) {
        seen_dot = 1;
        advance(&probe);
        final_is_type = is_type_like_ident(&probe.current);
        advance(&probe);
    }
    return seen_dot ? final_is_type : is_type_like_ident(&parser->current);
}

static int is_known_type(Parser* parser, const Token* token) {
    char* name = token_dup(token);
    int result = 0;
    if (!name) {
        return 0;
    }
    result = hashmap_contains(&parser->known_types, name) || ast_name_list_contains_text(&parser->scoped_type_names, name);
    free(name);
    return result;
}

static int is_known_static_field(Parser* parser) {
    Parser probe = *parser;
    char* owner = 0;
    char* field = 0;
    char* key = 0;
    int result = 0;
    if (probe.current.kind != TOKEN_IDENT || probe.next.kind != TOKEN_DOT) {
        return 0;
    }
    owner = token_dup(&probe.current);
    advance(&probe);
    advance(&probe);
    if (probe.current.kind != TOKEN_IDENT) {
        free(owner);
        return 0;
    }
    field = token_dup(&probe.current);
    key = dup_join3(owner, ".", field);
    result = hashmap_contains(&parser->static_fields, key);
    free(owner);
    free(field);
    free(key);
    return result;
}

static void advance(Parser* parser) {
    parser->current = parser->next;
    parser->next = lexer_next(&parser->lexer);
}

static int fail(Parser* parser, const char* error) {
    parser_set_error(parser, error);
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
    if (parser->current.kind == TOKEN_LEFT_PAREN) {
        return 1;
    }
    if (parser->current.kind == TOKEN_KW_SELF) {
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
    return parser->current.kind == TOKEN_IDENT && token_equals(&parser->current, "_");
}

static int pattern_has_explicit_type(Parser* parser) {
    Parser probe = *parser;
    if (is_type_start(&probe)) {
        (void)parse_type(&probe);
        if (!probe.error && probe.current.kind == TOKEN_IDENT) {
            return 1;
        }
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
    return probe.current.kind == TOKEN_IDENT &&
           (probe.next.kind == TOKEN_ASSIGN ||
            probe.next.kind == TOKEN_COMMA ||
            probe.next.kind == TOKEN_SEMICOLON);
}

static AstType parse_type_postfix(Parser* parser, AstType type) {
    while (parser->current.kind == TOKEN_LEFT_BRACKET || parser->current.kind == TOKEN_BANG || parser->current.kind == TOKEN_AMP || parser->current.kind == TOKEN_STAR || parser->current.kind == TOKEN_QUESTION || parser->current.kind == TOKEN_QUESTION_QUESTION || parser->current.kind == TOKEN_AT) {
        if (parser->current.kind == TOKEN_BANG) {
            type.mutable_flag = 1;
            advance(parser);
            continue;
        }
        if (parser->current.kind == TOKEN_AMP) {
            AstType referent = type;
            advance(parser);
            memset(&type, 0, sizeof(type));
            type.kind = AST_TYPE_REFERENCE;
            type.array_item = heap_type_copy(&referent);
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
        if (parser->current.kind == TOKEN_QUESTION || parser->current.kind == TOKEN_QUESTION_QUESTION) {
            AstType wrapped = type;
            advance(parser);
            if (wrapped.kind == AST_TYPE_OPTIONAL) {
                type = wrapped;
            } else {
                memset(&type, 0, sizeof(type));
                type.kind = AST_TYPE_OPTIONAL;
                type.array_item = heap_type_copy(&wrapped);
            }
            continue;
        }
        if (parser->current.kind == TOKEN_AT) {
            AstType wrapped = type;
            AstType error_type;
            advance(parser);
            if (!is_type_start(parser)) {
                fail(parser, "expected error type after '@'");
                return type;
            }
            error_type = parse_type(parser);
            memset(&type, 0, sizeof(type));
            type.kind = AST_TYPE_ERRORABLE;
            type.array_item = heap_type_copy(&wrapped);
            type.error_type = heap_type_copy(&error_type);
            continue;
        }
        if (parser->current.kind == TOKEN_LEFT_BRACKET) {
            AstType element = type;
            advance(parser);
            if (parser->current.kind == TOKEN_STAR) {
                advance(parser);
                if (!expect(parser, TOKEN_RIGHT_BRACKET, "expected ']' after pointer marker")) {
                    return type;
                }
                memset(&type, 0, sizeof(type));
                type.kind = AST_TYPE_MANY_POINTER;
                type.array_item = heap_type_copy(&element);
                continue;
            }
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
            return parse_type_postfix(parser, type);
        }
        if (!is_type_start(parser)) {
            parser_set_error(parser, "expected tuple item type");
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
                parser_set_error(parser, "expected tuple item type after ','");
                return type;
            }
            type_list_push(&type.tuple_items, parse_type(parser));
        }
        if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after tuple type")) {
            return type;
        }
        return parse_type_postfix(parser, type);
    }

    if (parser->current.kind == TOKEN_IDENT && token_equals(&parser->current, "_")) {
        type.kind = AST_TYPE_INFER;
        advance(parser);
        return parse_type_postfix(parser, type);
    }
    if (parser->current.kind == TOKEN_IDENT || parser->current.kind == TOKEN_KW_SELF) {
        if (token_equals(&parser->current, "Int")) {
            type.kind = AST_TYPE_INT;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        if (token_equals(&parser->current, "Int8")) {
            type.kind = AST_TYPE_I8;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        if (token_equals(&parser->current, "Int16")) {
            type.kind = AST_TYPE_I16;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        if (token_equals(&parser->current, "Int32")) {
            type.kind = AST_TYPE_I32;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        if (token_equals(&parser->current, "Int64")) {
            type.kind = AST_TYPE_I64;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        if (token_equals(&parser->current, "UInt16")) {
            type.kind = AST_TYPE_U16;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        if (token_equals(&parser->current, "UInt32")) {
            type.kind = AST_TYPE_U32;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        if (token_equals(&parser->current, "UInt64")) {
            type.kind = AST_TYPE_U64;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        if (token_equals(&parser->current, "Float16")) {
            type.kind = AST_TYPE_F16;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        if (token_equals(&parser->current, "Float32")) {
            type.kind = AST_TYPE_F32;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        if (token_equals(&parser->current, "Float64")) {
            type.kind = AST_TYPE_F64;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        if (token_equals(&parser->current, "Float")) {
            type.kind = AST_TYPE_FLOAT;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        if (token_equals(&parser->current, "Double")) {
            type.kind = AST_TYPE_DOUBLE;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        if (token_equals(&parser->current, "Char")) {
            type.kind = AST_TYPE_CHARACTER;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        if (token_equals(&parser->current, "UInt8")) {
            type.kind = AST_TYPE_UINT8;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        if (token_equals(&parser->current, "Bool")) {
            type.kind = AST_TYPE_BOOL;
            advance(parser);
            return parse_type_postfix(parser, type);
        }
        type.kind = AST_TYPE_NAMED;
        type.named_name = parse_qualified_name(parser);
        if (parser->current.kind == TOKEN_LT) {
            if (!parse_type_arg_list(parser, &type.type_args)) {
                return type;
            }
        }
        return parse_type_postfix(parser, type);
    }
    parser_set_error(parser, "expected type");
    return type;
}

static int type_uses_empty_tuple_void(const AstType* type) {
    return type && type->kind == AST_TYPE_VOID;
}

static int type_contains_invalid_errorable(const AstType* type, int allow_here) {
    int i = 0;
    if (!type) {
        return 0;
    }
    if (type->kind == AST_TYPE_ERRORABLE) {
        if (!allow_here) {
            return 1;
        }
        return type_contains_invalid_errorable(type->array_item, 0) ||
               type_contains_invalid_errorable(type->error_type, 0);
    }
    if (type->kind == AST_TYPE_NAMED && type->named_name && strcmp(type->named_name, "Fn") == 0) {
        if (type->type_args.count > 0 && type_contains_invalid_errorable(&type->type_args.items[0], 1)) {
            return 1;
        }
        for (i = 1; i < type->type_args.count; ++i) {
            if (type_contains_invalid_errorable(&type->type_args.items[i], 0)) {
                return 1;
            }
        }
        return 0;
    }
    if (type->array_item && type_contains_invalid_errorable(type->array_item, 0)) {
        return 1;
    }
    if (type->error_type && type_contains_invalid_errorable(type->error_type, 0)) {
        return 1;
    }
    for (i = 0; i < type->tuple_items.count; ++i) {
        if (type_contains_invalid_errorable(&type->tuple_items.items[i], 0)) {
            return 1;
        }
    }
    for (i = 0; i < type->type_args.count; ++i) {
        if (type_contains_invalid_errorable(&type->type_args.items[i], 0)) {
            return 1;
        }
    }
    return 0;
}

static int type_contains_errorable(const AstType* type) {
    return type_contains_invalid_errorable(type, 0);
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
    if (!is_name_token(&probe.current)) {
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
    if (!is_name_token(&probe.current)) {
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
    if (!is_name_token(&probe.current)) {
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
    if (probe.current.kind == TOKEN_LT) {
        if (!parse_named_generic_params(&probe, &(AstNameList){0})) {
            return 0;
        }
    }
    return probe.current.kind == TOKEN_LEFT_PAREN;
}

static int looks_like_static_field_decl(Parser* parser) {
    Parser probe = *parser;
    if (probe.current.kind != TOKEN_KW_STATIC) {
        return 0;
    }
    advance(&probe);
    if (!is_type_start(&probe)) {
        return 0;
    }
    (void)parse_type(&probe);
    if (probe.error || probe.current.kind != TOKEN_IDENT) {
        return 0;
    }
    advance(&probe);
    return probe.current.kind == TOKEN_ASSIGN;
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
    return probe.current.kind == TOKEN_LEFT_BRACKET || probe.current.kind == TOKEN_LEFT_PAREN;
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
static AstExpr* parse_variant_expr(Parser* parser, int pattern_flag);

static AstExpr* parse_expr(Parser* parser);
static AstExpr* parse_expr_stmt_target(Parser* parser);
static AstExpr* parse_if_expr(Parser* parser);
static AstExpr* parse_switch_expr(Parser* parser);
static AstExpr* parse_try_expr(Parser* parser);
static AstExpr* parse_block_expr(Parser* parser, const char* context);
static AstExpr* parse_multiplicative(Parser* parser);
static AstExpr* parse_additive(Parser* parser);
static AstExpr* parse_shift(Parser* parser);
static AstExpr* parse_bitwise_and(Parser* parser);
static AstExpr* parse_bitwise_xor(Parser* parser);
static AstExpr* parse_bitwise_or(Parser* parser);
static AstExpr* parse_logical_and(Parser* parser);
static AstExpr* parse_logical_or(Parser* parser);
static AstExpr* parse_expr_internal(Parser* parser, int allow_catch_handler);
static AstExpr* parse_expr_no_range_internal(Parser* parser, int allow_catch_handler);
static AstExpr* parse_value_branch_expr(Parser* parser, const char* context);
static AstStmt* parse_stmt(Parser* parser);

static AstExpr* parse_catch_handler_expr(Parser* parser, AstExpr* left) {
    AstExpr* out = new_expr(AST_EXPR_CATCH_HANDLER, parser->current.line);
    advance(parser);
    out->as.catch_handler.left = left;
    if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after catch")) {
        return 0;
    }
    if (parser->current.kind != TOKEN_IDENT) {
        fail(parser, "expected catch binding name");
        return 0;
    }
    out->as.catch_handler.binding_name = token_dup(&parser->current);
    advance(parser);
    if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after catch binding")) {
        return 0;
    }
    out->as.catch_handler.handler = parse_value_branch_expr(parser, "expected '}' after catch expression handler");
    if (!out->as.catch_handler.handler) {
        return 0;
    }
    return out;
}

static AstExpr* parse_prefixed_try_catch_expr(Parser* parser, int line) {
    AstExpr* out = new_expr(AST_EXPR_TRY, line);
    AstExprTryCatch catch_clause;
    memset(&catch_clause, 0, sizeof(catch_clause));
    out->as.try_expr.value = parse_expr_internal(parser, 0);
    if (!out->as.try_expr.value) {
        return 0;
    }
    if (parser->current.kind != TOKEN_KW_CATCH) {
        fail(parser, "expected catch after try expression");
        return 0;
    }
    catch_clause.line = parser->current.line;
    catch_clause.error_type.kind = AST_TYPE_INFER;
    advance(parser);
    if (parser->current.kind == TOKEN_LEFT_PAREN) {
        advance(parser);
        if (parser->current.kind != TOKEN_IDENT) {
            fail(parser, "expected catch binding name");
            return 0;
        }
        catch_clause.binding_name = token_dup(&parser->current);
        advance(parser);
        if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after catch binding")) {
            return 0;
        }
    } else {
        catch_clause.binding_name = strdup("_");
    }
    catch_clause.value = parse_value_branch_expr(parser, "expected '}' after try expression catch branch");
    if (!catch_clause.value) {
        return 0;
    }
    expr_try_catch_list_push(&out->as.try_expr.catches, catch_clause);
    return out;
}

static AstExpr* parse_value_branch_expr(Parser* parser, const char* context) {
    if (parser->current.kind == TOKEN_LEFT_BRACE) {
        return parse_block_expr(parser, context);
    }
    return parse_expr(parser);
}

static AstExpr* parse_block_expr(Parser* parser, const char* context) {
    AstExpr* expr = new_expr(AST_EXPR_BLOCK, parser->current.line);
    AstBlock* body = (AstBlock*)calloc(1, sizeof(AstBlock));
    expr->as.block_expr.body = body;
    advance(parser);
    while (parser->current.kind != TOKEN_RIGHT_BRACE && parser->current.kind != TOKEN_EOF) {
        Parser snapshot = *parser;
        AstExpr* tail = parse_expr(&snapshot);
        if (tail && snapshot.current.kind == TOKEN_RIGHT_BRACE) {
            *parser = snapshot;
            expr->as.block_expr.value = tail;
            break;
        }
        if (!body) {
            fail(parser, "out of memory");
            return 0;
        }
        stmt_list_push(&body->stmts, parse_stmt(parser));
        if (body->stmts.count <= 0 || !body->stmts.items[body->stmts.count - 1]) {
            return 0;
        }
    }
    if (!expr->as.block_expr.value) {
        fail(parser, "block expression requires final value");
        return 0;
    }
    if (!expect(parser, TOKEN_RIGHT_BRACE, context)) {
        return 0;
    }
    return expr;
}

static AstExpr* parse_if_expr(Parser* parser) {
    AstExpr* expr = new_expr(AST_EXPR_IF, parser->current.line);
    advance(parser);
    if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after if")) {
        return 0;
    }
    expr->as.if_expr.cond = parse_expr(parser);
    if (!expr->as.if_expr.cond) {
        return 0;
    }
    if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after if condition")) {
        return 0;
    }
    expr->as.if_expr.then_expr = parse_value_branch_expr(parser, "expected '}' after if expression then branch");
    if (!expr->as.if_expr.then_expr) {
        return 0;
    }
    if (parser->current.kind != TOKEN_KW_ELSE) {
        fail(parser, "if expression requires else branch");
        return 0;
    }
    advance(parser);
    expr->as.if_expr.else_expr = parse_value_branch_expr(parser, "expected '}' after if expression else branch");
    if (!expr->as.if_expr.else_expr) {
        return 0;
    }
    return expr;
}

static AstExpr* parse_switch_expr(Parser* parser) {
    AstExpr* expr = new_expr(AST_EXPR_SWITCH, parser->current.line);
    advance(parser);
    if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after switch")) {
        return 0;
    }
    expr->as.switch_expr.value = parse_expr(parser);
    if (!expr->as.switch_expr.value) {
        return 0;
    }
    if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after switch value")) {
        return 0;
    }
    if (!expect(parser, TOKEN_LEFT_BRACE, "expected '{' after switch")) {
        return 0;
    }
    while (parser->current.kind != TOKEN_RIGHT_BRACE && parser->current.kind != TOKEN_EOF) {
        AstSwitchExprCase switch_case;
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
        if (!expect(parser, TOKEN_FAT_ARROW, "expected '=>' after switch branch")) {
            return 0;
        }
        if (parser->current.kind == TOKEN_LEFT_BRACE) {
            switch_case.value = parse_value_branch_expr(parser, "expected '}' after switch expression branch");
            if (!switch_case.value) {
                return 0;
            }
        } else {
            switch_case.value = parse_expr(parser);
            if (!switch_case.value) {
                return 0;
            }
            if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after bare switch expression branch")) {
                return 0;
            }
        }
        switch_expr_case_list_push(&expr->as.switch_expr.cases, switch_case);
    }
    if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after switch")) {
        return 0;
    }
    return expr;
}

static AstExpr* parse_try_expr(Parser* parser) {
    AstExpr* expr = new_expr(AST_EXPR_TRY, parser->current.line);
    int line = parser->current.line;
    advance(parser);
    if (parser->current.kind != TOKEN_LEFT_BRACE) {
        return parse_prefixed_try_catch_expr(parser, line);
    }
    expr->as.try_expr.value = parse_block_expr(parser, "expected '}' after try expression body");
    if (!expr->as.try_expr.value) {
        return 0;
    }
    if (parser->current.kind != TOKEN_KW_CATCH) {
        fail(parser, "try expression requires at least one catch");
        return 0;
    }
    while (parser->current.kind == TOKEN_KW_CATCH) {
        AstExprTryCatch catch_clause;
        memset(&catch_clause, 0, sizeof(catch_clause));
        catch_clause.line = parser->current.line;
        advance(parser);
        if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after catch")) {
            return 0;
        }
        catch_clause.error_type = parse_type(parser);
        if (parser->error) {
            return 0;
        }
        if (catch_clause.error_type.kind == AST_TYPE_ERRORABLE) {
            fail(parser, "catch type must not use '@'");
            return 0;
        }
        if (parser->current.kind != TOKEN_IDENT) {
            fail(parser, "expected catch binding name");
            return 0;
        }
        catch_clause.binding_name = token_dup(&parser->current);
        advance(parser);
        if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after catch binding")) {
            return 0;
        }
        catch_clause.value = parse_value_branch_expr(parser, "expected '}' after try expression catch branch");
        if (!catch_clause.value) {
            return 0;
        }
        expr_try_catch_list_push(&expr->as.try_expr.catches, catch_clause);
    }
    return expr;
}
static AstExpr* parse_expr_no_range(Parser* parser);

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
    if (is_name_token(&parser->current)) {
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
    if (looks_like_type_implicit_suffix(parser)) {
        return parse_type_implicit_expr(parser, token.line);
    }

    if (looks_like_typed_array_constructor(parser)) {
        AstType array_type = parse_type(parser);
        if (array_type.kind != AST_TYPE_ARRAY) {
            fail(parser, "typed array constructor requires an array type");
            return 0;
        }
        if (parser->current.kind == TOKEN_LEFT_BRACKET) {
            AstExpr* array = new_expr(AST_EXPR_ARRAY, token.line);
            advance(parser);
            if (parser->current.kind != TOKEN_RIGHT_BRACKET) {
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
            if (!expect(parser, TOKEN_RIGHT_BRACKET, "expected ']' after array literal")) {
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

    if (is_name_token(&token) && looks_like_qualified_type_start(parser)) {
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
                    field_init.line = parser->current.line;
                    if (parser->current.kind == TOKEN_IDENT && parser->next.kind == TOKEN_COLON) {
                        field_init.name = token_dup(&parser->current);
                        advance(parser);
                        advance(parser);
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
                field_init.line = parser->current.line;
                if (parser->current.kind == TOKEN_IDENT && parser->next.kind == TOKEN_COLON) {
                    field_init.name = token_dup(&parser->current);
                    advance(parser);
                    advance(parser);
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

    if (token.kind == TOKEN_LEFT_BRACE) {
        AstExpr* struct_lit = new_expr(AST_EXPR_STRUCT, token.line);
        advance(parser);
        if (parser->current.kind != TOKEN_RIGHT_BRACE) {
            for (;;) {
                AstStructFieldInit field_init;
                memset(&field_init, 0, sizeof(field_init));
                field_init.line = parser->current.line;
                if (parser->current.kind != TOKEN_IDENT || parser->next.kind != TOKEN_COLON) {
                    fail(parser, "expected record field name");
                    return 0;
                }
                field_init.name = token_dup(&parser->current);
                advance(parser);
                advance(parser);
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
        if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after record literal")) {
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

    if (token.kind == TOKEN_FLOAT_LIT) {
        char* text = token_slice_dup(&token);
        if (!text) {
            fail(parser, "out of memory");
            return 0;
        }
        errno = 0;
        expr = new_expr(AST_EXPR_FLOAT, token.line);
        expr->as.float_value = strtod(text, &end);
        free(text);
        if (errno != 0 || !end || *end != '\0') {
            fail(parser, "invalid float literal");
            return 0;
        }
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

    if (token.kind == TOKEN_CHAR_LIT) {
        AstExpr string_expr;
        int64_t value = 0;
        memset(&string_expr, 0, sizeof(string_expr));
        string_expr.kind = AST_EXPR_STRING;
        string_expr.line = token.line;
        string_expr.as.string_lit.text = string_token_dup(&token);
        string_expr.as.string_lit.length = (int)(token.length >= 2 ? token.length - 2 : 0);
        if (!decode_single_unicode_scalar_text(string_expr.as.string_lit.text, string_expr.as.string_lit.length, &value)) {
            fail(parser, "character literal requires exactly one Unicode scalar");
            return 0;
        }
        expr = new_expr(AST_EXPR_CHAR, token.line);
        expr->as.char_value = value;
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
            !is_known_static_field(parser) &&
            !looks_like_qualified_init_call(parser) &&
            !looks_like_qualified_call(parser)) {
            return parse_variant_expr(parser, 0);
        }
    }
    if (token.kind == TOKEN_IDENT || token.kind == TOKEN_KW_SELF) {
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
            AstExpr* tuple = new_expr(AST_EXPR_TUPLE, token.line);
            advance(parser);
            return tuple;
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

    if (token.kind == TOKEN_LEFT_BRACKET) {
        AstExpr* array = new_expr(AST_EXPR_ARRAY, token.line);
        advance(parser);
        if (parser->current.kind != TOKEN_RIGHT_BRACKET) {
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
        if (!expect(parser, TOKEN_RIGHT_BRACKET, "expected ']' after array literal")) {
            return 0;
        }
        return array;
    }

    if (token.kind == TOKEN_KW_IF) {
        return parse_if_expr(parser);
    }

    if (token.kind == TOKEN_KW_TRY) {
        return parse_try_expr(parser);
    }

    if (token.kind == TOKEN_KW_SWITCH) {
        return parse_switch_expr(parser);
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
        if (parser->current.kind == TOKEN_QUESTION && parser->next.kind == TOKEN_DOLLAR) {
            advance(parser);
            expr = parse_implicit_member(parser, expr->line, expr);
            if (expr) {
                expr->as.implicit.optional_chain = 1;
            }
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
                    AstStructFieldInit arg;
                    memset(&arg, 0, sizeof(arg));
                    arg.line = parser->current.line;
                    if (parser->current.kind == TOKEN_IDENT && parser->next.kind == TOKEN_COLON) {
                        arg.name = token_dup(&parser->current);
                        advance(parser);
                        advance(parser);
                    }
                    arg.value = parse_expr(parser);
                    if (!arg.value) {
                        return 0;
                    }
                    struct_field_init_list_push(&call->as.call.args, arg);
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
    while (expr && (parser->current.kind == TOKEN_STAR || parser->current.kind == TOKEN_SLASH || parser->current.kind == TOKEN_PERCENT)) {
        AstExpr* right = 0;
        AstExpr* bin = new_expr(AST_EXPR_BINARY, parser->current.line);
        bin->as.binary.left = expr;
        if (parser->current.kind == TOKEN_STAR) {
            bin->as.binary.op = AST_BIN_MUL;
        } else if (parser->current.kind == TOKEN_PERCENT) {
            bin->as.binary.op = AST_BIN_MOD;
        } else {
            bin->as.binary.op = AST_BIN_DIV;
        }
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

static int parse_implicit_as_args(Parser* parser, AstExpr* expr) {
    if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after implicit operation")) {
        return 0;
    }
    if (parser->current.kind == TOKEN_RIGHT_PAREN) {
        return expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after as target type");
    }
    if (!is_type_start(parser)) {
        fail(parser, "expected as target type");
        return 0;
    }
    expr->as.implicit.has_type_arg = 1;
    expr->as.implicit.type_arg = parse_type(parser);
    if (parser->current.kind == TOKEN_COMMA) {
        fail(parser, "as accepts exactly one type argument");
        return 0;
    }
    return expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after as target type");
}

static int is_implicit_member_name(Token* token) {
    return token->kind == TOKEN_IDENT &&
           (token_equals(token, "as") ||
            token_equals(token, "ref") ||
            token_equals(token, "ptr") ||
            token_equals(token, "addr") ||
            token_equals(token, "free") ||
            token_equals(token, "some") ||
            token_equals(token, "offset"));
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
    if (strcmp(expr->as.implicit.member, "as") == 0) {
        return parse_implicit_as_args(parser, expr) ? expr : 0;
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
    if (strcmp(expr->as.implicit.member, "as") == 0) {
        fail(parser, "type implicit operation '.as(...)' is unsupported");
        return 0;
    }
    return parse_implicit_args(parser, &expr->as.implicit.args) ? expr : 0;
}

static AstExpr* parse_unary(Parser* parser) {
    if (parser->current.kind == TOKEN_TILDE) {
        AstExpr* expr = new_expr(AST_EXPR_BIT_NOT, parser->current.line);
        advance(parser);
        expr->as.unary.value = parse_unary(parser);
        return expr->as.unary.value ? expr : 0;
    }
    if (parser->current.kind == TOKEN_BANG) {
        AstExpr* right = 0;
        AstExpr* false_expr = new_expr(AST_EXPR_BOOL, parser->current.line);
        AstExpr* bin = new_expr(AST_EXPR_BINARY, parser->current.line);
        false_expr->as.bool_value = 0;
        advance(parser);
        right = parse_unary(parser);
        if (!right) {
            return 0;
        }
        bin->as.binary.left = right;
        bin->as.binary.op = AST_BIN_EQ;
        bin->as.binary.right = false_expr;
        return bin;
    }
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
        fail(parser, "explicit dereference syntax is not supported");
        return 0;
    }
    if (parser->current.kind == TOKEN_KW_NEW) {
        AstExpr* expr = new_expr(AST_EXPR_NEW, parser->current.line);
        advance(parser);
        expr->as.unary.value = parse_postfix(parser);
        return expr->as.unary.value ? expr : 0;
    }
    return parse_postfix(parser);
}

static AstExpr* parse_shift(Parser* parser) {
    AstExpr* expr = parse_additive(parser);
    while (expr &&
           ((parser->current.kind == TOKEN_LT && parser->next.kind == TOKEN_LT) ||
            (parser->current.kind == TOKEN_GT && parser->next.kind == TOKEN_GT))) {
        AstExpr* right = 0;
        AstExpr* bin = new_expr(AST_EXPR_BINARY, parser->current.line);
        bin->as.binary.left = expr;
        bin->as.binary.op = parser->current.kind == TOKEN_LT ? AST_BIN_SHL : AST_BIN_SHR;
        advance(parser);
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

static AstExpr* parse_bitwise_and(Parser* parser) {
    AstExpr* expr = parse_shift(parser);
    while (expr && parser->current.kind == TOKEN_AMP) {
        AstExpr* right = 0;
        AstExpr* bin = new_expr(AST_EXPR_BINARY, parser->current.line);
        bin->as.binary.left = expr;
        bin->as.binary.op = AST_BIN_BIT_AND;
        advance(parser);
        right = parse_shift(parser);
        if (!right) {
            return 0;
        }
        bin->as.binary.right = right;
        expr = bin;
    }
    return expr;
}

static AstExpr* parse_bitwise_xor(Parser* parser) {
    AstExpr* expr = parse_bitwise_and(parser);
    while (expr && parser->current.kind == TOKEN_CARET) {
        AstExpr* right = 0;
        AstExpr* bin = new_expr(AST_EXPR_BINARY, parser->current.line);
        bin->as.binary.left = expr;
        bin->as.binary.op = AST_BIN_BIT_XOR;
        advance(parser);
        right = parse_bitwise_and(parser);
        if (!right) {
            return 0;
        }
        bin->as.binary.right = right;
        expr = bin;
    }
    return expr;
}

static AstExpr* parse_bitwise_or(Parser* parser) {
    AstExpr* expr = parse_bitwise_xor(parser);
    while (expr && parser->current.kind == TOKEN_PIPE) {
        AstExpr* right = 0;
        AstExpr* bin = new_expr(AST_EXPR_BINARY, parser->current.line);
        bin->as.binary.left = expr;
        bin->as.binary.op = AST_BIN_BIT_OR;
        advance(parser);
        right = parse_bitwise_xor(parser);
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
    AstExpr* expr = parse_bitwise_or(parser);
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
        right = parse_bitwise_or(parser);
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
    while (expr && (parser->current.kind == TOKEN_EQ_EQ || parser->current.kind == TOKEN_BANG_EQ || parser->current.kind == TOKEN_KW_IS)) {
        AstExpr* right = 0;
        AstExpr* bin = new_expr(AST_EXPR_BINARY, parser->current.line);
        bin->as.binary.left = expr;
        if (parser->current.kind == TOKEN_KW_IS) {
            bin->as.binary.op = AST_BIN_IS;
            advance(parser);
            if (!looks_like_variant_pattern_expr(parser)) {
                fail(parser, "expected variant pattern after 'is'");
                return 0;
            }
            right = parse_variant_expr(parser, 1);
        } else {
            bin->as.binary.op = parser->current.kind == TOKEN_EQ_EQ ? AST_BIN_EQ : AST_BIN_NE;
            advance(parser);
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

static AstExpr* parse_logical_and(Parser* parser) {
    AstExpr* expr = parse_equality(parser);
    while (expr && parser->current.kind == TOKEN_AMP_AMP) {
        AstExpr* right = 0;
        AstExpr* bin = new_expr(AST_EXPR_BINARY, parser->current.line);
        bin->as.binary.left = expr;
        bin->as.binary.op = AST_BIN_LOGIC_AND;
        advance(parser);
        right = parse_equality(parser);
        if (!right) {
            return 0;
        }
        bin->as.binary.right = right;
        expr = bin;
    }
    return expr;
}

static AstExpr* parse_logical_or(Parser* parser) {
    AstExpr* expr = parse_logical_and(parser);
    while (expr && parser->current.kind == TOKEN_PIPE_PIPE) {
        AstExpr* right = 0;
        AstExpr* bin = new_expr(AST_EXPR_BINARY, parser->current.line);
        bin->as.binary.left = expr;
        bin->as.binary.op = AST_BIN_LOGIC_OR;
        advance(parser);
        right = parse_logical_and(parser);
        if (!right) {
            return 0;
        }
        bin->as.binary.right = right;
        expr = bin;
    }
    return expr;
}

static AstExpr* parse_coalesce(Parser* parser) {
    AstExpr* expr = parse_logical_or(parser);
    while (expr && parser->current.kind == TOKEN_QUESTION_QUESTION) {
        AstExpr* right = 0;
        AstExpr* out = new_expr(AST_EXPR_COALESCE, parser->current.line);
        out->as.coalesce.left = expr;
        advance(parser);
        right = parse_logical_or(parser);
        if (!right) {
            return 0;
        }
        out->as.coalesce.right = right;
        expr = out;
    }
    return expr;
}

static AstExpr* parse_var_decl_init_expr(Parser* parser) {
    AstExpr* expr = parse_logical_or(parser);
    if (!expr) {
        return 0;
    }
    while (parser->current.kind == TOKEN_QUESTION_QUESTION) {
        AstExpr* out = 0;
        advance(parser);
        if (parser->current.kind == TOKEN_KW_RETURN ||
            parser->current.kind == TOKEN_KW_BREAK ||
            parser->current.kind == TOKEN_KW_CONTINUE) {
            out = new_expr(AST_EXPR_COALESCE_CONTROL, parser->current.line);
            out->as.coalesce_control.left = expr;
            out->as.coalesce_control.return_expr = 0;
            switch (parser->current.kind) {
                case TOKEN_KW_RETURN:
                    out->as.coalesce_control.control = AST_COALESCE_RETURN;
                    break;
                case TOKEN_KW_BREAK:
                    out->as.coalesce_control.control = AST_COALESCE_BREAK;
                    break;
                default:
                    out->as.coalesce_control.control = AST_COALESCE_CONTINUE;
                    break;
            }
            advance(parser);
            if (out->as.coalesce_control.control == AST_COALESCE_RETURN &&
                parser->current.kind != TOKEN_SEMICOLON) {
                out->as.coalesce_control.return_expr = parse_logical_or(parser);
                if (!out->as.coalesce_control.return_expr) {
                    return 0;
                }
            }
            expr = out;
            continue;
        }
        out = new_expr(AST_EXPR_COALESCE, parser->current.line);
        out->as.coalesce.left = expr;
        out->as.coalesce.right = parse_logical_or(parser);
        if (!out->as.coalesce.right) {
            return 0;
        }
        expr = out;
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
    if (parser->current.kind == TOKEN_DOT_DOT) {
        AstExpr* end = 0;
        int line = parser->current.line;
        advance(parser);
        end = parse_expr_no_range_internal(parser, 1);
        if (!end) {
            return 0;
        }
        return make_range_expr(expr, end, line);
    }
    return expr;
}

static AstExpr* parse_expr_no_range_internal(Parser* parser, int allow_catch_handler) {
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

static AstExpr* parse_expr_no_range(Parser* parser) {
    return parse_expr_no_range_internal(parser, 1);
}

static AstExpr* parse_expr_internal(Parser* parser, int allow_catch_handler) {
    AstExpr* expr = parse_expr_no_range_internal(parser, allow_catch_handler);
    if (!expr) {
        return 0;
    }
    if (parser->current.kind == TOKEN_DOT_DOT) {
        AstExpr* end = 0;
        int line = parser->current.line;
        advance(parser);
        end = parse_expr_no_range_internal(parser, allow_catch_handler);
        if (!end) {
            return 0;
        }
        return make_range_expr(expr, end, line);
    }
    return expr;
}

static AstExpr* parse_expr_stmt_target(Parser* parser) {
    return parse_expr_internal(parser, 0);
}

static AstExpr* parse_expr(Parser* parser) {
    return parse_expr_internal(parser, 1);
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

    if (parser->current.kind == TOKEN_KW_THROW) {
        stmt = new_stmt(AST_STMT_THROW, parser->current.line);
        advance(parser);
        stmt->as.throw_stmt.expr = parse_expr(parser);
        if (!stmt->as.throw_stmt.expr) {
            return 0;
        }
        if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after throw")) {
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

    if (parser->current.kind == TOKEN_KW_DEFER) {
        stmt = new_stmt(AST_STMT_DEFER, parser->current.line);
        advance(parser);
        if (parser->current.kind == TOKEN_LEFT_BRACE) {
            if (!parse_block(parser, &stmt->as.defer_stmt.body)) {
                return 0;
            }
            return stmt;
        }
        {
            AstStmt* expr_stmt = new_stmt(AST_STMT_EXPR, parser->current.line);
            expr_stmt->as.expr_stmt.expr = parse_expr(parser);
            if (!expr_stmt->as.expr_stmt.expr) {
                return 0;
            }
            if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after defer expression")) {
                return 0;
            }
            stmt_list_push(&stmt->as.defer_stmt.body.stmts, expr_stmt);
            return stmt;
        }
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
            if (parser->current.kind == TOKEN_KW_IF) {
                AstStmt* else_if = parse_stmt(parser);
                if (!else_if) {
                    return 0;
                }
                stmt_list_push(&stmt->as.if_stmt.else_block.stmts, else_if);
            } else {
                if (!parse_block(parser, &stmt->as.if_stmt.else_block)) {
                    return 0;
                }
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
            if (!expect(parser, TOKEN_FAT_ARROW, "expected '=>' after switch branch")) {
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

    if (parser->current.kind == TOKEN_KW_TRY) {
        if (parser->next.kind != TOKEN_LEFT_BRACE) {
            AstExpr* target = 0;
            int line = parser->current.line;
            advance(parser);
            target = parse_expr_stmt_target(parser);
            if (!target) {
                return 0;
            }
            if (parser->current.kind == TOKEN_KW_CATCH && parser->next.kind == TOKEN_LEFT_PAREN) {
                Parser probe = *parser;
                advance(&probe);
                advance(&probe);
                if (probe.current.kind == TOKEN_IDENT) {
                    advance(&probe);
                    if (probe.current.kind == TOKEN_RIGHT_PAREN) {
                        advance(&probe);
                        if (probe.current.kind == TOKEN_LEFT_BRACE) {
                            stmt = new_stmt(AST_STMT_EXPR_CATCH, line);
                            stmt->as.expr_catch_stmt.expr = target;
                            advance(parser);
                            if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after catch")) {
                                return 0;
                            }
                            if (parser->current.kind != TOKEN_IDENT) {
                                fail(parser, "expected catch binding name");
                                return 0;
                            }
                            stmt->as.expr_catch_stmt.binding_name = token_dup(&parser->current);
                            advance(parser);
                            if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after catch binding")) {
                                return 0;
                            }
                            if (!parse_block(parser, &stmt->as.expr_catch_stmt.body)) {
                                return 0;
                            }
                            if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after catch handler")) {
                                return 0;
                            }
                            return stmt;
                        }
                    }
                }
            }
            if (parser->current.kind != TOKEN_KW_CATCH) {
                fail(parser, "expected catch after try expression");
                return 0;
            }
            stmt = new_stmt(AST_STMT_EXPR, line);
            if (parser->next.kind == TOKEN_LEFT_PAREN) {
                AstExpr* out = new_expr(AST_EXPR_TRY, line);
                AstExprTryCatch catch_clause;
                memset(&catch_clause, 0, sizeof(catch_clause));
                out->as.try_expr.value = target;
                catch_clause.line = parser->current.line;
                catch_clause.error_type.kind = AST_TYPE_INFER;
                advance(parser);
                if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after catch")) {
                    return 0;
                }
                if (parser->current.kind != TOKEN_IDENT) {
                    fail(parser, "expected catch binding name");
                    return 0;
                }
                catch_clause.binding_name = token_dup(&parser->current);
                advance(parser);
                if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after catch binding")) {
                    return 0;
                }
                catch_clause.value = parse_value_branch_expr(parser, "expected '}' after try expression catch branch");
                if (!catch_clause.value) {
                    return 0;
                }
                expr_try_catch_list_push(&out->as.try_expr.catches, catch_clause);
                stmt->as.expr_stmt.expr = out;
            } else {
                AstExpr* out = new_expr(AST_EXPR_TRY, line);
                AstExprTryCatch catch_clause;
                memset(&catch_clause, 0, sizeof(catch_clause));
                out->as.try_expr.value = target;
                catch_clause.line = parser->current.line;
                catch_clause.error_type.kind = AST_TYPE_INFER;
                catch_clause.binding_name = strdup("_");
                advance(parser);
                catch_clause.value = parse_value_branch_expr(parser, "expected '}' after try expression catch branch");
                if (!catch_clause.value) {
                    return 0;
                }
                expr_try_catch_list_push(&out->as.try_expr.catches, catch_clause);
                stmt->as.expr_stmt.expr = out;
            }
            if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after expression")) {
                return 0;
            }
            return stmt;
        }
        stmt = new_stmt(AST_STMT_TRY, parser->current.line);
        advance(parser);
        if (!parse_block(parser, &stmt->as.try_stmt.try_body)) {
            return 0;
        }
        if (parser->current.kind != TOKEN_KW_CATCH) {
            fail(parser, "expected at least one catch after try block");
            return 0;
        }
        while (parser->current.kind == TOKEN_KW_CATCH) {
            AstTryCatch catch_clause;
            memset(&catch_clause, 0, sizeof(catch_clause));
            catch_clause.line = parser->current.line;
            advance(parser);
            if (!expect(parser, TOKEN_LEFT_PAREN, "expected '(' after catch")) {
                return 0;
            }
            if (!is_type_start(parser)) {
                fail(parser, "expected catch error type");
                return 0;
            }
            catch_clause.error_type = parse_type(parser);
            if (type_contains_errorable(&catch_clause.error_type)) {
                fail(parser, "catch error type cannot be errorable");
                return 0;
            }
            if (parser->current.kind != TOKEN_IDENT) {
                fail(parser, "expected catch binding name");
                return 0;
            }
            catch_clause.binding_name = token_dup(&parser->current);
            advance(parser);
            if (!expect(parser, TOKEN_RIGHT_PAREN, "expected ')' after catch binding")) {
                return 0;
            }
            if (!parse_block(parser, &catch_clause.body)) {
                return 0;
            }
            try_catch_list_push(&stmt->as.try_stmt.catches, catch_clause);
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
            stmt->as.for_range.start = parse_expr_no_range(parser);
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
            stmt->as.for_range.end = parse_expr_no_range(parser);
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
        if (type_contains_errorable(&stmt->as.for_range.type)) {
            fail(parser, "errorable type is only allowed in function return types");
            return 0;
        }
        if (parser->current.kind != TOKEN_IDENT) {
            fail(parser, "expected loop variable name");
            return 0;
        }
        stmt->as.for_range.name = token_dup(&parser->current);
        advance(parser);
        if (!expect(parser, TOKEN_KW_IN, "expected 'in' in for range")) {
            return 0;
        }
        stmt->as.for_range.start = parse_expr_no_range(parser);
        if (!stmt->as.for_range.start) {
            return 0;
        }
        if (!expect(parser, TOKEN_DOT_DOT, "expected '..' in for range")) {
            return 0;
        }
        stmt->as.for_range.end = parse_expr_no_range(parser);
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
        AstType decl_type;
        AstStmt* first_decl = 0;
        AstStmt* group_stmt = 0;
        int decl_count = 0;
        decl_type = parse_type(parser);
        if (type_contains_errorable(&decl_type)) {
            fail(parser, "errorable type is only allowed in function return types");
            return 0;
        }
        for (;;) {
            AstStmt* decl = 0;
            if (parser->current.kind != TOKEN_IDENT) {
                fail(parser, "expected identifier after type");
                return 0;
            }
            decl = new_stmt(AST_STMT_VAR_DECL, parser->current.line);
            decl->as.var_decl.type = ast_type_copy_local(&decl_type);
            decl->as.var_decl.name = token_dup(&parser->current);
            advance(parser);
            if (parser->current.kind != TOKEN_ASSIGN) {
                fail(parser, "expected '=' in variable declaration");
                return 0;
            }
            advance(parser);
            decl->as.var_decl.init = parse_var_decl_init_expr(parser);
            if (!decl->as.var_decl.init) {
                return 0;
            }
            if (decl_count == 0) {
                first_decl = decl;
            } else {
                if (!group_stmt) {
                    group_stmt = new_stmt(AST_STMT_GROUP, first_decl->line);
                    stmt_list_push(&group_stmt->as.group_stmt.stmts, first_decl);
                }
                stmt_list_push(&group_stmt->as.group_stmt.stmts, decl);
            }
            decl_count += 1;
            if (parser->current.kind != TOKEN_COMMA) {
                break;
            }
            advance(parser);
        }
        if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after variable declaration")) {
            return 0;
        }
        return group_stmt ? group_stmt : first_decl;
    }

    if (parser->current.kind == TOKEN_IDENT || parser->current.kind == TOKEN_KW_SELF || parser->current.kind == TOKEN_STAR || parser->current.kind == TOKEN_LEFT_PAREN) {
        AstExpr* target = parse_expr_stmt_target(parser);
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

    {
        int line = parser->current.line;
        AstExpr* expr = parse_expr_stmt_target(parser);
        if (expr) {
            stmt = new_stmt(AST_STMT_EXPR, line);
            stmt->as.expr_stmt.expr = expr;
            if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after expression")) {
                return 0;
            }
            return stmt;
        }
    }
    return 0;
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
            int param_line = parser->current.line;
            memset(&param, 0, sizeof(param));
            if (parser->current.kind == TOKEN_IDENT && parser->next.kind == TOKEN_COLON) {
                param.label = token_dup(&parser->current);
                advance(parser);
                advance(parser);
            }
            if (!is_type_start(parser)) {
                return fail(parser, "expected parameter type");
            }
            param.type = parse_type(parser);
            if (param.type.kind == AST_TYPE_INFER) {
                return fail(parser, "parameter type cannot be inferred");
            }
            if (type_contains_errorable(&param.type)) {
                return fail(parser, "errorable type is only allowed in function return types");
            }
            if (parser->current.kind == TOKEN_IDENT) {
                param.name = token_dup(&parser->current);
                param.line = parser->current.line;
                advance(parser);
            } else if (param.label) {
                param.name = strdup(param.label);
                param.line = param_line;
            } else {
                return fail(parser, "expected parameter name");
            }
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

static int parse_method_decl(Parser* parser, AstProgram* out_program, const char* owner_type_name, AstWhereConstraintList* where_constraints) {
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
    if (where_constraints) {
        fn.where_constraints = *where_constraints;
        memset(where_constraints, 0, sizeof(*where_constraints));
    }
    fn.line = parser->current.line;
    advance(parser);
    if (parser->current.kind == TOKEN_LT) {
        if (!parse_named_generic_params(parser, &fn.type_params)) {
            return 0;
        }
    }
    if (!parse_params(parser, &fn.params)) {
        return 0;
    }
    if (!parse_block(parser, &fn.body)) {
        return 0;
    }
    function_list_push(&out_program->functions, fn);
    return 1;
}

static int parse_decl_concept_names(Parser* parser, AstNameList* out) {
    if (parser->current.kind != TOKEN_COLON) {
        return 1;
    }
    advance(parser);
    for (;;) {
        char* name = 0;
        if (parser->current.kind != TOKEN_IDENT) {
            return fail(parser, "expected trait name after ':'");
        }
        name = parse_qualified_name(parser);
        if (!name) {
            return 0;
        }
        name_list_push(out, name);
        if (parser->current.kind == TOKEN_COMMA) {
            advance(parser);
            continue;
        }
        break;
    }
    return 1;
}

static AstNameList* find_local_ast_nominal_concept_names(AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->structs.count; ++i) {
        if (strcmp(program->structs.items[i].name, name) == 0) {
            return &program->structs.items[i].concept_names;
        }
    }
    for (i = 0; i < program->enums.count; ++i) {
        if (strcmp(program->enums.items[i].name, name) == 0) {
            return &program->enums.items[i].concept_names;
        }
    }
    for (i = 0; i < program->unions.count; ++i) {
        if (strcmp(program->unions.items[i].name, name) == 0) {
            return &program->unions.items[i].concept_names;
        }
    }
    return 0;
}

static AstAssocTypeBindingList* find_local_ast_nominal_assoc_type_bindings(AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->structs.count; ++i) {
        if (strcmp(program->structs.items[i].name, name) == 0) {
            return &program->structs.items[i].assoc_type_bindings;
        }
    }
    for (i = 0; i < program->enums.count; ++i) {
        if (strcmp(program->enums.items[i].name, name) == 0) {
            return &program->enums.items[i].assoc_type_bindings;
        }
    }
    for (i = 0; i < program->unions.count; ++i) {
        if (strcmp(program->unions.items[i].name, name) == 0) {
            return &program->unions.items[i].assoc_type_bindings;
        }
    }
    return 0;
}

static void name_list_clone(AstNameList* out, const AstNameList* in) {
    int i = 0;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < in->count; ++i) {
        name_list_push(out, dup_text(in->items[i]));
    }
}

static void where_constraint_list_clone(AstWhereConstraintList* out, const AstWhereConstraintList* in) {
    int i = 0;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < in->count; ++i) {
        AstWhereConstraint item;
        memset(&item, 0, sizeof(item));
        item.param_name = dup_text(in->items[i].param_name);
        item.concept_name = in->items[i].concept_name ? dup_text(in->items[i].concept_name) : 0;
        item.equal_type = in->items[i].equal_type;
        item.line = in->items[i].line;
        item.kind = in->items[i].kind;
        where_constraint_list_push(out, item);
    }
}

static void where_constraint_list_append_clone(AstWhereConstraintList* out, const AstWhereConstraintList* in) {
    int i = 0;
    for (i = 0; i < in->count; ++i) {
        AstWhereConstraint item;
        memset(&item, 0, sizeof(item));
        item.param_name = dup_text(in->items[i].param_name);
        item.concept_name = in->items[i].concept_name ? dup_text(in->items[i].concept_name) : 0;
        item.equal_type = in->items[i].equal_type;
        item.line = in->items[i].line;
        item.kind = in->items[i].kind;
        where_constraint_list_push(out, item);
    }
}

static int parse_assoc_type_binding_name(Parser* parser,
                                         char** out_concept_name,
                                         char** out_assoc_name,
                                         const char* missing_name_error) {
    char* qualified = 0;
    char* last_dot = 0;
    *out_concept_name = 0;
    *out_assoc_name = 0;
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, missing_name_error);
    }
    qualified = parse_qualified_name(parser);
    if (!qualified) {
        return 0;
    }
    last_dot = strrchr(qualified, '.');
    if (!last_dot) {
        *out_assoc_name = qualified;
        return 1;
    }
    *last_dot = '\0';
    *out_concept_name = dup_text(qualified);
    *out_assoc_name = dup_text(last_dot + 1);
    free(qualified);
    if (!*out_concept_name || !*out_assoc_name) {
        return fail(parser, "out of memory");
    }
    if (!text_is_ident_name(*out_assoc_name)) {
        return fail(parser, missing_name_error);
    }
    return 1;
}

static int nominal_decl_concept_names_add_unique(AstNameList* names, const char* concept_name) {
    int i = 0;
    if (!names) {
        return 0;
    }
    for (i = 0; i < names->count; ++i) {
        if (strcmp(names->items[i], concept_name) == 0) {
            return 1;
        }
    }
    name_list_push(names, concept_name);
    return 1;
}

static int parse_extend_decl(Parser* parser, AstProgram* out_program, int public_flag, AstWhereConstraintList* leading_where_constraints) {
    char* owner_name = 0;
    AstNameList concept_names;
    AstNameList* owner_concept_names = 0;
    AstAssocTypeBindingList* owner_assoc_type_bindings = 0;
    memset(&concept_names, 0, sizeof(concept_names));
    if (public_flag) {
        return fail(parser, "extend must not be public");
    }
    advance(parser);
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, "expected type name after extend");
    }
    owner_name = parse_qualified_name(parser);
    if (!owner_name) {
        return 0;
    }
    owner_concept_names = find_local_ast_nominal_concept_names(out_program, owner_name);
    owner_assoc_type_bindings = find_local_ast_nominal_assoc_type_bindings(out_program, owner_name);
    if (!owner_concept_names) {
        return fail(parser, "extend target must be a local type declared earlier");
    }
    if (leading_where_constraints && leading_where_constraints->count != 0) {
        const AstStructDecl* owner_struct = find_parsed_struct(out_program, owner_name);
        const AstUnionDecl* owner_union = find_parsed_union(out_program, owner_name);
        if ((!owner_struct || owner_struct->type_params.count == 0) &&
            (!owner_union || owner_union->type_params.count == 0)) {
            return fail(parser, "@where(...) requires generic parameters");
        }
    }
    if (!parse_decl_concept_names(parser, &concept_names)) {
        return 0;
    }
    if (!expect(parser, TOKEN_LEFT_BRACE, "expected '{' after extend target")) {
        return 0;
    }
    while (parser->current.kind != TOKEN_RIGHT_BRACE && parser->current.kind != TOKEN_EOF) {
        AstWhereConstraintList where_constraints;
        memset(&where_constraints, 0, sizeof(where_constraints));
        if (leading_where_constraints) {
            where_constraint_list_append_clone(&where_constraints, leading_where_constraints);
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
        if (parser->current.kind == TOKEN_KW_TYPE) {
            AstAssocTypeBinding binding;
            memset(&binding, 0, sizeof(binding));
            if (concept_names.count == 0) {
                return fail(parser, "type binding in extend requires explicit trait list");
            }
            advance(parser);
            binding.line = parser->current.line;
            if (!parse_assoc_type_binding_name(parser, &binding.concept_name, &binding.name, "expected associated type name")) {
                return 0;
            }
            if (!expect(parser, TOKEN_ASSIGN, "expected '=' in associated type binding")) {
                return 0;
            }
            if (!is_type_start(parser)) {
                return fail(parser, "expected type in associated type binding");
            }
            binding.value = parse_type(parser);
            if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after associated type binding")) {
                return 0;
            }
            name_list_clone(&binding.context_concept_names, &concept_names);
            assoc_type_binding_list_push(owner_assoc_type_bindings, binding);
            continue;
        }
        if (parser->current.kind == TOKEN_KW_PUBLIC &&
            parser->next.kind == TOKEN_IDENT &&
            (token_equals(&parser->next, "init") || token_equals(&parser->next, "deinit"))) {
            return fail(parser, "extend does not support init or deinit");
        }
        if (parser->current.kind == TOKEN_KW_STATIC &&
            parser->next.kind == TOKEN_IDENT &&
            (token_equals(&parser->next, "init") || token_equals(&parser->next, "deinit"))) {
            return fail(parser, "extend does not support init or deinit");
        }
        if (parser->current.kind == TOKEN_IDENT &&
            (token_equals(&parser->current, "init") || token_equals(&parser->current, "deinit")) &&
            parser->next.kind == TOKEN_LEFT_PAREN) {
            return fail(parser, "extend does not support init or deinit");
        }
        if (parser->current.kind == TOKEN_KW_STATIC || looks_like_method_decl(parser)) {
            if (!parse_method_decl(parser, out_program, owner_name, &where_constraints)) {
                return 0;
            }
            continue;
        }
        return fail(parser, "extend block only supports methods");
    }
    if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after extend block")) {
        return 0;
    }
    {
        int i = 0;
        for (i = 0; i < concept_names.count; ++i) {
            nominal_decl_concept_names_add_unique(owner_concept_names, concept_names.items[i]);
        }
    }
    return 1;
}

static int parse_import_decl(Parser* parser, AstProgram* out_program, int public_flag) {
    AstImportDecl import_decl;
    char* import_text = 0;
    memset(&import_decl, 0, sizeof(import_decl));
    import_decl.public_flag = public_flag;
    import_decl.line = parser->current.line;
    advance(parser);
    if (parser->current.kind == TOKEN_IDENT && parser->next.kind == TOKEN_ASSIGN) {
        import_decl.alias_name = token_dup(&parser->current);
        advance(parser);
        advance(parser);
    }
    if (parser->current.kind == TOKEN_IDENT) {
        import_decl.path = token_dup(&parser->current);
        advance(parser);
    } else if (parser->current.kind == TOKEN_STRING_LIT) {
        import_text = string_token_dup(&parser->current);
        if (!import_text) {
            return fail(parser, "out of memory");
        }
        if (text_is_ident_name(import_text)) {
            free(import_text);
            return fail(parser, "package imports must not use quotes");
        }
        import_decl.path = import_text;
        advance(parser);
    } else {
        return fail(parser, "expected import path string or package name");
    }
    if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after import")) {
        return 0;
    }
    import_list_push(&out_program->imports, import_decl);
    return 1;
}

static int parse_extern_item(Parser* parser, AstProgram* out_program, int inherited_public_flag, int in_block) {
    AstType type;
    int public_flag = inherited_public_flag;
    int line = parser->current.line;
    char* name = 0;
    if (parser->current.kind == TOKEN_KW_PUBLIC) {
        public_flag = 1;
        advance(parser);
    }
    if (!is_type_start(parser)) {
        return fail(parser, in_block
            ? "extern block only supports function or global declarations"
            : "extern only supports function or global declarations");
    }
    type = parse_type(parser);
    if (type.kind == AST_TYPE_INFER) {
        return fail(parser, "extern declaration type cannot be inferred");
    }
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, "expected extern declaration name");
    }
    name = token_dup(&parser->current);
    advance(parser);
    if (parser->current.kind == TOKEN_LEFT_PAREN) {
        AstFunction fn;
        memset(&fn, 0, sizeof(fn));
        fn.extern_flag = 1;
        fn.public_flag = public_flag;
        fn.return_type = type;
        fn.name = name;
        fn.line = line;
        if (!parse_params(parser, &fn.params)) {
            return 0;
        }
        if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after extern function declaration")) {
            return 0;
        }
        function_list_push(&out_program->functions, fn);
        return 1;
    }
    if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after extern global declaration")) {
        return 0;
    }
    {
        AstGlobal global;
        memset(&global, 0, sizeof(global));
        global.type = type;
        global.name = name;
        global.public_flag = public_flag;
        global.extern_flag = 1;
        global.line = line;
        global_list_push(&out_program->globals, global);
    }
    return 1;
}

static int parse_extern_decl(Parser* parser, AstProgram* out_program, int inherited_public_flag) {
    if (parser->current.kind == TOKEN_LEFT_BRACE) {
        if (inherited_public_flag) {
            return fail(parser, "extern block must not be public");
        }
        advance(parser);
        while (parser->current.kind != TOKEN_RIGHT_BRACE && parser->current.kind != TOKEN_EOF) {
            if (!parse_extern_item(parser, out_program, 0, 1)) {
                return 0;
            }
        }
        return expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after extern block");
    }
    return parse_extern_item(parser, out_program, inherited_public_flag, 0);
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
    if (!parse_decl_concept_names(parser, &enum_decl.concept_names)) {
        return 0;
    }
    if (!expect(parser, TOKEN_LEFT_BRACE, "expected '{' after enum name")) {
        return 0;
    }
    while (parser->current.kind != TOKEN_RIGHT_BRACE && parser->current.kind != TOKEN_EOF) {
        if (parser->current.kind == TOKEN_KW_STATIC || looks_like_method_decl(parser)) {
            if (!parse_method_decl(parser, out_program, enum_decl.name, 0)) {
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

static int parse_struct_decl(Parser* parser, AstProgram* out_program, AstNameList* generic_params, int record_flag) {
    AstStructDecl struct_decl;
    memset(&struct_decl, 0, sizeof(struct_decl));
    struct_decl.record_flag = record_flag;
    if (generic_params) {
        struct_decl.type_params = *generic_params;
        memset(generic_params, 0, sizeof(*generic_params));
    }
    advance(parser);
    if (parser->current.kind != TOKEN_IDENT) {
        return fail(parser, record_flag ? "expected record name" : "expected struct name");
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
    if (!parse_decl_concept_names(parser, &struct_decl.concept_names)) {
        return 0;
    }
    if (record_flag && struct_decl.concept_names.count != 0) {
        return fail(parser, "record trait implementation is not supported");
    }
    if (!expect(parser, TOKEN_LEFT_BRACE, record_flag ? "expected '{' after record name" : "expected '{' after struct name")) {
        return 0;
    }
    while (parser->current.kind != TOKEN_RIGHT_BRACE && parser->current.kind != TOKEN_EOF) {
        AstWhereConstraintList where_constraints;
        memset(&where_constraints, 0, sizeof(where_constraints));
        while (parser->current.kind == TOKEN_AT) {
            if (parser->next.kind == TOKEN_IDENT && token_equals(&parser->next, "where")) {
                if (!parse_where_annotation(parser, &where_constraints)) {
                    return 0;
                }
                continue;
            }
            return fail(parser, "only @where(...) annotations are supported here");
        }
        if (parser->current.kind == TOKEN_KW_TYPE) {
            AstAssocTypeBinding binding;
            memset(&binding, 0, sizeof(binding));
            if (record_flag) {
                return fail(parser, "record associated type binding is not supported");
            }
            if (struct_decl.concept_names.count == 0) {
                return fail(parser, "associated type binding requires declared trait implementation");
            }
            advance(parser);
            binding.line = parser->current.line;
            if (!parse_assoc_type_binding_name(parser, &binding.concept_name, &binding.name, "expected associated type name")) {
                return 0;
            }
            if (!expect(parser, TOKEN_ASSIGN, "expected '=' in associated type binding")) {
                return 0;
            }
            if (!is_type_start(parser)) {
                return fail(parser, "expected type in associated type binding");
            }
            binding.value = parse_type(parser);
            if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after associated type binding")) {
                return 0;
            }
            name_list_clone(&binding.context_concept_names, &struct_decl.concept_names);
            assoc_type_binding_list_push(&struct_decl.assoc_type_bindings, binding);
            continue;
        }
        if (parser->current.kind == TOKEN_KW_PUBLIC &&
            parser->next.kind == TOKEN_IDENT &&
            token_equals(&parser->next, "deinit")) {
            return fail(parser, "deinit must not be public");
        }
        if (record_flag && parser->current.kind == TOKEN_KW_PUBLIC) {
            return fail(parser, "record fields must not be public");
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
        if (looks_like_static_field_decl(parser)) {
            AstGlobal global;
            char* field_name = 0;
            memset(&global, 0, sizeof(global));
            if (record_flag) {
                return fail(parser, "record static fields are not supported");
            }
            if (struct_decl.type_params.count != 0) {
                return fail(parser, "generic struct static fields are not supported");
            }
            advance(parser);
            global.type = parse_type(parser);
            if (type_contains_errorable(&global.type)) {
                return fail(parser, "errorable type is only allowed in function return types");
            }
            field_name = token_dup(&parser->current);
            global.name = make_static_field_name(struct_decl.name, field_name);
            hashmap_set(&parser->static_fields, dup_join3(struct_decl.name, ".", field_name), (void*)1);
            free(field_name);
            global.line = parser->current.line;
            advance(parser);
            if (!expect(parser, TOKEN_ASSIGN, "expected '=' in static field declaration")) {
                return 0;
            }
            global.init = parse_expr(parser);
            if (!global.init) {
                return 0;
            }
            if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after static field declaration")) {
                return 0;
            }
            global_list_push(&out_program->globals, global);
            continue;
        }
        if (parser->current.kind == TOKEN_IDENT &&
            token_equals(&parser->current, "init") &&
            (parser->next.kind == TOKEN_LEFT_PAREN ||
             parser->next.kind == TOKEN_QUESTION ||
             parser->next.kind == TOKEN_IDENT)) {
            AstStructInitDecl init_decl;
            int init_index = 0;
            memset(&init_decl, 0, sizeof(init_decl));
            if (record_flag) {
                return fail(parser, "record init is not supported");
            }
            init_decl.line = parser->current.line;
            advance(parser);
            if (parser->current.kind == TOKEN_QUESTION) {
                init_decl.failable_flag = 1;
                advance(parser);
            }
            if (parser->current.kind == TOKEN_IDENT && parser->next.kind == TOKEN_LEFT_PAREN) {
                init_decl.name = token_dup(&parser->current);
                advance(parser);
            }
            if (!parse_params(parser, &init_decl.params)) {
                return 0;
            }
            for (init_index = 0; init_index < struct_decl.init_overloads.count; ++init_index) {
                AstStructInitDecl* existing = &struct_decl.init_overloads.items[init_index];
                int param_index = 0;
                if ((existing->name || init_decl.name) &&
                    (!existing->name || !init_decl.name || strcmp(existing->name, init_decl.name) != 0)) {
                    continue;
                }
                if (existing->params.count != init_decl.params.count) {
                    continue;
                }
                for (param_index = 0; param_index < existing->params.count; ++param_index) {
                    if (!ast_type_equals(&existing->params.items[param_index].type, &init_decl.params.items[param_index].type)) {
                        break;
                    }
                }
                if (param_index == existing->params.count) {
                    return fail(parser, "duplicate struct init");
                }
            }
            if (!parse_block(parser, &init_decl.body)) {
                return 0;
            }
            struct_init_decl_list_push(&struct_decl.init_overloads, init_decl);
            continue;
        }
        if (parser->current.kind == TOKEN_IDENT && token_equals(&parser->current, "deinit") && parser->next.kind == TOKEN_LEFT_PAREN) {
            if (record_flag) {
                return fail(parser, "record deinit is not supported");
            }
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
            if (record_flag) {
                return fail(parser, "record methods are not supported");
            }
            if (!parse_method_decl(parser, out_program, struct_decl.name, &where_constraints)) {
                return 0;
            }
            continue;
        }
        if (where_constraints.count != 0) {
            return fail(parser, record_flag ? "@where(...) requires a generic function, struct, or record" : "@where(...) requires a generic function or struct");
        }
        AstType field_type;
        if (!is_type_start(parser)) {
            return fail(parser, record_flag ? "expected record field type" : "expected struct field type");
        }
        field_type = parse_type(parser);
        if (type_contains_errorable(&field_type)) {
            return fail(parser, "errorable type is only allowed in function return types");
        }
        for (;;) {
            AstStructField field;
            memset(&field, 0, sizeof(field));
            field.type = ast_type_copy_local(&field_type);
            if (parser->current.kind != TOKEN_IDENT) {
                return fail(parser, record_flag ? "expected record field name" : "expected struct field name");
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
            struct_field_list_push(&struct_decl.fields, field);
            if (parser->current.kind != TOKEN_COMMA) {
                break;
            }
            advance(parser);
        }
        if (!expect(parser, TOKEN_SEMICOLON, record_flag ? "expected ';' after record field" : "expected ';' after struct field")) {
            return 0;
        }
    }
    if (!expect(parser, TOKEN_RIGHT_BRACE, record_flag ? "expected '}' after record declaration" : "expected '}' after struct declaration")) {
        return 0;
    }
    struct_list_push(&out_program->structs, struct_decl);
    return 1;
}

static int parse_union_decl(Parser* parser, AstProgram* out_program, AstNameList* generic_params) {
    AstUnionDecl union_decl;
    memset(&union_decl, 0, sizeof(union_decl));
    if (generic_params) {
        union_decl.type_params = *generic_params;
        memset(generic_params, 0, sizeof(*generic_params));
    }
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
    if (parser->current.kind == TOKEN_LT) {
        if (union_decl.type_params.count != 0) {
            return fail(parser, "duplicate generic parameter list");
        }
        if (!parse_named_generic_params(parser, &union_decl.type_params)) {
            return 0;
        }
    }
    if (!parse_decl_concept_names(parser, &union_decl.concept_names)) {
        return 0;
    }
    if (!expect(parser, TOKEN_LEFT_BRACE, "expected '{' after union name")) {
        return 0;
    }
    while (parser->current.kind != TOKEN_RIGHT_BRACE && parser->current.kind != TOKEN_EOF) {
        if (parser->current.kind == TOKEN_KW_STATIC || looks_like_method_decl(parser)) {
            if (!parse_method_decl(parser, out_program, union_decl.name, 0)) {
                return 0;
            }
            continue;
        }
        {
            AstType variant_type;
            int parsed_any = 0;
            if (!is_type_start(parser)) {
                return fail(parser, "expected union variant type");
            }
            variant_type = parse_type(parser);
            while (parser->current.kind == TOKEN_IDENT) {
                AstUnionVariant variant;
                memset(&variant, 0, sizeof(variant));
                variant.type = ast_type_copy_local(&variant_type);
                variant.name = token_dup(&parser->current);
                variant.line = parser->current.line;
                advance(parser);
                union_variant_list_push(&union_decl.variants, variant);
                parsed_any = 1;
                if (parser->current.kind != TOKEN_COMMA) {
                    break;
                }
                advance(parser);
            }
            if (!parsed_any) {
                return fail(parser, "expected union variant name");
            }
            if (!expect(parser, TOKEN_SEMICOLON, "expected ';' after union variant")) {
                return 0;
            }
            continue;
        }
    }
    if (!expect(parser, TOKEN_RIGHT_BRACE, "expected '}' after union declaration")) {
        return 0;
    }
    union_list_push(&out_program->unions, union_decl);
    return 1;
}

void parser_init(Parser* parser, const char* source, const char* filename) {
    memset(parser, 0, sizeof(*parser));
    parser->source = source;
    parser->filename = filename;
    lexer_init(&parser->lexer, source);
    parser->error = 0;
    parser->error_line = 1;
    parser->error_column = 1;
    hashmap_init(&parser->known_types);
    hashmap_init(&parser->static_fields);
    memset(&parser->scoped_type_names, 0, sizeof(parser->scoped_type_names));
    register_known_type(parser, "Int");
    register_known_type(parser, "Int8");
    register_known_type(parser, "Int16");
    register_known_type(parser, "Int32");
    register_known_type(parser, "Int64");
    register_known_type(parser, "UInt16");
    register_known_type(parser, "UInt32");
    register_known_type(parser, "UInt64");
    register_known_type(parser, "Float16");
    register_known_type(parser, "Float32");
    register_known_type(parser, "Float64");
    register_known_type(parser, "Float");
    register_known_type(parser, "Double");
    register_known_type(parser, "Char");
    register_known_type(parser, "UInt8");
    register_known_type(parser, "Bool");
    parser->current = lexer_next(&parser->lexer);
    parser->next = lexer_next(&parser->lexer);
}

int parser_parse_program(Parser* parser, AstProgram* out_program) {
    g_active_parser = parser;
    memset(out_program, 0, sizeof(*out_program));
    while (parser->current.kind != TOKEN_EOF) {
        AstType type;
        char* name = 0;
        int public_flag = 0;
        AstNameList generic_params;
        AstWhereConstraintList where_constraints;
        memset(&generic_params, 0, sizeof(generic_params));
        memset(&where_constraints, 0, sizeof(where_constraints));

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
        if (parser->current.kind == TOKEN_KW_IMPORT) {
            if (where_constraints.count != 0) {
                return fail(parser, "@where(...) requires a generic function or struct");
            }
            if (!parse_import_decl(parser, out_program, public_flag)) {
                return 0;
            }
            continue;
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
        if (parser->current.kind == TOKEN_KW_EXTERN) {
            if (where_constraints.count != 0) {
                return fail(parser, "@where(...) requires a generic function or struct");
            }
            advance(parser);
            if (!parse_extern_decl(parser, out_program, public_flag)) {
                return 0;
            }
            continue;
        }
        if (parser->current.kind == TOKEN_KW_CONCEPT) {
            if (!parse_concept_decl(parser, out_program, public_flag, &where_constraints)) {
                return 0;
            }
            continue;
        }
        if (parser->current.kind == TOKEN_KW_EXTEND) {
            if (generic_params.count != 0) {
                return fail(parser, "generic extend is not supported");
            }
            if (!parse_extend_decl(parser, out_program, public_flag, &where_constraints)) {
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
        if (parser->current.kind == TOKEN_KW_STRUCT || parser->current.kind == TOKEN_KW_RECORD) {
            int record_flag = parser->current.kind == TOKEN_KW_RECORD;
            if (!parse_struct_decl(parser, out_program, &generic_params, record_flag)) {
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
            if (!parse_union_decl(parser, out_program, &generic_params)) {
                return 0;
            }
            out_program->unions.items[out_program->unions.count - 1].where_constraints = where_constraints;
            if (where_constraints.count != 0 &&
                out_program->unions.items[out_program->unions.count - 1].type_params.count == 0) {
                return fail(parser, "@where(...) requires generic parameters");
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
            if (type_contains_errorable(&type)) {
                return fail(parser, "errorable type is only allowed in function return types");
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
    g_active_parser = 0;
    return 1;
}
