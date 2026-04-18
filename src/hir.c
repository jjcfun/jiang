#include "lower.h"
#include "vec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Scope {
    struct Scope* parent;
    HirBindingList bindings;
} Scope;

typedef struct LowerContext {
    HirProgram* program;
    const AstProgram* ast;
    const char* error;
    HirFunction* current_function;
    Scope* scope;
    int loop_depth;
    int temp_index;
} LowerContext;

#define type_list_push(list, type) VEC_PUSH((list), (type))
#define binding_list_push(list, binding) VEC_PUSH((list), (binding))
#define stmt_list_push(list, stmt) VEC_PUSH((list), (stmt))
#define expr_list_push(list, expr) VEC_PUSH((list), (expr))
#define function_list_push(list, fn) VEC_PUSH((list), (fn))
#define global_list_push(list, global) VEC_PUSH((list), (global))
#define enum_member_list_push(list, member) VEC_PUSH((list), (member))
#define struct_field_list_push(list, field) VEC_PUSH((list), (field))
#define struct_field_init_list_push(list, field) VEC_PUSH((list), (field))
#define struct_list_push(list, struct_decl) VEC_PUSH((list), (struct_decl))
#define enum_list_push(list, enum_decl) VEC_PUSH((list), (enum_decl))
#define union_variant_list_push(list, variant) VEC_PUSH((list), (variant))
#define union_list_push(list, union_decl) VEC_PUSH((list), (union_decl))

static int fail(LowerContext* ctx, const char* error);
static HirExpr* lower_expr(LowerContext* ctx, const AstExpr* expr);

static char* make_struct_init_name(const char* struct_name) {
    const char* prefix = "__struct_init_";
    size_t prefix_len = strlen(prefix);
    size_t name_len = strlen(struct_name);
    char* out = (char*)malloc(prefix_len + name_len + 1);
    if (!out) {
        return 0;
    }
    memcpy(out, prefix, prefix_len);
    memcpy(out + prefix_len, struct_name, name_len + 1);
    return out;
}

static HirType* primitive_type(HirProgram* program, HirTypeKind kind) {
    switch (kind) {
        case HIR_TYPE_INT: return &program->int_type;
        case HIR_TYPE_BOOL: return &program->bool_type;
        case HIR_TYPE_VOID: return &program->void_type;
        default: return 0;
    }
}

static HirEnumDecl* find_enum(HirProgram* program, const char* name) {
    return (HirEnumDecl*)hashmap_get(&program->enum_name_map, name);
}

static HirStructDecl* find_struct(HirProgram* program, const char* name) {
    return (HirStructDecl*)hashmap_get(&program->struct_name_map, name);
}

static HirEnumMember* find_enum_member(HirProgram* program, const char* name) {
    return (HirEnumMember*)hashmap_get(&program->enum_member_map, name);
}

static HirUnionDecl* find_union(HirProgram* program, const char* name) {
    return (HirUnionDecl*)hashmap_get(&program->union_name_map, name);
}

static HirUnionVariant* find_variant(HirProgram* program, const char* name) {
    return (HirUnionVariant*)hashmap_get(&program->variant_map, name);
}

static HirType* new_owned_type(HirProgram* program, HirTypeKind kind) {
    HirType* type = (HirType*)calloc(1, sizeof(HirType));
    type->kind = kind;
    type_list_push(&program->owned_types, type);
    return type;
}

static int type_equals(HirType* left, HirType* right) {
    int i = 0;
    if (left == right) {
        return 1;
    }
    if (!left || !right || left->kind != right->kind) {
        return 0;
    }
    if (left->kind == HIR_TYPE_ARRAY) {
        return left->array_length == right->array_length && type_equals(left->array_item, right->array_item);
    }
    if (left->kind == HIR_TYPE_ENUM) {
        return left->enum_decl == right->enum_decl;
    }
    if (left->kind == HIR_TYPE_STRUCT) {
        return left->struct_decl == right->struct_decl;
    }
    if (left->kind == HIR_TYPE_UNION) {
        return left->union_decl == right->union_decl;
    }
    if (left->kind != HIR_TYPE_TUPLE) {
        return 0;
    }
    if (left->tuple_items.count != right->tuple_items.count) {
        return 0;
    }
    for (i = 0; i < left->tuple_items.count; ++i) {
        if (!type_equals(left->tuple_items.items[i], right->tuple_items.items[i])) {
            return 0;
        }
    }
    return 1;
}

static HirType* lower_type(LowerContext* ctx, const AstType* type) {
    int i = 0;
    switch (type->kind) {
        case AST_TYPE_INT:
            return primitive_type(ctx->program, HIR_TYPE_INT);
        case AST_TYPE_BOOL:
            return primitive_type(ctx->program, HIR_TYPE_BOOL);
        case AST_TYPE_VOID:
            return primitive_type(ctx->program, HIR_TYPE_VOID);
        case AST_TYPE_ARRAY: {
            HirType* array = new_owned_type(ctx->program, HIR_TYPE_ARRAY);
            array->array_item = lower_type(ctx, type->array_item);
            array->array_length = type->array_length;
            return array;
        }
        case AST_TYPE_NAMED: {
            HirEnumDecl* enum_decl = find_enum(ctx->program, type->named_name);
            HirStructDecl* struct_decl = find_struct(ctx->program, type->named_name);
            HirUnionDecl* union_decl = find_union(ctx->program, type->named_name);
            if (enum_decl) {
                HirType* enum_type = new_owned_type(ctx->program, HIR_TYPE_ENUM);
                enum_type->enum_decl = enum_decl;
                return enum_type;
            }
            if (struct_decl) {
                HirType* struct_type = new_owned_type(ctx->program, HIR_TYPE_STRUCT);
                struct_type->struct_decl = struct_decl;
                return struct_type;
            }
            if (union_decl) {
                HirType* union_type = new_owned_type(ctx->program, HIR_TYPE_UNION);
                union_type->union_decl = union_decl;
                return union_type;
            }
            fail(ctx, "unknown named type");
            return 0;
        }
        case AST_TYPE_TUPLE: {
            HirType* tuple = new_owned_type(ctx->program, HIR_TYPE_TUPLE);
            for (i = 0; i < type->tuple_items.count; ++i) {
                type_list_push(&tuple->tuple_items, lower_type(ctx, &type->tuple_items.items[i]));
            }
            return tuple;
        }
        default:
            return primitive_type(ctx->program, HIR_TYPE_INT);
    }
}

static HirBinding* new_binding(HirType* type, const char* name, HirBindingKind kind, int line) {
    HirBinding* binding = (HirBinding*)calloc(1, sizeof(HirBinding));
    binding->type = type;
    binding->name = (char*)name;
    binding->kind = kind;
    binding->line = line;
    return binding;
}

static HirExpr* new_expr(HirExprKind kind, HirType* type, int line) {
    HirExpr* expr = (HirExpr*)calloc(1, sizeof(HirExpr));
    expr->kind = kind;
    expr->type = type;
    expr->line = line;
    return expr;
}

static HirStmt* new_stmt(HirStmtKind kind, int line) {
    HirStmt* stmt = (HirStmt*)calloc(1, sizeof(HirStmt));
    stmt->kind = kind;
    stmt->line = line;
    return stmt;
}

static HirStmtKind stmt_kind_from_ast(AstStmtKind kind) {
    switch (kind) {
        case AST_STMT_RETURN: return HIR_STMT_RETURN;
        case AST_STMT_VAR_DECL: return HIR_STMT_VAR_DECL;
        case AST_STMT_ASSIGN: return HIR_STMT_ASSIGN;
        case AST_STMT_IF: return HIR_STMT_IF;
        case AST_STMT_WHILE: return HIR_STMT_WHILE;
        case AST_STMT_FOR_RANGE: return HIR_STMT_FOR_RANGE;
        case AST_STMT_BREAK: return HIR_STMT_BREAK;
        case AST_STMT_CONTINUE: return HIR_STMT_CONTINUE;
        case AST_STMT_EXPR: return HIR_STMT_EXPR;
        default: return HIR_STMT_EXPR;
    }
}

static char* make_temp_name(LowerContext* ctx) {
    char buffer[32];
    char* name = 0;
    size_t length = 0;
    snprintf(buffer, sizeof(buffer), "__tupletmp%d", ctx->temp_index++);
    length = strlen(buffer);
    name = (char*)malloc(length + 1);
    if (!name) {
        return 0;
    }
    memcpy(name, buffer, length + 1);
    return name;
}

static HirExpr* make_binding_expr(HirBinding* binding, int line) {
    HirExpr* expr = new_expr(HIR_EXPR_BINDING, binding->type, line);
    expr->as.binding = binding;
    return expr;
}

static HirExpr* make_int_expr(LowerContext* ctx, int value, int line) {
    HirExpr* expr = new_expr(HIR_EXPR_INT, primitive_type(ctx->program, HIR_TYPE_INT), line);
    expr->as.int_value = value;
    return expr;
}

static int apply_array_length_inference(HirType* declared, HirType* actual) {
    if (!declared || !actual) {
        return 0;
    }
    if (declared->kind == HIR_TYPE_ARRAY && actual->kind == HIR_TYPE_ARRAY && declared->array_length < 0) {
        if (!type_equals(declared->array_item, actual->array_item)) {
            return 0;
        }
        declared->array_length = actual->array_length;
        return 1;
    }
    return 0;
}

static int fail(LowerContext* ctx, const char* error) {
    ctx->error = error;
    return 0;
}

static HirFunction* find_function(HirProgram* program, const char* name) {
    return (HirFunction*)hashmap_get(&program->function_map, name);
}

static HirGlobal* find_global(HirProgram* program, const char* name) {
    return (HirGlobal*)hashmap_get(&program->global_map, name);
}

static HirBinding* lookup_binding(LowerContext* ctx, const char* name) {
    Scope* scope = ctx->scope;
    while (scope) {
        int i = scope->bindings.count - 1;
        for (; i >= 0; --i) {
            if (strcmp(scope->bindings.items[i]->name, name) == 0) {
                return scope->bindings.items[i];
            }
        }
        scope = scope->parent;
    }
    {
        HirGlobal* global = find_global(ctx->program, name);
        if (global) {
            return global->binding;
        }
    }
    return 0;
}

static void rebuild_global_map(HirProgram* program) {
    int i = 0;
    hashmap_free(&program->global_map);
    hashmap_init(&program->global_map);
    for (i = 0; i < program->globals.count; ++i) {
        hashmap_set(&program->global_map, program->globals.items[i].binding->name, &program->globals.items[i]);
    }
}

static void rebuild_function_map(HirProgram* program) {
    int i = 0;
    hashmap_free(&program->function_map);
    hashmap_init(&program->function_map);
    for (i = 0; i < program->functions.count; ++i) {
        hashmap_set(&program->function_map, program->functions.items[i].name, &program->functions.items[i]);
    }
}

static void rebuild_enum_maps(HirProgram* program) {
    int i = 0;
    hashmap_free(&program->enum_name_map);
    hashmap_init(&program->enum_name_map);
    hashmap_free(&program->enum_member_map);
    hashmap_init(&program->enum_member_map);
    for (i = 0; i < program->enums.count; ++i) {
        int j = 0;
        hashmap_set(&program->enum_name_map, program->enums.items[i].name, &program->enums.items[i]);
        for (j = 0; j < program->enums.items[i].members.count; ++j) {
            hashmap_set(&program->enum_member_map, program->enums.items[i].members.items[j].name, &program->enums.items[i].members.items[j]);
        }
    }
}

static void rebuild_struct_map(HirProgram* program) {
    int i = 0;
    hashmap_free(&program->struct_name_map);
    hashmap_init(&program->struct_name_map);
    for (i = 0; i < program->structs.count; ++i) {
        hashmap_set(&program->struct_name_map, program->structs.items[i].name, &program->structs.items[i]);
    }
}

static int payload_slot_count(HirType* type) {
    int i = 0;
    if (!type || type->kind == HIR_TYPE_VOID) {
        return 0;
    }
    if (type->kind == HIR_TYPE_INT || type->kind == HIR_TYPE_BOOL) {
        return 1;
    }
    if (type->kind == HIR_TYPE_TUPLE) {
        for (i = 0; i < type->tuple_items.count; ++i) {
            if (type->tuple_items.items[i]->kind != HIR_TYPE_INT && type->tuple_items.items[i]->kind != HIR_TYPE_BOOL) {
                return -1;
            }
        }
        return type->tuple_items.count;
    }
    return -1;
}

static HirEnumMember* find_enum_member_in_decl(HirEnumDecl* enum_decl, const char* name) {
    int i = 0;
    for (i = 0; i < enum_decl->members.count; ++i) {
        if (strcmp(enum_decl->members.items[i].name, name) == 0) {
            return &enum_decl->members.items[i];
        }
    }
    return 0;
}

static HirUnionVariant* find_union_variant(HirUnionDecl* union_decl, const char* name) {
    int i = 0;
    for (i = 0; i < union_decl->variants.count; ++i) {
        if (strcmp(union_decl->variants.items[i].name, name) == 0) {
            return &union_decl->variants.items[i];
        }
    }
    return 0;
}

static HirStructField* find_struct_field(HirStructDecl* struct_decl, const char* name, int* field_index) {
    int i = 0;
    for (i = 0; i < struct_decl->fields.count; ++i) {
        if (strcmp(struct_decl->fields.items[i].name, name) == 0) {
            if (field_index) {
                *field_index = i;
            }
            return &struct_decl->fields.items[i];
        }
    }
    return 0;
}

static const AstStructDecl* find_ast_struct(const AstProgram* ast, const char* name) {
    int i = 0;
    for (i = 0; i < ast->structs.count; ++i) {
        if (strcmp(ast->structs.items[i].name, name) == 0) {
            return &ast->structs.items[i];
        }
    }
    return 0;
}

static HirEnumMember* resolve_enum_expr(LowerContext* ctx, const AstExpr* expr, HirEnumDecl** out_enum) {
    HirEnumMember* member = 0;
    HirEnumDecl* enum_decl = 0;
    int i = 0;
    if (expr->as.variant.union_name) {
        enum_decl = find_enum(ctx->program, expr->as.variant.union_name);
        if (!enum_decl) {
            return 0;
        }
        member = find_enum_member_in_decl(enum_decl, expr->as.variant.variant_name);
    } else {
        for (i = 0; i < ctx->program->enums.count; ++i) {
            HirEnumMember* candidate = find_enum_member_in_decl(&ctx->program->enums.items[i], expr->as.variant.variant_name);
            if (candidate) {
                enum_decl = &ctx->program->enums.items[i];
                member = candidate;
                break;
            }
        }
    }
    if (!member || !enum_decl) {
        return 0;
    }
    if (out_enum) {
        *out_enum = enum_decl;
    }
    return member;
}

static int bind_in_current_scope(LowerContext* ctx, HirBinding* binding) {
    int i = 0;
    for (i = 0; i < ctx->scope->bindings.count; ++i) {
        if (strcmp(ctx->scope->bindings.items[i]->name, binding->name) == 0) {
            return fail(ctx, "duplicate local binding");
        }
    }
    binding_list_push(&ctx->scope->bindings, binding);
    return 1;
}

static void push_scope(LowerContext* ctx, Scope* scope) {
    memset(scope, 0, sizeof(*scope));
    scope->parent = ctx->scope;
    ctx->scope = scope;
}

static void pop_scope(LowerContext* ctx) {
    ctx->scope = ctx->scope->parent;
}

static HirExpr* lower_expr(LowerContext* ctx, const AstExpr* expr);
static HirExpr* make_union_tag_expr(LowerContext* ctx, HirExpr* value, int line);
static HirExpr* make_union_field_expr(LowerContext* ctx, HirExpr* value, HirUnionVariant* variant, int field_index, HirType* type, int line);
static int lower_variant_pattern_bind(LowerContext* ctx, HirExpr* value, const AstExpr* pattern, HirBlock* out_block, HirExpr** cond_out);
static int lower_pattern_bind(LowerContext* ctx, const AstBindingPattern* pattern, HirExpr* init, HirBlock* out_block);

static HirBuiltinKind builtin_kind(const char* name) {
    if (strcmp(name, "assert") == 0) return HIR_BUILTIN_ASSERT;
    if (strcmp(name, "print") == 0) return HIR_BUILTIN_PRINT;
    if (strcmp(name, "panic") == 0) return HIR_BUILTIN_PANIC;
    return HIR_BUILTIN_NONE;
}

static int lower_builtin_call(LowerContext* ctx, const AstExpr* expr, HirExpr* out, HirBuiltinKind builtin) {
    HirExpr* arg = 0;
    out->as.call.builtin = builtin;
    out->as.call.callee = 0;
    if (builtin == HIR_BUILTIN_PANIC) {
        if (expr->as.call.args.count != 0) return fail(ctx, "panic expects no arguments");
        out->type = primitive_type(ctx->program, HIR_TYPE_INT);
        return 1;
    }
    if (expr->as.call.args.count != 1) return fail(ctx, builtin == HIR_BUILTIN_ASSERT ? "assert expects exactly one argument" : "print expects exactly one argument");
    arg = lower_expr(ctx, expr->as.call.args.items[0]);
    if (!arg) return 0;
    if (builtin == HIR_BUILTIN_ASSERT) {
        if (arg->type->kind != HIR_TYPE_BOOL) return fail(ctx, "assert requires a Bool argument");
    } else if (arg->type->kind != HIR_TYPE_INT && arg->type->kind != HIR_TYPE_BOOL) {
        return fail(ctx, "print requires an Int or Bool argument");
    }
    expr_list_push(&out->as.call.args, arg);
    out->type = primitive_type(ctx->program, HIR_TYPE_INT);
    return 1;
}

static int lower_call_args(LowerContext* ctx, const AstExprList* ast_args, HirExprList* out_args, HirFunction* callee) {
    int i = 0;
    if (ast_args->count != callee->params.count) {
        return fail(ctx, "call argument count mismatch");
    }
    for (i = 0; i < ast_args->count; ++i) {
        HirExpr* arg = lower_expr(ctx, ast_args->items[i]);
        if (!arg) {
            return 0;
        }
        if (!type_equals(arg->type, callee->params.items[i]->type)) {
            return fail(ctx, "call argument type mismatch");
        }
        expr_list_push(out_args, arg);
    }
    return 1;
}

static HirUnionVariant* resolve_variant_expr(LowerContext* ctx, const AstExpr* expr, HirUnionDecl** out_union) {
    HirUnionVariant* variant = 0;
    HirUnionDecl* union_decl = 0;
    int i = 0;
    if (expr->as.variant.union_name) {
        union_decl = find_union(ctx->program, expr->as.variant.union_name);
        if (!union_decl) {
            fail(ctx, "unknown union");
            return 0;
        }
        variant = find_union_variant(union_decl, expr->as.variant.variant_name);
    } else {
        for (i = 0; i < ctx->program->unions.count; ++i) {
            HirUnionVariant* candidate = find_union_variant(&ctx->program->unions.items[i], expr->as.variant.variant_name);
            if (candidate) {
                union_decl = &ctx->program->unions.items[i];
                variant = candidate;
                break;
            }
        }
    }
    if (!variant || !union_decl) {
        fail(ctx, "unknown union variant");
        return 0;
    }
    if (out_union) {
        *out_union = union_decl;
    }
    return variant;
}

static int validate_struct_init_expr(LowerContext* ctx, HirStructDecl* struct_decl, const AstExpr* expr, int* field_state) {
    int i = 0;
    if (!expr) {
        return 1;
    }
    switch (expr->kind) {
        case AST_EXPR_NAME:
            if (strcmp(expr->as.name, "self") == 0) {
                return fail(ctx, "init self escape");
            }
            return 1;
        case AST_EXPR_FIELD:
            if (expr->as.field.base && expr->as.field.base->kind == AST_EXPR_NAME && strcmp(expr->as.field.base->as.name, "self") == 0) {
                int field_index = -1;
                if (!find_struct_field(struct_decl, expr->as.field.name, &field_index)) {
                    return fail(ctx, "unknown field");
                }
                if (!field_state[field_index]) {
                    return fail(ctx, "init read before field initialization");
                }
                return 1;
            }
            return validate_struct_init_expr(ctx, struct_decl, expr->as.field.base, field_state);
        case AST_EXPR_BINARY:
            return validate_struct_init_expr(ctx, struct_decl, expr->as.binary.left, field_state) &&
                   validate_struct_init_expr(ctx, struct_decl, expr->as.binary.right, field_state);
        case AST_EXPR_TERNARY:
            return validate_struct_init_expr(ctx, struct_decl, expr->as.ternary.cond, field_state) &&
                   validate_struct_init_expr(ctx, struct_decl, expr->as.ternary.then_expr, field_state) &&
                   validate_struct_init_expr(ctx, struct_decl, expr->as.ternary.else_expr, field_state);
        case AST_EXPR_CALL:
            for (i = 0; i < expr->as.call.args.count; ++i) {
                if (!validate_struct_init_expr(ctx, struct_decl, expr->as.call.args.items[i], field_state)) {
                    return 0;
                }
            }
            return 1;
        case AST_EXPR_VARIANT:
            return validate_struct_init_expr(ctx, struct_decl, expr->as.variant.payload, field_state);
        case AST_EXPR_STRUCT:
            for (i = 0; i < expr->as.struct_lit.fields.count; ++i) {
                if (!validate_struct_init_expr(ctx, struct_decl, expr->as.struct_lit.fields.items[i].value, field_state)) {
                    return 0;
                }
            }
            return 1;
        case AST_EXPR_TUPLE:
            for (i = 0; i < expr->as.tuple.items.count; ++i) {
                if (!validate_struct_init_expr(ctx, struct_decl, expr->as.tuple.items.items[i], field_state)) {
                    return 0;
                }
            }
            return 1;
        case AST_EXPR_ARRAY:
            for (i = 0; i < expr->as.array.items.count; ++i) {
                if (!validate_struct_init_expr(ctx, struct_decl, expr->as.array.items.items[i], field_state)) {
                    return 0;
                }
            }
            return 1;
        case AST_EXPR_INDEX:
            return validate_struct_init_expr(ctx, struct_decl, expr->as.index.base, field_state) &&
                   validate_struct_init_expr(ctx, struct_decl, expr->as.index.index, field_state);
        default:
            return 1;
    }
}

static int validate_struct_init_block(LowerContext* ctx, HirStructDecl* struct_decl, const AstBlock* block, int* field_state);

static int validate_struct_init_stmt(LowerContext* ctx, HirStructDecl* struct_decl, const AstStmt* stmt, int* field_state) {
    int i = 0;
    switch (stmt->kind) {
        case AST_STMT_RETURN:
            if (stmt->as.ret.expr) {
                return fail(ctx, "struct init must not return a value");
            }
            return 1;
        case AST_STMT_ASSIGN:
            if (stmt->as.assign.target->kind == AST_EXPR_FIELD &&
                stmt->as.assign.target->as.field.base &&
                stmt->as.assign.target->as.field.base->kind == AST_EXPR_NAME &&
                strcmp(stmt->as.assign.target->as.field.base->as.name, "self") == 0) {
                HirStructField* field = 0;
                int field_index = -1;
                if (!validate_struct_init_expr(ctx, struct_decl, stmt->as.assign.value, field_state)) {
                    return 0;
                }
                field = find_struct_field(struct_decl, stmt->as.assign.target->as.field.name, &field_index);
                if (!field) {
                    return fail(ctx, "unknown field");
                }
                if (field_state[field_index] && !field->mutable_flag) {
                    return fail(ctx, "immutable struct field reassigned in init");
                }
                field_state[field_index] = 1;
                return 1;
            }
            return validate_struct_init_expr(ctx, struct_decl, stmt->as.assign.target, field_state) &&
                   validate_struct_init_expr(ctx, struct_decl, stmt->as.assign.value, field_state);
        case AST_STMT_IF: {
            int* then_state = (int*)malloc((size_t)struct_decl->fields.count * sizeof(int));
            int* else_state = (int*)malloc((size_t)struct_decl->fields.count * sizeof(int));
            if (!validate_struct_init_expr(ctx, struct_decl, stmt->as.if_stmt.cond, field_state)) {
                free(then_state);
                free(else_state);
                return 0;
            }
            memcpy(then_state, field_state, (size_t)struct_decl->fields.count * sizeof(int));
            memcpy(else_state, field_state, (size_t)struct_decl->fields.count * sizeof(int));
            if (!validate_struct_init_block(ctx, struct_decl, &stmt->as.if_stmt.then_block, then_state)) {
                free(then_state);
                free(else_state);
                return 0;
            }
            if (stmt->as.if_stmt.has_else) {
                if (!validate_struct_init_block(ctx, struct_decl, &stmt->as.if_stmt.else_block, else_state)) {
                    free(then_state);
                    free(else_state);
                    return 0;
                }
                for (i = 0; i < struct_decl->fields.count; ++i) {
                    field_state[i] = then_state[i] && else_state[i];
                }
            }
            free(then_state);
            free(else_state);
            return 1;
        }
        case AST_STMT_EXPR:
            return validate_struct_init_expr(ctx, struct_decl, stmt->as.expr_stmt.expr, field_state);
        case AST_STMT_VAR_DECL:
            return validate_struct_init_expr(ctx, struct_decl, stmt->as.var_decl.init, field_state);
        default:
            return 1;
    }
}

static int validate_struct_init_block(LowerContext* ctx, HirStructDecl* struct_decl, const AstBlock* block, int* field_state) {
    int i = 0;
    for (i = 0; i < block->stmts.count; ++i) {
        if (!validate_struct_init_stmt(ctx, struct_decl, block->stmts.items[i], field_state)) {
            return 0;
        }
    }
    return 1;
}

static HirExpr* lower_expr(LowerContext* ctx, const AstExpr* expr) {
    HirExpr* out = 0;
    int i = 0;
    switch (expr->kind) {
        case AST_EXPR_INT:
            out = new_expr(HIR_EXPR_INT, primitive_type(ctx->program, HIR_TYPE_INT), expr->line);
            out->as.int_value = expr->as.int_value;
            return out;
        case AST_EXPR_BOOL:
            out = new_expr(HIR_EXPR_BOOL, primitive_type(ctx->program, HIR_TYPE_BOOL), expr->line);
            out->as.bool_value = expr->as.bool_value;
            return out;
        case AST_EXPR_NAME: {
            HirBinding* binding = lookup_binding(ctx, expr->as.name);
            if (!binding) {
                fail(ctx, "unknown identifier");
                return 0;
            }
            out = new_expr(HIR_EXPR_BINDING, binding->type, expr->line);
            out->as.binding = binding;
            return out;
        }
        case AST_EXPR_TERNARY: {
            HirExpr* cond = lower_expr(ctx, expr->as.ternary.cond);
            HirExpr* then_expr = 0;
            HirExpr* else_expr = 0;
            if (!cond) {
                return 0;
            }
            if (cond->type->kind != HIR_TYPE_BOOL) {
                fail(ctx, "ternary condition must be Bool");
                return 0;
            }
            then_expr = lower_expr(ctx, expr->as.ternary.then_expr);
            if (!then_expr) {
                return 0;
            }
            else_expr = lower_expr(ctx, expr->as.ternary.else_expr);
            if (!else_expr) {
                return 0;
            }
            if (!type_equals(then_expr->type, else_expr->type)) {
                fail(ctx, "ternary branch type mismatch");
                return 0;
            }
            if (then_expr->type->kind == HIR_TYPE_TUPLE ||
                then_expr->type->kind == HIR_TYPE_ARRAY ||
                then_expr->type->kind == HIR_TYPE_UNION ||
                then_expr->type->kind == HIR_TYPE_VOID) {
                fail(ctx, "ternary aggregate result unsupported");
                return 0;
            }
            out = new_expr(HIR_EXPR_TERNARY, then_expr->type, expr->line);
            out->as.ternary.cond = cond;
            out->as.ternary.then_expr = then_expr;
            out->as.ternary.else_expr = else_expr;
            return out;
        }
        case AST_EXPR_CALL: {
            HirBuiltinKind builtin = builtin_kind(expr->as.call.callee);
            if (builtin != HIR_BUILTIN_NONE) {
                out = new_expr(HIR_EXPR_CALL, primitive_type(ctx->program, HIR_TYPE_INT), expr->line);
                if (!lower_builtin_call(ctx, expr, out, builtin)) return 0;
                return out;
            }
            HirFunction* callee = find_function(ctx->program, expr->as.call.callee);
            if (!callee) {
                const char* dot = strchr(expr->as.call.callee, '.');
                if (dot) {
                    size_t prefix_len = (size_t)(dot - expr->as.call.callee);
                    if (strcmp(dot + 1, "init") == 0) {
                        char* type_name = (char*)malloc(prefix_len + 1);
                        HirStructDecl* struct_decl = 0;
                        if (!type_name) {
                            fail(ctx, "out of memory");
                            return 0;
                        }
                        memcpy(type_name, expr->as.call.callee, prefix_len);
                        type_name[prefix_len] = '\0';
                        struct_decl = find_struct(ctx->program, type_name);
                        free(type_name);
                        if (struct_decl && struct_decl->has_init) {
                            callee = find_function(ctx->program, struct_decl->init_name);
                        }
                    }
                } else {
                    HirStructDecl* struct_decl = find_struct(ctx->program, expr->as.call.callee);
                    if (struct_decl && struct_decl->has_init) {
                        callee = find_function(ctx->program, struct_decl->init_name);
                    }
                }
            }
            if (!callee) {
                fail(ctx, "unknown function");
                return 0;
            }
            out = new_expr(HIR_EXPR_CALL, callee->return_type, expr->line);
            out->as.call.callee = callee;
            out->as.call.builtin = HIR_BUILTIN_NONE;
            if (!lower_call_args(ctx, &expr->as.call.args, &out->as.call.args, callee)) {
                return 0;
            }
            return out;
        }
        case AST_EXPR_VARIANT: {
            HirEnumDecl* enum_decl = 0;
            HirEnumMember* enum_member = 0;
            HirUnionDecl* union_decl = 0;
            HirType* union_type = 0;
            if (!expr->as.variant.payload && expr->as.variant.bindings.count == 0) {
                enum_member = resolve_enum_expr(ctx, expr, &enum_decl);
                if (enum_member) {
                    HirType* enum_type = new_owned_type(ctx->program, HIR_TYPE_ENUM);
                    enum_type->enum_decl = enum_decl;
                    out = new_expr(HIR_EXPR_ENUM_MEMBER, enum_type, expr->line);
                    out->as.enum_member.member = enum_member;
                    return out;
                }
            }
            {
                HirUnionVariant* variant = resolve_variant_expr(ctx, expr, &union_decl);
                if (!variant) {
                    return 0;
                }
            if (!expr->as.variant.payload && expr->as.variant.bindings.count == 0) {
                return make_int_expr(ctx, variant->tag_value, expr->line);
            }
            if (expr->as.variant.pattern_flag) {
                fail(ctx, "variant pattern is not a value expression");
                return 0;
            }
            union_type = new_owned_type(ctx->program, HIR_TYPE_UNION);
            union_type->union_decl = union_decl;
            out = new_expr(HIR_EXPR_VARIANT, union_type, expr->line);
            out->as.variant.variant = variant;
            if (variant->payload_type->kind == HIR_TYPE_VOID) {
                if (expr->as.variant.payload) {
                    fail(ctx, "void variant does not accept a payload");
                    return 0;
                }
                return out;
            }
            if (!expr->as.variant.payload) {
                fail(ctx, "variant payload required");
                return 0;
            }
            out->as.variant.payload = lower_expr(ctx, expr->as.variant.payload);
            if (!out->as.variant.payload) {
                return 0;
            }
            if (!type_equals(out->as.variant.payload->type, variant->payload_type)) {
                fail(ctx, "variant payload type mismatch");
                return 0;
            }
            return out;
            }
        }
        case AST_EXPR_FIELD: {
            HirExpr* base = lower_expr(ctx, expr->as.field.base);
            if (!base) {
                return 0;
            }
            if (base->type->kind == HIR_TYPE_ENUM && strcmp(expr->as.field.name, "value") == 0) {
                out = new_expr(HIR_EXPR_ENUM_VALUE, primitive_type(ctx->program, HIR_TYPE_INT), expr->line);
                out->as.enum_value.value = base;
                return out;
            }
            if (base->type->kind == HIR_TYPE_UNION && strcmp(expr->as.field.name, "tag") == 0) {
                out = make_union_tag_expr(ctx, base, expr->line);
                return out;
            }
            if (base->type->kind == HIR_TYPE_UNION) {
                HirUnionVariant* variant = find_union_variant(base->type->union_decl, expr->as.field.name);
                if (variant && variant->payload_type->kind != HIR_TYPE_VOID && variant->payload_type->kind != HIR_TYPE_TUPLE) {
                    out = make_union_field_expr(ctx, base, variant, 0, variant->payload_type, expr->line);
                    return out;
                }
            }
            if (base->type->kind == HIR_TYPE_STRUCT) {
                int field_index = -1;
                HirStructField* field = find_struct_field(base->type->struct_decl, expr->as.field.name, &field_index);
                if (!field) {
                    fail(ctx, "unknown field");
                    return 0;
                }
                out = new_expr(HIR_EXPR_STRUCT_FIELD, field->type, expr->line);
                out->as.struct_field.base = base;
                out->as.struct_field.field = field;
                out->as.struct_field.field_index = field_index;
                return out;
            }
            fail(ctx, "unknown field");
            return 0;
        }
        case AST_EXPR_STRUCT: {
            HirStructDecl* struct_decl = find_struct(ctx->program, expr->as.struct_lit.type_name);
            HirType* struct_type = 0;
            int i = 0;
            int* seen = 0;
            if (!struct_decl) {
                fail(ctx, "unknown named type");
                return 0;
            }
            struct_type = new_owned_type(ctx->program, HIR_TYPE_STRUCT);
            struct_type->struct_decl = struct_decl;
            out = new_expr(HIR_EXPR_STRUCT, struct_type, expr->line);
            seen = (int*)calloc((size_t)struct_decl->fields.count, sizeof(int));
            if (!seen) {
                fail(ctx, "out of memory");
                return 0;
            }
            for (i = 0; i < expr->as.struct_lit.fields.count; ++i) {
                AstStructFieldInit* ast_field = &expr->as.struct_lit.fields.items[i];
                HirStructFieldInit field_init;
                HirStructField* field = 0;
                HirExpr* value = 0;
                int field_index = -1;
                memset(&field_init, 0, sizeof(field_init));
                field = find_struct_field(struct_decl, ast_field->name, &field_index);
                if (!field) {
                    free(seen);
                    fail(ctx, "unknown field");
                    return 0;
                }
                if (seen[field_index]) {
                    free(seen);
                    fail(ctx, "duplicate struct field initializer");
                    return 0;
                }
                value = lower_expr(ctx, ast_field->value);
                if (!value) {
                    free(seen);
                    return 0;
                }
                if (!type_equals(value->type, field->type)) {
                    free(seen);
                    fail(ctx, "struct field initializer type mismatch");
                    return 0;
                }
                seen[field_index] = 1;
                field_init.field = field;
                field_init.value = value;
                struct_field_init_list_push(&out->as.struct_lit.fields, field_init);
            }
            for (i = 0; i < struct_decl->fields.count; ++i) {
                if (!seen[i]) {
                    free(seen);
                    fail(ctx, "missing struct field initializer");
                    return 0;
                }
            }
            free(seen);
            return out;
        }
        case AST_EXPR_BINARY: {
            HirExpr* left = lower_expr(ctx, expr->as.binary.left);
            HirExpr* right = 0;
            if (!left) {
                return 0;
            }
            right = lower_expr(ctx, expr->as.binary.right);
            if (!right) {
                return 0;
            }
            out = new_expr(HIR_EXPR_BINARY, primitive_type(ctx->program, HIR_TYPE_INT), expr->line);
            out->as.binary.left = left;
            out->as.binary.right = right;
            switch (expr->as.binary.op) {
                case AST_BIN_ADD: out->as.binary.op = HIR_BIN_ADD; break;
                case AST_BIN_SUB: out->as.binary.op = HIR_BIN_SUB; break;
                case AST_BIN_MUL: out->as.binary.op = HIR_BIN_MUL; break;
                case AST_BIN_DIV: out->as.binary.op = HIR_BIN_DIV; break;
                case AST_BIN_EQ: out->as.binary.op = HIR_BIN_EQ; out->type = primitive_type(ctx->program, HIR_TYPE_BOOL); break;
                case AST_BIN_NE: out->as.binary.op = HIR_BIN_NE; out->type = primitive_type(ctx->program, HIR_TYPE_BOOL); break;
                case AST_BIN_LT: out->as.binary.op = HIR_BIN_LT; out->type = primitive_type(ctx->program, HIR_TYPE_BOOL); break;
                case AST_BIN_LE: out->as.binary.op = HIR_BIN_LE; out->type = primitive_type(ctx->program, HIR_TYPE_BOOL); break;
                case AST_BIN_GT: out->as.binary.op = HIR_BIN_GT; out->type = primitive_type(ctx->program, HIR_TYPE_BOOL); break;
                case AST_BIN_GE: out->as.binary.op = HIR_BIN_GE; out->type = primitive_type(ctx->program, HIR_TYPE_BOOL); break;
            }
            if (expr->as.binary.op == AST_BIN_ADD || expr->as.binary.op == AST_BIN_SUB || expr->as.binary.op == AST_BIN_MUL || expr->as.binary.op == AST_BIN_DIV) {
                if (left->type->kind != HIR_TYPE_INT || right->type->kind != HIR_TYPE_INT) {
                    fail(ctx, "arithmetic requires Int operands");
                    return 0;
                }
            } else {
                if (!type_equals(left->type, right->type)) {
                    fail(ctx, "comparison requires matching operand types");
                    return 0;
                }
                if (left->type->kind != HIR_TYPE_INT && left->type->kind != HIR_TYPE_BOOL && left->type->kind != HIR_TYPE_ENUM) {
                    fail(ctx, "comparison operand type unsupported");
                    return 0;
                }
            }
            return out;
        }
        case AST_EXPR_TUPLE: {
            HirType* tuple_type = new_owned_type(ctx->program, HIR_TYPE_TUPLE);
            out = new_expr(HIR_EXPR_TUPLE, tuple_type, expr->line);
            for (i = 0; i < expr->as.tuple.items.count; ++i) {
                HirExpr* item = lower_expr(ctx, expr->as.tuple.items.items[i]);
                if (!item) {
                    return 0;
                }
                expr_list_push(&out->as.tuple.items, item);
                type_list_push(&tuple_type->tuple_items, item->type);
            }
            return out;
        }
        case AST_EXPR_ARRAY: {
            HirType* array_type = new_owned_type(ctx->program, HIR_TYPE_ARRAY);
            out = new_expr(HIR_EXPR_ARRAY, array_type, expr->line);
            array_type->array_length = expr->as.array.items.count;
            for (i = 0; i < expr->as.array.items.count; ++i) {
                HirExpr* item = lower_expr(ctx, expr->as.array.items.items[i]);
                if (!item) {
                    return 0;
                }
                if (i == 0) {
                    array_type->array_item = item->type;
                } else if (!type_equals(array_type->array_item, item->type)) {
                    fail(ctx, "array literal items must have matching types");
                    return 0;
                }
                expr_list_push(&out->as.array.items, item);
            }
            if (!array_type->array_item) {
                fail(ctx, "empty array literal is not supported");
                return 0;
            }
            return out;
        }
        case AST_EXPR_INDEX: {
            HirExpr* base = lower_expr(ctx, expr->as.index.base);
            HirExpr* index = 0;
            if (!base) {
                return 0;
            }
            index = lower_expr(ctx, expr->as.index.index);
            if (!index) {
                return 0;
            }
            if (base->type->kind == HIR_TYPE_TUPLE) {
                if (expr->as.index.index->kind != AST_EXPR_INT) {
                    fail(ctx, "tuple index must be an integer literal");
                    return 0;
                }
                if (expr->as.index.index->as.int_value < 0 || expr->as.index.index->as.int_value >= base->type->tuple_items.count) {
                    fail(ctx, "tuple index out of bounds");
                    return 0;
                }
                out = new_expr(HIR_EXPR_INDEX, base->type->tuple_items.items[expr->as.index.index->as.int_value], expr->line);
            } else if (base->type->kind == HIR_TYPE_ARRAY) {
                if (index->type->kind != HIR_TYPE_INT) {
                    fail(ctx, "array index must be Int");
                    return 0;
                }
                out = new_expr(HIR_EXPR_INDEX, base->type->array_item, expr->line);
            } else {
                fail(ctx, "indexing currently requires a tuple or array base");
                return 0;
            }
            out->as.index.base = base;
            out->as.index.index = index;
            return out;
        }
    }
    fail(ctx, "unsupported expression kind");
    return 0;
}

static int lower_block(LowerContext* ctx, const AstBlock* ast_block, HirBlock* out_block);
static HirStmt* lower_stmt(LowerContext* ctx, const AstStmt* stmt);

static HirExpr* make_zero_expr(LowerContext* ctx, HirType* type, int line) {
    int i = 0;
    HirExpr* expr = 0;
    switch (type->kind) {
        case HIR_TYPE_INT:
            expr = new_expr(HIR_EXPR_INT, type, line);
            expr->as.int_value = 0;
            return expr;
        case HIR_TYPE_BOOL:
            expr = new_expr(HIR_EXPR_BOOL, type, line);
            expr->as.bool_value = 0;
            return expr;
        case HIR_TYPE_STRUCT:
            expr = new_expr(HIR_EXPR_STRUCT, type, line);
            for (i = 0; i < type->struct_decl->fields.count; ++i) {
                HirStructFieldInit field_init;
                memset(&field_init, 0, sizeof(field_init));
                field_init.field = &type->struct_decl->fields.items[i];
                field_init.value = make_zero_expr(ctx, type->struct_decl->fields.items[i].type, line);
                struct_field_init_list_push(&expr->as.struct_lit.fields, field_init);
            }
            return expr;
        case HIR_TYPE_TUPLE:
            expr = new_expr(HIR_EXPR_TUPLE, type, line);
            for (i = 0; i < type->tuple_items.count; ++i) {
                expr_list_push(&expr->as.tuple.items, make_zero_expr(ctx, type->tuple_items.items[i], line));
            }
            return expr;
        case HIR_TYPE_ARRAY:
            expr = new_expr(HIR_EXPR_ARRAY, type, line);
            for (i = 0; i < type->array_length; ++i) {
                expr_list_push(&expr->as.array.items, make_zero_expr(ctx, type->array_item, line));
            }
            return expr;
        case HIR_TYPE_UNION:
            return new_expr(HIR_EXPR_VARIANT, type, line);
        case HIR_TYPE_VOID:
            return 0;
        default:
            return new_expr(HIR_EXPR_INT, primitive_type(ctx->program, HIR_TYPE_INT), line);
    }
}

static int lower_init_block(LowerContext* ctx, const AstBlock* ast_block, HirBlock* out_block, HirBinding* self_binding) {
    int i = 0;
    for (i = 0; i < ast_block->stmts.count; ++i) {
        const AstStmt* ast_stmt = ast_block->stmts.items[i];
        HirStmt* stmt = 0;
        if (ast_stmt->kind == AST_STMT_RETURN) {
            stmt = new_stmt(HIR_STMT_RETURN, ast_stmt->line);
            if (ast_stmt->as.ret.expr) {
                return fail(ctx, "struct init must not return a value");
            }
            stmt->as.ret.expr = make_binding_expr(self_binding, ast_stmt->line);
            stmt_list_push(&out_block->stmts, stmt);
            continue;
        }
        if (ast_stmt->kind == AST_STMT_IF) {
            Scope then_scope;
            stmt = new_stmt(HIR_STMT_IF, ast_stmt->line);
            stmt->as.if_stmt.cond = lower_expr(ctx, ast_stmt->as.if_stmt.cond);
            if (!stmt->as.if_stmt.cond) {
                return 0;
            }
            if (stmt->as.if_stmt.cond->type->kind != HIR_TYPE_BOOL) {
                return fail(ctx, "if condition must be Bool");
            }
            push_scope(ctx, &then_scope);
            if (!lower_init_block(ctx, &ast_stmt->as.if_stmt.then_block, &stmt->as.if_stmt.then_block, self_binding)) {
                return 0;
            }
            pop_scope(ctx);
            if (ast_stmt->as.if_stmt.has_else) {
                Scope else_scope;
                stmt->as.if_stmt.has_else = 1;
                push_scope(ctx, &else_scope);
                if (!lower_init_block(ctx, &ast_stmt->as.if_stmt.else_block, &stmt->as.if_stmt.else_block, self_binding)) {
                    return 0;
                }
                pop_scope(ctx);
            }
            stmt_list_push(&out_block->stmts, stmt);
            continue;
        }
        stmt = lower_stmt(ctx, ast_stmt);
        if (!stmt) {
            return 0;
        }
        stmt_list_push(&out_block->stmts, stmt);
    }
    return 1;
}

static HirStmt* lower_stmt(LowerContext* ctx, const AstStmt* stmt) {
    HirStmt* out = new_stmt(stmt_kind_from_ast(stmt->kind), stmt->line);
    switch (stmt->kind) {
        case AST_STMT_RETURN:
            if (!stmt->as.ret.expr) {
                if (ctx->current_function->return_type->kind != HIR_TYPE_VOID) {
                    fail(ctx, "return type mismatch");
                    return 0;
                }
                out->as.ret.expr = 0;
            } else {
                out->as.ret.expr = lower_expr(ctx, stmt->as.ret.expr);
                if (!out->as.ret.expr) {
                    return 0;
                }
                if (!type_equals(out->as.ret.expr->type, ctx->current_function->return_type)) {
                    fail(ctx, "return type mismatch");
                    return 0;
                }
            }
            return out;
        case AST_STMT_VAR_DECL: {
            HirBinding* binding = 0;
            HirExpr* init = lower_expr(ctx, stmt->as.var_decl.init);
            HirType* type = primitive_type(ctx->program, HIR_TYPE_INT);
            if (!init) {
                return 0;
            }
            if (stmt->as.var_decl.type.kind == AST_TYPE_INFER) {
                type = init->type;
            } else {
                type = lower_type(ctx, &stmt->as.var_decl.type);
                if (!type_equals(init->type, type) && !apply_array_length_inference(type, init->type)) {
                    fail(ctx, "variable initializer type mismatch");
                    return 0;
                }
            }
            binding = new_binding(type, stmt->as.var_decl.name, HIR_BINDING_LOCAL, stmt->line);
            if (!bind_in_current_scope(ctx, binding)) {
                return 0;
            }
            binding_list_push(&ctx->current_function->locals, binding);
            out->as.var_decl.binding = binding;
            out->as.var_decl.init = init;
            return out;
        }
        case AST_STMT_ASSIGN: {
            out->as.assign.target = lower_expr(ctx, stmt->as.assign.target);
            if (!out->as.assign.target) {
                return 0;
            }
            if (out->as.assign.target->kind == HIR_EXPR_BINDING) {
                out->as.assign.binding = out->as.assign.target->as.binding;
            } else if (out->as.assign.target->kind == HIR_EXPR_INDEX) {
                if (out->as.assign.target->as.index.base->type->kind != HIR_TYPE_ARRAY) {
                    fail(ctx, "assignment target not found");
                    return 0;
                }
            } else if (out->as.assign.target->kind != HIR_EXPR_STRUCT_FIELD) {
                fail(ctx, "assignment target not found");
                return 0;
            }
            out->as.assign.value = lower_expr(ctx, stmt->as.assign.value);
            if (!out->as.assign.value) {
                return 0;
            }
            if (!type_equals(out->as.assign.value->type, out->as.assign.target->type)) {
                fail(ctx, "assignment type mismatch");
                return 0;
            }
            return out;
        }
        case AST_STMT_IF: {
            Scope inner;
            if (stmt->as.if_stmt.cond &&
                stmt->as.if_stmt.cond->kind == AST_EXPR_BINARY &&
                stmt->as.if_stmt.cond->as.binary.op == AST_BIN_EQ &&
                stmt->as.if_stmt.cond->as.binary.right &&
                stmt->as.if_stmt.cond->as.binary.right->kind == AST_EXPR_VARIANT &&
                stmt->as.if_stmt.cond->as.binary.right->as.variant.pattern_flag) {
                HirExpr* value = lower_expr(ctx, stmt->as.if_stmt.cond->as.binary.left);
                if (!value) {
                    return 0;
                }
                push_scope(ctx, &inner);
                if (!lower_variant_pattern_bind(ctx, value, stmt->as.if_stmt.cond->as.binary.right, &out->as.if_stmt.then_block, &out->as.if_stmt.cond)) {
                    return 0;
                }
                if (!lower_block(ctx, &stmt->as.if_stmt.then_block, &out->as.if_stmt.then_block)) {
                    return 0;
                }
                pop_scope(ctx);
            } else {
                out->as.if_stmt.cond = lower_expr(ctx, stmt->as.if_stmt.cond);
                if (!out->as.if_stmt.cond) {
                    return 0;
                }
                if (out->as.if_stmt.cond->type->kind != HIR_TYPE_BOOL) {
                    fail(ctx, "if condition must be Bool");
                    return 0;
                }
                push_scope(ctx, &inner);
                if (!lower_block(ctx, &stmt->as.if_stmt.then_block, &out->as.if_stmt.then_block)) {
                    return 0;
                }
                pop_scope(ctx);
            }
            if (stmt->as.if_stmt.has_else) {
                Scope else_scope;
                out->as.if_stmt.has_else = 1;
                push_scope(ctx, &else_scope);
                if (!lower_block(ctx, &stmt->as.if_stmt.else_block, &out->as.if_stmt.else_block)) {
                    return 0;
                }
                pop_scope(ctx);
            }
            return out;
        }
        case AST_STMT_WHILE: {
            Scope inner;
            out->as.while_stmt.cond = lower_expr(ctx, stmt->as.while_stmt.cond);
            if (!out->as.while_stmt.cond) {
                return 0;
            }
            if (out->as.while_stmt.cond->type->kind != HIR_TYPE_BOOL) {
                fail(ctx, "while condition must be Bool");
                return 0;
            }
            ctx->loop_depth += 1;
            push_scope(ctx, &inner);
            if (!lower_block(ctx, &stmt->as.while_stmt.body, &out->as.while_stmt.body)) {
                return 0;
            }
            pop_scope(ctx);
            ctx->loop_depth -= 1;
            return out;
        }
        case AST_STMT_FOR_RANGE: {
            Scope inner;
            HirBinding* binding = 0;
            HirExpr* start = lower_expr(ctx, stmt->as.for_range.start);
            HirExpr* end = 0;
            HirType* type = stmt->as.for_range.type.kind == AST_TYPE_INFER
                ? primitive_type(ctx->program, HIR_TYPE_INT)
                : lower_type(ctx, &stmt->as.for_range.type);
            if (!start) {
                return 0;
            }
            end = lower_expr(ctx, stmt->as.for_range.end);
            if (!end) {
                return 0;
            }
            if (type->kind != HIR_TYPE_INT || start->type->kind != HIR_TYPE_INT || end->type->kind != HIR_TYPE_INT) {
                fail(ctx, "for-range currently requires Int bounds");
                return 0;
            }
            binding = new_binding(primitive_type(ctx->program, HIR_TYPE_INT), stmt->as.for_range.name, HIR_BINDING_LOCAL, stmt->line);
            binding_list_push(&ctx->current_function->locals, binding);
            out->as.for_range.binding = binding;
            out->as.for_range.start = start;
            out->as.for_range.end = end;
            ctx->loop_depth += 1;
            push_scope(ctx, &inner);
            if (!bind_in_current_scope(ctx, binding)) {
                return 0;
            }
            if (!lower_block(ctx, &stmt->as.for_range.body, &out->as.for_range.body)) {
                return 0;
            }
            pop_scope(ctx);
            ctx->loop_depth -= 1;
            return out;
        }
        case AST_STMT_BREAK:
            if (ctx->loop_depth <= 0) {
                fail(ctx, "break used outside loop");
                return 0;
            }
            return out;
        case AST_STMT_CONTINUE:
            if (ctx->loop_depth <= 0) {
                fail(ctx, "continue used outside loop");
                return 0;
            }
            return out;
        case AST_STMT_EXPR:
            out->as.expr_stmt.expr = lower_expr(ctx, stmt->as.expr_stmt.expr);
            if (!out->as.expr_stmt.expr) {
                return 0;
            }
            return out;
        default:
            fail(ctx, "unsupported statement kind");
            return 0;
    }
}

static int lower_destructure_stmt(LowerContext* ctx, const AstStmt* stmt, HirBlock* out_block) {
    HirExpr* init = 0;
    int i = 0;
    if (stmt->as.destructure.bindings.count <= 0) {
        return fail(ctx, "empty destructure is not supported");
    }
    init = lower_expr(ctx, stmt->as.destructure.init);
    if (!init) {
        return 0;
    }
    if (stmt->as.destructure.bindings.count == 1) {
        HirBinding* binding = 0;
        HirStmt* decl = 0;
        HirType* type = 0;
        if (stmt->as.destructure.bindings.items[0].type.kind == AST_TYPE_INFER) {
            type = init->type;
        } else {
            type = lower_type(ctx, &stmt->as.destructure.bindings.items[0].type);
            if (!type_equals(type, init->type)) {
                return fail(ctx, "destructure binding type mismatch");
            }
        }
        binding = new_binding(type, stmt->as.destructure.bindings.items[0].name, HIR_BINDING_LOCAL, stmt->as.destructure.bindings.items[0].line);
        if (!bind_in_current_scope(ctx, binding)) {
            return 0;
        }
        binding_list_push(&ctx->current_function->locals, binding);
        decl = new_stmt(HIR_STMT_VAR_DECL, stmt->line);
        decl->as.var_decl.binding = binding;
        decl->as.var_decl.init = init;
        stmt_list_push(&out_block->stmts, decl);
        return 1;
    }
    if (init->type->kind != HIR_TYPE_TUPLE) {
        return fail(ctx, "destructure requires a tuple initializer");
    }
    if (init->type->tuple_items.count != stmt->as.destructure.bindings.count) {
        return fail(ctx, "destructure arity mismatch");
    }
    {
        char* temp_name = make_temp_name(ctx);
        HirBinding* temp_binding = new_binding(init->type, temp_name, HIR_BINDING_LOCAL, stmt->line);
        HirStmt* temp_decl = new_stmt(HIR_STMT_VAR_DECL, stmt->line);
        binding_list_push(&ctx->current_function->locals, temp_binding);
        temp_decl->as.var_decl.binding = temp_binding;
        temp_decl->as.var_decl.init = init;
        stmt_list_push(&out_block->stmts, temp_decl);
        for (i = 0; i < stmt->as.destructure.bindings.count; ++i) {
            HirBinding* binding = 0;
            HirStmt* decl = 0;
            HirExpr* item = 0;
            HirType* item_type = init->type->tuple_items.items[i];
            HirType* binding_type = 0;
            if (stmt->as.destructure.bindings.items[i].type.kind == AST_TYPE_INFER) {
                binding_type = item_type;
            } else {
                binding_type = lower_type(ctx, &stmt->as.destructure.bindings.items[i].type);
                if (!type_equals(binding_type, item_type)) {
                    return fail(ctx, "destructure binding type mismatch");
                }
            }
            binding = new_binding(binding_type, stmt->as.destructure.bindings.items[i].name, HIR_BINDING_LOCAL, stmt->as.destructure.bindings.items[i].line);
            if (!bind_in_current_scope(ctx, binding)) {
                return 0;
            }
            binding_list_push(&ctx->current_function->locals, binding);
            item = new_expr(HIR_EXPR_INDEX, item_type, stmt->line);
            item->as.index.base = new_expr(HIR_EXPR_BINDING, temp_binding->type, stmt->line);
            item->as.index.base->as.binding = temp_binding;
            item->as.index.index = make_int_expr(ctx, i, stmt->line);
            decl = new_stmt(HIR_STMT_VAR_DECL, stmt->line);
            decl->as.var_decl.binding = binding;
            decl->as.var_decl.init = item;
            stmt_list_push(&out_block->stmts, decl);
        }
    }
    return 1;
}

static HirExpr* make_union_tag_expr(LowerContext* ctx, HirExpr* value, int line) {
    HirExpr* expr = new_expr(HIR_EXPR_UNION_TAG, primitive_type(ctx->program, HIR_TYPE_INT), line);
    expr->as.union_tag.value = value;
    return expr;
}

static HirExpr* make_union_field_expr(LowerContext* ctx, HirExpr* value, HirUnionVariant* variant, int field_index, HirType* type, int line) {
    HirExpr* expr = new_expr(HIR_EXPR_UNION_FIELD, type, line);
    expr->as.union_field.value = value;
    expr->as.union_field.variant = variant;
    expr->as.union_field.field_index = field_index;
    return expr;
}

static int lower_variant_pattern_bind(LowerContext* ctx, HirExpr* value, const AstExpr* pattern, HirBlock* out_block, HirExpr** cond_out) {
    HirUnionDecl* union_decl = 0;
    HirUnionVariant* variant = resolve_variant_expr(ctx, pattern, &union_decl);
    HirExpr* cond = 0;
    int i = 0;
    if (!variant) {
        return 0;
    }
    if (value->type->kind != HIR_TYPE_UNION || value->type->union_decl != union_decl) {
        return fail(ctx, "union pattern type mismatch");
    }
    cond = new_expr(HIR_EXPR_BINARY, primitive_type(ctx->program, HIR_TYPE_BOOL), pattern->line);
    cond->as.binary.op = HIR_BIN_EQ;
    cond->as.binary.left = make_union_tag_expr(ctx, value, pattern->line);
    cond->as.binary.right = make_int_expr(ctx, variant->tag_value, pattern->line);
    *cond_out = cond;
    if (variant->payload_type->kind == HIR_TYPE_VOID) {
        if (pattern->as.variant.bindings.count != 0) {
            return fail(ctx, "void variant pattern must not bind payload");
        }
        return 1;
    }
    if (variant->payload_type->kind == HIR_TYPE_TUPLE) {
        if (pattern->as.variant.bindings.count != variant->payload_type->tuple_items.count) {
            return fail(ctx, "tuple variant pattern arity mismatch");
        }
        for (i = 0; i < pattern->as.variant.bindings.count; ++i) {
            HirExpr* field = make_union_field_expr(ctx, value, variant, i, variant->payload_type->tuple_items.items[i], pattern->line);
            if (!lower_pattern_bind(ctx, pattern->as.variant.bindings.items[i], field, out_block)) {
                return 0;
            }
        }
        return 1;
    }
    if (pattern->as.variant.bindings.count == 0) {
        return 1;
    }
    if (pattern->as.variant.bindings.count != 1) {
        return fail(ctx, "non-tuple variant pattern arity mismatch");
    }
    return lower_pattern_bind(ctx, pattern->as.variant.bindings.items[0], make_union_field_expr(ctx, value, variant, 0, variant->payload_type, pattern->line), out_block);
}

static int validate_switch_cases(LowerContext* ctx, HirExpr* value, const AstStmt* stmt) {
    int i = 0;
    int have_else = 0;
    if (value->type->kind == HIR_TYPE_ENUM) {
        int member_count = value->type->enum_decl->members.count;
        int* seen = (int*)calloc((size_t)member_count, sizeof(int));
        int seen_count = 0;
        if (!seen) {
            return fail(ctx, "out of memory");
        }
        for (i = 0; i < stmt->as.switch_stmt.cases.count; ++i) {
            const AstSwitchCase* ast_case = &stmt->as.switch_stmt.cases.items[i];
            if (ast_case->is_else) {
                if (have_else) {
                    free(seen);
                    return fail(ctx, "duplicate else case");
                }
                have_else = 1;
                continue;
            }
            {
                HirExpr* case_value = lower_expr(ctx, ast_case->pattern);
                int j = 0;
                if (!case_value) {
                    free(seen);
                    return 0;
                }
                if (!type_equals(value->type, case_value->type)) {
                    free(seen);
                    return fail(ctx, "switch case type mismatch");
                }
                if (case_value->kind == HIR_EXPR_ENUM_MEMBER) {
                    for (j = 0; j < member_count; ++j) {
                        if (&value->type->enum_decl->members.items[j] == case_value->as.enum_member.member) {
                            if (seen[j]) {
                                free(seen);
                                return fail(ctx, "duplicate switch case");
                            }
                            seen[j] = 1;
                            seen_count += 1;
                            break;
                        }
                    }
                }
            }
        }
        if (!have_else && seen_count < member_count) {
            free(seen);
            return fail(ctx, "non-exhaustive enum switch");
        }
        free(seen);
        return 1;
    }
    if (value->type->kind == HIR_TYPE_UNION) {
        int variant_count = value->type->union_decl->variants.count;
        int* seen = (int*)calloc((size_t)variant_count, sizeof(int));
        int seen_count = 0;
        if (!seen) {
            return fail(ctx, "out of memory");
        }
        for (i = 0; i < stmt->as.switch_stmt.cases.count; ++i) {
            const AstSwitchCase* ast_case = &stmt->as.switch_stmt.cases.items[i];
            if (ast_case->is_else) {
                if (have_else) {
                    free(seen);
                    return fail(ctx, "duplicate else case");
                }
                have_else = 1;
                continue;
            }
            if (ast_case->pattern->kind != AST_EXPR_VARIANT) {
                free(seen);
                return fail(ctx, "switch case type mismatch");
            }
            {
                HirUnionDecl* union_decl = 0;
                HirUnionVariant* variant = resolve_variant_expr(ctx, ast_case->pattern, &union_decl);
                if (!variant) {
                    free(seen);
                    return 0;
                }
                if (union_decl != value->type->union_decl) {
                    free(seen);
                    return fail(ctx, "switch case type mismatch");
                }
                if (seen[variant->tag_value]) {
                    free(seen);
                    return fail(ctx, "duplicate switch case");
                }
                seen[variant->tag_value] = 1;
                seen_count += 1;
            }
        }
        if (!have_else && seen_count < variant_count) {
            free(seen);
            return fail(ctx, "non-exhaustive union switch");
        }
        free(seen);
        return 1;
    }
    for (i = 0; i < stmt->as.switch_stmt.cases.count; ++i) {
        const AstSwitchCase* ast_case = &stmt->as.switch_stmt.cases.items[i];
        if (ast_case->is_else) {
            if (have_else) {
                return fail(ctx, "duplicate else case");
            }
            have_else = 1;
            continue;
        }
        {
            HirExpr* case_value = lower_expr(ctx, ast_case->pattern);
            if (!case_value) {
                return 0;
            }
            if (!type_equals(value->type, case_value->type)) {
                return fail(ctx, "switch case type mismatch");
            }
        }
    }
    return 1;
}

static int lower_switch_stmt(LowerContext* ctx, const AstStmt* stmt, HirBlock* out_block) {
    HirExpr* value = lower_expr(ctx, stmt->as.switch_stmt.value);
    int i = stmt->as.switch_stmt.cases.count - 1;
    HirBlock else_block;
    int have_else = 0;
    memset(&else_block, 0, sizeof(else_block));
    if (!value) {
        return 0;
    }
    if (!validate_switch_cases(ctx, value, stmt)) {
        return 0;
    }
    for (; i >= 0; --i) {
        const AstSwitchCase* ast_case = &stmt->as.switch_stmt.cases.items[i];
        if (ast_case->is_else) {
            if (!lower_block(ctx, &ast_case->body, &else_block)) {
                return 0;
            }
            have_else = 1;
            continue;
        }
        {
            HirStmt* if_stmt = new_stmt(HIR_STMT_IF, stmt->line);
            Scope then_scope;
            Scope else_scope;
            push_scope(ctx, &then_scope);
            if (value->type->kind == HIR_TYPE_UNION && ast_case->pattern->kind == AST_EXPR_VARIANT) {
                if (!lower_variant_pattern_bind(ctx, value, ast_case->pattern, &if_stmt->as.if_stmt.then_block, &if_stmt->as.if_stmt.cond)) {
                    return 0;
                }
            } else {
                HirExpr* case_value = lower_expr(ctx, ast_case->pattern);
                if (!case_value) {
                    return 0;
                }
                if (!type_equals(value->type, case_value->type)) {
                    return fail(ctx, "switch case type mismatch");
                }
                if_stmt->as.if_stmt.cond = new_expr(HIR_EXPR_BINARY, primitive_type(ctx->program, HIR_TYPE_BOOL), ast_case->pattern->line);
                if_stmt->as.if_stmt.cond->as.binary.op = HIR_BIN_EQ;
                if_stmt->as.if_stmt.cond->as.binary.left = value;
                if_stmt->as.if_stmt.cond->as.binary.right = case_value;
            }
            if (!lower_block(ctx, &ast_case->body, &if_stmt->as.if_stmt.then_block)) {
                return 0;
            }
            pop_scope(ctx);
            if (have_else) {
                if_stmt->as.if_stmt.has_else = 1;
                push_scope(ctx, &else_scope);
                if_stmt->as.if_stmt.else_block = else_block;
                pop_scope(ctx);
            }
            memset(&else_block, 0, sizeof(else_block));
            stmt_list_push(&else_block.stmts, if_stmt);
            have_else = 1;
        }
    }
    if (have_else) {
        int j = 0;
        for (j = 0; j < else_block.stmts.count; ++j) {
            stmt_list_push(&out_block->stmts, else_block.stmts.items[j]);
        }
    }
    return 1;
}

static int lower_pattern_bind(LowerContext* ctx, const AstBindingPattern* pattern, HirExpr* init, HirBlock* out_block) {
    int i = 0;
    if (pattern->kind == AST_BINDING_NAME) {
        HirBinding* binding = 0;
        HirStmt* decl = 0;
        HirType* type = 0;
        if (pattern->type.kind == AST_TYPE_INFER) {
            type = init->type;
        } else {
            type = lower_type(ctx, &pattern->type);
            if (!type_equals(type, init->type)) {
                return fail(ctx, "binding type mismatch");
            }
        }
        binding = new_binding(type, pattern->name, HIR_BINDING_LOCAL, pattern->line);
        if (!bind_in_current_scope(ctx, binding)) {
            return 0;
        }
        binding_list_push(&ctx->current_function->locals, binding);
        decl = new_stmt(HIR_STMT_VAR_DECL, pattern->line);
        decl->as.var_decl.binding = binding;
        decl->as.var_decl.init = init;
        stmt_list_push(&out_block->stmts, decl);
        return 1;
    }
    if (init->type->kind != HIR_TYPE_TUPLE) {
        return fail(ctx, "tuple binding requires a tuple value");
    }
    if (init->type->tuple_items.count != pattern->items.count) {
        return fail(ctx, "tuple binding arity mismatch");
    }
    for (i = 0; i < pattern->items.count; ++i) {
        HirExpr* item = new_expr(HIR_EXPR_INDEX, init->type->tuple_items.items[i], pattern->line);
        item->as.index.base = init;
        item->as.index.index = make_int_expr(ctx, i, pattern->line);
        if (!lower_pattern_bind(ctx, pattern->items.items[i], item, out_block)) {
            return 0;
        }
    }
    return 1;
}

static int lower_for_each_stmt(LowerContext* ctx, const AstStmt* stmt, HirBlock* out_block) {
    HirExpr* iterable = lower_expr(ctx, stmt->as.for_each.iterable);
    HirBinding* iterable_binding = 0;
    HirStmt* iterable_decl = 0;
    HirStmt* loop_stmt = 0;
    Scope inner;
    char* iterable_name = 0;
    char* index_name = 0;
    HirBinding* index_binding = 0;
    HirExpr* item_expr = 0;
    if (!iterable) {
        return 0;
    }
    if (iterable->type->kind != HIR_TYPE_ARRAY) {
        return fail(ctx, "for-each currently requires an array iterable");
    }
    iterable_name = make_temp_name(ctx);
    iterable_binding = new_binding(iterable->type, iterable_name, HIR_BINDING_LOCAL, stmt->line);
    binding_list_push(&ctx->current_function->locals, iterable_binding);
    iterable_decl = new_stmt(HIR_STMT_VAR_DECL, stmt->line);
    iterable_decl->as.var_decl.binding = iterable_binding;
    iterable_decl->as.var_decl.init = iterable;
    stmt_list_push(&out_block->stmts, iterable_decl);

    index_name = make_temp_name(ctx);
    index_binding = new_binding(primitive_type(ctx->program, HIR_TYPE_INT), index_name, HIR_BINDING_LOCAL, stmt->line);
    binding_list_push(&ctx->current_function->locals, index_binding);
    loop_stmt = new_stmt(HIR_STMT_FOR_RANGE, stmt->line);
    loop_stmt->as.for_range.binding = index_binding;
    loop_stmt->as.for_range.start = make_int_expr(ctx, 0, stmt->line);
    loop_stmt->as.for_range.end = make_int_expr(ctx, iterable->type->array_length, stmt->line);

    push_scope(ctx, &inner);
    item_expr = new_expr(HIR_EXPR_INDEX, iterable->type->array_item, stmt->line);
    item_expr->as.index.base = make_binding_expr(iterable_binding, stmt->line);
    item_expr->as.index.index = make_binding_expr(index_binding, stmt->line);
    if (stmt->as.for_each.indexed_flag) {
        HirType* pair_type = new_owned_type(ctx->program, HIR_TYPE_TUPLE);
        HirExpr* pair_expr = new_expr(HIR_EXPR_TUPLE, pair_type, stmt->line);
        expr_list_push(&pair_expr->as.tuple.items, make_binding_expr(index_binding, stmt->line));
        type_list_push(&pair_type->tuple_items, primitive_type(ctx->program, HIR_TYPE_INT));
        expr_list_push(&pair_expr->as.tuple.items, item_expr);
        type_list_push(&pair_type->tuple_items, iterable->type->array_item);
        if (!lower_pattern_bind(ctx, stmt->as.for_each.pattern, pair_expr, &loop_stmt->as.for_range.body)) {
            pop_scope(ctx);
            return 0;
        }
    } else {
        if (!lower_pattern_bind(ctx, stmt->as.for_each.pattern, item_expr, &loop_stmt->as.for_range.body)) {
            pop_scope(ctx);
            return 0;
        }
    }
    if (!lower_block(ctx, &stmt->as.for_each.body, &loop_stmt->as.for_range.body)) {
        pop_scope(ctx);
        return 0;
    }
    pop_scope(ctx);
    stmt_list_push(&out_block->stmts, loop_stmt);
    return 1;
}

static int lower_block(LowerContext* ctx, const AstBlock* ast_block, HirBlock* out_block) {
    int i = 0;
    for (i = 0; i < ast_block->stmts.count; ++i) {
        HirStmt* stmt = 0;
        if (ast_block->stmts.items[i]->kind == AST_STMT_DESTRUCTURE) {
            if (!lower_destructure_stmt(ctx, ast_block->stmts.items[i], out_block)) {
                return 0;
            }
            continue;
        }
        if (ast_block->stmts.items[i]->kind == AST_STMT_FOR_EACH) {
            if (!lower_for_each_stmt(ctx, ast_block->stmts.items[i], out_block)) {
                return 0;
            }
            continue;
        }
        if (ast_block->stmts.items[i]->kind == AST_STMT_SWITCH) {
            if (!lower_switch_stmt(ctx, ast_block->stmts.items[i], out_block)) {
                return 0;
            }
            continue;
        }
        stmt = lower_stmt(ctx, ast_block->stmts.items[i]);
        if (!stmt) {
            return 0;
        }
        stmt_list_push(&out_block->stmts, stmt);
    }
    return 1;
}

static int register_globals(LowerContext* ctx) {
    int i = 0;
    for (i = 0; i < ctx->ast->globals.count; ++i) {
        const AstGlobal* ast_global = &ctx->ast->globals.items[i];
        HirGlobal hir_global;
        HirType* type = primitive_type(ctx->program, HIR_TYPE_VOID);
        memset(&hir_global, 0, sizeof(hir_global));
        if (hashmap_contains(&ctx->program->global_map, ast_global->name)) {
            return fail(ctx, "duplicate global");
        }
        if (ast_global->type.kind != AST_TYPE_INFER) {
            type = lower_type(ctx, &ast_global->type);
        }
        hir_global.binding = new_binding(type, ast_global->name, HIR_BINDING_GLOBAL, ast_global->line);
        hir_global.line = ast_global->line;
        global_list_push(&ctx->program->globals, hir_global);
        hashmap_set(&ctx->program->global_map, hir_global.binding->name, (void*)1);
    }
    rebuild_global_map(ctx->program);
    return 1;
}

static int register_enums(LowerContext* ctx) {
    int i = 0;
    for (i = 0; i < ctx->ast->enums.count; ++i) {
        HirEnumDecl enum_decl;
        int next_value = 0;
        int j = 0;
        if (hashmap_contains(&ctx->program->type_name_map, ctx->ast->enums.items[i].name)) {
            return fail(ctx, "duplicate type name");
        }
        memset(&enum_decl, 0, sizeof(enum_decl));
        enum_decl.name = ctx->ast->enums.items[i].name;
        for (j = 0; j < ctx->ast->enums.items[i].members.count; ++j) {
            HirEnumMember member;
            memset(&member, 0, sizeof(member));
            if (hashmap_contains(&ctx->program->enum_member_map, ctx->ast->enums.items[i].members.items[j].name)) {
                return fail(ctx, "duplicate enum member");
            }
            member.name = ctx->ast->enums.items[i].members.items[j].name;
            if (ctx->ast->enums.items[i].members.items[j].has_value) {
                member.value = ctx->ast->enums.items[i].members.items[j].value;
                next_value = (int)member.value + 1;
            } else {
                member.value = next_value++;
            }
            enum_member_list_push(&enum_decl.members, member);
            hashmap_set(&ctx->program->enum_member_map, member.name, (void*)1);
        }
        enum_list_push(&ctx->program->enums, enum_decl);
        hashmap_set(&ctx->program->type_name_map, ctx->ast->enums.items[i].name, (void*)1);
    }
    rebuild_enum_maps(ctx->program);
    return 1;
}

static int register_structs(LowerContext* ctx) {
    int i = 0;
    for (i = 0; i < ctx->ast->structs.count; ++i) {
        const AstStructDecl* ast_struct = &ctx->ast->structs.items[i];
        HirStructDecl struct_decl;
        memset(&struct_decl, 0, sizeof(struct_decl));
        if (hashmap_contains(&ctx->program->type_name_map, ast_struct->name)) {
            return fail(ctx, "duplicate type name");
        }
        struct_decl.name = ast_struct->name;
        struct_decl.has_init = ast_struct->has_init;
        if (ast_struct->has_init) {
            struct_decl.init_name = make_struct_init_name(ast_struct->name);
        }
        struct_list_push(&ctx->program->structs, struct_decl);
        hashmap_set(&ctx->program->type_name_map, ast_struct->name, (void*)1);
    }
    rebuild_struct_map(ctx->program);
    for (i = 0; i < ctx->ast->structs.count; ++i) {
        const AstStructDecl* ast_struct = &ctx->ast->structs.items[i];
        HirStructDecl* struct_decl = &ctx->program->structs.items[i];
        int j = 0;
        for (j = 0; j < ast_struct->fields.count; ++j) {
            HirStructField field;
            int field_index = -1;
            memset(&field, 0, sizeof(field));
            if (find_struct_field(struct_decl, ast_struct->fields.items[j].name, &field_index)) {
                return fail(ctx, "duplicate field declaration");
            }
            field.name = ast_struct->fields.items[j].name;
            field.type = lower_type(ctx, &ast_struct->fields.items[j].type);
            field.mutable_flag = ast_struct->fields.items[j].type.mutable_flag;
            struct_field_list_push(&struct_decl->fields, field);
        }
    }
    return 1;
}

static int register_unions(LowerContext* ctx) {
    int i = 0;
    for (i = 0; i < ctx->ast->unions.count; ++i) {
        HirUnionDecl union_decl;
        const AstUnionDecl* ast_union = &ctx->ast->unions.items[i];
        int j = 0;
        memset(&union_decl, 0, sizeof(union_decl));
        if (ast_union->tag_name && !hashmap_contains(&ctx->program->type_name_map, ast_union->tag_name)) {
            return fail(ctx, "unknown union tag enum");
        }
        if (hashmap_contains(&ctx->program->union_name_map, ast_union->name) || hashmap_contains(&ctx->program->type_name_map, ast_union->name)) {
            return fail(ctx, "duplicate union name");
        }
        union_decl.name = ast_union->name;
        union_decl.tag_name = ast_union->tag_name;
        for (j = 0; j < ast_union->variants.count; ++j) {
            HirUnionVariant variant;
            memset(&variant, 0, sizeof(variant));
            variant.name = ast_union->variants.items[j].name;
            variant.tag_value = j;
            variant.payload_type = lower_type(ctx, &ast_union->variants.items[j].type);
            variant.payload_slots = payload_slot_count(variant.payload_type);
            if (variant.payload_slots < 0) {
                return fail(ctx, "unsupported union payload type");
            }
            if (variant.payload_slots > union_decl.payload_slots) {
                union_decl.payload_slots = variant.payload_slots;
            }
            union_variant_list_push(&union_decl.variants, variant);
        }
        union_list_push(&ctx->program->unions, union_decl);
        hashmap_set(&ctx->program->union_name_map, union_decl.name, (void*)1);
        hashmap_set(&ctx->program->type_name_map, union_decl.name, (void*)1);
    }
    hashmap_free(&ctx->program->union_name_map);
    hashmap_init(&ctx->program->union_name_map);
    hashmap_free(&ctx->program->variant_map);
    hashmap_init(&ctx->program->variant_map);
    for (i = 0; i < ctx->program->unions.count; ++i) {
        int j = 0;
        hashmap_set(&ctx->program->union_name_map, ctx->program->unions.items[i].name, &ctx->program->unions.items[i]);
        for (j = 0; j < ctx->program->unions.items[i].variants.count; ++j) {
            hashmap_set(&ctx->program->variant_map, ctx->program->unions.items[i].variants.items[j].name, &ctx->program->unions.items[i].variants.items[j]);
        }
    }
    return 1;
}

static int register_struct_init_functions(LowerContext* ctx) {
    int i = 0;
    for (i = 0; i < ctx->program->structs.count; ++i) {
        const AstStructDecl* ast_struct = &ctx->ast->structs.items[i];
        HirStructDecl* struct_decl = &ctx->program->structs.items[i];
        HirFunction hir_fn;
        int param_index = 0;
        HirType* return_type = 0;
        if (!ast_struct->has_init) {
            continue;
        }
        if (hashmap_contains(&ctx->program->function_map, struct_decl->init_name)) {
            return fail(ctx, "duplicate function");
        }
        memset(&hir_fn, 0, sizeof(hir_fn));
        return_type = new_owned_type(ctx->program, HIR_TYPE_STRUCT);
        return_type->struct_decl = struct_decl;
        hir_fn.return_type = return_type;
        hir_fn.name = struct_decl->init_name;
        hir_fn.line = ast_struct->init_line;
        hir_fn.struct_init_flag = 1;
        hir_fn.owner_struct = struct_decl;
        for (param_index = 0; param_index < ast_struct->init_params.count; ++param_index) {
            HirBinding* param_binding = new_binding(lower_type(ctx, &ast_struct->init_params.items[param_index].type), ast_struct->init_params.items[param_index].name, HIR_BINDING_PARAM, ast_struct->init_params.items[param_index].line);
            binding_list_push(&hir_fn.params, param_binding);
        }
        function_list_push(&ctx->program->functions, hir_fn);
        hashmap_set(&ctx->program->function_map, hir_fn.name, (void*)1);
    }
    rebuild_function_map(ctx->program);
    return 1;
}

static int register_functions(LowerContext* ctx) {
    int i = 0;
    for (i = 0; i < ctx->ast->functions.count; ++i) {
        const AstFunction* ast_fn = &ctx->ast->functions.items[i];
        HirFunction hir_fn;
        int param_index = 0;
        memset(&hir_fn, 0, sizeof(hir_fn));
        if (hashmap_contains(&ctx->program->function_map, ast_fn->name)) {
            return fail(ctx, "duplicate function");
        }
        if (hashmap_contains(&ctx->program->type_name_map, ast_fn->name)) {
            return fail(ctx, "type and function name conflict");
        }
        hir_fn.return_type = lower_type(ctx, &ast_fn->return_type);
        hir_fn.name = ast_fn->name;
        hir_fn.line = ast_fn->line;
        for (param_index = 0; param_index < ast_fn->params.count; ++param_index) {
            HirBinding* param_binding = new_binding(lower_type(ctx, &ast_fn->params.items[param_index].type), ast_fn->params.items[param_index].name, HIR_BINDING_PARAM, ast_fn->params.items[param_index].line);
            binding_list_push(&hir_fn.params, param_binding);
        }
        function_list_push(&ctx->program->functions, hir_fn);
        hashmap_set(&ctx->program->function_map, hir_fn.name, (void*)1);
    }
    rebuild_function_map(ctx->program);
    return 1;
}

static int lower_globals(LowerContext* ctx) {
    int i = 0;
    for (i = 0; i < ctx->ast->globals.count; ++i) {
        HirGlobal* hir_global = &ctx->program->globals.items[i];
        hir_global->init = lower_expr(ctx, ctx->ast->globals.items[i].init);
        if (!hir_global->init) {
            return 0;
        }
        if (ctx->ast->globals.items[i].type.kind == AST_TYPE_INFER) {
            hir_global->binding->type = hir_global->init->type;
        } else if (!type_equals(hir_global->init->type, hir_global->binding->type) &&
                   !apply_array_length_inference(hir_global->binding->type, hir_global->init->type)) {
            return fail(ctx, "global initializer type mismatch");
        }
    }
    return 1;
}

static int lower_functions(LowerContext* ctx) {
    int i = 0;
    int user_index = 0;
    for (i = 0; i < ctx->program->functions.count; ++i) {
        Scope root_scope;
        ctx->current_function = &ctx->program->functions.items[i];
        push_scope(ctx, &root_scope);
        if (ctx->current_function->struct_init_flag) {
            const AstStructDecl* ast_struct = find_ast_struct(ctx->ast, ctx->current_function->owner_struct->name);
            HirBinding* self_binding = 0;
            HirStmt* self_decl = 0;
            int* field_state = 0;
            int j = 0;
            for (j = 0; j < ctx->current_function->params.count; ++j) {
                if (!bind_in_current_scope(ctx, ctx->current_function->params.items[j])) {
                    return 0;
                }
            }
            field_state = (int*)calloc((size_t)ctx->current_function->owner_struct->fields.count, sizeof(int));
            if (!field_state) {
                return fail(ctx, "out of memory");
            }
            for (j = 0; j < ctx->current_function->owner_struct->fields.count; ++j) {
                if (ast_struct->fields.items[j].default_value) {
                    field_state[j] = 1;
                }
            }
            if (!validate_struct_init_block(ctx, ctx->current_function->owner_struct, &ast_struct->init_body, field_state)) {
                free(field_state);
                return 0;
            }
            for (j = 0; j < ctx->current_function->owner_struct->fields.count; ++j) {
                if (!field_state[j]) {
                    free(field_state);
                    return fail(ctx, "missing struct init field");
                }
            }
            free(field_state);
            self_binding = new_binding(ctx->current_function->return_type, "self", HIR_BINDING_LOCAL, ast_struct->init_line);
            binding_list_push(&ctx->current_function->locals, self_binding);
            if (!bind_in_current_scope(ctx, self_binding)) {
                return 0;
            }
            self_decl = new_stmt(HIR_STMT_VAR_DECL, ast_struct->init_line);
            self_decl->as.var_decl.binding = self_binding;
            self_decl->as.var_decl.init = make_zero_expr(ctx, ctx->current_function->return_type, ast_struct->init_line);
            stmt_list_push(&ctx->current_function->body.stmts, self_decl);
            for (j = 0; j < ast_struct->fields.count; ++j) {
                if (ast_struct->fields.items[j].default_value) {
                    HirStmt* assign = new_stmt(HIR_STMT_ASSIGN, ast_struct->fields.items[j].line);
                    AstExpr self_expr;
                    AstExpr field_expr;
                    memset(&self_expr, 0, sizeof(self_expr));
                    memset(&field_expr, 0, sizeof(field_expr));
                    self_expr.kind = AST_EXPR_NAME;
                    self_expr.line = ast_struct->fields.items[j].line;
                    self_expr.as.name = "self";
                    field_expr.kind = AST_EXPR_FIELD;
                    field_expr.line = ast_struct->fields.items[j].line;
                    field_expr.as.field.base = &self_expr;
                    field_expr.as.field.name = ast_struct->fields.items[j].name;
                    assign->as.assign.target = lower_expr(ctx, &field_expr);
                    assign->as.assign.value = lower_expr(ctx, ast_struct->fields.items[j].default_value);
                    if (!assign->as.assign.target || !assign->as.assign.value) {
                        return 0;
                    }
                    stmt_list_push(&ctx->current_function->body.stmts, assign);
                }
            }
            if (!lower_init_block(ctx, &ast_struct->init_body, &ctx->current_function->body, self_binding)) {
                return 0;
            }
            if (ctx->current_function->body.stmts.count == 0 ||
                ctx->current_function->body.stmts.items[ctx->current_function->body.stmts.count - 1]->kind != HIR_STMT_RETURN) {
                HirStmt* ret = new_stmt(HIR_STMT_RETURN, ast_struct->init_line);
                ret->as.ret.expr = make_binding_expr(self_binding, ast_struct->init_line);
                stmt_list_push(&ctx->current_function->body.stmts, ret);
            }
        } else {
            int param_index = 0;
            for (param_index = 0; param_index < ctx->current_function->params.count; ++param_index) {
                if (!bind_in_current_scope(ctx, ctx->current_function->params.items[param_index])) {
                    return 0;
                }
            }
            if (!lower_block(ctx, &ctx->ast->functions.items[user_index].body, &ctx->current_function->body)) {
                return 0;
            }
            user_index += 1;
        }
        pop_scope(ctx);
    }
    return 1;
}

int lower_ast_to_hir(const AstProgram* ast, HirProgram* hir, const char** error) {
    LowerContext ctx;
    memset(hir, 0, sizeof(*hir));
    memset(&ctx, 0, sizeof(ctx));
    hir->int_type.kind = HIR_TYPE_INT;
    hir->bool_type.kind = HIR_TYPE_BOOL;
    hir->void_type.kind = HIR_TYPE_VOID;
    hashmap_init(&hir->global_map);
    hashmap_init(&hir->function_map);
    hashmap_init(&hir->type_name_map);
    hashmap_init(&hir->struct_name_map);
    hashmap_init(&hir->enum_name_map);
    hashmap_init(&hir->enum_member_map);
    hashmap_init(&hir->union_name_map);
    hashmap_init(&hir->variant_map);
    ctx.program = hir;
    ctx.ast = ast;
    if (!register_structs(&ctx) || !register_enums(&ctx) || !register_unions(&ctx) || !register_globals(&ctx) || !register_struct_init_functions(&ctx) || !register_functions(&ctx) || !lower_globals(&ctx) || !lower_functions(&ctx)) {
        *error = ctx.error;
        return 0;
    }
    return 1;
}
