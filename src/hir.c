#include "lower.h"
#include "vec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Scope {
    struct Scope* parent;
    HirBindingList bindings;
} Scope;

typedef struct HirBlockList {
    HirBlock* items;
    int count;
    int capacity;
} HirBlockList;

typedef struct DeferFrame {
    int defer_start;
    int loop_boundary;
} DeferFrame;

typedef struct DeferFrameList {
    DeferFrame* items;
    int count;
    int capacity;
} DeferFrameList;

typedef struct TryScope {
    HirTryCatchList catches;
    int defer_start;
} TryScope;

typedef struct TryScopeList {
    TryScope* items;
    int count;
    int capacity;
} TryScopeList;

typedef struct LowerContext {
    HirProgram* program;
    const AstProgram* ast;
    const char* error;
    int error_line;
    int error_column;
    int current_line;
    int current_column;
    HirFunction* current_function;
    Scope* scope;
    HirBindingList freed_bindings;
    HirBlockList active_defers;
    DeferFrameList defer_frames;
    TryScopeList try_scopes;
    int infer_try_error_flag;
    HirType* inferred_try_error_type;
    int loop_depth;
    int temp_index;
} LowerContext;

static LowerContext* g_active_lower_ctx = 0;

typedef enum HirNominalKind {
    HIR_NOMINAL_NONE = 0,
    HIR_NOMINAL_BUILTIN,
    HIR_NOMINAL_ENUM,
    HIR_NOMINAL_STRUCT,
    HIR_NOMINAL_UNION,
} HirNominalKind;

typedef enum HirBuiltinNominalKind {
    HIR_BUILTIN_NOMINAL_NONE = 0,
    HIR_BUILTIN_NOMINAL_INT,
    HIR_BUILTIN_NOMINAL_I8,
    HIR_BUILTIN_NOMINAL_I16,
    HIR_BUILTIN_NOMINAL_I32,
    HIR_BUILTIN_NOMINAL_I64,
    HIR_BUILTIN_NOMINAL_U8,
    HIR_BUILTIN_NOMINAL_U16,
    HIR_BUILTIN_NOMINAL_U32,
    HIR_BUILTIN_NOMINAL_U64,
    HIR_BUILTIN_NOMINAL_F16,
    HIR_BUILTIN_NOMINAL_F32,
    HIR_BUILTIN_NOMINAL_F64,
    HIR_BUILTIN_NOMINAL_FLOAT,
    HIR_BUILTIN_NOMINAL_DOUBLE,
    HIR_BUILTIN_NOMINAL_CHARACTER,
    HIR_BUILTIN_NOMINAL_UINT8,
    HIR_BUILTIN_NOMINAL_BOOL,
    HIR_BUILTIN_NOMINAL_VOID,
} HirBuiltinNominalKind;

typedef struct HirBuiltinNominalDecl {
    HirBuiltinNominalKind kind;
    const char* name;
} HirBuiltinNominalDecl;

typedef struct HirNominalDeclRef {
    HirNominalKind kind;
    const char* name;
    void* decl;
} HirNominalDeclRef;

typedef enum HirTypeQueryKind {
    HIR_TYPE_QUERY_NONE = 0,
    HIR_TYPE_QUERY_NOMINAL,
    HIR_TYPE_QUERY_TUPLE,
    HIR_TYPE_QUERY_SLICE,
    HIR_TYPE_QUERY_POINTER,
    HIR_TYPE_QUERY_MANY_POINTER,
    HIR_TYPE_QUERY_ARRAY,
    HIR_TYPE_QUERY_OPTIONAL,
} HirTypeQueryKind;

typedef struct HirTypeQueryRef {
    HirTypeQueryKind kind;
    HirType* source;
    HirNominalDeclRef nominal;
    HirType* item_type;
    int array_length;
} HirTypeQueryRef;

static const HirBuiltinNominalDecl HIR_BUILTIN_INT_DECL = { HIR_BUILTIN_NOMINAL_INT, "Int" };
static const HirBuiltinNominalDecl HIR_BUILTIN_I8_DECL = { HIR_BUILTIN_NOMINAL_I8, "Int8" };
static const HirBuiltinNominalDecl HIR_BUILTIN_I16_DECL = { HIR_BUILTIN_NOMINAL_I16, "Int16" };
static const HirBuiltinNominalDecl HIR_BUILTIN_I32_DECL = { HIR_BUILTIN_NOMINAL_I32, "Int32" };
static const HirBuiltinNominalDecl HIR_BUILTIN_I64_DECL = { HIR_BUILTIN_NOMINAL_I64, "Int64" };
static const HirBuiltinNominalDecl HIR_BUILTIN_U8_DECL = { HIR_BUILTIN_NOMINAL_U8, "UInt8" };
static const HirBuiltinNominalDecl HIR_BUILTIN_U16_DECL = { HIR_BUILTIN_NOMINAL_U16, "UInt16" };
static const HirBuiltinNominalDecl HIR_BUILTIN_U32_DECL = { HIR_BUILTIN_NOMINAL_U32, "UInt32" };
static const HirBuiltinNominalDecl HIR_BUILTIN_U64_DECL = { HIR_BUILTIN_NOMINAL_U64, "UInt64" };
static const HirBuiltinNominalDecl HIR_BUILTIN_F16_DECL = { HIR_BUILTIN_NOMINAL_F16, "Float16" };
static const HirBuiltinNominalDecl HIR_BUILTIN_F32_DECL = { HIR_BUILTIN_NOMINAL_F32, "Float32" };
static const HirBuiltinNominalDecl HIR_BUILTIN_F64_DECL = { HIR_BUILTIN_NOMINAL_F64, "Float64" };
static const HirBuiltinNominalDecl HIR_BUILTIN_FLOAT_DECL = { HIR_BUILTIN_NOMINAL_FLOAT, "Float" };
static const HirBuiltinNominalDecl HIR_BUILTIN_DOUBLE_DECL = { HIR_BUILTIN_NOMINAL_DOUBLE, "Double" };
static const HirBuiltinNominalDecl HIR_BUILTIN_CHARACTER_DECL = { HIR_BUILTIN_NOMINAL_CHARACTER, "Char" };
static const HirBuiltinNominalDecl HIR_BUILTIN_UINT8_DECL = { HIR_BUILTIN_NOMINAL_UINT8, "UInt8" };
static const HirBuiltinNominalDecl HIR_BUILTIN_BOOL_DECL = { HIR_BUILTIN_NOMINAL_BOOL, "Bool" };
static const HirBuiltinNominalDecl HIR_BUILTIN_VOID_DECL = { HIR_BUILTIN_NOMINAL_VOID, "Void" };

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
#define errorable_entry_list_push(list, entry) VEC_PUSH((list), (entry))
#define try_catch_list_push(list, try_catch) VEC_PUSH((list), (try_catch))
#define try_scope_list_push(list, scope) VEC_PUSH((list), (scope))
#define defer_block_list_push(list, block) VEC_PUSH((list), (block))
#define defer_frame_list_push(list, frame) VEC_PUSH((list), (frame))
#define name_list_push(list, name) VEC_PUSH((list), (name))

static int fail(LowerContext* ctx, const char* error);
static int fail_unknown_field(LowerContext* ctx, const char* struct_name, const char* field_name);
static HirExpr* lower_expr(LowerContext* ctx, const AstExpr* expr);
static HirExpr* lower_expr_expected(LowerContext* ctx, const AstExpr* expr, HirType* expected_type);
static HirExpr* lower_expr_preserve_pointer(LowerContext* ctx, const AstExpr* expr);
static HirType* new_owned_type(HirProgram* program, HirTypeKind kind);
static HirType* primitive_type(HirProgram* program, HirTypeKind kind);
static int is_lvalue_expr(const HirExpr* expr);
static HirExpr* make_zero_expr(LowerContext* ctx, HirType* type, int line);
static HirExpr* make_optional_value_expr(LowerContext* ctx, HirExpr* value, int line);
static HirBinding* lookup_binding(LowerContext* ctx, const char* name);
static int bind_in_current_scope(LowerContext* ctx, HirBinding* binding);
static int hir_expr_is_new_constructible(const HirExpr* expr);
static void mark_binding_freed(LowerContext* ctx, HirBinding* binding);
static HirExpr* lower_new_primitive_constructor(LowerContext* ctx, const AstExpr* expr);

static AstExpr* new_synthetic_name_expr(const char* name, int line) {
    AstExpr* expr = (AstExpr*)calloc(1, sizeof(AstExpr));
    if (!expr) {
        return 0;
    }
    expr->kind = AST_EXPR_NAME;
    expr->line = line;
    expr->as.name = strdup(name);
    return expr;
}

static AstExpr* new_synthetic_field_expr(AstExpr* base, const char* name, int line) {
    AstExpr* expr = (AstExpr*)calloc(1, sizeof(AstExpr));
    if (!expr) {
        return 0;
    }
    expr->kind = AST_EXPR_FIELD;
    expr->line = line;
    expr->as.field.base = base;
    expr->as.field.name = strdup(name);
    return expr;
}

static AstExpr* qualified_path_to_expr(const char* path, int line) {
    char* copy = strdup(path);
    char* part = copy;
    char* dot = 0;
    AstExpr* expr = 0;
    if (!copy) {
        return 0;
    }
    dot = strchr(part, '.');
    if (dot) {
        *dot = '\0';
    }
    expr = new_synthetic_name_expr(part, line);
    while (expr && dot) {
        part = dot + 1;
        dot = strchr(part, '.');
        if (dot) {
            *dot = '\0';
        }
        expr = new_synthetic_field_expr(expr, part, line);
    }
    free(copy);
    return expr;
}
static HirExpr* lower_primitive_init_constructor(LowerContext* ctx, const AstExpr* expr);
static HirStructField* find_struct_field(HirStructDecl* struct_decl, const char* name, int* field_index);
static const AstStructDecl* find_ast_struct(const AstProgram* ast, const char* name);
static const AstEnumDecl* find_ast_enum(const AstProgram* ast, const char* name);
static const AstUnionDecl* find_ast_union(const AstProgram* ast, const char* name);
static int is_mutable_assignment_target(const HirExpr* expr);
static int type_assignment_compatible(HirType* actual, HirType* expected);
static int type_equals(HirType* left, HirType* right);
static int same_nominal_type(HirType* left, HirType* right);
static int same_nominal_type_strict(HirType* left, HirType* right);
static int nominal_names_equivalent(const char* left, const char* right);
static int lower_var_decl_coalesce_control(LowerContext* ctx, const AstStmt* stmt, HirBlock* out_block);
static int lower_block(LowerContext* ctx, const AstBlock* ast_block, HirBlock* out_block, int loop_boundary);
static HirStmt* lower_stmt(LowerContext* ctx, const AstStmt* stmt);
static HirType* function_type_from_function(LowerContext* ctx, HirFunction* fn);
static HirType* current_self_owner_type(LowerContext* ctx);
static HirType* requalify_nominal_owner_type(LowerContext* ctx, HirType* base_type, int mutable_flag);
static HirType* resolve_owner_type_name(LowerContext* ctx, const char* owner_type_name);
static HirType* errorable_value_type(HirType* type);
static HirType* errorable_error_type(HirType* type);
static int is_errorable_type(HirType* type);
static HirExpr* maybe_wrap_expected_errorable_expr(LowerContext* ctx, HirExpr* value, HirType* expected_type, int line);
static HirExpr* lower_expr_value(LowerContext* ctx, const AstExpr* expr);
static int current_try_catches_error(LowerContext* ctx, HirType* error_type);
static int current_try_defer_start(LowerContext* ctx);

static void append_block_stmts(HirBlock* out_block, const HirBlock* block) {
    int i = 0;
    for (i = 0; i < block->stmts.count; ++i) {
        stmt_list_push(&out_block->stmts, block->stmts.items[i]);
    }
}

static int current_try_catches_error(LowerContext* ctx, HirType* error_type) {
    int i = 0;
    if (!ctx || ctx->try_scopes.count <= 0 || !error_type) {
        return 0;
    }
    for (i = 0; i < ctx->try_scopes.items[ctx->try_scopes.count - 1].catches.count; ++i) {
        if (type_equals(ctx->try_scopes.items[ctx->try_scopes.count - 1].catches.items[i].error_type, error_type)) {
            return 1;
        }
    }
    return 0;
}

static int current_try_defer_start(LowerContext* ctx) {
    if (!ctx || ctx->try_scopes.count <= 0) {
        return 0;
    }
    return ctx->try_scopes.items[ctx->try_scopes.count - 1].defer_start;
}

static void emit_deferred_blocks(LowerContext* ctx, HirBlock* out_block, int start) {
    int i = 0;
    if (start < 0) {
        start = 0;
    }
    for (i = ctx->active_defers.count - 1; i >= start; --i) {
        append_block_stmts(out_block, &ctx->active_defers.items[i]);
    }
}

static int nearest_loop_defer_start(LowerContext* ctx) {
    int i = 0;
    for (i = ctx->defer_frames.count - 1; i >= 0; --i) {
        if (ctx->defer_frames.items[i].loop_boundary) {
            return ctx->defer_frames.items[i].defer_start;
        }
    }
    return -1;
}

static int defer_body_has_control_flow(const AstBlock* block) {
    int i = 0;
    for (i = 0; i < block->stmts.count; ++i) {
        const AstStmt* stmt = block->stmts.items[i];
        switch (stmt->kind) {
            case AST_STMT_RETURN:
            case AST_STMT_BREAK:
            case AST_STMT_CONTINUE:
                return 1;
            case AST_STMT_VAR_DECL:
                if (stmt->as.var_decl.init && stmt->as.var_decl.init->kind == AST_EXPR_COALESCE_CONTROL) {
                    return 1;
                }
                break;
            case AST_STMT_GROUP:
                if (defer_body_has_control_flow(&stmt->as.group_stmt)) {
                    return 1;
                }
                break;
            case AST_STMT_IF:
                if (defer_body_has_control_flow(&stmt->as.if_stmt.then_block) ||
                    (stmt->as.if_stmt.has_else && defer_body_has_control_flow(&stmt->as.if_stmt.else_block))) {
                    return 1;
                }
                break;
            case AST_STMT_SWITCH: {
                int j = 0;
                for (j = 0; j < stmt->as.switch_stmt.cases.count; ++j) {
                    if (defer_body_has_control_flow(&stmt->as.switch_stmt.cases.items[j].body)) {
                        return 1;
                    }
                }
                break;
            }
            case AST_STMT_WHILE:
                if (defer_body_has_control_flow(&stmt->as.while_stmt.body)) {
                    return 1;
                }
                break;
            case AST_STMT_FOR_RANGE:
                if (defer_body_has_control_flow(&stmt->as.for_range.body)) {
                    return 1;
                }
                break;
            case AST_STMT_FOR_EACH:
                if (defer_body_has_control_flow(&stmt->as.for_each.body)) {
                    return 1;
                }
                break;
            case AST_STMT_DEFER:
                if (defer_body_has_control_flow(&stmt->as.defer_stmt.body)) {
                    return 1;
                }
                break;
            default:
                break;
        }
    }
    return 0;
}

static int type_is_imported_nominal(HirType* type) {
    if (!type) {
        return 0;
    }
    switch (type->kind) {
        case HIR_TYPE_STRUCT:
            return type->struct_decl && type->struct_decl->name && strchr(type->struct_decl->name, '.') != 0;
        case HIR_TYPE_ENUM:
            return type->enum_decl && type->enum_decl->name && strchr(type->enum_decl->name, '.') != 0;
        case HIR_TYPE_UNION:
            return type->union_decl && type->union_decl->name && strchr(type->union_decl->name, '.') != 0;
        default:
            return 0;
    }
}

static int same_method_owner_type(HirType* left, HirType* right) {
    return left && right && type_equals(left, right);
}

static int method_source_name_matches(const char* candidate, const char* requested) {
    if (!candidate || !requested) {
        return 0;
    }
    if (strcmp(candidate, requested) == 0) {
        return 1;
    }
    if (candidate[0] == '#' && strcmp(candidate + 1, requested) == 0) {
        return 1;
    }
    return 0;
}

static int method_visible_from_context(LowerContext* ctx, HirFunction* method, HirType* owner_type) {
    if (!method || !owner_type) {
        return 0;
    }
    if (method->public_flag) {
        return 1;
    }
    if (!type_is_imported_nominal(owner_type)) {
        return 1;
    }
    if (ctx->current_function && ctx->current_function->receiver_type &&
        same_method_owner_type(ctx->current_function->receiver_type, owner_type)) {
        return 1;
    }
    if (ctx->current_function &&
        ctx->current_function->owner_struct &&
        owner_type->kind == HIR_TYPE_STRUCT &&
        owner_type->struct_decl == ctx->current_function->owner_struct) {
        return 1;
    }
    return 0;
}

static int is_expected_type_shorthand_expr(const AstExpr* expr) {
    return expr &&
           expr->kind == AST_EXPR_VARIANT &&
           !expr->as.variant.union_name;
}

static int is_expected_type_null_expr(const AstExpr* expr) {
    return expr && expr->kind == AST_EXPR_NULL;
}

static int is_numeric_literal_expr(const AstExpr* expr) {
    return expr &&
           (expr->kind == AST_EXPR_INT ||
            expr->kind == AST_EXPR_FLOAT ||
            expr->kind == AST_EXPR_CHAR);
}

static int is_pure_optional_base_expr(const AstExpr* expr) {
    if (!expr) {
        return 0;
    }
    switch (expr->kind) {
        case AST_EXPR_NAME:
        case AST_EXPR_INT:
        case AST_EXPR_FLOAT:
        case AST_EXPR_CHAR:
        case AST_EXPR_BOOL:
            return 1;
        case AST_EXPR_FIELD:
        case AST_EXPR_OPTIONAL_FIELD:
            return is_pure_optional_base_expr(expr->as.field.base);
        case AST_EXPR_INDEX:
        case AST_EXPR_OPTIONAL_INDEX:
            return is_pure_optional_base_expr(expr->as.index.base) &&
                   is_pure_optional_base_expr(expr->as.index.index);
        default:
            return 0;
    }
}

static int optional_null_compare_binding_name(const AstExpr* expr, int* non_null_then, const char** name_out) {
    const AstExpr* name_expr = 0;
    const AstExpr* null_expr = 0;
    if (!expr || expr->kind != AST_EXPR_BINARY || (expr->as.binary.op != AST_BIN_EQ && expr->as.binary.op != AST_BIN_NE)) {
        return 0;
    }
    if (expr->as.binary.left && expr->as.binary.left->kind == AST_EXPR_NULL) {
        null_expr = expr->as.binary.left;
        name_expr = expr->as.binary.right;
    } else if (expr->as.binary.right && expr->as.binary.right->kind == AST_EXPR_NULL) {
        null_expr = expr->as.binary.right;
        name_expr = expr->as.binary.left;
    }
    (void)null_expr;
    if (!name_expr || name_expr->kind != AST_EXPR_NAME) {
        return 0;
    }
    *name_out = name_expr->as.name;
    *non_null_then = expr->as.binary.op == AST_BIN_NE;
    return 1;
}

static char* make_struct_init_name(const char* struct_name, int index) {
    char suffix[32];
    const char* prefix = "__struct_init_";
    size_t prefix_len = strlen(prefix);
    size_t name_len = strlen(struct_name);
    size_t suffix_len = 0;
    char* out = 0;
    snprintf(suffix, sizeof(suffix), "_%d", index);
    suffix_len = strlen(suffix);
    out = (char*)malloc(prefix_len + name_len + suffix_len + 1);
    if (!out) {
        return 0;
    }
    memcpy(out, prefix, prefix_len);
    memcpy(out + prefix_len, struct_name, name_len);
    memcpy(out + prefix_len + name_len, suffix, suffix_len + 1);
    return out;
}

static char* make_struct_deinit_name(const char* struct_name) {
    const char* prefix = "__struct_deinit_";
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

static char* make_method_name(const char* owner_name, const char* method_name, int static_flag) {
    const char* prefix = static_flag ? "__static_method_" : "__method_";
    size_t prefix_len = strlen(prefix);
    size_t owner_len = strlen(owner_name);
    size_t method_len = strlen(method_name);
    char* out = (char*)malloc(prefix_len + owner_len + 1 + method_len + 1);
    if (!out) {
        return 0;
    }
    memcpy(out, prefix, prefix_len);
    memcpy(out + prefix_len, owner_name, owner_len);
    out[prefix_len + owner_len] = '_';
    memcpy(out + prefix_len + owner_len + 1, method_name, method_len);
    out[prefix_len + owner_len + 1 + method_len] = '\0';
    return out;
}

static char* make_unique_function_name(HirProgram* program, const char* base_name) {
    char suffix[32];
    char* candidate = 0;
    int index = 1;
    if (!hashmap_contains(&program->function_map, base_name)) {
        return strdup(base_name);
    }
    for (;;) {
        snprintf(suffix, sizeof(suffix), "__ov%d", index++);
        candidate = (char*)malloc(strlen(base_name) + strlen(suffix) + 1);
        if (!candidate) {
            return 0;
        }
        strcpy(candidate, base_name);
        strcat(candidate, suffix);
        if (!hashmap_contains(&program->function_map, candidate)) {
            return candidate;
        }
        free(candidate);
    }
}

static HirType* primitive_type(HirProgram* program, HirTypeKind kind) {
    switch (kind) {
        case HIR_TYPE_INT: return &program->int_type;
        case HIR_TYPE_I8: return &program->i8_type;
        case HIR_TYPE_I16: return &program->i16_type;
        case HIR_TYPE_I32: return &program->i32_type;
        case HIR_TYPE_I64: return &program->i64_type;
        case HIR_TYPE_U8: return &program->u8_type;
        case HIR_TYPE_U16: return &program->u16_type;
        case HIR_TYPE_U32: return &program->u32_type;
        case HIR_TYPE_U64: return &program->u64_type;
        case HIR_TYPE_F16: return &program->f16_type;
        case HIR_TYPE_F32: return &program->f32_type;
        case HIR_TYPE_F64: return &program->f64_type;
        case HIR_TYPE_FLOAT: return &program->float_type;
        case HIR_TYPE_DOUBLE: return &program->double_type;
        case HIR_TYPE_CHARACTER: return &program->character_type;
        case HIR_TYPE_UINT8: return &program->uint8_type;
        case HIR_TYPE_BOOL: return &program->bool_type;
        case HIR_TYPE_VOID: return &program->void_type;
        default: return 0;
    }
}

static HirType* qualified_primitive_type(HirProgram* program, HirTypeKind kind, int mutable_flag) {
    HirType* type = 0;
    if (!mutable_flag) {
        return primitive_type(program, kind);
    }
    type = new_owned_type(program, kind);
    type->mutable_flag = 1;
    return type;
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
    if (left->kind == HIR_TYPE_SLICE) {
        return type_equals(left->array_item, right->array_item);
    }
    if (left->kind == HIR_TYPE_REFERENCE || left->kind == HIR_TYPE_POINTER || left->kind == HIR_TYPE_MANY_POINTER) {
        return type_equals(left->array_item, right->array_item);
    }
    if (left->kind == HIR_TYPE_OPTIONAL) {
        return type_equals(left->array_item, right->array_item);
    }
    if (left->kind == HIR_TYPE_FUNCTION) {
        if (!type_equals(left->return_type, right->return_type)) {
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
    if (left->kind == HIR_TYPE_ENUM) {
        return left->enum_decl == right->enum_decl ||
               (left->enum_decl && right->enum_decl &&
                nominal_names_equivalent(left->enum_decl->name, right->enum_decl->name));
    }
    if (left->kind == HIR_TYPE_STRUCT) {
        return left->struct_decl == right->struct_decl ||
               (left->struct_decl && right->struct_decl &&
                nominal_names_equivalent(left->struct_decl->name, right->struct_decl->name));
    }
    if (left->kind == HIR_TYPE_UNION) {
        if (is_errorable_type(left) || is_errorable_type(right)) {
            return is_errorable_type(left) &&
                   is_errorable_type(right) &&
                   type_equals(errorable_value_type(left), errorable_value_type(right)) &&
                   type_equals(errorable_error_type(left), errorable_error_type(right));
        }
        return left->union_decl == right->union_decl ||
               (left->union_decl && right->union_decl &&
                nominal_names_equivalent(left->union_decl->name, right->union_decl->name));
    }
    if (left->kind != HIR_TYPE_TUPLE) {
        return 1;
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

static const char* nominal_public_suffix(const char* name) {
    const char* last_dot = name ? strrchr(name, '.') : 0;
    const char* scan = 0;
    if (!last_dot) {
        return name;
    }
    scan = last_dot;
    while (scan > name) {
        scan -= 1;
        if (*scan == '.') {
            return scan + 1;
        }
    }
    return name;
}

static int suffix_range_equal(const char* left, size_t left_len, const char* right, size_t right_len) {
    const char* left_suffix = left;
    const char* right_suffix = right;
    size_t left_suffix_len = left_len;
    size_t right_suffix_len = right_len;
    size_t i = 0;
    for (i = 0; i + 2 < left_len; ++i) {
        if (left[i] == '_' && left[i + 1] == 'd' && left[i + 2] == '_') {
            left_suffix = left + i + 3;
            left_suffix_len = left_len - i - 3;
        }
    }
    for (i = 0; i + 2 < right_len; ++i) {
        if (right[i] == '_' && right[i + 1] == 'd' && right[i + 2] == '_') {
            right_suffix = right + i + 3;
            right_suffix_len = right_len - i - 3;
        }
    }
    if (left_suffix_len != right_suffix_len) {
        return 0;
    }
    return strncmp(left_suffix, right_suffix, left_suffix_len) == 0;
}

static int generic_nominal_names_equivalent(const char* left, const char* right) {
    const char* left_args = strstr(left, "__");
    const char* right_args = strstr(right, "__");
    if (!left_args || !right_args) {
        return 0;
    }
    if ((left_args == 0) != (right_args == 0)) {
        return 0;
    }
    {
        size_t left_base_len = (size_t)(left_args - left);
        size_t right_base_len = (size_t)(right_args - right);
        const char* left_base = nominal_public_suffix(left);
        const char* right_base = nominal_public_suffix(right);
        size_t left_suffix_len = left_base_len - (size_t)(left_base - left);
        size_t right_suffix_len = right_base_len - (size_t)(right_base - right);
        if (left_suffix_len != right_suffix_len || strncmp(left_base, right_base, left_suffix_len) != 0) {
            return 0;
        }
    }
    left_args += 2;
    right_args += 2;
    while (*left_args || *right_args) {
        const char* left_next = strstr(left_args, "__");
        const char* right_next = strstr(right_args, "__");
        size_t left_len = left_next ? (size_t)(left_next - left_args) : strlen(left_args);
        size_t right_len = right_next ? (size_t)(right_next - right_args) : strlen(right_args);
        if (!suffix_range_equal(left_args, left_len, right_args, right_len)) {
            return 0;
        }
        if ((left_next == 0) != (right_next == 0)) {
            return 0;
        }
        if (!left_next) {
            break;
        }
        left_args = left_next + 2;
        right_args = right_next + 2;
    }
    return 1;
}

static int nominal_names_equivalent(const char* left, const char* right) {
    if (!left || !right) {
        return 0;
    }
    if (strcmp(left, right) == 0) {
        return 1;
    }
    if (generic_nominal_names_equivalent(left, right)) {
        return 1;
    }
    return strcmp(nominal_public_suffix(left), nominal_public_suffix(right)) == 0;
}

static int type_assignment_compatible_inner(HirType* actual, HirType* expected, int through_alias) {
    int i = 0;
    if (actual == expected) {
        return 1;
    }
    if (!actual || !expected || actual->kind != expected->kind) {
        return 0;
    }
    switch (expected->kind) {
        case HIR_TYPE_ARRAY:
            return actual->array_length == expected->array_length &&
                   type_assignment_compatible_inner(actual->array_item, expected->array_item, through_alias);
        case HIR_TYPE_SLICE:
        case HIR_TYPE_REFERENCE:
        case HIR_TYPE_POINTER:
        case HIR_TYPE_MANY_POINTER:
            return type_assignment_compatible_inner(actual->array_item, expected->array_item, 1);
        case HIR_TYPE_OPTIONAL:
            return type_assignment_compatible_inner(actual->array_item, expected->array_item, through_alias);
        case HIR_TYPE_FUNCTION:
            if (!type_assignment_compatible_inner(actual->return_type, expected->return_type, through_alias)) {
                return 0;
            }
            if (actual->tuple_items.count != expected->tuple_items.count) {
                return 0;
            }
            for (i = 0; i < actual->tuple_items.count; ++i) {
                if (!type_assignment_compatible_inner(actual->tuple_items.items[i], expected->tuple_items.items[i], through_alias)) {
                    return 0;
                }
            }
            return 1;
        case HIR_TYPE_ENUM:
            return actual->enum_decl == expected->enum_decl ||
                   (actual->enum_decl && expected->enum_decl &&
                    nominal_names_equivalent(actual->enum_decl->name, expected->enum_decl->name));
        case HIR_TYPE_STRUCT:
            return actual->struct_decl == expected->struct_decl ||
                   (actual->struct_decl && expected->struct_decl &&
                    nominal_names_equivalent(actual->struct_decl->name, expected->struct_decl->name));
        case HIR_TYPE_UNION:
            if (is_errorable_type(actual) || is_errorable_type(expected)) {
                return is_errorable_type(actual) &&
                       is_errorable_type(expected) &&
                       type_assignment_compatible_inner(errorable_value_type(actual), errorable_value_type(expected), through_alias) &&
                       type_equals(errorable_error_type(actual), errorable_error_type(expected));
            }
            return actual->union_decl == expected->union_decl ||
                   (actual->union_decl && expected->union_decl &&
                    nominal_names_equivalent(actual->union_decl->name, expected->union_decl->name));
        case HIR_TYPE_TUPLE:
            if (actual->tuple_items.count != expected->tuple_items.count) {
                return 0;
            }
            for (i = 0; i < actual->tuple_items.count; ++i) {
                if (!type_assignment_compatible_inner(actual->tuple_items.items[i], expected->tuple_items.items[i], through_alias)) {
                    return 0;
                }
            }
            return 1;
        default:
            if (through_alias && expected->mutable_flag && !actual->mutable_flag) {
                return 0;
            }
            return 1;
    }
}

static int type_assignment_compatible(HirType* actual, HirType* expected) {
    return type_assignment_compatible_inner(actual, expected, 0);
}

static int is_errorable_type(HirType* type) {
    return type && type->kind == HIR_TYPE_UNION && type->union_decl && type->union_decl->errorable_flag;
}

static HirType* errorable_value_type(HirType* type) {
    if (!is_errorable_type(type)) {
        return 0;
    }
    return type->union_decl->variants.items[0].payload_type;
}

static HirType* errorable_error_type(HirType* type) {
    if (!is_errorable_type(type)) {
        return 0;
    }
    return type->union_decl->variants.items[1].payload_type;
}

static int64_t align_up_bytes(int64_t value, int64_t align) {
    if (align <= 1) {
        return value;
    }
    return ((value + align - 1) / align) * align;
}

static int64_t type_alignment_bytes(HirType* type) {
    int i = 0;
    int64_t align = 1;
    if (!type) {
        return 1;
    }
    switch (type->kind) {
        case HIR_TYPE_I8:
        case HIR_TYPE_U8:
        case HIR_TYPE_UINT8:
        case HIR_TYPE_BOOL:
        case HIR_TYPE_VOID:
            return 1;
        case HIR_TYPE_I16:
        case HIR_TYPE_U16:
        case HIR_TYPE_F16:
            return 2;
        case HIR_TYPE_I32:
        case HIR_TYPE_U32:
        case HIR_TYPE_CHARACTER:
        case HIR_TYPE_F32:
        case HIR_TYPE_FLOAT:
            return 4;
        case HIR_TYPE_INT:
        case HIR_TYPE_I64:
        case HIR_TYPE_U64:
        case HIR_TYPE_F64:
        case HIR_TYPE_DOUBLE:
        case HIR_TYPE_STRING:
        case HIR_TYPE_REFERENCE:
        case HIR_TYPE_POINTER:
        case HIR_TYPE_MANY_POINTER:
        case HIR_TYPE_SLICE:
        case HIR_TYPE_ENUM:
        case HIR_TYPE_FUNCTION:
            return 8;
        case HIR_TYPE_ARRAY:
        case HIR_TYPE_OPTIONAL:
            return type_alignment_bytes(type->array_item);
        case HIR_TYPE_TUPLE:
            for (i = 0; i < type->tuple_items.count; ++i) {
                int64_t item_align = type_alignment_bytes(type->tuple_items.items[i]);
                if (item_align > align) {
                    align = item_align;
                }
            }
            return align;
        case HIR_TYPE_STRUCT:
            for (i = 0; i < type->struct_decl->fields.count; ++i) {
                int64_t field_align = type_alignment_bytes(type->struct_decl->fields.items[i].type);
                if (field_align > align) {
                    align = field_align;
                }
            }
            return align;
        case HIR_TYPE_UNION:
            return type->union_decl->payload_align > 8 ? type->union_decl->payload_align : 8;
    }
    return 1;
}

static int64_t max_type_alignment_bytes(void) {
    return JIANG_MAX_TYPE_ALIGNMENT_BYTES;
}

static int64_t type_size_bytes(HirType* type) {
    int i = 0;
    int64_t size = 0;
    if (!type) {
        return 0;
    }
    switch (type->kind) {
        case HIR_TYPE_INT:
            return 8;
        case HIR_TYPE_I8:
        case HIR_TYPE_U8:
        case HIR_TYPE_UINT8:
        case HIR_TYPE_BOOL:
            return 1;
        case HIR_TYPE_I16:
        case HIR_TYPE_U16:
        case HIR_TYPE_F16:
            return 2;
        case HIR_TYPE_I32:
        case HIR_TYPE_U32:
        case HIR_TYPE_CHARACTER:
        case HIR_TYPE_F32:
        case HIR_TYPE_FLOAT:
            return 4;
        case HIR_TYPE_I64:
        case HIR_TYPE_U64:
        case HIR_TYPE_F64:
        case HIR_TYPE_DOUBLE:
            return 8;
        case HIR_TYPE_STRING:
            return 16;
        case HIR_TYPE_VOID:
            return 0;
        case HIR_TYPE_REFERENCE:
        case HIR_TYPE_POINTER:
        case HIR_TYPE_MANY_POINTER:
        case HIR_TYPE_FUNCTION:
            return 8;
        case HIR_TYPE_SLICE:
            return 16;
        case HIR_TYPE_ARRAY:
            return (int64_t)type->array_length * type_size_bytes(type->array_item);
        case HIR_TYPE_OPTIONAL:
            size = 1;
            size = align_up_bytes(size, type_alignment_bytes(type->array_item));
            size += type_size_bytes(type->array_item);
            return align_up_bytes(size, type_alignment_bytes(type));
        case HIR_TYPE_TUPLE:
            for (i = 0; i < type->tuple_items.count; ++i) {
                size = align_up_bytes(size, type_alignment_bytes(type->tuple_items.items[i]));
                size += type_size_bytes(type->tuple_items.items[i]);
            }
            return align_up_bytes(size, type_alignment_bytes(type));
        case HIR_TYPE_STRUCT:
            for (i = 0; i < type->struct_decl->fields.count; ++i) {
                size = align_up_bytes(size, type_alignment_bytes(type->struct_decl->fields.items[i].type));
                size += type_size_bytes(type->struct_decl->fields.items[i].type);
            }
            return align_up_bytes(size, type_alignment_bytes(type));
        case HIR_TYPE_ENUM:
            return 8;
        case HIR_TYPE_UNION: {
            size = 8 + type->union_decl->payload_size;
            return align_up_bytes(size, type_alignment_bytes(type));
        }
    }
    return 0;
}

static int is_integer_like_type(HirType* type) {
    return type &&
           (type->kind == HIR_TYPE_INT ||
            type->kind == HIR_TYPE_I8 ||
            type->kind == HIR_TYPE_I16 ||
            type->kind == HIR_TYPE_I32 ||
            type->kind == HIR_TYPE_I64 ||
            type->kind == HIR_TYPE_U8 ||
            type->kind == HIR_TYPE_U16 ||
            type->kind == HIR_TYPE_U32 ||
            type->kind == HIR_TYPE_U64 ||
            type->kind == HIR_TYPE_UINT8 ||
            type->kind == HIR_TYPE_BOOL);
}

static int is_bitwise_integer_type(HirType* type) {
    return type &&
           (type->kind == HIR_TYPE_INT ||
            type->kind == HIR_TYPE_I8 ||
            type->kind == HIR_TYPE_I16 ||
            type->kind == HIR_TYPE_I32 ||
            type->kind == HIR_TYPE_I64 ||
            type->kind == HIR_TYPE_U8 ||
            type->kind == HIR_TYPE_U16 ||
            type->kind == HIR_TYPE_U32 ||
            type->kind == HIR_TYPE_U64 ||
            type->kind == HIR_TYPE_UINT8);
}

static int is_float_like_type(HirType* type) {
    return type &&
           (type->kind == HIR_TYPE_F16 ||
            type->kind == HIR_TYPE_F32 ||
            type->kind == HIR_TYPE_F64 ||
           (type->kind == HIR_TYPE_FLOAT ||
            type->kind == HIR_TYPE_DOUBLE));
}

static int is_integer_literal_target_type(HirType* type) {
    return type &&
           ((is_integer_like_type(type) && type->kind != HIR_TYPE_BOOL) ||
            type->kind == HIR_TYPE_CHARACTER);
}

static int char_literal_fits_integer_type(int64_t value, HirType* type) {
    if (!type) {
        return 0;
    }
    switch (type->kind) {
        case HIR_TYPE_INT:
        case HIR_TYPE_I64:
            return value >= INT64_MIN && value <= INT64_MAX;
        case HIR_TYPE_I8:
            return value >= -128 && value <= 127;
        case HIR_TYPE_I16:
            return value >= -32768 && value <= 32767;
        case HIR_TYPE_I32:
            return value >= -2147483648LL && value <= 2147483647LL;
        case HIR_TYPE_UINT8:
        case HIR_TYPE_U8:
            return value >= 0 && value <= 255;
        case HIR_TYPE_U16:
            return value >= 0 && value <= 65535;
        case HIR_TYPE_U32:
        case HIR_TYPE_U64:
            return value >= 0;
        default:
            return 0;
    }
}

static int is_numeric_promotion_type(HirType* type) {
    return type &&
           (type->kind == HIR_TYPE_INT ||
            type->kind == HIR_TYPE_FLOAT ||
            type->kind == HIR_TYPE_DOUBLE);
}

static HirType* common_numeric_type(HirProgram* program, HirType* left, HirType* right) {
    if (!left || !right) {
        return 0;
    }
    if (!is_numeric_promotion_type(left) || !is_numeric_promotion_type(right)) {
        return 0;
    }
    if (left->kind == HIR_TYPE_DOUBLE || right->kind == HIR_TYPE_DOUBLE) {
        return primitive_type(program, HIR_TYPE_DOUBLE);
    }
    if (left->kind == HIR_TYPE_FLOAT || right->kind == HIR_TYPE_FLOAT) {
        return primitive_type(program, HIR_TYPE_FLOAT);
    }
    return primitive_type(program, HIR_TYPE_INT);
}

static int is_primitive_init_cast_type(HirType* type) {
    return type &&
           ((is_integer_like_type(type) && type->kind != HIR_TYPE_BOOL) ||
            is_float_like_type(type) ||
            type->kind == HIR_TYPE_CHARACTER);
}

static int as_compatible(HirType* from, HirType* to) {
    if (!from || !to) {
        return 0;
    }
    if (type_equals(from, to)) {
        return 1;
    }
    if (is_integer_like_type(from) && is_integer_like_type(to)) {
        return 1;
    }
    if ((is_integer_like_type(from) || from->kind == HIR_TYPE_CHARACTER) &&
        is_float_like_type(to)) {
        return 1;
    }
    if (is_float_like_type(from) &&
        (is_float_like_type(to) || is_integer_like_type(to) || to->kind == HIR_TYPE_CHARACTER)) {
        return 1;
    }
    if ((from->kind == HIR_TYPE_REFERENCE || from->kind == HIR_TYPE_POINTER || from->kind == HIR_TYPE_MANY_POINTER) && to->kind == HIR_TYPE_INT) {
        return 1;
    }
    if (from->kind == HIR_TYPE_INT && (to->kind == HIR_TYPE_REFERENCE || to->kind == HIR_TYPE_POINTER || to->kind == HIR_TYPE_MANY_POINTER)) {
        return 1;
    }
    if (from->kind == HIR_TYPE_ARRAY &&
        to->kind == HIR_TYPE_MANY_POINTER &&
        type_equals(from->array_item, to->array_item)) {
        return 1;
    }
    if ((from->kind == HIR_TYPE_REFERENCE || from->kind == HIR_TYPE_POINTER || from->kind == HIR_TYPE_MANY_POINTER) &&
        (to->kind == HIR_TYPE_REFERENCE || to->kind == HIR_TYPE_POINTER || to->kind == HIR_TYPE_MANY_POINTER)) {
        return 1;
    }
    return 0;
}

static HirType* reference_to_type(LowerContext* ctx, HirType* referent) {
    HirType* reference = new_owned_type(ctx->program, HIR_TYPE_REFERENCE);
    reference->array_item = referent;
    return reference;
}

static HirType* pointer_to_type(LowerContext* ctx, HirType* pointee) {
    HirType* pointer = new_owned_type(ctx->program, HIR_TYPE_POINTER);
    pointer->array_item = pointee;
    return pointer;
}

static HirType* get_errorable_type(LowerContext* ctx, HirType* value_type, HirType* error_type) {
    int i = 0;
    for (i = 0; i < ctx->program->errorable_types.count; ++i) {
        HirErrorableEntry* entry = &ctx->program->errorable_types.items[i];
        if (type_equals(entry->value_type, value_type) && type_equals(entry->error_type, error_type)) {
            return entry->result_type;
        }
    }
    {
        HirUnionDecl* union_decl = (HirUnionDecl*)calloc(1, sizeof(HirUnionDecl));
        HirType* result_type = 0;
        HirUnionVariant value_variant;
        HirUnionVariant error_variant;
        HirErrorableEntry entry;
        if (!union_decl) {
            fail(ctx, "out of memory");
            return 0;
        }
        union_decl->name = "__result";
        union_decl->errorable_flag = 1;
        memset(&value_variant, 0, sizeof(value_variant));
        memset(&error_variant, 0, sizeof(error_variant));
        value_variant.name = "value";
        value_variant.payload_type = value_type;
        value_variant.tag_value = 0;
        error_variant.name = "error";
        error_variant.payload_type = error_type;
        error_variant.tag_value = 1;
        union_variant_list_push(&union_decl->variants, value_variant);
        union_variant_list_push(&union_decl->variants, error_variant);
        union_decl->payload_size = type_size_bytes(value_type);
        if (type_size_bytes(error_type) > union_decl->payload_size) {
            union_decl->payload_size = type_size_bytes(error_type);
        }
        union_decl->payload_align = type_alignment_bytes(value_type);
        if (type_alignment_bytes(error_type) > union_decl->payload_align) {
            union_decl->payload_align = type_alignment_bytes(error_type);
        }
        result_type = new_owned_type(ctx->program, HIR_TYPE_UNION);
        result_type->union_decl = union_decl;
        memset(&entry, 0, sizeof(entry));
        entry.value_type = value_type;
        entry.error_type = error_type;
        entry.result_type = result_type;
        entry.union_decl = union_decl;
        errorable_entry_list_push(&ctx->program->errorable_types, entry);
        return result_type;
    }
}

static HirType* lower_type(LowerContext* ctx, const AstType* type) {
    int i = 0;
    switch (type->kind) {
        case AST_TYPE_INT:
            return qualified_primitive_type(ctx->program, HIR_TYPE_INT, type->mutable_flag);
        case AST_TYPE_I8:
            return qualified_primitive_type(ctx->program, HIR_TYPE_I8, type->mutable_flag);
        case AST_TYPE_I16:
            return qualified_primitive_type(ctx->program, HIR_TYPE_I16, type->mutable_flag);
        case AST_TYPE_I32:
            return qualified_primitive_type(ctx->program, HIR_TYPE_I32, type->mutable_flag);
        case AST_TYPE_I64:
            return qualified_primitive_type(ctx->program, HIR_TYPE_I64, type->mutable_flag);
        case AST_TYPE_U8:
            return qualified_primitive_type(ctx->program, HIR_TYPE_U8, type->mutable_flag);
        case AST_TYPE_U16:
            return qualified_primitive_type(ctx->program, HIR_TYPE_U16, type->mutable_flag);
        case AST_TYPE_U32:
            return qualified_primitive_type(ctx->program, HIR_TYPE_U32, type->mutable_flag);
        case AST_TYPE_U64:
            return qualified_primitive_type(ctx->program, HIR_TYPE_U64, type->mutable_flag);
        case AST_TYPE_F16:
            return qualified_primitive_type(ctx->program, HIR_TYPE_F16, type->mutable_flag);
        case AST_TYPE_F32:
            return qualified_primitive_type(ctx->program, HIR_TYPE_F32, type->mutable_flag);
        case AST_TYPE_F64:
            return qualified_primitive_type(ctx->program, HIR_TYPE_F64, type->mutable_flag);
        case AST_TYPE_FLOAT:
            return qualified_primitive_type(ctx->program, HIR_TYPE_FLOAT, type->mutable_flag);
        case AST_TYPE_DOUBLE:
            return qualified_primitive_type(ctx->program, HIR_TYPE_DOUBLE, type->mutable_flag);
        case AST_TYPE_CHARACTER:
            return qualified_primitive_type(ctx->program, HIR_TYPE_CHARACTER, type->mutable_flag);
        case AST_TYPE_UINT8:
            return qualified_primitive_type(ctx->program, HIR_TYPE_UINT8, type->mutable_flag);
        case AST_TYPE_BOOL:
            return qualified_primitive_type(ctx->program, HIR_TYPE_BOOL, type->mutable_flag);
        case AST_TYPE_VOID:
            return qualified_primitive_type(ctx->program, HIR_TYPE_VOID, type->mutable_flag);
        case AST_TYPE_REFERENCE: {
            HirType* reference = new_owned_type(ctx->program, HIR_TYPE_REFERENCE);
            reference->mutable_flag = type->mutable_flag;
            reference->array_item = lower_type(ctx, type->array_item);
            return reference;
        }
        case AST_TYPE_POINTER: {
            HirType* pointer = new_owned_type(ctx->program, HIR_TYPE_POINTER);
            pointer->mutable_flag = type->mutable_flag;
            pointer->array_item = lower_type(ctx, type->array_item);
            return pointer;
        }
        case AST_TYPE_MANY_POINTER: {
            HirType* pointer = new_owned_type(ctx->program, HIR_TYPE_MANY_POINTER);
            pointer->mutable_flag = type->mutable_flag;
            pointer->array_item = lower_type(ctx, type->array_item);
            return pointer;
        }
        case AST_TYPE_SLICE: {
            HirType* slice = new_owned_type(ctx->program, HIR_TYPE_SLICE);
            slice->mutable_flag = type->mutable_flag;
            slice->array_item = lower_type(ctx, type->array_item);
            return slice;
        }
        case AST_TYPE_ARRAY: {
            HirType* array = new_owned_type(ctx->program, HIR_TYPE_ARRAY);
            array->mutable_flag = type->mutable_flag;
            array->array_item = lower_type(ctx, type->array_item);
            array->array_length = type->array_length;
            return array;
        }
        case AST_TYPE_OPTIONAL: {
            HirType* optional = new_owned_type(ctx->program, HIR_TYPE_OPTIONAL);
            optional->mutable_flag = type->mutable_flag;
            optional->array_item = lower_type(ctx, type->array_item);
            return optional;
        }
        case AST_TYPE_ERRORABLE:
            return get_errorable_type(ctx, lower_type(ctx, type->array_item), lower_type(ctx, type->error_type));
        case AST_TYPE_NAMED: {
            if (type->named_name && strcmp(type->named_name, "Fn") == 0) {
                HirType* fn_type = 0;
                if (type->type_args.count == 0) {
                    fail(ctx, "Fn requires at least a return type");
                    return 0;
                }
                fn_type = new_owned_type(ctx->program, HIR_TYPE_FUNCTION);
                fn_type->mutable_flag = type->mutable_flag;
                fn_type->return_type = lower_type(ctx, &type->type_args.items[0]);
                if (!fn_type->return_type) {
                    return 0;
                }
                for (i = 1; i < type->type_args.count; ++i) {
                    HirType* param_type = lower_type(ctx, &type->type_args.items[i]);
                    if (!param_type) {
                        return 0;
                    }
                    type_list_push(&fn_type->tuple_items, param_type);
                }
                return fn_type;
            }
            {
                HirType* nominal_type = resolve_owner_type_name(ctx, type->named_name);
                if (!nominal_type) {
                    static char unknown_named_type_error[256];
                    snprintf(unknown_named_type_error, sizeof(unknown_named_type_error),
                             "unknown named type: %s", type->named_name ? type->named_name : "<null>");
                    fail(ctx, unknown_named_type_error);
                    return 0;
                }
                if (nominal_type->kind == HIR_TYPE_ENUM ||
                    nominal_type->kind == HIR_TYPE_STRUCT ||
                    nominal_type->kind == HIR_TYPE_UNION) {
                    return requalify_nominal_owner_type(ctx, nominal_type, type->mutable_flag);
                }
            }
            {
                static char unknown_named_type_error[256];
                snprintf(unknown_named_type_error, sizeof(unknown_named_type_error),
                         "unknown named type: %s", type->named_name ? type->named_name : "<null>");
                fail(ctx, unknown_named_type_error);
            }
            return 0;
        }
        case AST_TYPE_TUPLE: {
            HirType* tuple = new_owned_type(ctx->program, HIR_TYPE_TUPLE);
            tuple->mutable_flag = type->mutable_flag;
            for (i = 0; i < type->tuple_items.count; ++i) {
                type_list_push(&tuple->tuple_items, lower_type(ctx, &type->tuple_items.items[i]));
            }
            return tuple;
        }
        default:
            return primitive_type(ctx->program, HIR_TYPE_INT);
    }
}

static HirBinding* new_binding(HirType* type, int mutable_flag, const char* name, HirBindingKind kind, int line) {
    HirBinding* binding = (HirBinding*)calloc(1, sizeof(HirBinding));
    binding->type = type;
    binding->name = (char*)name;
    binding->mutable_flag = mutable_flag;
    binding->kind = kind;
    binding->line = line;
    return binding;
}

static HirType* function_type_from_function(LowerContext* ctx, HirFunction* fn) {
    int i = 0;
    HirType* type = 0;
    if (!fn) {
        return 0;
    }
    type = new_owned_type(ctx->program, HIR_TYPE_FUNCTION);
    type->return_type = fn->return_type;
    for (i = 0; i < fn->params.count; ++i) {
        type_list_push(&type->tuple_items, fn->params.items[i]->type);
    }
    return type;
}

static HirExpr* new_expr(HirExprKind kind, HirType* type, int line) {
    HirExpr* expr = (HirExpr*)calloc(1, sizeof(HirExpr));
    expr->kind = kind;
    expr->type = type;
    expr->line = line;
    expr->column = g_active_lower_ctx ? g_active_lower_ctx->current_column : 0;
    return expr;
}

static HirStmt* new_stmt(HirStmtKind kind, int line) {
    HirStmt* stmt = (HirStmt*)calloc(1, sizeof(HirStmt));
    stmt->kind = kind;
    stmt->line = line;
    stmt->column = g_active_lower_ctx ? g_active_lower_ctx->current_column : 0;
    return stmt;
}

static int lower_block_expr_stmts(LowerContext* ctx, const AstBlock* ast_block, HirBlock* out_block) {
    int i = 0;
    for (i = 0; i < ast_block->stmts.count; ++i) {
        const AstStmt* stmt = ast_block->stmts.items[i];
        HirStmt* lowered = 0;
        int is_last = i + 1 == ast_block->stmts.count;
        if (stmt->kind == AST_STMT_GROUP) {
            if (!lower_block_expr_stmts(ctx, &stmt->as.group_stmt, out_block)) {
                return 0;
            }
            continue;
        }
        if (stmt->kind == AST_STMT_VAR_DECL &&
            stmt->as.var_decl.init &&
            stmt->as.var_decl.init->kind == AST_EXPR_COALESCE_CONTROL) {
            return fail(ctx, "coalesce control is not supported in expression block");
        }
        switch (stmt->kind) {
            case AST_STMT_VAR_DECL:
            case AST_STMT_ASSIGN:
            case AST_STMT_EXPR:
                break;
            case AST_STMT_RETURN:
            case AST_STMT_THROW:
                if (is_last) {
                    break;
                }
                return fail(ctx, "early-exit statement must end expression block");
            default:
                return fail(ctx, "unsupported statement in expression block");
        }
        lowered = lower_stmt(ctx, stmt);
        if (!lowered) {
            return 0;
        }
        if (lowered->kind != HIR_STMT_VAR_DECL &&
            lowered->kind != HIR_STMT_ASSIGN &&
            lowered->kind != HIR_STMT_EXPR &&
            lowered->kind != HIR_STMT_RETURN &&
            lowered->kind != HIR_STMT_THROW) {
            return fail(ctx, "unsupported statement in expression block");
        }
        if (lowered->kind == HIR_STMT_EXPR &&
            lowered->as.expr_stmt.expr &&
            lowered->as.expr_stmt.expr->kind == HIR_EXPR_PROPAGATE) {
            return fail(ctx, "implicit error propagation is not supported in expression block statement");
        }
        stmt_list_push(&out_block->stmts, lowered);
    }
    return 1;
}

static int hir_expr_is_never_exit(const HirExpr* expr) {
    return expr && expr->kind == HIR_EXPR_BLOCK && expr->as.block_expr.value == 0;
}

static HirStmtKind stmt_kind_from_ast(AstStmtKind kind) {
    switch (kind) {
        case AST_STMT_RETURN: return HIR_STMT_RETURN;
        case AST_STMT_THROW: return HIR_STMT_THROW;
        case AST_STMT_VAR_DECL: return HIR_STMT_VAR_DECL;
        case AST_STMT_GROUP: return HIR_STMT_EXPR;
        case AST_STMT_ASSIGN: return HIR_STMT_ASSIGN;
        case AST_STMT_IF: return HIR_STMT_IF;
        case AST_STMT_TRY: return HIR_STMT_TRY;
        case AST_STMT_EXPR_CATCH: return HIR_STMT_TRY;
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

static HirExpr* make_optional_value_expr(LowerContext* ctx, HirExpr* value, int line) {
    HirExpr* expr = 0;
    if (!value || value->type->kind != HIR_TYPE_OPTIONAL) {
        return 0;
    }
    expr = new_expr(HIR_EXPR_OPTIONAL_VALUE, value->type->array_item, line);
    expr->as.optional_value.value = value;
    return expr;
}

static HirType* make_optional_type(LowerContext* ctx, HirType* inner) {
    HirType* type = 0;
    if (inner->kind == HIR_TYPE_OPTIONAL) {
        return inner;
    }
    type = new_owned_type(ctx->program, HIR_TYPE_OPTIONAL);
    type->array_item = inner;
    return type;
}

static HirExpr* wrap_optional_result(LowerContext* ctx, HirExpr* value, int line) {
    HirExpr* some = 0;
    if (!value) {
        return 0;
    }
    if (value->type->kind == HIR_TYPE_OPTIONAL) {
        return value;
    }
    some = new_expr(HIR_EXPR_OPTIONAL_SOME, make_optional_type(ctx, value->type), line);
    some->as.unary.value = value;
    return some;
}

static HirExpr* make_null_expr(HirType* optional_type, int line) {
    return new_expr(HIR_EXPR_NULL, optional_type, line);
}

static HirExpr* maybe_wrap_expected_optional_expr(LowerContext* ctx, HirExpr* value, HirType* expected_type, int line) {
    if (!value) {
        return 0;
    }
    if (expected_type && expected_type->kind == HIR_TYPE_OPTIONAL && type_equals(expected_type->array_item, value->type)) {
        HirExpr* some = new_expr(HIR_EXPR_OPTIONAL_SOME, expected_type, line);
        some->as.unary.value = value;
        return some;
    }
    return value;
}

static HirExpr* maybe_wrap_expected_errorable_expr(LowerContext* ctx, HirExpr* value, HirType* expected_type, int line) {
    HirType* current_return_type = 0;
    if (!value) {
        return 0;
    }
    current_return_type = ctx->current_function ? ctx->current_function->return_type : 0;
    if (expected_type && is_errorable_type(expected_type)) {
        if (is_errorable_type(value->type)) {
            return value;
        }
        if (type_equals(value->type, errorable_value_type(expected_type))) {
            HirExpr* wrapped = new_expr(HIR_EXPR_VARIANT, expected_type, line);
            wrapped->as.variant.variant = &expected_type->union_decl->variants.items[0];
            wrapped->as.variant.payload = value;
            return wrapped;
        }
        return value;
    }
    if (expected_type && !is_errorable_type(expected_type) && is_errorable_type(value->type) &&
        type_equals(expected_type, errorable_value_type(value->type))) {
        HirExpr* propagate = new_expr(HIR_EXPR_PROPAGATE, expected_type, line);
        if (current_try_catches_error(ctx, errorable_error_type(value->type))) {
            propagate->as.propagate.value = value;
            propagate->as.propagate.result_type = 0;
            return propagate;
        }
        if (is_errorable_type(current_return_type) &&
            type_equals(errorable_error_type(current_return_type), errorable_error_type(value->type))) {
            propagate->as.propagate.value = value;
            propagate->as.propagate.result_type = current_return_type;
            return propagate;
        }
    }
    return value;
}

static HirExpr* lower_expr_value(LowerContext* ctx, const AstExpr* expr) {
    HirExpr* value = lower_expr(ctx, expr);
    HirType* current_return_type = 0;
    if (!value) {
        return 0;
    }
    if (!is_errorable_type(value->type)) {
        return value;
    }
    current_return_type = ctx->current_function ? ctx->current_function->return_type : 0;
    if (current_try_catches_error(ctx, errorable_error_type(value->type))) {
        HirExpr* propagate = new_expr(HIR_EXPR_PROPAGATE, errorable_value_type(value->type), expr->line);
        propagate->as.propagate.value = value;
        propagate->as.propagate.result_type = 0;
        return propagate;
    }
    if (ctx->infer_try_error_flag && ctx->try_scopes.count > 0) {
        if (!ctx->inferred_try_error_type) {
            ctx->inferred_try_error_type = errorable_error_type(value->type);
        } else if (!type_equals(ctx->inferred_try_error_type, errorable_error_type(value->type))) {
            fail(ctx, "try expression body must use a single error type");
            return 0;
        }
        {
            HirExpr* propagate = new_expr(HIR_EXPR_PROPAGATE, errorable_value_type(value->type), expr->line);
            propagate->as.propagate.value = value;
            propagate->as.propagate.result_type = 0;
            return propagate;
        }
    }
    if (is_errorable_type(current_return_type) &&
        type_equals(errorable_error_type(current_return_type), errorable_error_type(value->type))) {
        HirExpr* propagate = new_expr(HIR_EXPR_PROPAGATE, errorable_value_type(value->type), expr->line);
        propagate->as.propagate.value = value;
        propagate->as.propagate.result_type = current_return_type;
        return propagate;
    }
    if (ctx->try_scopes.count > 0) {
        fail(ctx, "uncaught error type in try block");
        return 0;
    }
    fail(ctx, "errorable expression must be handled");
    return 0;
}

static HirExpr* lower_optional_chain_field(LowerContext* ctx, const AstExpr* expr) {
    HirExpr* base = 0;
    HirExpr* some_base = 0;
    HirExpr* access = 0;
    HirExpr* cond = 0;
    HirExpr* out = 0;
    HirType* result_type = 0;
    if (!is_pure_optional_base_expr(expr->as.field.base)) {
        fail(ctx, "optional chain requires pure base expression");
        return 0;
    }
    base = lower_expr(ctx, expr->as.field.base);
    if (!base) {
        return 0;
    }
    if (base->type->kind != HIR_TYPE_OPTIONAL) {
        fail(ctx, "optional chain requires optional base");
        return 0;
    }
    some_base = make_optional_value_expr(ctx, base, expr->line);
    if (!some_base) {
        return 0;
    }
    if (base->type->array_item->kind == HIR_TYPE_STRUCT) {
        int field_index = -1;
        HirStructField* field = find_struct_field(base->type->array_item->struct_decl, expr->as.field.name, &field_index);
        if (!field) {
            fail_unknown_field(ctx, base->type->array_item->struct_decl->name, expr->as.field.name);
            return 0;
        }
        access = new_expr(HIR_EXPR_STRUCT_FIELD, field->type, expr->line);
        access->as.struct_field.base = some_base;
        access->as.struct_field.field = field;
        access->as.struct_field.field_index = field_index;
    } else {
        fail(ctx, "unknown field");
        return 0;
    }
    access = wrap_optional_result(ctx, access, expr->line);
    if (!access) {
        return 0;
    }
    result_type = access->type;
    cond = new_expr(HIR_EXPR_BINARY, primitive_type(ctx->program, HIR_TYPE_BOOL), expr->line);
    cond->as.binary.op = HIR_BIN_NE;
    cond->as.binary.left = base;
    cond->as.binary.right = make_null_expr(base->type, expr->line);
    out = new_expr(HIR_EXPR_TERNARY, result_type, expr->line);
    out->as.ternary.cond = cond;
    out->as.ternary.then_expr = access;
    out->as.ternary.else_expr = make_null_expr(result_type, expr->line);
    return out;
}

static HirExpr* lower_optional_chain_index(LowerContext* ctx, const AstExpr* expr) {
    HirExpr* base = 0;
    HirExpr* index = 0;
    HirExpr* some_base = 0;
    HirExpr* access = 0;
    HirExpr* cond = 0;
    HirExpr* out = 0;
    HirType* result_type = 0;
    if (!is_pure_optional_base_expr(expr->as.index.base)) {
        fail(ctx, "optional chain requires pure base expression");
        return 0;
    }
    base = lower_expr(ctx, expr->as.index.base);
    if (!base) {
        return 0;
    }
    if (base->type->kind != HIR_TYPE_OPTIONAL) {
        fail(ctx, "optional chain requires optional base");
        return 0;
    }
    index = lower_expr(ctx, expr->as.index.index);
    if (!index) {
        return 0;
    }
    some_base = make_optional_value_expr(ctx, base, expr->line);
    if (!some_base) {
        return 0;
    }
    if ((some_base->type->kind == HIR_TYPE_REFERENCE || some_base->type->kind == HIR_TYPE_POINTER) &&
        some_base->type->array_item &&
        (some_base->type->array_item->kind == HIR_TYPE_TUPLE ||
         some_base->type->array_item->kind == HIR_TYPE_ARRAY ||
         some_base->type->array_item->kind == HIR_TYPE_SLICE)) {
        HirExpr* deref = new_expr(HIR_EXPR_DEREF, some_base->type->array_item, expr->line);
        deref->as.unary.value = some_base;
        some_base = deref;
    }
    if (some_base->type->kind == HIR_TYPE_TUPLE) {
        if (expr->as.index.index->kind != AST_EXPR_INT) {
            fail(ctx, "tuple index must be an integer literal");
            return 0;
        }
        if (expr->as.index.index->as.int_value < 0 || expr->as.index.index->as.int_value >= some_base->type->tuple_items.count) {
            fail(ctx, "tuple index out of bounds");
            return 0;
        }
        access = new_expr(HIR_EXPR_INDEX, some_base->type->tuple_items.items[expr->as.index.index->as.int_value], expr->line);
    } else if (some_base->type->kind == HIR_TYPE_ARRAY || some_base->type->kind == HIR_TYPE_SLICE || some_base->type->kind == HIR_TYPE_MANY_POINTER) {
        if (index->type->kind != HIR_TYPE_INT) {
            fail(ctx, "array index must be Int");
            return 0;
        }
        access = new_expr(HIR_EXPR_INDEX, some_base->type->array_item, expr->line);
    } else {
        fail(ctx, "indexing currently requires a tuple, array, slice, or many-pointer base");
        return 0;
    }
    access->as.index.base = some_base;
    access->as.index.index = index;
    access = wrap_optional_result(ctx, access, expr->line);
    if (!access) {
        return 0;
    }
    result_type = access->type;
    cond = new_expr(HIR_EXPR_BINARY, primitive_type(ctx->program, HIR_TYPE_BOOL), expr->line);
    cond->as.binary.op = HIR_BIN_NE;
    cond->as.binary.left = base;
    cond->as.binary.right = make_null_expr(base->type, expr->line);
    out = new_expr(HIR_EXPR_TERNARY, result_type, expr->line);
    out->as.ternary.cond = cond;
    out->as.ternary.then_expr = access;
    out->as.ternary.else_expr = make_null_expr(result_type, expr->line);
    return out;
}

static HirExpr* lower_optional_chain_implicit(LowerContext* ctx, const AstExpr* expr) {
    HirExpr* base = 0;
    HirExpr* free_value = 0;
    HirExpr* cond = 0;
    HirExpr* then_expr = 0;
    HirExpr* else_expr = 0;
    HirExpr* out = 0;
    HirType* void_type = primitive_type(ctx->program, HIR_TYPE_VOID);
    if (expr->as.implicit.target_is_type) {
        fail(ctx, "optional chain requires value target");
        return 0;
    }
    if (strcmp(expr->as.implicit.member, "free") != 0) {
        fail(ctx, "optional implicit chain currently supports '.free()'");
        return 0;
    }
    if (expr->as.implicit.has_type_arg || expr->as.implicit.args.count != 0) {
        fail(ctx, "implicit operation '.free()' takes no arguments");
        return 0;
    }
    if (!is_pure_optional_base_expr(expr->as.implicit.value_target)) {
        fail(ctx, "optional chain requires pure base expression");
        return 0;
    }
    base = lower_expr_preserve_pointer(ctx, expr->as.implicit.value_target);
    if (!base) {
        return 0;
    }
    if (base->type->kind == HIR_TYPE_OPTIONAL) {
        free_value = make_optional_value_expr(ctx, base, expr->line);
        if (!free_value) {
            return 0;
        }
    } else {
        free_value = base;
    }
    if (free_value->type->kind != HIR_TYPE_POINTER && free_value->type->kind != HIR_TYPE_MANY_POINTER) {
        fail(ctx, "free requires pointer");
        return 0;
    }
    if (free_value->kind == HIR_EXPR_BINDING) {
        mark_binding_freed(ctx, free_value->as.binding);
    } else if (base->kind == HIR_EXPR_BINDING) {
        mark_binding_freed(ctx, base->as.binding);
    }
    cond = new_expr(HIR_EXPR_BINARY, primitive_type(ctx->program, HIR_TYPE_BOOL), expr->line);
    cond->as.binary.op = HIR_BIN_NE;
    cond->as.binary.left = base;
    cond->as.binary.right = make_null_expr(base->type, expr->line);
    then_expr = new_expr(HIR_EXPR_FREE, void_type, expr->line);
    then_expr->as.unary.value = free_value;
    else_expr = new_expr(HIR_EXPR_NULL, void_type, expr->line);
    out = new_expr(HIR_EXPR_IF, void_type, expr->line);
    out->as.if_expr.cond = cond;
    out->as.if_expr.then_expr = then_expr;
    out->as.if_expr.else_expr = else_expr;
    return out;
}

static int bind_optional_narrow(LowerContext* ctx, const char* name, HirBlock* out_block, int line) {
    HirBinding* original = lookup_binding(ctx, name);
    HirBinding* narrowed = 0;
    HirStmt* decl = 0;
    HirExpr* original_expr = 0;
    HirExpr* unwrapped = 0;
    if (!original || original->type->kind != HIR_TYPE_OPTIONAL) {
        return 1;
    }
    narrowed = new_binding(original->type->array_item, 0, (char*)name, HIR_BINDING_LOCAL, line);
    if (!bind_in_current_scope(ctx, narrowed)) {
        return 0;
    }
    binding_list_push(&ctx->current_function->locals, narrowed);
    original_expr = new_expr(HIR_EXPR_BINDING, original->type, line);
    original_expr->as.binding = original;
    unwrapped = make_optional_value_expr(ctx, original_expr, line);
    if (!unwrapped) {
        return 0;
    }
    decl = new_stmt(HIR_STMT_VAR_DECL, line);
    decl->as.var_decl.binding = narrowed;
    decl->as.var_decl.init = unwrapped;
    stmt_list_push(&out_block->stmts, decl);
    return 1;
}

static int apply_array_length_inference(HirType* declared, HirType* actual) {
    if (!declared || !actual) {
        return 0;
    }
    if (declared->kind == HIR_TYPE_ARRAY && actual->kind == HIR_TYPE_ARRAY && declared->array_length < 0) {
        if (!type_assignment_compatible(actual->array_item, declared->array_item)) {
            return 0;
        }
        declared->array_length = actual->array_length;
        return 1;
    }
    return 0;
}

static HirExpr* maybe_decay_array_to_slice(LowerContext* ctx, HirExpr* expr, HirType* expected_type, int line) {
    HirExpr* slice = 0;
    if (!expr || !expected_type) {
        return expr;
    }
    if (expected_type->kind != HIR_TYPE_SLICE || expr->type->kind != HIR_TYPE_ARRAY) {
        return expr;
    }
    if (!type_assignment_compatible_inner(expr->type->array_item, expected_type->array_item, 1)) {
        fail(ctx, "array to slice type mismatch");
        return 0;
    }
    slice = new_expr(HIR_EXPR_SLICE, expected_type, line);
    if (!slice) {
        return 0;
    }
    slice->as.slice.base = expr;
    slice->as.slice.start = 0;
    slice->as.slice.end = 0;
    return slice;
}

static int ast_range_bounds(const AstExpr* expr, const AstExpr** start, const AstExpr** end) {
    int i = 0;
    if (!expr ||
        expr->kind != AST_EXPR_STRUCT ||
        !expr->as.struct_lit.type_name ||
        strcmp(expr->as.struct_lit.type_name, "Range") != 0) {
        return 0;
    }
    *start = 0;
    *end = 0;
    for (i = 0; i < expr->as.struct_lit.fields.count; ++i) {
        AstStructFieldInit* field = &expr->as.struct_lit.fields.items[i];
        if (strcmp(field->name, "start") == 0) {
            *start = field->value;
        } else if (strcmp(field->name, "end") == 0) {
            *end = field->value;
        }
    }
    return *start && *end;
}

static HirExpr* maybe_auto_deref_pointer_expr(HirExpr* expr, HirType* expected_type, int line) {
    HirExpr* deref = 0;
    if (!expr || !expr->type || (expr->type->kind != HIR_TYPE_REFERENCE && expr->type->kind != HIR_TYPE_POINTER)) {
        return expr;
    }
    if (expected_type &&
        (expected_type->kind == HIR_TYPE_REFERENCE || expected_type->kind == HIR_TYPE_POINTER || expected_type->kind == HIR_TYPE_MANY_POINTER)) {
        return expr;
    }
    deref = new_expr(HIR_EXPR_DEREF, expr->type->array_item, line);
    deref->as.unary.value = expr;
    return deref;
}

static HirExpr* lower_expr_preserve_pointer(LowerContext* ctx, const AstExpr* expr) {
    HirExpr* base = 0;
    HirType* base_type = 0;
    HirStructField* field = 0;
    int field_index = -1;
    HirBinding* binding = 0;
    HirExpr* out = 0;
    if (expr && expr->kind == AST_EXPR_NAME) {
        binding = lookup_binding(ctx, expr->as.name);
        if (!binding) {
            static char unknown_identifier_error[256];
            snprintf(unknown_identifier_error, sizeof(unknown_identifier_error),
                     "unknown identifier: %s", expr->as.name ? expr->as.name : "<null>");
            fail(ctx, unknown_identifier_error);
            return 0;
        }
        out = new_expr(HIR_EXPR_BINDING, binding->type, expr->line);
        out->as.binding = binding;
        return out;
    }
    if (expr && expr->kind == AST_EXPR_FIELD) {
        base = lower_expr_preserve_pointer(ctx, expr->as.field.base);
        if (!base) {
            return 0;
        }
        base_type = base->type;
        if ((base_type->kind == HIR_TYPE_REFERENCE || base_type->kind == HIR_TYPE_POINTER) && base_type->array_item && base_type->array_item->kind == HIR_TYPE_STRUCT) {
            base_type = base_type->array_item;
        }
        if (base_type->kind != HIR_TYPE_STRUCT) {
            return lower_expr(ctx, expr);
        }
        field = find_struct_field(base_type->struct_decl, expr->as.field.name, &field_index);
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
    return lower_expr(ctx, expr);
}

static int is_binding_freed(LowerContext* ctx, HirBinding* binding) {
    int i = 0;
    for (i = 0; i < ctx->freed_bindings.count; ++i) {
        if (ctx->freed_bindings.items[i] == binding) {
            return 1;
        }
    }
    return 0;
}

static void mark_binding_freed(LowerContext* ctx, HirBinding* binding) {
    if (!binding || is_binding_freed(ctx, binding)) {
        return;
    }
    binding_list_push(&ctx->freed_bindings, binding);
}

static int is_lvalue_expr(const HirExpr* expr) {
    if (!expr) {
        return 0;
    }
    switch (expr->kind) {
        case HIR_EXPR_BINDING:
        case HIR_EXPR_STRUCT_FIELD:
        case HIR_EXPR_INDEX:
        case HIR_EXPR_DEREF:
            return 1;
        default:
            return 0;
    }
}

static int is_mutable_assignment_target(const HirExpr* expr) {
    if (!expr) {
        return 0;
    }
    switch (expr->kind) {
        case HIR_EXPR_BINDING:
            return expr->as.binding && expr->as.binding->mutable_flag;
        case HIR_EXPR_STRUCT_FIELD:
            return expr->as.struct_field.field && expr->as.struct_field.field->mutable_flag;
        case HIR_EXPR_INDEX:
            if (!expr->as.index.base || !expr->as.index.base->type) {
                return 0;
            }
            if ((expr->as.index.base->type->kind == HIR_TYPE_ARRAY ||
                 expr->as.index.base->type->kind == HIR_TYPE_SLICE ||
                 expr->as.index.base->type->kind == HIR_TYPE_MANY_POINTER) &&
                expr->as.index.base->type->array_item) {
                return expr->as.index.base->type->mutable_flag ||
                       expr->as.index.base->type->array_item->mutable_flag;
            }
            return 0;
        case HIR_EXPR_DEREF:
            return expr->type && expr->type->mutable_flag;
        default:
            return 0;
    }
}

static int is_struct_init_self_field_assign(LowerContext* ctx, const AstExpr* expr) {
    return ctx &&
           ctx->current_function &&
           ctx->current_function->struct_init_flag &&
           expr &&
           expr->kind == AST_EXPR_FIELD &&
           expr->as.field.base &&
           expr->as.field.base->kind == AST_EXPR_NAME &&
           strcmp(expr->as.field.base->as.name, "self") == 0;
}

static int fail(LowerContext* ctx, const char* error) {
    ctx->error = error;
    if (!ctx->error_line) {
        ctx->error_line = ctx->current_line;
        ctx->error_column = ctx->current_column;
    }
    return 0;
}

static int fail_at(LowerContext* ctx, int line, int column, const char* error) {
    ctx->current_line = line;
    ctx->current_column = column;
    ctx->error_line = line;
    ctx->error_column = column;
    ctx->error = error;
    return 0;
}

static int fail_unknown_field(LowerContext* ctx, const char* struct_name, const char* field_name) {
    char buffer[512];
    char* message = 0;
    if (struct_name && field_name) {
        snprintf(buffer, sizeof(buffer), "unknown field '%s' in struct '%s'", field_name, struct_name);
    } else if (field_name) {
        snprintf(buffer, sizeof(buffer), "unknown field '%s'", field_name);
    } else {
        snprintf(buffer, sizeof(buffer), "unknown field");
    }
    message = strdup(buffer);
    if (!message) {
        return fail(ctx, "unknown field");
    }
    return fail(ctx, message);
}

static int function_param_types_equal(const HirBindingList* left, const HirBindingList* right) {
    int i = 0;
    if (left->count != right->count) {
        return 0;
    }
    for (i = 0; i < left->count; ++i) {
        if (!type_equals(left->items[i]->type, right->items[i]->type)) {
            return 0;
        }
    }
    return 1;
}

static int function_signature_conflicts(HirFunction* left, HirFunction* right) {
    if (left->struct_init_flag || right->struct_init_flag) {
        return left->struct_init_flag &&
               right->struct_init_flag &&
               left->owner_struct == right->owner_struct &&
               ((left->source_name == 0 && right->source_name == 0) ||
                (left->source_name && right->source_name && strcmp(left->source_name, right->source_name) == 0)) &&
               function_param_types_equal(&left->params, &right->params);
    }
    if (left->struct_deinit_flag || right->struct_deinit_flag) {
        return left->struct_deinit_flag &&
               right->struct_deinit_flag &&
               left->owner_struct == right->owner_struct;
    }
    if (left->method_flag != right->method_flag) {
        return 0;
    }
    if (!left->method_flag) {
        return strcmp(left->name, right->name) == 0 &&
               function_param_types_equal(&left->params, &right->params);
    }
    return left->static_method_flag == right->static_method_flag &&
           left->receiver_type &&
           right->receiver_type &&
           same_nominal_type_strict(left->receiver_type, right->receiver_type) &&
           strcmp(left->method_name, right->method_name) == 0 &&
           function_param_types_equal(&left->params, &right->params);
}

static int function_matches_fn_type(HirFunction* fn, HirType* fn_type) {
    int i = 0;
    if (!fn || !fn_type || fn_type->kind != HIR_TYPE_FUNCTION) {
        return 0;
    }
    if (!type_equals(fn->return_type, fn_type->return_type)) {
        return 0;
    }
    if (fn->params.count != fn_type->tuple_items.count) {
        return 0;
    }
    for (i = 0; i < fn->params.count; ++i) {
        if (!type_equals(fn->params.items[i]->type, fn_type->tuple_items.items[i])) {
            return 0;
        }
    }
    return 1;
}

static int function_matches_exact_name(HirFunction* fn, const char* name) {
    return fn &&
           !fn->method_flag &&
           !fn->struct_init_flag &&
           !fn->struct_deinit_flag &&
           strcmp(fn->source_name ? fn->source_name : fn->name, name) == 0;
}

static HirFunction* find_struct_init_function(HirProgram* program, HirStructDecl* struct_decl, int init_index) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        HirFunction* fn = &program->functions.items[i];
        if (fn->struct_init_flag &&
            fn->owner_struct == struct_decl &&
            fn->struct_init_index == init_index) {
            return fn;
        }
    }
    return 0;
}

static int struct_init_name_matches(HirFunction* fn, const char* init_name) {
    if (!fn || !fn->struct_init_flag) {
        return 0;
    }
    if (!init_name) {
        return fn->source_name == 0;
    }
    return fn->source_name && strcmp(fn->source_name, init_name) == 0;
}

static int struct_has_init_name(HirProgram* program, HirStructDecl* struct_decl, const char* init_name) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        HirFunction* fn = &program->functions.items[i];
        if (fn->owner_struct == struct_decl && struct_init_name_matches(fn, init_name)) {
            return 1;
        }
    }
    return 0;
}

static int lower_struct_init_args_for_function(LowerContext* ctx,
                                               const AstStructFieldInitList* ast_args,
                                               HirExprList* out_args,
                                               HirFunction* init_fn) {
    int i = 0;
    if (ast_args->count != init_fn->params.count) {
        return 0;
    }
    for (i = 0; i < ast_args->count; ++i) {
        AstStructFieldInit* ast_arg = &ast_args->items[i];
        HirExpr* arg = 0;
        HirBinding* param = init_fn->params.items[i];
        if (ast_arg->name) {
            return fail_at(ctx, ast_arg->line, 0, "labeled init arguments are not supported");
        }
        arg = lower_expr_expected(ctx, ast_arg->value, param->type);
        if (!arg) {
            return 0;
        }
        arg = maybe_decay_array_to_slice(ctx, arg, param->type, ast_arg->line);
        if (!arg) {
            return 0;
        }
        if ((param->type->kind == HIR_TYPE_REFERENCE ||
             param->type->kind == HIR_TYPE_POINTER ||
             param->type->kind == HIR_TYPE_MANY_POINTER) &&
            param->type->array_item &&
            param->type->array_item->mutable_flag &&
            arg->kind == HIR_EXPR_ADDR &&
            (!arg->as.unary.value || !arg->as.unary.value->type || !arg->as.unary.value->type->mutable_flag)) {
            return fail_at(ctx, ast_arg->line, 0, "init argument type mismatch");
        }
        if (!type_assignment_compatible(arg->type, param->type)) {
            return fail_at(ctx, ast_arg->line, 0, "init argument type mismatch");
        }
        expr_list_push(out_args, arg);
    }
    return 1;
}

static HirGlobal* find_global(HirProgram* program, const char* name) {
    return (HirGlobal*)hashmap_get(&program->global_map, name);
}

static char* make_static_field_global_name(const char* owner, const char* field) {
    size_t owner_len = strlen(owner);
    size_t field_len = strlen(field);
    char* out = (char*)malloc(owner_len + field_len + 3);
    if (!out) {
        return 0;
    }
    memcpy(out, owner, owner_len);
    memcpy(out + owner_len, ".#", 2);
    memcpy(out + owner_len + 2, field, field_len);
    out[owner_len + 2 + field_len] = '\0';
    return out;
}

static HirExpr* make_static_field_expr(LowerContext* ctx, const char* owner, const char* field, int line) {
    char* global_name = make_static_field_global_name(owner, field);
    HirGlobal* global = global_name ? find_global(ctx->program, global_name) : 0;
    HirExpr* out = 0;
    if (!global && owner) {
        const char* dot = strrchr(owner, '.');
        if (dot) {
            size_t prefix_len = (size_t)(dot - owner);
            size_t owner_tail_len = strlen(dot + 1);
            size_t field_len = strlen(field);
            char* imported_hidden_name = (char*)malloc(prefix_len + owner_tail_len + field_len + 5);
            if (imported_hidden_name) {
                memcpy(imported_hidden_name, owner, prefix_len);
                memcpy(imported_hidden_name + prefix_len, ".#", 2);
                memcpy(imported_hidden_name + prefix_len + 2, dot + 1, owner_tail_len);
                memcpy(imported_hidden_name + prefix_len + 2 + owner_tail_len, ".#", 2);
                memcpy(imported_hidden_name + prefix_len + 4 + owner_tail_len, field, field_len);
                imported_hidden_name[prefix_len + 4 + owner_tail_len + field_len] = '\0';
                global = find_global(ctx->program, imported_hidden_name);
                free(imported_hidden_name);
            }
        }
    }
    if (!global && owner) {
        const char* owner_tail = strrchr(owner, '.');
        size_t owner_tail_len = 0;
        size_t field_len = strlen(field);
        int i = 0;
        owner_tail = owner_tail ? owner_tail + 1 : owner;
        owner_tail_len = strlen(owner_tail);
        for (i = 0; i < ctx->program->globals.count; ++i) {
            const char* name = ctx->program->globals.items[i].binding->name;
            size_t name_len = strlen(name);
            if (name_len > field_len + 2 &&
                strcmp(name + name_len - field_len, field) == 0 &&
                name[name_len - field_len - 1] == '#' &&
                name[name_len - field_len - 2] == '.') {
                size_t owner_end = name_len - field_len - 2;
                if ((owner_end >= owner_tail_len &&
                     strncmp(name + owner_end - owner_tail_len, owner_tail, owner_tail_len) == 0) ||
                    (owner_end >= owner_tail_len + 1 &&
                     name[owner_end - owner_tail_len - 1] == '#' &&
                     strncmp(name + owner_end - owner_tail_len, owner_tail, owner_tail_len) == 0)) {
                    global = &ctx->program->globals.items[i];
                    break;
                }
            }
        }
    }
    free(global_name);
    if (!global) {
        return 0;
    }
    out = new_expr(HIR_EXPR_BINDING, global->binding->type, line);
    out->as.binding = global->binding;
    return out;
}

static const HirBuiltinNominalDecl* find_builtin_nominal(const char* name) {
    if (strcmp(name, "Int") == 0) return &HIR_BUILTIN_INT_DECL;
    if (strcmp(name, "Int8") == 0) return &HIR_BUILTIN_I8_DECL;
    if (strcmp(name, "Int16") == 0) return &HIR_BUILTIN_I16_DECL;
    if (strcmp(name, "Int32") == 0) return &HIR_BUILTIN_I32_DECL;
    if (strcmp(name, "Int64") == 0) return &HIR_BUILTIN_I64_DECL;
    if (strcmp(name, "UInt16") == 0) return &HIR_BUILTIN_U16_DECL;
    if (strcmp(name, "UInt32") == 0) return &HIR_BUILTIN_U32_DECL;
    if (strcmp(name, "UInt64") == 0) return &HIR_BUILTIN_U64_DECL;
    if (strcmp(name, "Float16") == 0) return &HIR_BUILTIN_F16_DECL;
    if (strcmp(name, "Float32") == 0) return &HIR_BUILTIN_F32_DECL;
    if (strcmp(name, "Float64") == 0) return &HIR_BUILTIN_F64_DECL;
    if (strcmp(name, "Float") == 0) return &HIR_BUILTIN_FLOAT_DECL;
    if (strcmp(name, "Double") == 0) return &HIR_BUILTIN_DOUBLE_DECL;
    if (strcmp(name, "Char") == 0) return &HIR_BUILTIN_CHARACTER_DECL;
    if (strcmp(name, "UInt8") == 0) return &HIR_BUILTIN_UINT8_DECL;
    if (strcmp(name, "Bool") == 0) return &HIR_BUILTIN_BOOL_DECL;
    if (strcmp(name, "Void") == 0) return &HIR_BUILTIN_VOID_DECL;
    return 0;
}

static HirNominalDeclRef find_nominal_decl(HirProgram* program, const char* name) {
    const HirBuiltinNominalDecl* builtin_decl = find_builtin_nominal(name);
    HirEnumDecl* enum_decl = find_enum(program, name);
    HirStructDecl* struct_decl = 0;
    HirUnionDecl* union_decl = 0;
    HirNominalDeclRef out;
    memset(&out, 0, sizeof(out));
    if (builtin_decl) {
        out.kind = HIR_NOMINAL_BUILTIN;
        out.name = builtin_decl->name;
        out.decl = (void*)builtin_decl;
        return out;
    }
    if (enum_decl) {
        out.kind = HIR_NOMINAL_ENUM;
        out.name = enum_decl->name;
        out.decl = enum_decl;
        return out;
    }
    struct_decl = find_struct(program, name);
    if (struct_decl) {
        out.kind = HIR_NOMINAL_STRUCT;
        out.name = struct_decl->name;
        out.decl = struct_decl;
        return out;
    }
    union_decl = find_union(program, name);
    if (union_decl) {
        out.kind = HIR_NOMINAL_UNION;
        out.name = union_decl->name;
        out.decl = union_decl;
        return out;
    }
    return out;
}

static HirTypeQueryRef describe_hir_type(HirType* type) {
    HirTypeQueryRef out;
    memset(&out, 0, sizeof(out));
    if (!type) {
        return out;
    }
    out.source = type;
    out.array_length = type->array_length;
    switch (type->kind) {
        case HIR_TYPE_INT:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "Int";
            out.nominal.decl = (void*)&HIR_BUILTIN_INT_DECL;
            return out;
        case HIR_TYPE_I8:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "Int8";
            out.nominal.decl = (void*)&HIR_BUILTIN_I8_DECL;
            return out;
        case HIR_TYPE_I16:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "Int16";
            out.nominal.decl = (void*)&HIR_BUILTIN_I16_DECL;
            return out;
        case HIR_TYPE_I32:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "Int32";
            out.nominal.decl = (void*)&HIR_BUILTIN_I32_DECL;
            return out;
        case HIR_TYPE_I64:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "Int64";
            out.nominal.decl = (void*)&HIR_BUILTIN_I64_DECL;
            return out;
        case HIR_TYPE_U8:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "UInt8";
            out.nominal.decl = (void*)&HIR_BUILTIN_U8_DECL;
            return out;
        case HIR_TYPE_U16:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "UInt16";
            out.nominal.decl = (void*)&HIR_BUILTIN_U16_DECL;
            return out;
        case HIR_TYPE_U32:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "UInt32";
            out.nominal.decl = (void*)&HIR_BUILTIN_U32_DECL;
            return out;
        case HIR_TYPE_U64:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "UInt64";
            out.nominal.decl = (void*)&HIR_BUILTIN_U64_DECL;
            return out;
        case HIR_TYPE_F16:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "Float16";
            out.nominal.decl = (void*)&HIR_BUILTIN_F16_DECL;
            return out;
        case HIR_TYPE_F32:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "Float32";
            out.nominal.decl = (void*)&HIR_BUILTIN_F32_DECL;
            return out;
        case HIR_TYPE_F64:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "Float64";
            out.nominal.decl = (void*)&HIR_BUILTIN_F64_DECL;
            return out;
        case HIR_TYPE_FLOAT:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "Float";
            out.nominal.decl = (void*)&HIR_BUILTIN_FLOAT_DECL;
            return out;
        case HIR_TYPE_DOUBLE:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "Double";
            out.nominal.decl = (void*)&HIR_BUILTIN_DOUBLE_DECL;
            return out;
        case HIR_TYPE_CHARACTER:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "Char";
            out.nominal.decl = (void*)&HIR_BUILTIN_CHARACTER_DECL;
            return out;
        case HIR_TYPE_UINT8:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "UInt8";
            out.nominal.decl = (void*)&HIR_BUILTIN_UINT8_DECL;
            return out;
        case HIR_TYPE_BOOL:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "Bool";
            out.nominal.decl = (void*)&HIR_BUILTIN_BOOL_DECL;
            return out;
        case HIR_TYPE_VOID:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_BUILTIN;
            out.nominal.name = "Void";
            out.nominal.decl = (void*)&HIR_BUILTIN_VOID_DECL;
            return out;
        case HIR_TYPE_ENUM:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_ENUM;
            out.nominal.name = type->enum_decl ? type->enum_decl->name : 0;
            out.nominal.decl = type->enum_decl;
            return out;
        case HIR_TYPE_STRUCT:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_STRUCT;
            out.nominal.name = type->struct_decl ? type->struct_decl->name : 0;
            out.nominal.decl = type->struct_decl;
            return out;
        case HIR_TYPE_UNION:
            out.kind = HIR_TYPE_QUERY_NOMINAL;
            out.nominal.kind = HIR_NOMINAL_UNION;
            out.nominal.name = type->union_decl ? type->union_decl->name : 0;
            out.nominal.decl = type->union_decl;
            return out;
        case HIR_TYPE_TUPLE:
            out.kind = HIR_TYPE_QUERY_TUPLE;
            return out;
        case HIR_TYPE_SLICE:
            out.kind = HIR_TYPE_QUERY_SLICE;
            out.item_type = type->array_item;
            return out;
        case HIR_TYPE_REFERENCE:
            out.kind = HIR_TYPE_QUERY_POINTER;
            out.item_type = type->array_item;
            return out;
        case HIR_TYPE_POINTER:
            out.kind = HIR_TYPE_QUERY_POINTER;
            out.item_type = type->array_item;
            return out;
        case HIR_TYPE_MANY_POINTER:
            out.kind = HIR_TYPE_QUERY_MANY_POINTER;
            out.item_type = type->array_item;
            return out;
        case HIR_TYPE_ARRAY:
            out.kind = HIR_TYPE_QUERY_ARRAY;
            out.item_type = type->array_item;
            return out;
        case HIR_TYPE_OPTIONAL:
            out.kind = HIR_TYPE_QUERY_OPTIONAL;
            out.item_type = type->array_item;
            return out;
        default:
            return out;
    }
}

static int same_nominal_type(HirType* left, HirType* right) {
    HirTypeQueryRef left_query = describe_hir_type(left);
    HirTypeQueryRef right_query = describe_hir_type(right);
    if (left_query.kind != HIR_TYPE_QUERY_NOMINAL || right_query.kind != HIR_TYPE_QUERY_NOMINAL) {
        return 0;
    }
    if (left_query.nominal.kind != right_query.nominal.kind) {
        return 0;
    }
    if (left_query.nominal.kind == HIR_NOMINAL_BUILTIN) {
        return left_query.nominal.decl == right_query.nominal.decl;
    }
    if (left_query.nominal.decl == right_query.nominal.decl) {
        return 1;
    }
    return nominal_names_equivalent(left_query.nominal.name, right_query.nominal.name);
}

static int same_nominal_type_strict(HirType* left, HirType* right) {
    HirTypeQueryRef left_query = describe_hir_type(left);
    HirTypeQueryRef right_query = describe_hir_type(right);
    if (left_query.kind != HIR_TYPE_QUERY_NOMINAL || right_query.kind != HIR_TYPE_QUERY_NOMINAL) {
        return 0;
    }
    if (left_query.nominal.kind != right_query.nominal.kind) {
        return 0;
    }
    return left_query.nominal.decl == right_query.nominal.decl;
}

static HirFunction* find_type_method(HirProgram* program, HirType* receiver_type, const char* method_name, int static_flag) {
    HirTypeQueryRef query = describe_hir_type(receiver_type);
    int i = 0;
    int pass = 0;
    if (query.kind != HIR_TYPE_QUERY_NOMINAL) {
        return 0;
    }
    for (pass = 0; pass < 2; ++pass) {
        for (i = 0; i < program->functions.count; ++i) {
            HirFunction* fn = &program->functions.items[i];
            if (!fn->method_flag || fn->static_method_flag != static_flag || !method_source_name_matches(fn->method_name, method_name)) {
                continue;
            }
            if (pass == 0) {
                if (same_nominal_type_strict(receiver_type, fn->receiver_type)) {
                    return fn;
                }
            } else if (!same_nominal_type_strict(receiver_type, fn->receiver_type) &&
                       same_nominal_type(receiver_type, fn->receiver_type)) {
                return fn;
            }
        }
    }
    return 0;
}

static HirBinding* lookup_binding(LowerContext* ctx, const char* name) {
    Scope* scope = ctx->scope;
    while (scope) {
        int i = scope->bindings.count - 1;
        for (; i >= 0; --i) {
            if (strcmp(scope->bindings.items[i]->name, name) == 0) {
                if (is_binding_freed(ctx, scope->bindings.items[i])) {
                    fail(ctx, "use after free");
                    return 0;
                }
                return scope->bindings.items[i];
            }
        }
        scope = scope->parent;
    }
    {
        HirGlobal* global = find_global(ctx->program, name);
        if (global) {
            if (is_binding_freed(ctx, global->binding)) {
                fail(ctx, "use after free");
                return 0;
            }
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

static void rebuild_union_map(HirProgram* program) {
    int i = 0;
    hashmap_free(&program->union_name_map);
    hashmap_init(&program->union_name_map);
    hashmap_free(&program->variant_map);
    hashmap_init(&program->variant_map);
    for (i = 0; i < program->unions.count; ++i) {
        int j = 0;
        hashmap_set(&program->union_name_map, program->unions.items[i].name, &program->unions.items[i]);
        for (j = 0; j < program->unions.items[i].variants.count; ++j) {
            hashmap_set(&program->variant_map, program->unions.items[i].variants.items[j].name, &program->unions.items[i].variants.items[j]);
        }
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

static const AstEnumDecl* find_ast_enum(const AstProgram* ast, const char* name) {
    int i = 0;
    for (i = 0; i < ast->enums.count; ++i) {
        if (strcmp(ast->enums.items[i].name, name) == 0) {
            return &ast->enums.items[i];
        }
    }
    return 0;
}

static const AstUnionDecl* find_ast_union(const AstProgram* ast, const char* name) {
    int i = 0;
    for (i = 0; i < ast->unions.count; ++i) {
        if (strcmp(ast->unions.items[i].name, name) == 0) {
            return &ast->unions.items[i];
        }
    }
    return 0;
}

static int ast_name_list_contains_local(const AstNameList* list, const char* name) {
    int i = 0;
    for (i = 0; i < list->count; ++i) {
        if (strcmp(list->items[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static const char* unqualified_type_name_local(const char* name) {
    const char* last_dot = strrchr(name, '.');
    return last_dot ? last_dot + 1 : name;
}

static int type_name_matches_local(const char* actual_name, const char* target_name) {
    return strcmp(actual_name, target_name) == 0 ||
           strcmp(unqualified_type_name_local(actual_name), target_name) == 0 ||
           strcmp(actual_name, unqualified_type_name_local(target_name)) == 0 ||
           strcmp(unqualified_type_name_local(actual_name), unqualified_type_name_local(target_name)) == 0;
}

static const AstConceptDecl* find_ast_concept_local(const AstProgram* ast, const char* name) {
    int i = 0;
    for (i = 0; i < ast->concepts.count; ++i) {
        if (type_name_matches_local(ast->concepts.items[i].name, name)) {
            return &ast->concepts.items[i];
        }
    }
    return 0;
}

static int concept_name_is_or_inherits_local(const AstProgram* ast,
                                             const char* actual_name,
                                             const char* target_name,
                                             AstNameList* seen_concepts) {
    const AstConceptDecl* concept = 0;
    int i = 0;
    if (type_name_matches_local(actual_name, target_name)) {
        return 1;
    }
    if (ast_name_list_contains_local(seen_concepts, actual_name)) {
        return 0;
    }
    name_list_push(seen_concepts, actual_name);
    concept = find_ast_concept_local(ast, actual_name);
    if (!concept) {
        return 0;
    }
    for (i = 0; i < concept->concept_names.count; ++i) {
        if (concept_name_is_or_inherits_local(ast, concept->concept_names.items[i], target_name, seen_concepts)) {
            return 1;
        }
    }
    return 0;
}

static int nominal_declares_concept_local(const AstProgram* ast,
                                          const AstNameList* concept_names,
                                          const char* concept_name) {
    AstNameList seen_concepts;
    int i = 0;
    memset(&seen_concepts, 0, sizeof(seen_concepts));
    for (i = 0; i < concept_names->count; ++i) {
        if (concept_name_is_or_inherits_local(ast, concept_names->items[i], concept_name, &seen_concepts)) {
            return 1;
        }
    }
    return 0;
}

static int hir_type_declares_concept_or_child(const AstProgram* ast,
                                              HirType* type,
                                              const char* concept_name) {
    if (!type) {
        return 0;
    }
    switch (type->kind) {
        case HIR_TYPE_STRUCT: {
            const AstStructDecl* decl = find_ast_struct(ast, type->struct_decl->name);
            return decl && nominal_declares_concept_local(ast, &decl->concept_names, concept_name);
        }
        case HIR_TYPE_ENUM: {
            const AstEnumDecl* decl = find_ast_enum(ast, type->enum_decl->name);
            return decl && nominal_declares_concept_local(ast, &decl->concept_names, concept_name);
        }
        case HIR_TYPE_UNION: {
            const AstUnionDecl* decl = find_ast_union(ast, type->union_decl->name);
            return decl && nominal_declares_concept_local(ast, &decl->concept_names, concept_name);
        }
        default:
            return 0;
    }
}

static HirType* instance_method_owner_type(HirType* type) {
    if (!type) {
        return 0;
    }
    if ((type->kind == HIR_TYPE_REFERENCE || type->kind == HIR_TYPE_POINTER) && type->array_item &&
        (type->array_item->kind == HIR_TYPE_STRUCT ||
         type->array_item->kind == HIR_TYPE_ENUM ||
         type->array_item->kind == HIR_TYPE_UNION)) {
        return type->array_item;
    }
    if (type->kind == HIR_TYPE_STRUCT ||
        type->kind == HIR_TYPE_ENUM ||
        type->kind == HIR_TYPE_UNION) {
        return type;
    }
    return 0;
}

static HirExpr* make_instance_method_receiver(LowerContext* ctx, HirExpr* base, HirType* owner_type, int line) {
    if (!base || !owner_type) {
        return 0;
    }
    if (owner_type->kind == HIR_TYPE_STRUCT) {
        if (base->type->kind == HIR_TYPE_REFERENCE) {
            return base;
        }
        if (base->type->kind == HIR_TYPE_POINTER) {
            HirExpr* deref = new_expr(HIR_EXPR_DEREF, owner_type, line);
            HirExpr* addr = 0;
            deref->as.unary.value = base;
            addr = new_expr(HIR_EXPR_ADDR, reference_to_type(ctx, owner_type), line);
            addr->as.unary.value = deref;
            return addr;
        }
        if (!is_lvalue_expr(base)) {
            fail(ctx, "indexing requires lvalue base");
            return 0;
        }
        {
            HirExpr* addr = new_expr(HIR_EXPR_ADDR, reference_to_type(ctx, owner_type), line);
            addr->as.unary.value = base;
            return addr;
        }
    }
    if (base->type->kind == HIR_TYPE_REFERENCE || base->type->kind == HIR_TYPE_POINTER) {
        HirExpr* deref = new_expr(HIR_EXPR_DEREF, owner_type, line);
        deref->as.unary.value = base;
        return deref;
    }
    return base;
}

static HirExpr* lower_subscriptable_get_expr(LowerContext* ctx,
                                             HirExpr* base,
                                             const AstExpr* index_expr,
                                             HirType* expected_type,
                                             int line) {
    HirType* owner_type = instance_method_owner_type(base->type);
    HirFunction* method = 0;
    HirExpr* index = 0;
    HirExpr* receiver = 0;
    HirExpr* call = 0;
    if (!owner_type || !hir_type_declares_concept_or_child(ctx->ast, owner_type, "SubscriptGet")) {
        return 0;
    }
    method = find_type_method(ctx->program, owner_type, "subscript_get", 0);
    if (!method) {
        fail(ctx, "unknown function");
        return 0;
    }
    if (!method_visible_from_context(ctx, method, owner_type)) {
        fail(ctx, "unknown function");
        return 0;
    }
    if (method->params.count != 2) {
        fail(ctx, "internal error: invalid subscript_get signature");
        return 0;
    }
    receiver = make_instance_method_receiver(ctx, base, owner_type, line);
    if (!receiver) {
        return 0;
    }
    index = lower_expr_expected(ctx, index_expr, method->params.items[1]->type);
    if (!index) {
        return 0;
    }
    if (!type_assignment_compatible(index->type, method->params.items[1]->type)) {
        fail(ctx, "subscript index type mismatch");
        return 0;
    }
    call = new_expr(HIR_EXPR_CALL, method->return_type, line);
    call->as.call.callee = method;
    call->as.call.builtin = HIR_BUILTIN_NONE;
    expr_list_push(&call->as.call.args, receiver);
    expr_list_push(&call->as.call.args, index);
    return maybe_wrap_expected_optional_expr(ctx, call, expected_type, line);
}

static HirStmt* lower_subscriptable_set_stmt(LowerContext* ctx,
                                             const AstExpr* target,
                                             const AstExpr* value_expr,
                                             int line) {
    HirExpr* base = lower_expr(ctx, target->as.index.base);
    HirType* owner_type = 0;
    HirFunction* method = 0;
    HirExpr* receiver = 0;
    HirExpr* index = 0;
    HirExpr* value = 0;
    HirExpr* call = 0;
    HirStmt* stmt = 0;
    if (!base) {
        return 0;
    }
    owner_type = instance_method_owner_type(base->type);
    if (!owner_type) {
        return 0;
    }
    if (!hir_type_declares_concept_or_child(ctx->ast, owner_type, "SubscriptGet")) {
        return 0;
    }
    if (!hir_type_declares_concept_or_child(ctx->ast, owner_type, "SubscriptSet")) {
        fail(ctx, "assignment target is immutable");
        return 0;
    }
    method = find_type_method(ctx->program, owner_type, "subscript_set", 0);
    if (!method) {
        fail(ctx, "assignment target is immutable");
        return 0;
    }
    if (!method_visible_from_context(ctx, method, owner_type)) {
        fail(ctx, "unknown function");
        return 0;
    }
    if (method->params.count != 3 || method->return_type->kind != HIR_TYPE_VOID) {
        fail(ctx, "internal error: invalid subscript_set signature");
        return 0;
    }
    receiver = make_instance_method_receiver(ctx, base, owner_type, line);
    if (!receiver) {
        return 0;
    }
    index = lower_expr_expected(ctx, target->as.index.index, method->params.items[1]->type);
    if (!index) {
        return 0;
    }
    if (!type_assignment_compatible(index->type, method->params.items[1]->type)) {
        fail(ctx, "subscript index type mismatch");
        return 0;
    }
    value = lower_expr_expected(ctx, value_expr, method->params.items[2]->type);
    if (!value) {
        return 0;
    }
    value = maybe_decay_array_to_slice(ctx, value, method->params.items[2]->type, line);
    if (!value) {
        return 0;
    }
    if (!type_assignment_compatible(value->type, method->params.items[2]->type)) {
        fail(ctx, "assignment type mismatch");
        return 0;
    }
    call = new_expr(HIR_EXPR_CALL, method->return_type, line);
    call->as.call.callee = method;
    call->as.call.builtin = HIR_BUILTIN_NONE;
    expr_list_push(&call->as.call.args, receiver);
    expr_list_push(&call->as.call.args, index);
    expr_list_push(&call->as.call.args, value);
    stmt = new_stmt(HIR_STMT_EXPR, line);
    stmt->as.expr_stmt.expr = call;
    return stmt;
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

static HirExpr* make_union_tag_expr(LowerContext* ctx, HirExpr* value, int line);
static HirExpr* make_union_field_expr(LowerContext* ctx, HirExpr* value, HirUnionVariant* variant, int field_index, HirType* type, int line);
static int lower_variant_pattern_bind(LowerContext* ctx, HirExpr* value, const AstExpr* pattern, HirBlock* out_block, HirExpr** cond_out);
static int lower_pattern_bind(LowerContext* ctx, const AstBindingPattern* pattern, HirExpr* init, HirBlock* out_block);
static int bind_optional_narrow(LowerContext* ctx, const char* name, HirBlock* out_block, int line);
static HirExpr* lower_switch_expr(LowerContext* ctx, const AstExpr* expr);

static HirBuiltinKind builtin_kind(const char* name) {
    if (strcmp(name, "assert") == 0) return HIR_BUILTIN_ASSERT;
    if (strcmp(name, "print") == 0) return HIR_BUILTIN_PRINT;
    if (strcmp(name, "panic") == 0) return HIR_BUILTIN_PANIC;
    if (strcmp(name, "__builtin.assert") == 0) return HIR_BUILTIN_ASSERT;
    if (strcmp(name, "__builtin.print") == 0) return HIR_BUILTIN_PRINT;
    if (strcmp(name, "__builtin.panic") == 0) return HIR_BUILTIN_PANIC;
    return HIR_BUILTIN_NONE;
}

static HirBuiltinKind builtin_method_kind(HirType* receiver_type, const char* method_name, int static_flag) {
    HirTypeQueryRef query = describe_hir_type(receiver_type);
    const HirBuiltinNominalDecl* builtin = 0;
    if (static_flag || query.kind != HIR_TYPE_QUERY_NOMINAL || query.nominal.kind != HIR_NOMINAL_BUILTIN) {
        return HIR_BUILTIN_NONE;
    }
    builtin = (const HirBuiltinNominalDecl*)query.nominal.decl;
    if (strcmp(method_name, "equal") == 0) {
            switch (builtin->kind) {
            case HIR_BUILTIN_NOMINAL_INT:
            case HIR_BUILTIN_NOMINAL_I8:
            case HIR_BUILTIN_NOMINAL_I16:
            case HIR_BUILTIN_NOMINAL_I32:
            case HIR_BUILTIN_NOMINAL_I64:
            case HIR_BUILTIN_NOMINAL_U8:
            case HIR_BUILTIN_NOMINAL_U16:
            case HIR_BUILTIN_NOMINAL_U32:
            case HIR_BUILTIN_NOMINAL_U64:
            case HIR_BUILTIN_NOMINAL_F16:
            case HIR_BUILTIN_NOMINAL_F32:
            case HIR_BUILTIN_NOMINAL_F64:
            case HIR_BUILTIN_NOMINAL_FLOAT:
            case HIR_BUILTIN_NOMINAL_DOUBLE:
            case HIR_BUILTIN_NOMINAL_CHARACTER:
            case HIR_BUILTIN_NOMINAL_UINT8:
            case HIR_BUILTIN_NOMINAL_BOOL:
                return HIR_BUILTIN_EQUAL;
            default:
                return HIR_BUILTIN_NONE;
        }
    }
    if (strcmp(method_name, "hash") == 0) {
            switch (builtin->kind) {
            case HIR_BUILTIN_NOMINAL_INT:
            case HIR_BUILTIN_NOMINAL_I8:
            case HIR_BUILTIN_NOMINAL_I16:
            case HIR_BUILTIN_NOMINAL_I32:
            case HIR_BUILTIN_NOMINAL_I64:
            case HIR_BUILTIN_NOMINAL_U8:
            case HIR_BUILTIN_NOMINAL_U16:
            case HIR_BUILTIN_NOMINAL_U32:
            case HIR_BUILTIN_NOMINAL_U64:
            case HIR_BUILTIN_NOMINAL_CHARACTER:
            case HIR_BUILTIN_NOMINAL_UINT8:
            case HIR_BUILTIN_NOMINAL_BOOL:
                return HIR_BUILTIN_HASH;
            default:
                return HIR_BUILTIN_NONE;
        }
    }
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
    if (expr->as.call.args.count != 1 || expr->as.call.args.items[0].name) return fail(ctx, builtin == HIR_BUILTIN_ASSERT ? "assert expects exactly one argument" : "print expects exactly one argument");
    arg = lower_expr(ctx, expr->as.call.args.items[0].value);
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

static int lower_builtin_method_call(LowerContext* ctx,
                                     int line,
                                     HirBuiltinKind builtin,
                                     HirType* receiver_type,
                                     HirExpr* receiver_arg,
                                     const AstStructFieldInitList* ast_args,
                                     HirExpr* out) {
    out->as.call.builtin = builtin;
    out->as.call.callee = 0;
    expr_list_push(&out->as.call.args, receiver_arg);
    if (builtin == HIR_BUILTIN_EQUAL) {
        HirExpr* arg = 0;
        if (ast_args->count != 1 || ast_args->items[0].name) {
            return fail(ctx, "equal expects exactly one argument");
        }
        arg = lower_expr_expected(ctx, ast_args->items[0].value, receiver_type);
        if (!arg) {
            return 0;
        }
        if (!type_assignment_compatible(arg->type, receiver_type)) {
            return fail(ctx, "equal argument type mismatch");
        }
        expr_list_push(&out->as.call.args, arg);
        out->type = primitive_type(ctx->program, HIR_TYPE_BOOL);
        return 1;
    }
    if (builtin == HIR_BUILTIN_HASH) {
        if (ast_args->count != 0) {
            return fail(ctx, "hash expects no arguments");
        }
        out->type = primitive_type(ctx->program, HIR_TYPE_U64);
        return 1;
    }
    (void)line;
    return fail(ctx, "unsupported builtin method");
}

static int lower_call_arg_for_param(LowerContext* ctx,
                                    AstStructFieldInit* ast_arg,
                                    HirBinding* param,
                                    int exact_type,
                                    const char* type_error,
                                    HirExpr** out_arg) {
    HirExpr* arg = lower_expr_expected(ctx, ast_arg->value, param->type);
    if (!arg) {
        return 0;
    }
    arg = maybe_decay_array_to_slice(ctx, arg, param->type, ast_arg->line);
    if (!arg) {
        return 0;
    }
    if ((param->type->kind == HIR_TYPE_REFERENCE ||
         param->type->kind == HIR_TYPE_POINTER ||
         param->type->kind == HIR_TYPE_MANY_POINTER) &&
        param->type->array_item &&
        param->type->array_item->mutable_flag &&
        arg->kind == HIR_EXPR_ADDR &&
        (!arg->as.unary.value || !arg->as.unary.value->type || !arg->as.unary.value->type->mutable_flag)) {
        return fail_at(ctx, ast_arg->line, 0, type_error);
    }
    if (exact_type) {
        if (!type_equals(arg->type, param->type)) {
            return 0;
        }
    } else if (!type_assignment_compatible(arg->type, param->type)) {
        return fail_at(ctx, ast_arg->line, 0, type_error);
    }
    *out_arg = arg;
    return 1;
}

static int lower_call_args_from(LowerContext* ctx, const AstStructFieldInitList* ast_args, HirExprList* out_args, HirFunction* callee, int param_offset) {
    int i = 0;
    int param_count = callee->params.count - param_offset;
    int next_positional = 0;
    int* assigned = 0;
    HirExpr** ordered_args = 0;
    if (ast_args->count != param_count) {
        return fail(ctx, "call argument count mismatch");
    }
    if (param_count == 0) {
        return 1;
    }
    assigned = (int*)calloc((size_t)param_count, sizeof(int));
    ordered_args = (HirExpr**)calloc((size_t)param_count, sizeof(HirExpr*));
    if (!assigned || !ordered_args) {
        free(assigned);
        free(ordered_args);
        return fail(ctx, "out of memory");
    }
    for (i = 0; i < ast_args->count; ++i) {
        AstStructFieldInit* ast_arg = &ast_args->items[i];
        HirBinding* param = 0;
        int param_index = -1;
        int j = 0;
        if (ast_arg->name) {
            for (j = 0; j < param_count; ++j) {
                HirBinding* candidate = callee->params.items[param_offset + j];
                if (!assigned[j] && candidate->label && strcmp(candidate->label, ast_arg->name) == 0) {
                    param_index = j;
                    break;
                }
            }
        } else {
            while (next_positional < param_count &&
                   (assigned[next_positional] || callee->params.items[param_offset + next_positional]->label)) {
                next_positional += 1;
            }
            if (next_positional < param_count) {
                param_index = next_positional;
                next_positional += 1;
            }
        }
        if (param_index < 0) {
            free(assigned);
            free(ordered_args);
            return fail_at(ctx, ast_arg->line, 0, "call argument label mismatch");
        }
        param = callee->params.items[param_offset + param_index];
        if (!lower_call_arg_for_param(ctx, ast_arg, param, 0, "call argument type mismatch", &ordered_args[param_index])) {
            free(assigned);
            free(ordered_args);
            return 0;
        }
        assigned[param_index] = 1;
    }
    for (i = 0; i < param_count; ++i) {
        if (!assigned[i]) {
            free(assigned);
            free(ordered_args);
            return fail(ctx, "call argument count mismatch");
        }
        expr_list_push(out_args, ordered_args[i]);
    }
    free(assigned);
    free(ordered_args);
    return 1;
}

static int lower_call_args(LowerContext* ctx, const AstStructFieldInitList* ast_args, HirExprList* out_args, HirFunction* callee) {
    return lower_call_args_from(ctx, ast_args, out_args, callee, 0);
}

static int lower_call_args_exact_from(LowerContext* ctx,
                                      const AstStructFieldInitList* ast_args,
                                      HirExprList* out_args,
                                      HirFunction* callee,
                                      int param_offset) {
    int i = 0;
    int param_count = callee->params.count - param_offset;
    int next_positional = 0;
    int* assigned = 0;
    HirExpr** ordered_args = 0;
    if (ast_args->count != param_count) {
        return 0;
    }
    if (param_count == 0) {
        return 1;
    }
    assigned = (int*)calloc((size_t)param_count, sizeof(int));
    ordered_args = (HirExpr**)calloc((size_t)param_count, sizeof(HirExpr*));
    if (!assigned || !ordered_args) {
        free(assigned);
        free(ordered_args);
        return fail(ctx, "out of memory");
    }
    for (i = 0; i < ast_args->count; ++i) {
        AstStructFieldInit* ast_arg = &ast_args->items[i];
        HirBinding* param = 0;
        int param_index = -1;
        int j = 0;
        if (ast_arg->name) {
            for (j = 0; j < param_count; ++j) {
                HirBinding* candidate = callee->params.items[param_offset + j];
                if (!assigned[j] && candidate->label && strcmp(candidate->label, ast_arg->name) == 0) {
                    param_index = j;
                    break;
                }
            }
        } else {
            while (next_positional < param_count &&
                   (assigned[next_positional] || callee->params.items[param_offset + next_positional]->label)) {
                next_positional += 1;
            }
            if (next_positional < param_count) {
                param_index = next_positional;
                next_positional += 1;
            }
        }
        if (param_index < 0) {
            free(assigned);
            free(ordered_args);
            return 0;
        }
        param = callee->params.items[param_offset + param_index];
        if (!lower_call_arg_for_param(ctx, ast_arg, param, 1, "call argument type mismatch", &ordered_args[param_index])) {
            free(assigned);
            free(ordered_args);
            return 0;
        }
        assigned[param_index] = 1;
    }
    for (i = 0; i < param_count; ++i) {
        if (!assigned[i]) {
            free(assigned);
            free(ordered_args);
            return 0;
        }
        expr_list_push(out_args, ordered_args[i]);
    }
    free(assigned);
    free(ordered_args);
    return 1;
}

static int lower_call_args_exact(LowerContext* ctx,
                                 const AstStructFieldInitList* ast_args,
                                 HirExprList* out_args,
                                 HirFunction* callee) {
    return lower_call_args_exact_from(ctx, ast_args, out_args, callee, 0);
}

static HirFunction* resolve_top_level_function_call(LowerContext* ctx,
                                                    const char* name,
                                                    const AstStructFieldInitList* ast_args,
                                                    HirExprList* out_args) {
    HirFunction* matched = 0;
    HirExprList matched_args;
    int match_count = 0;
    int i = 0;
    memset(&matched_args, 0, sizeof(matched_args));
    for (i = 0; i < ctx->program->functions.count; ++i) {
        HirFunction* candidate = &ctx->program->functions.items[i];
        HirExprList candidate_args;
        const char* saved_error = ctx->error;
        memset(&candidate_args, 0, sizeof(candidate_args));
        if (!function_matches_exact_name(candidate, name)) {
            continue;
        }
        ctx->error = 0;
        if (!lower_call_args_exact(ctx, ast_args, &candidate_args, candidate)) {
            ctx->error = saved_error;
            continue;
        }
        ctx->error = saved_error;
        matched = candidate;
        matched_args = candidate_args;
        match_count += 1;
    }
    if (match_count == 0) {
        return 0;
    }
    if (match_count > 1) {
        fail(ctx, "ambiguous overload");
        return 0;
    }
    out_args->items = matched_args.items;
    out_args->count = matched_args.count;
    out_args->capacity = matched_args.capacity;
    return matched;
}

static int lower_struct_init_args_named(LowerContext* ctx,
                                        HirStructDecl* struct_decl,
                                        const char* init_name,
                                        const AstStructFieldInitList* ast_args,
                                        HirExprList* out_args,
                                        HirFunction** out_fn,
                                        int line) {
    int param_count = 0;
    int i = 0;
    int match_count = 0;
    HirFunction* matched_fn = 0;
    HirExprList matched_args;
    memset(&matched_args, 0, sizeof(matched_args));
    (void)line;
    if (!struct_decl->has_init) {
        return fail(ctx, "internal error: struct has no init");
    }
    for (i = 0; i < ctx->program->functions.count; ++i) {
        HirFunction* candidate = &ctx->program->functions.items[i];
        HirExprList candidate_args;
        const char* saved_error = ctx->error;
        memset(&candidate_args, 0, sizeof(candidate_args));
        if (!candidate->struct_init_flag ||
            candidate->owner_struct != struct_decl ||
            !struct_init_name_matches(candidate, init_name)) {
            continue;
        }
        param_count = candidate->params.count;
        if (ast_args->count != param_count) {
            continue;
        }
        ctx->error = 0;
        if (!lower_struct_init_args_for_function(ctx, ast_args, &candidate_args, candidate)) {
            ctx->error = saved_error;
            continue;
        }
        matched_args = candidate_args;
        matched_fn = candidate;
        match_count += 1;
        if (match_count > 1) {
            return fail(ctx, "ambiguous init overload");
        }
    }
    if (match_count == 0) {
        static char error_buffer[512];
        snprintf(error_buffer,
                 sizeof(error_buffer),
                 "no matching init overload: %s with %d args",
                 struct_decl->name ? struct_decl->name : "<null>",
                 ast_args->count);
        return fail(ctx, error_buffer);
    }
    out_args->items = matched_args.items;
    out_args->count = matched_args.count;
    out_args->capacity = matched_args.capacity;
    if (out_fn) {
        *out_fn = matched_fn;
    }
    return 1;
}

static int lower_struct_init_args(LowerContext* ctx, HirStructDecl* struct_decl, const AstStructFieldInitList* ast_args, HirExprList* out_args, int line) {
    return lower_struct_init_args_named(ctx, struct_decl, 0, ast_args, out_args, 0, line);
}

static int lower_default_struct_call_args(LowerContext* ctx,
                                          const AstStructDecl* ast_struct,
                                          HirStructDecl* struct_decl,
                                          const AstStructFieldInitList* ast_args,
                                          HirExpr* out,
                                          int line) {
    int i = 0;
    int* seen = 0;
    seen = (int*)calloc((size_t)struct_decl->fields.count, sizeof(int));
    if (!seen) {
        return fail(ctx, "out of memory");
    }
    for (i = 0; i < ast_args->count; ++i) {
        AstStructFieldInit* ast_field = &ast_args->items[i];
        HirStructFieldInit field_init;
        HirStructField* field = 0;
        HirExpr* value = 0;
        int field_index = -1;
        memset(&field_init, 0, sizeof(field_init));
        if (!ast_field->name) {
            free(seen);
            return fail(ctx, "default struct initialization requires labeled fields");
        }
        field = find_struct_field(struct_decl, ast_field->name, &field_index);
        if (!field) {
            free(seen);
            return fail_unknown_field(ctx, struct_decl->name, ast_field->name);
        }
        if (seen[field_index]) {
            free(seen);
            return fail(ctx, "duplicate struct field initializer");
        }
        value = lower_expr_expected(ctx, ast_field->value, field->type);
        if (!value) {
            free(seen);
            return 0;
        }
        value = maybe_decay_array_to_slice(ctx, value, field->type, line);
        if (!value) {
            free(seen);
            return 0;
        }
        if (!type_assignment_compatible(value->type, field->type)) {
            free(seen);
            return fail(ctx, "struct field initializer type mismatch");
        }
        seen[field_index] = 1;
        field_init.field = field;
        field_init.value = value;
        struct_field_init_list_push(&out->as.struct_lit.fields, field_init);
    }
    for (i = 0; i < struct_decl->fields.count; ++i) {
        if (!seen[i]) {
            HirStructFieldInit field_init;
            AstExpr* default_value = ast_struct ? ast_struct->fields.items[i].default_value : 0;
            if (!default_value && struct_decl->fields.items[i].type->kind != HIR_TYPE_OPTIONAL) {
                free(seen);
                return fail(ctx, "missing struct field initializer");
            }
            memset(&field_init, 0, sizeof(field_init));
            field_init.field = &struct_decl->fields.items[i];
            if (default_value) {
                field_init.value = lower_expr_expected(ctx, default_value, struct_decl->fields.items[i].type);
                if (!field_init.value) {
                    free(seen);
                    return 0;
                }
                field_init.value = maybe_decay_array_to_slice(ctx, field_init.value, struct_decl->fields.items[i].type, line);
                if (!field_init.value) {
                    free(seen);
                    return 0;
                }
                if (!type_assignment_compatible(field_init.value->type, struct_decl->fields.items[i].type)) {
                    free(seen);
                    return fail(ctx, "struct field initializer type mismatch");
                }
            } else {
                field_init.value = make_zero_expr(ctx, struct_decl->fields.items[i].type, line);
            }
            struct_field_init_list_push(&out->as.struct_lit.fields, field_init);
        }
    }
    free(seen);
    return 1;
}

static int ast_struct_declares_trait(const AstStructDecl* decl, const char* trait_name) {
    int i = 0;
    if (!decl) {
        return 0;
    }
    for (i = 0; i < decl->concept_names.count; ++i) {
        if (strcmp(decl->concept_names.items[i], trait_name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ast_concept_declares_method_name_local(const AstProgram* ast,
                                                  const AstConceptDecl* concept,
                                                  const char* method_name,
                                                  AstNameList* seen_concepts) {
    int i = 0;
    if (!concept) {
        return 0;
    }
    if (ast_name_list_contains_local(seen_concepts, concept->name)) {
        return 0;
    }
    name_list_push(seen_concepts, concept->name);
    for (i = 0; i < concept->methods.count; ++i) {
        if (strcmp(concept->methods.items[i].name, method_name) == 0) {
            return 1;
        }
    }
    for (i = 0; i < concept->concept_names.count; ++i) {
        const AstConceptDecl* parent = find_ast_concept_local(ast, concept->concept_names.items[i]);
        if (ast_concept_declares_method_name_local(ast, parent, method_name, seen_concepts)) {
            return 1;
        }
    }
    return 0;
}

static int hir_type_declares_trait_method_name(const AstProgram* ast,
                                               HirType* type,
                                               const char* trait_name,
                                               const char* method_name) {
    const AstConceptDecl* trait = find_ast_concept_local(ast, trait_name);
    AstNameList seen_concepts;
    if (!trait || !hir_type_declares_concept_or_child(ast, type, trait_name)) {
        return 0;
    }
    memset(&seen_concepts, 0, sizeof(seen_concepts));
    return ast_concept_declares_method_name_local(ast, trait, method_name, &seen_concepts);
}

static int parse_trait_qualified_callee(const char* callee,
                                        char** owner_name,
                                        char** trait_name,
                                        char** member_name) {
    const char* marker = strstr(callee, ".[");
    const char* close = 0;
    const char* dot = 0;
    size_t owner_len = 0;
    size_t trait_len = 0;
    if (!marker) {
        return 0;
    }
    close = strchr(marker + 2, ']');
    if (!close || close[1] != '.') {
        return 0;
    }
    dot = close + 1;
    owner_len = (size_t)(marker - callee);
    trait_len = (size_t)(close - (marker + 2));
    *owner_name = (char*)malloc(owner_len + 1);
    *trait_name = (char*)malloc(trait_len + 1);
    *member_name = strdup(dot + 1);
    if (!*owner_name || !*trait_name || !*member_name) {
        free(*owner_name);
        free(*trait_name);
        free(*member_name);
        *owner_name = 0;
        *trait_name = 0;
        *member_name = 0;
        return 0;
    }
    memcpy(*owner_name, callee, owner_len);
    (*owner_name)[owner_len] = '\0';
    memcpy(*trait_name, marker + 2, trait_len);
    (*trait_name)[trait_len] = '\0';
    return 1;
}

static HirFunction* resolve_method_call_overload(LowerContext* ctx,
                                                 HirType* owner_type,
                                                 const char* method_name,
                                                 int static_flag,
                                                 const AstStructFieldInitList* ast_args,
                                                 int param_offset,
                                                 const char* trait_name,
                                                 HirExprList* out_args) {
    HirFunction* matched = 0;
    HirExprList matched_args;
    int match_count = 0;
    int i = 0;
    int pass = 0;
    memset(&matched_args, 0, sizeof(matched_args));
    if (trait_name && !hir_type_declares_trait_method_name(ctx->ast, owner_type, trait_name, method_name)) {
        fail(ctx, "trait method not found");
        return 0;
    }
    for (pass = 0; pass < 2 && match_count == 0; ++pass) {
        for (i = 0; i < ctx->program->functions.count; ++i) {
            HirFunction* candidate = &ctx->program->functions.items[i];
            HirExprList candidate_args;
            const char* saved_error = ctx->error;
            memset(&candidate_args, 0, sizeof(candidate_args));
            if (!candidate->method_flag ||
                candidate->static_method_flag != static_flag ||
                !method_source_name_matches(candidate->method_name, method_name)) {
                continue;
            }
            if (pass == 0) {
                if (!same_nominal_type_strict(owner_type, candidate->receiver_type)) {
                    continue;
                }
            } else if (same_nominal_type_strict(owner_type, candidate->receiver_type) ||
                       !same_nominal_type(owner_type, candidate->receiver_type)) {
                continue;
            }
            if (!method_visible_from_context(ctx, candidate, owner_type)) {
                continue;
            }
            ctx->error = 0;
            if (!lower_call_args_exact_from(ctx, ast_args, &candidate_args, candidate, param_offset)) {
                ctx->error = saved_error;
                continue;
            }
            ctx->error = saved_error;
            matched = candidate;
            matched_args = candidate_args;
            match_count += 1;
        }
    }
    if (match_count == 0) {
        return 0;
    }
    if (match_count > 1) {
        fail(ctx, "ambiguous overload");
        return 0;
    }
    out_args->items = matched_args.items;
    out_args->count = matched_args.count;
    out_args->capacity = matched_args.capacity;
    return matched;
}

static HirFunction* resolve_function_value_overload(LowerContext* ctx,
                                                    const char* name,
                                                    HirType* expected_type) {
    HirFunction* matched = 0;
    int match_count = 0;
    int i = 0;
    for (i = 0; i < ctx->program->functions.count; ++i) {
        HirFunction* candidate = &ctx->program->functions.items[i];
        if (!function_matches_exact_name(candidate, name)) {
            continue;
        }
        if (expected_type && expected_type->kind == HIR_TYPE_FUNCTION) {
            if (!function_matches_fn_type(candidate, expected_type)) {
                continue;
            }
        }
        matched = candidate;
        match_count += 1;
    }
    if (match_count == 1) {
        return matched;
    }
    if (match_count > 1) {
        fail(ctx, "ambiguous overload");
    }
    return 0;
}

static HirFunction* resolve_method_value_overload(LowerContext* ctx,
                                                  HirType* owner_type,
                                                  const char* method_name,
                                                  int static_flag,
                                                  const char* trait_name,
                                                  HirType* expected_type) {
    HirFunction* matched = 0;
    int match_count = 0;
    int i = 0;
    int pass = 0;
    if (trait_name && !hir_type_declares_trait_method_name(ctx->ast, owner_type, trait_name, method_name)) {
        fail(ctx, "trait method not found");
        return 0;
    }
    for (pass = 0; pass < 2 && match_count == 0; ++pass) {
        for (i = 0; i < ctx->program->functions.count; ++i) {
            HirFunction* candidate = &ctx->program->functions.items[i];
            if (!candidate->method_flag ||
                candidate->static_method_flag != static_flag ||
                !method_source_name_matches(candidate->method_name, method_name)) {
                continue;
            }
            if (pass == 0) {
                if (!same_nominal_type_strict(owner_type, candidate->receiver_type)) {
                    continue;
                }
            } else if (same_nominal_type_strict(owner_type, candidate->receiver_type) ||
                       !same_nominal_type(owner_type, candidate->receiver_type)) {
                continue;
            }
            if (!method_visible_from_context(ctx, candidate, owner_type)) {
                continue;
            }
            if (expected_type && expected_type->kind == HIR_TYPE_FUNCTION &&
                !function_matches_fn_type(candidate, expected_type)) {
                continue;
            }
            matched = candidate;
            match_count += 1;
        }
    }
    if (match_count == 1) {
        return matched;
    }
    if (match_count > 1) {
        fail(ctx, "ambiguous overload");
    }
    return 0;
}

static int hir_type_is_uint8_array_or_slice(const HirType* type) {
    return type &&
           (type->kind == HIR_TYPE_ARRAY || type->kind == HIR_TYPE_SLICE) &&
           type->array_item &&
           type->array_item->kind == HIR_TYPE_UINT8;
}

static int lower_string_char_literal_value(const AstExpr* expr, int64_t* out_value) {
    const unsigned char* text = 0;
    int length = 0;
    int64_t value = 0;
    if (!expr || expr->kind != AST_EXPR_STRING || !out_value) {
        return 0;
    }
    text = (const unsigned char*)expr->as.string_lit.text;
    length = expr->as.string_lit.length;
    if (length <= 0) {
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

static HirExpr* lower_string_uint8_array_literal(LowerContext* ctx, const AstExpr* expr) {
    HirType* array_type = new_owned_type(ctx->program, HIR_TYPE_ARRAY);
    HirExpr* array_value = new_expr(HIR_EXPR_ARRAY, array_type, expr->line);
    int i = 0;
    array_type->array_item = primitive_type(ctx->program, HIR_TYPE_UINT8);
    array_type->array_length = expr->as.string_lit.length;
    for (i = 0; i < expr->as.string_lit.length; ++i) {
        HirExpr* item = new_expr(HIR_EXPR_INT, primitive_type(ctx->program, HIR_TYPE_UINT8), expr->line);
        item->as.int_value = (unsigned char)expr->as.string_lit.text[i];
        expr_list_push(&array_value->as.array.items, item);
    }
    return array_value;
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

static HirType* find_named_owner_type(HirProgram* program, const char* owner_type_name) {
    HirNominalDeclRef nominal = find_nominal_decl(program, owner_type_name);
    HirType* type = 0;
    switch (nominal.kind) {
        case HIR_NOMINAL_BUILTIN:
            if (strcmp(nominal.name, "Int") == 0) return primitive_type(program, HIR_TYPE_INT);
            if (strcmp(nominal.name, "Int8") == 0) return primitive_type(program, HIR_TYPE_I8);
            if (strcmp(nominal.name, "Int16") == 0) return primitive_type(program, HIR_TYPE_I16);
            if (strcmp(nominal.name, "Int32") == 0) return primitive_type(program, HIR_TYPE_I32);
            if (strcmp(nominal.name, "Int64") == 0) return primitive_type(program, HIR_TYPE_I64);
            if (strcmp(nominal.name, "UInt16") == 0) return primitive_type(program, HIR_TYPE_U16);
            if (strcmp(nominal.name, "UInt32") == 0) return primitive_type(program, HIR_TYPE_U32);
            if (strcmp(nominal.name, "UInt64") == 0) return primitive_type(program, HIR_TYPE_U64);
            if (strcmp(nominal.name, "Float16") == 0) return primitive_type(program, HIR_TYPE_F16);
            if (strcmp(nominal.name, "Float32") == 0) return primitive_type(program, HIR_TYPE_F32);
            if (strcmp(nominal.name, "Float64") == 0) return primitive_type(program, HIR_TYPE_F64);
            if (strcmp(nominal.name, "Float") == 0) return primitive_type(program, HIR_TYPE_FLOAT);
            if (strcmp(nominal.name, "Double") == 0) return primitive_type(program, HIR_TYPE_DOUBLE);
            if (strcmp(nominal.name, "Char") == 0) return primitive_type(program, HIR_TYPE_CHARACTER);
            if (strcmp(nominal.name, "UInt8") == 0) return primitive_type(program, HIR_TYPE_UINT8);
            if (strcmp(nominal.name, "Bool") == 0) return primitive_type(program, HIR_TYPE_BOOL);
            if (strcmp(nominal.name, "Void") == 0) return primitive_type(program, HIR_TYPE_VOID);
            return 0;
        case HIR_NOMINAL_ENUM:
            type = new_owned_type(program, HIR_TYPE_ENUM);
            type->enum_decl = (HirEnumDecl*)nominal.decl;
            return type;
        case HIR_NOMINAL_STRUCT:
            type = new_owned_type(program, HIR_TYPE_STRUCT);
            type->struct_decl = (HirStructDecl*)nominal.decl;
            return type;
        case HIR_NOMINAL_UNION:
            type = new_owned_type(program, HIR_TYPE_UNION);
            type->union_decl = (HirUnionDecl*)nominal.decl;
            return type;
        case HIR_NOMINAL_NONE:
        default:
            break;
    }
    return 0;
}

static HirType* current_self_owner_type(LowerContext* ctx) {
    if (!ctx || !ctx->current_function) {
        return 0;
    }
    if (ctx->current_function->receiver_type) {
        return ctx->current_function->receiver_type;
    }
    if (ctx->current_function->owner_struct) {
        HirType* type = new_owned_type(ctx->program, HIR_TYPE_STRUCT);
        type->struct_decl = ctx->current_function->owner_struct;
        return type;
    }
    return 0;
}

static HirType* requalify_nominal_owner_type(LowerContext* ctx, HirType* base_type, int mutable_flag) {
    HirType* out = 0;
    if (!ctx || !base_type) {
        return 0;
    }
    out = new_owned_type(ctx->program, base_type->kind);
    out->mutable_flag = mutable_flag;
    switch (base_type->kind) {
        case HIR_TYPE_STRUCT:
            out->struct_decl = base_type->struct_decl;
            return out;
        case HIR_TYPE_ENUM:
            out->enum_decl = base_type->enum_decl;
            return out;
        case HIR_TYPE_UNION:
            out->union_decl = base_type->union_decl;
            return out;
        default:
            return 0;
    }
}

static HirType* resolve_owner_type_name(LowerContext* ctx, const char* owner_type_name) {
    if (owner_type_name && strcmp(owner_type_name, "Self") == 0) {
        return current_self_owner_type(ctx);
    }
    return find_named_owner_type(ctx->program, owner_type_name);
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
                    return fail_unknown_field(ctx, struct_decl->name, expr->as.field.name);
                }
                if (!field_state[field_index]) {
                    return fail(ctx, "init read before field initialization");
                }
                return 1;
            }
            return validate_struct_init_expr(ctx, struct_decl, expr->as.field.base, field_state);
        case AST_EXPR_ADDR:
            if (expr->as.unary.value &&
                expr->as.unary.value->kind == AST_EXPR_FIELD &&
                expr->as.unary.value->as.field.base &&
                expr->as.unary.value->as.field.base->kind == AST_EXPR_NAME &&
                strcmp(expr->as.unary.value->as.field.base->as.name, "self") == 0) {
                return fail(ctx, "init self field address escape");
            }
            return validate_struct_init_expr(ctx, struct_decl, expr->as.unary.value, field_state);
        case AST_EXPR_IMPLICIT:
            if (!expr->as.implicit.target_is_type &&
                expr->as.implicit.value_target &&
                expr->as.implicit.value_target->kind == AST_EXPR_FIELD &&
                expr->as.implicit.value_target->as.field.base &&
                expr->as.implicit.value_target->as.field.base->kind == AST_EXPR_NAME &&
                strcmp(expr->as.implicit.value_target->as.field.base->as.name, "self") == 0 &&
                expr->as.implicit.member &&
                (strcmp(expr->as.implicit.member, "ptr") == 0 ||
                 strcmp(expr->as.implicit.member, "ref") == 0 ||
                 strcmp(expr->as.implicit.member, "addr") == 0)) {
                return fail(ctx, "init self field address escape");
            }
            return validate_struct_init_expr(ctx, struct_decl, expr->as.implicit.value_target, field_state);
        case AST_EXPR_BINARY:
            return validate_struct_init_expr(ctx, struct_decl, expr->as.binary.left, field_state) &&
                   validate_struct_init_expr(ctx, struct_decl, expr->as.binary.right, field_state);
        case AST_EXPR_TERNARY:
            return validate_struct_init_expr(ctx, struct_decl, expr->as.ternary.cond, field_state) &&
                   validate_struct_init_expr(ctx, struct_decl, expr->as.ternary.then_expr, field_state) &&
                   validate_struct_init_expr(ctx, struct_decl, expr->as.ternary.else_expr, field_state);
        case AST_EXPR_CALL:
            for (i = 0; i < expr->as.call.args.count; ++i) {
                if (!validate_struct_init_expr(ctx, struct_decl, expr->as.call.args.items[i].value, field_state)) {
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
                if (ctx->current_function &&
                    ctx->current_function->struct_init_failable_flag &&
                    stmt->as.ret.expr->kind == AST_EXPR_NULL) {
                    return 1;
                }
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
                    return fail_unknown_field(ctx, struct_decl->name, stmt->as.assign.target->as.field.name);
                }
                if (field_state[field_index] == 1 && !field->mutable_flag) {
                    return fail(ctx, "immutable struct field reassigned in init");
                }
                field_state[field_index] = 1;
                return 1;
            }
            return validate_struct_init_expr(ctx, struct_decl, stmt->as.assign.target, field_state) &&
                   validate_struct_init_expr(ctx, struct_decl, stmt->as.assign.value, field_state);
        case AST_STMT_GROUP:
            return validate_struct_init_block(ctx, struct_decl, &stmt->as.group_stmt, field_state);
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
                    if (!then_state[i] || !else_state[i]) {
                        field_state[i] = 0;
                    } else if (then_state[i] == 1 || else_state[i] == 1) {
                        field_state[i] = 1;
                    } else {
                        field_state[i] = 2;
                    }
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

static HirExpr* lower_expr_expected(LowerContext* ctx, const AstExpr* expr, HirType* expected_type) {
    HirExpr* out = 0;
    int i = 0;
    int saved_line = ctx->current_line;
    if (expr) {
        ctx->current_line = expr->line;
        ctx->current_column = expr->column;
    }
    if (expected_type && expected_type->kind == HIR_TYPE_OPTIONAL && expr->kind != AST_EXPR_NULL) {
        out = lower_expr_expected(ctx, expr, expected_type->array_item);
        if (!out) {
            return 0;
        }
        if (type_assignment_compatible(out->type, expected_type)) {
            return out;
        }
        if (type_assignment_compatible(out->type, expected_type->array_item)) {
            HirExpr* some = new_expr(HIR_EXPR_OPTIONAL_SOME, expected_type, expr->line);
            some->as.unary.value = out;
            return some;
        }
        return out;
    }
    switch (expr->kind) {
        case AST_EXPR_INT:
            if (expected_type &&
                expected_type->kind == HIR_TYPE_OPTIONAL &&
                is_integer_literal_target_type(expected_type->array_item)) {
                out = new_expr(HIR_EXPR_OPTIONAL_SOME, expected_type, expr->line);
                out->as.unary.value = new_expr(HIR_EXPR_INT, expected_type->array_item, expr->line);
                out->as.unary.value->as.int_value = expr->as.int_value;
                return out;
            }
            if (expected_type &&
                expected_type->kind == HIR_TYPE_OPTIONAL &&
                is_float_like_type(expected_type->array_item)) {
                out = new_expr(HIR_EXPR_OPTIONAL_SOME, expected_type, expr->line);
                out->as.unary.value = new_expr(HIR_EXPR_FLOAT, expected_type->array_item, expr->line);
                out->as.unary.value->as.float_value = (double)expr->as.int_value;
                return out;
            }
            if (expected_type && is_float_like_type(expected_type)) {
                out = new_expr(HIR_EXPR_FLOAT, expected_type, expr->line);
                out->as.float_value = (double)expr->as.int_value;
                return out;
            }
            out = new_expr(HIR_EXPR_INT,
                           expected_type && is_integer_literal_target_type(expected_type)
                               ? expected_type
                               : primitive_type(ctx->program, HIR_TYPE_INT),
                           expr->line);
            out->as.int_value = expr->as.int_value;
            return out;
        case AST_EXPR_FLOAT:
            if (expected_type && expected_type->kind == HIR_TYPE_OPTIONAL &&
                is_float_like_type(expected_type->array_item)) {
                HirType* item_type = expected_type->array_item;
                out = new_expr(HIR_EXPR_OPTIONAL_SOME, expected_type, expr->line);
                out->as.unary.value = new_expr(HIR_EXPR_FLOAT, item_type, expr->line);
                out->as.unary.value->as.float_value = expr->as.float_value;
                return out;
            }
            out = new_expr(HIR_EXPR_FLOAT,
                           expected_type && is_float_like_type(expected_type)
                               ? expected_type
                               : primitive_type(ctx->program, HIR_TYPE_DOUBLE),
                           expr->line);
            out->as.float_value = expr->as.float_value;
            return out;
        case AST_EXPR_CHAR:
            if (expected_type && expected_type->kind == HIR_TYPE_OPTIONAL &&
                type_equals(expected_type->array_item, primitive_type(ctx->program, HIR_TYPE_CHARACTER))) {
                out = new_expr(HIR_EXPR_OPTIONAL_SOME, expected_type, expr->line);
                out->as.unary.value = new_expr(HIR_EXPR_CHAR, primitive_type(ctx->program, HIR_TYPE_CHARACTER), expr->line);
                out->as.unary.value->as.char_value = expr->as.char_value;
                return out;
            }
            if (expected_type && expected_type->kind == HIR_TYPE_OPTIONAL &&
                expected_type->array_item &&
                is_integer_like_type(expected_type->array_item) &&
                expected_type->array_item->kind != HIR_TYPE_BOOL) {
                if (!char_literal_fits_integer_type(expr->as.char_value, expected_type->array_item)) {
                    fail(ctx, "character literal out of range for integer type");
                    return 0;
                }
                out = new_expr(HIR_EXPR_OPTIONAL_SOME, expected_type, expr->line);
                out->as.unary.value = new_expr(HIR_EXPR_INT, expected_type->array_item, expr->line);
                out->as.unary.value->as.int_value = expr->as.char_value;
                return out;
            }
            if (expected_type &&
                is_integer_like_type(expected_type) &&
                expected_type->kind != HIR_TYPE_BOOL) {
                if (!char_literal_fits_integer_type(expr->as.char_value, expected_type)) {
                    fail(ctx, "character literal out of range for integer type");
                    return 0;
                }
                out = new_expr(HIR_EXPR_INT, expected_type, expr->line);
                out->as.int_value = expr->as.char_value;
                return out;
            }
            out = new_expr(HIR_EXPR_CHAR, primitive_type(ctx->program, HIR_TYPE_CHARACTER), expr->line);
            out->as.char_value = expr->as.char_value;
            return out;
        case AST_EXPR_BOOL:
            if (expected_type && expected_type->kind == HIR_TYPE_OPTIONAL && type_equals(expected_type->array_item, primitive_type(ctx->program, HIR_TYPE_BOOL))) {
                out = new_expr(HIR_EXPR_OPTIONAL_SOME, expected_type, expr->line);
                out->as.unary.value = new_expr(HIR_EXPR_BOOL, primitive_type(ctx->program, HIR_TYPE_BOOL), expr->line);
                out->as.unary.value->as.bool_value = expr->as.bool_value;
                return out;
            }
            out = new_expr(HIR_EXPR_BOOL, primitive_type(ctx->program, HIR_TYPE_BOOL), expr->line);
            out->as.bool_value = expr->as.bool_value;
            return out;
        case AST_EXPR_NULL:
            if (!expected_type ||
                (expected_type->kind != HIR_TYPE_OPTIONAL &&
                 expected_type->kind != HIR_TYPE_POINTER &&
                 expected_type->kind != HIR_TYPE_MANY_POINTER)) {
                fail(ctx, "null requires optional or pointer type");
                return 0;
            }
            return new_expr(HIR_EXPR_NULL, expected_type, expr->line);
        case AST_EXPR_IMPLICIT: {
            if (expr->as.implicit.optional_chain) {
                return lower_optional_chain_implicit(ctx, expr);
            }
            if (expr->as.implicit.target_is_type) {
                HirType* type = 0;
                type = lower_type(ctx, &expr->as.implicit.type_target);
                if (!type) {
                    return 0;
                }
                if (strcmp(expr->as.implicit.member, "size") == 0) {
                    if (expr->as.implicit.args.count != 0) {
                        fail(ctx, "type implicit operation '.size()' takes no arguments");
                        return 0;
                    }
                    out = new_expr(HIR_EXPR_INT, primitive_type(ctx->program, HIR_TYPE_INT), expr->line);
                    out->as.int_value = type_size_bytes(type);
                    return out;
                }
                if (strcmp(expr->as.implicit.member, "align") == 0) {
                    if (expr->as.implicit.args.count != 0) {
                        fail(ctx, "type implicit operation '.align()' takes no arguments");
                        return 0;
                    }
                    out = new_expr(HIR_EXPR_INT, primitive_type(ctx->program, HIR_TYPE_INT), expr->line);
                    out->as.int_value = type_alignment_bytes(type);
                    return out;
                }
                if (strcmp(expr->as.implicit.member, "max_align") == 0) {
                    if (expr->as.implicit.args.count != 0) {
                        fail(ctx, "type implicit operation '.max_align()' takes no arguments");
                        return 0;
                    }
                    out = new_expr(HIR_EXPR_INT, primitive_type(ctx->program, HIR_TYPE_INT), expr->line);
                    out->as.int_value = max_type_alignment_bytes();
                    return out;
                }
                if (strcmp(expr->as.implicit.member, "alloc") == 0) {
                    HirType* pointer_type = 0;
                    if (expr->as.implicit.args.count != 0) {
                        fail(ctx, "type implicit operation '.alloc()' takes no arguments");
                        return 0;
                    }
                    if (type->kind == HIR_TYPE_VOID) {
                        fail(ctx, "alloc requires non-void type");
                        return 0;
                    }
                    pointer_type = new_owned_type(ctx->program, HIR_TYPE_POINTER);
                    pointer_type->mutable_flag = 1;
                    pointer_type->array_item = type;
                    out = new_expr(HIR_EXPR_CALL, pointer_type, expr->line);
                    out->as.call.callee = 0;
                    out->as.call.builtin = HIR_BUILTIN_ALLOC;
                    return out;
                }
                if (strcmp(expr->as.implicit.member, "alloc_array") == 0) {
                    HirType* many_pointer_type = 0;
                    HirExpr* count = 0;
                    if (expr->as.implicit.args.count != 1) {
                        fail(ctx, "type implicit operation '.alloc_array()' expects exactly one argument");
                        return 0;
                    }
                    if (type->kind == HIR_TYPE_VOID) {
                        fail(ctx, "alloc_array requires non-void element type");
                        return 0;
                    }
                    count = lower_expr_expected(ctx, expr->as.implicit.args.items[0], primitive_type(ctx->program, HIR_TYPE_INT));
                    if (!count) {
                        return 0;
                    }
                    if (count->type->kind != HIR_TYPE_INT) {
                        fail(ctx, "alloc_array length must be Int");
                        return 0;
                    }
                    many_pointer_type = new_owned_type(ctx->program, HIR_TYPE_MANY_POINTER);
                    many_pointer_type->mutable_flag = 1;
                    many_pointer_type->array_item = type;
                    out = new_expr(HIR_EXPR_CALL, many_pointer_type, expr->line);
                    out->as.call.callee = 0;
                    out->as.call.builtin = HIR_BUILTIN_ALLOC_ARRAY;
                    expr_list_push(&out->as.call.args, count);
                    return out;
                }
                fail(ctx, "unknown implicit type operation");
                return 0;
            }
            if (strcmp(expr->as.implicit.member, "as") == 0) {
                HirExpr* value = 0;
                HirType* target_type = 0;
                if (expr->as.implicit.args.count != 0) {
                    fail(ctx, "as accepts only a type argument");
                    return 0;
                }
                value = lower_expr_preserve_pointer(ctx, expr->as.implicit.value_target);
                if (!value) {
                    return 0;
                }
                if (expr->as.implicit.has_type_arg) {
                    target_type = lower_type(ctx, &expr->as.implicit.type_arg);
                    if (!target_type) {
                        return 0;
                    }
                    if (target_type->mutable_flag) {
                        fail(ctx, "as target type cannot include top-level '!'");
                        return 0;
                    }
                } else {
                    target_type = expected_type;
                    if (!target_type) {
                        fail(ctx, "as requires target type");
                        return 0;
                    }
                }
                if (!as_compatible(value->type, target_type)) {
                    fail(ctx, "invalid as conversion");
                    return 0;
                }
                out = new_expr(HIR_EXPR_AS, target_type, expr->line);
                out->as.unary.value = value;
                return out;
            }
            if (strcmp(expr->as.implicit.member, "ref") == 0) {
                HirExpr* value = 0;
                HirType* pointer_type = 0;
                if (expr->as.implicit.args.count != 0) {
                    fail(ctx, "implicit operation '.ref()' takes no arguments");
                    return 0;
                }
                value = lower_expr_preserve_pointer(ctx, expr->as.implicit.value_target);
                if (!value) {
                    return 0;
                }
                if (!is_lvalue_expr(value)) {
                    fail(ctx, "address-of requires lvalue");
                    return 0;
                }
                pointer_type = new_owned_type(ctx->program, HIR_TYPE_REFERENCE);
                pointer_type->array_item = value->type;
                out = new_expr(HIR_EXPR_ADDR, pointer_type, expr->line);
                out->as.unary.value = value;
                return out;
            }
            if (strcmp(expr->as.implicit.member, "ptr") == 0) {
                HirExpr* value = 0;
                HirType* pointer_type = 0;
                if (expr->as.implicit.has_type_arg || expr->as.implicit.args.count != 0) {
                    fail(ctx, "implicit operation '.ptr()' takes no arguments");
                    return 0;
                }
                value = lower_expr_preserve_pointer(ctx, expr->as.implicit.value_target);
                if (!value) {
                    return 0;
                }
                if (!is_lvalue_expr(value)) {
                    fail(ctx, "address-of requires lvalue");
                    return 0;
                }
                pointer_type = new_owned_type(ctx->program, HIR_TYPE_POINTER);
                pointer_type->array_item = value->type;
                out = new_expr(HIR_EXPR_ADDR, pointer_type, expr->line);
                out->as.unary.value = value;
                return out;
            }
            if (strcmp(expr->as.implicit.member, "addr") == 0) {
                HirExpr* value = 0;
                HirType* pointer_type = 0;
                HirExpr* addr = 0;
                if (expr->as.implicit.has_type_arg || expr->as.implicit.args.count != 0) {
                    fail(ctx, "implicit operation '.addr()' takes no arguments");
                    return 0;
                }
                value = lower_expr_preserve_pointer(ctx, expr->as.implicit.value_target);
                if (!value) {
                    return 0;
                }
                if (!is_lvalue_expr(value)) {
                    fail(ctx, "address-of requires lvalue");
                    return 0;
                }
                pointer_type = new_owned_type(ctx->program, HIR_TYPE_POINTER);
                pointer_type->array_item = value->type;
                addr = new_expr(HIR_EXPR_ADDR, pointer_type, expr->line);
                addr->as.unary.value = value;
                out = new_expr(HIR_EXPR_AS, primitive_type(ctx->program, HIR_TYPE_INT), expr->line);
                out->as.unary.value = addr;
                return out;
            }
            if (strcmp(expr->as.implicit.member, "free") == 0) {
                HirExpr* value = 0;
                if (expr->as.implicit.args.count != 0) {
                    fail(ctx, "implicit operation '.free()' takes no arguments");
                    return 0;
                }
                value = lower_expr_preserve_pointer(ctx, expr->as.implicit.value_target);
                if (!value) {
                    return 0;
                }
                if (value->type->kind != HIR_TYPE_POINTER && value->type->kind != HIR_TYPE_MANY_POINTER) {
                    fail(ctx, "free requires pointer");
                    return 0;
                }
                if (value->kind == HIR_EXPR_BINDING) {
                    mark_binding_freed(ctx, value->as.binding);
                }
                out = new_expr(HIR_EXPR_FREE, primitive_type(ctx->program, HIR_TYPE_VOID), expr->line);
                out->as.unary.value = value;
                return out;
            }
            if (strcmp(expr->as.implicit.member, "some") == 0) {
                HirExpr* value = 0;
                if (expr->as.implicit.has_type_arg || expr->as.implicit.args.count != 0) {
                    fail(ctx, "implicit operation '.some()' takes no arguments");
                    return 0;
                }
                value = lower_expr_preserve_pointer(ctx, expr->as.implicit.value_target);
                if (!value) {
                    return 0;
                }
                if (value->type->kind != HIR_TYPE_OPTIONAL) {
                    fail(ctx, "some requires optional value");
                    return 0;
                }
                return make_optional_value_expr(ctx, value, expr->line);
            }
            fail(ctx, "unknown implicit value operation");
            return 0;
        }
        case AST_EXPR_SIZE_OF: {
            HirType* type = lower_type(ctx, &expr->as.size_of_type);
            if (!type) {
                return 0;
            }
            out = new_expr(HIR_EXPR_INT, primitive_type(ctx->program, HIR_TYPE_INT), expr->line);
            out->as.int_value = type_size_bytes(type);
            return out;
        }
        case AST_EXPR_STRING: {
            HirExpr* array_value = 0;
            int64_t char_value = 0;
            if (expected_type &&
                expected_type->kind == HIR_TYPE_OPTIONAL &&
                type_equals(expected_type->array_item, primitive_type(ctx->program, HIR_TYPE_CHARACTER))) {
                if (!lower_string_char_literal_value(expr, &char_value)) {
                    fail(ctx, "Char literal requires exactly one Unicode scalar");
                    return 0;
                }
                out = new_expr(HIR_EXPR_OPTIONAL_SOME, expected_type, expr->line);
                out->as.unary.value = new_expr(HIR_EXPR_CHAR, primitive_type(ctx->program, HIR_TYPE_CHARACTER), expr->line);
                out->as.unary.value->as.char_value = char_value;
                return out;
            }
            if (expected_type &&
                type_equals(expected_type, primitive_type(ctx->program, HIR_TYPE_CHARACTER))) {
                if (!lower_string_char_literal_value(expr, &char_value)) {
                    fail(ctx, "Char literal requires exactly one Unicode scalar");
                    return 0;
                }
                out = new_expr(HIR_EXPR_CHAR, primitive_type(ctx->program, HIR_TYPE_CHARACTER), expr->line);
                out->as.char_value = char_value;
                return out;
            }
            if (expected_type &&
                expected_type->kind == HIR_TYPE_POINTER &&
                expected_type->array_item &&
                expected_type->array_item->kind == HIR_TYPE_UINT8) {
                out = new_expr(HIR_EXPR_CSTRING, expected_type, expr->line);
                out->as.cstring_lit.text = strdup(expr->as.string_lit.text);
                out->as.cstring_lit.length = expr->as.string_lit.length;
                return out;
            }
            if (expected_type &&
                expected_type->kind == HIR_TYPE_STRUCT &&
                expected_type->struct_decl) {
                HirFunction* init_fn = 0;
                int init_index = 0;
                HirType* param_type = 0;
                HirExpr* arg = 0;
                if (expected_type->struct_decl->from_string_literal &&
                    expected_type->struct_decl->has_init) {
                    for (init_index = 0; init_index < expected_type->struct_decl->init_count; ++init_index) {
                        HirFunction* candidate = find_struct_init_function(ctx->program, expected_type->struct_decl, init_index);
                        if (candidate && candidate->params.count == 1) {
                            param_type = candidate->params.items[0]->type;
                            if (hir_type_is_uint8_array_or_slice(param_type)) {
                                if (init_fn) {
                                    fail(ctx, "FromStringLiteral requires exactly one matching init(UInt8[] bytes)");
                                    return 0;
                                }
                                init_fn = candidate;
                            }
                        }
                    }
                    if (!init_fn) {
                        fail(ctx, "FromStringLiteral requires init(UInt8[] bytes)");
                        return 0;
                    }
                    arg = lower_string_uint8_array_literal(ctx, expr);
                    arg = maybe_decay_array_to_slice(ctx, arg, param_type, expr->line);
                    if (!arg) {
                        return 0;
                    }
                    if (!type_equals(arg->type, param_type)) {
                        fail(ctx, "string literal initializer type mismatch");
                        return 0;
                    }
                    out = new_expr(HIR_EXPR_CALL, init_fn->return_type, expr->line);
                    out->as.call.callee = init_fn;
                    out->as.call.builtin = HIR_BUILTIN_NONE;
                    expr_list_push(&out->as.call.args, arg);
                    return out;
                }
            }
            if (!expected_type ||
                ((expected_type->kind != HIR_TYPE_ARRAY &&
                  expected_type->kind != HIR_TYPE_SLICE) ||
                 (!expected_type->array_item || expected_type->array_item->kind != HIR_TYPE_UINT8))) {
                fail(ctx, "string literal requires UInt8 array or slice type");
                return 0;
            }
            array_value = lower_string_uint8_array_literal(ctx, expr);
            return array_value;
        }
        case AST_EXPR_NAME: {
            HirBinding* binding = lookup_binding(ctx, expr->as.name);
            if (!binding) {
                HirFunction* fn = resolve_function_value_overload(ctx, expr->as.name, expected_type);
                if (!fn) {
                    if (ctx->error) {
                        return 0;
                    }
                    {
                        static char unknown_identifier_error[256];
                        snprintf(unknown_identifier_error, sizeof(unknown_identifier_error),
                                 "unknown identifier: %s", expr->as.name ? expr->as.name : "<null>");
                        fail(ctx, unknown_identifier_error);
                    }
                    return 0;
                }
                out = new_expr(HIR_EXPR_FUNCTION, function_type_from_function(ctx, fn), expr->line);
                out->as.function = fn;
                return out;
            }
            if (expected_type && expected_type->kind == HIR_TYPE_OPTIONAL && type_equals(expected_type->array_item, binding->type)) {
                out = new_expr(HIR_EXPR_OPTIONAL_SOME, expected_type, expr->line);
                out->as.unary.value = new_expr(HIR_EXPR_BINDING, binding->type, expr->line);
                out->as.unary.value->as.binding = binding;
                return out;
            }
            out = new_expr(HIR_EXPR_BINDING, binding->type, expr->line);
            out->as.binding = binding;
            out = maybe_auto_deref_pointer_expr(out, expected_type, expr->line);
            return maybe_wrap_expected_errorable_expr(ctx, out, expected_type, expr->line);
        }
        case AST_EXPR_OPTIONAL_FIELD:
            return lower_optional_chain_field(ctx, expr);
        case AST_EXPR_OPTIONAL_INDEX:
            return lower_optional_chain_index(ctx, expr);
        case AST_EXPR_COALESCE: {
            HirExpr* left = 0;
            HirExpr* right = 0;
            if (!is_pure_optional_base_expr(expr->as.coalesce.left)) {
                fail(ctx, "optional coalesce requires pure left operand");
                return 0;
            }
            left = lower_expr(ctx, expr->as.coalesce.left);
            if (!left) {
                return 0;
            }
            if (left->type->kind != HIR_TYPE_OPTIONAL) {
                fail(ctx, "optional coalesce requires optional left operand");
                return 0;
            }
            right = lower_expr_expected(ctx, expr->as.coalesce.right, left->type->array_item);
            if (!right) {
                return 0;
            }
            right = maybe_decay_array_to_slice(ctx, right, left->type->array_item, expr->line);
            if (!right) {
                return 0;
            }
            if (!type_equals(right->type, left->type->array_item)) {
                fail(ctx, "optional coalesce fallback type mismatch");
                return 0;
            }
            out = new_expr(HIR_EXPR_COALESCE, left->type->array_item, expr->line);
            out->as.coalesce.left = left;
            out->as.coalesce.right = right;
            return out;
        }
        case AST_EXPR_CATCH_FALLBACK: {
            HirExpr* left = lower_expr(ctx, expr->as.catch_fallback.left);
            HirExpr* fallback = 0;
            HirType* value_type = 0;
            if (!left) {
                return 0;
            }
            if (!is_errorable_type(left->type)) {
                fail(ctx, "catch fallback requires errorable expression");
                return 0;
            }
            value_type = errorable_value_type(left->type);
            fallback = lower_expr_expected(ctx, expr->as.catch_fallback.fallback, value_type);
            if (!fallback) {
                return 0;
            }
            fallback = maybe_decay_array_to_slice(ctx, fallback, value_type, expr->line);
            if (!fallback) {
                return 0;
            }
            if (!type_assignment_compatible(fallback->type, value_type)) {
                fail(ctx, "catch fallback type mismatch");
                return 0;
            }
            out = new_expr(HIR_EXPR_CATCH_FALLBACK, value_type, expr->line);
            out->as.catch_fallback.left = left;
            out->as.catch_fallback.fallback = fallback;
            return maybe_wrap_expected_errorable_expr(ctx, out, expected_type, expr->line);
        }
        case AST_EXPR_CATCH_HANDLER: {
            HirExpr* left = lower_expr(ctx, expr->as.catch_handler.left);
            HirExpr* handler = 0;
            HirType* value_type = 0;
            HirType* error_type = 0;
            HirBinding* binding = 0;
            Scope catch_scope;
            if (!left) {
                return 0;
            }
            if (!is_errorable_type(left->type)) {
                fail(ctx, "catch handler requires errorable expression");
                return 0;
            }
            value_type = errorable_value_type(left->type);
            error_type = errorable_error_type(left->type);
            binding = new_binding(error_type, 0, expr->as.catch_handler.binding_name, HIR_BINDING_LOCAL, expr->line);
            binding_list_push(&ctx->current_function->locals, binding);
            push_scope(ctx, &catch_scope);
            if (!bind_in_current_scope(ctx, binding)) {
                pop_scope(ctx);
                return 0;
            }
            handler = lower_expr_expected(ctx, expr->as.catch_handler.handler, value_type);
            if (!handler) {
                pop_scope(ctx);
                return 0;
            }
            handler = maybe_decay_array_to_slice(ctx, handler, value_type, expr->line);
            if (!handler) {
                pop_scope(ctx);
                return 0;
            }
            pop_scope(ctx);
            if (!type_assignment_compatible(handler->type, value_type)) {
                fail(ctx, "catch handler type mismatch");
                return 0;
            }
            out = new_expr(HIR_EXPR_CATCH_HANDLER, value_type, expr->line);
            out->as.catch_handler.left = left;
            out->as.catch_handler.binding = binding;
            out->as.catch_handler.handler = handler;
            return maybe_wrap_expected_errorable_expr(ctx, out, expected_type, expr->line);
        }
        case AST_EXPR_BLOCK: {
            Scope block_scope;
            HirBlock* body = (HirBlock*)calloc(1, sizeof(HirBlock));
            HirExpr* value = 0;
            if (!body) {
                fail(ctx, "out of memory");
                return 0;
            }
            push_scope(ctx, &block_scope);
            if (!lower_block_expr_stmts(ctx, expr->as.block_expr.body, body)) {
                pop_scope(ctx);
                return 0;
            }
            if (expr->as.block_expr.value) {
                value = lower_expr_value(ctx, expr->as.block_expr.value);
                if (!value) {
                    pop_scope(ctx);
                    return 0;
                }
            }
            pop_scope(ctx);
            out = new_expr(HIR_EXPR_BLOCK, value ? value->type : (expected_type ? expected_type : primitive_type(ctx->program, HIR_TYPE_VOID)), expr->line);
            out->as.block_expr.body = body;
            out->as.block_expr.value = value;
            if (!value) {
                return out;
            }
            return maybe_wrap_expected_errorable_expr(ctx, out, expected_type, expr->line);
        }
        case AST_EXPR_TRY: {
            HirExpr* value = 0;
            HirType* value_type = 0;
            HirType* produced_error_type = 0;
            TryScope try_scope;
            int saved_infer_try_error_flag = ctx->infer_try_error_flag;
            HirType* saved_inferred_try_error_type = ctx->inferred_try_error_type;
            int i = 0;
            HirExpr* result = 0;
            memset(&try_scope, 0, sizeof(try_scope));
            for (i = 0; i < expr->as.try_expr.catches.count; ++i) {
                if (expr->as.try_expr.catches.items[i].error_type.kind != AST_TYPE_INFER) {
                    HirTryCatch catch_item;
                    int j = 0;
                    memset(&catch_item, 0, sizeof(catch_item));
                    catch_item.error_type = lower_type(ctx, &expr->as.try_expr.catches.items[i].error_type);
                    if (!catch_item.error_type) {
                        return 0;
                    }
                    for (j = 0; j < try_scope.catches.count; ++j) {
                        if (type_equals(try_scope.catches.items[j].error_type, catch_item.error_type)) {
                            fail(ctx, "duplicate catch type");
                            return 0;
                        }
                    }
                    try_catch_list_push(&try_scope.catches, catch_item);
                }
            }
            try_scope.defer_start = ctx->active_defers.count;
            try_scope_list_push(&ctx->try_scopes, try_scope);
            ctx->infer_try_error_flag = 0;
            ctx->inferred_try_error_type = 0;
            for (i = 0; i < expr->as.try_expr.catches.count; ++i) {
                if (expr->as.try_expr.catches.items[i].error_type.kind == AST_TYPE_INFER) {
                    ctx->infer_try_error_flag = 1;
                    break;
                }
            }
            value = lower_expr_value(ctx, expr->as.try_expr.value);
            produced_error_type = ctx->inferred_try_error_type;
            ctx->try_scopes.count -= 1;
            ctx->infer_try_error_flag = saved_infer_try_error_flag;
            ctx->inferred_try_error_type = saved_inferred_try_error_type;
            if (!value) {
                return 0;
            }
            value_type = value->type;
            if (!produced_error_type) {
                for (i = 0; i < expr->as.try_expr.catches.count; ++i) {
                    if (expr->as.try_expr.catches.items[i].error_type.kind != AST_TYPE_INFER) {
                        produced_error_type = lower_type(ctx, &expr->as.try_expr.catches.items[i].error_type);
                        if (!produced_error_type) {
                            return 0;
                        }
                        break;
                    }
                }
            }
            if (!produced_error_type) {
                fail(ctx, "try expression requires errorable body");
                return 0;
            }
            for (i = 0; i < expr->as.try_expr.catches.count; ++i) {
                HirType* catch_error_type = expr->as.try_expr.catches.items[i].error_type.kind == AST_TYPE_INFER
                    ? produced_error_type
                    : lower_type(ctx, &expr->as.try_expr.catches.items[i].error_type);
                HirBinding* binding = 0;
                HirExpr* handler = 0;
                Scope catch_scope;
                if (!catch_error_type) {
                    return 0;
                }
                if (!type_equals(catch_error_type, produced_error_type)) {
                    fail(ctx, "try expression catch type does not match body error type");
                    return 0;
                }
                if (result) {
                    fail(ctx, "duplicate catch type");
                    return 0;
                }
                binding = new_binding(catch_error_type, 0, expr->as.try_expr.catches.items[i].binding_name, HIR_BINDING_LOCAL, expr->as.try_expr.catches.items[i].line);
                binding_list_push(&ctx->current_function->locals, binding);
                push_scope(ctx, &catch_scope);
                if (!bind_in_current_scope(ctx, binding)) {
                    pop_scope(ctx);
                    return 0;
                }
                handler = lower_expr_expected(ctx, expr->as.try_expr.catches.items[i].value, value_type);
                if (!handler) {
                    pop_scope(ctx);
                    return 0;
                }
                handler = maybe_decay_array_to_slice(ctx, handler, value_type, expr->line);
                if (!handler) {
                    pop_scope(ctx);
                    return 0;
                }
                pop_scope(ctx);
                if (!type_assignment_compatible(handler->type, value_type)) {
                    fail(ctx, "try expression catch branch type mismatch");
                    return 0;
                }
                result = new_expr(HIR_EXPR_CATCH_HANDLER, value_type, expr->line);
                result->as.catch_handler.left = value;
                result->as.catch_handler.binding = binding;
                result->as.catch_handler.handler = handler;
            }
            if (!result) {
                fail(ctx, "try expression requires at least one matching catch");
                return 0;
            }
            return maybe_wrap_expected_errorable_expr(ctx, result, expected_type, expr->line);
        }
        case AST_EXPR_COALESCE_CONTROL:
            fail(ctx, "control-flow coalesce is only supported in local variable initialization");
            return 0;
        case AST_EXPR_ADDR: {
            HirExpr* value = lower_expr_value(ctx, expr->as.unary.value);
            HirType* pointer_type = 0;
            if (!value) {
                return 0;
            }
            if (!is_lvalue_expr(value)) {
                fail(ctx, "address-of requires lvalue");
                return 0;
            }
            pointer_type = new_owned_type(ctx->program, HIR_TYPE_POINTER);
            pointer_type->array_item = value->type;
            out = new_expr(HIR_EXPR_ADDR, pointer_type, expr->line);
            out->as.unary.value = value;
            return out;
        }
        case AST_EXPR_DEREF: {
            HirExpr* value = lower_expr_value(ctx, expr->as.unary.value);
            if (!value) {
                return 0;
            }
            if (value->type->kind != HIR_TYPE_REFERENCE && value->type->kind != HIR_TYPE_POINTER) {
                fail(ctx, "deref requires pointer");
                return 0;
            }
            out = new_expr(HIR_EXPR_DEREF, value->type->array_item, expr->line);
            out->as.unary.value = value;
            return out;
        }
        case AST_EXPR_NEW: {
            HirExpr* value = lower_new_primitive_constructor(ctx, expr->as.unary.value);
            HirType* pointer_type = 0;
            if (!value) {
                value = lower_expr(ctx, expr->as.unary.value);
                if (!value) {
                    return 0;
                }
                if (!hir_expr_is_new_constructible(value)) {
                    fail(ctx, "new requires a construction expression");
                    return 0;
                }
            }
            pointer_type = new_owned_type(ctx->program, HIR_TYPE_POINTER);
            pointer_type->array_item = value->type;
            out = new_expr(HIR_EXPR_NEW, pointer_type, expr->line);
            out->as.unary.value = value;
            return out;
        }
        case AST_EXPR_FREE: {
            HirExpr* value = lower_expr_value(ctx, expr->as.unary.value);
            if (!value) {
                return 0;
            }
            if (value->type->kind != HIR_TYPE_POINTER && value->type->kind != HIR_TYPE_MANY_POINTER) {
                fail(ctx, "free requires pointer");
                return 0;
            }
            if (value->kind == HIR_EXPR_BINDING) {
                mark_binding_freed(ctx, value->as.binding);
            }
            out = new_expr(HIR_EXPR_FREE, primitive_type(ctx->program, HIR_TYPE_VOID), expr->line);
            out->as.unary.value = value;
            return out;
        }
        case AST_EXPR_BIT_NOT: {
            HirExpr* value = lower_expr_value(ctx, expr->as.unary.value);
            if (!value) {
                return 0;
            }
            if (!is_bitwise_integer_type(value->type)) {
                fail(ctx, "bitwise not requires integer operand");
                return 0;
            }
            out = new_expr(HIR_EXPR_BIT_NOT, value->type, expr->line);
            out->as.unary.value = value;
            return maybe_wrap_expected_optional_expr(ctx, out, expected_type, expr->line);
        }
        case AST_EXPR_IF: {
            HirExpr* cond = lower_expr_expected(ctx, expr->as.if_expr.cond, primitive_type(ctx->program, HIR_TYPE_BOOL));
            HirExpr* then_expr = 0;
            HirExpr* else_expr = 0;
            if (!cond) {
                return 0;
            }
            if (cond->type->kind != HIR_TYPE_BOOL) {
                fail(ctx, "if expression condition must be Bool");
                return 0;
            }
            then_expr = lower_expr_value(ctx, expr->as.if_expr.then_expr);
            if (!then_expr) {
                return 0;
            }
            else_expr = lower_expr_value(ctx, expr->as.if_expr.else_expr);
            if (!else_expr) {
                return 0;
            }
            if (hir_expr_is_never_exit(then_expr) && !hir_expr_is_never_exit(else_expr)) {
                then_expr->type = else_expr->type;
            } else if (hir_expr_is_never_exit(else_expr) && !hir_expr_is_never_exit(then_expr)) {
                else_expr->type = then_expr->type;
            }
            if (!type_equals(then_expr->type, else_expr->type)) {
                fail(ctx, "if expression branch type mismatch");
                return 0;
            }
            out = new_expr(HIR_EXPR_IF, then_expr->type, expr->line);
            out->as.if_expr.cond = cond;
            out->as.if_expr.then_expr = then_expr;
            out->as.if_expr.else_expr = else_expr;
            return out;
        }
        case AST_EXPR_SWITCH:
            return lower_switch_expr(ctx, expr);
        case AST_EXPR_TERNARY: {
            HirExpr* cond = lower_expr_expected(ctx, expr->as.ternary.cond, primitive_type(ctx->program, HIR_TYPE_BOOL));
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
            const char* dot = strrchr(expr->as.call.callee, '.');
            HirBuiltinKind builtin = builtin_kind(expr->as.call.callee);
            HirExpr* primitive_init = 0;
            if (builtin != HIR_BUILTIN_NONE) {
                out = new_expr(HIR_EXPR_CALL, primitive_type(ctx->program, HIR_TYPE_INT), expr->line);
                if (!lower_builtin_call(ctx, expr, out, builtin)) return 0;
                return out;
            }
            primitive_init = lower_primitive_init_constructor(ctx, expr);
            if (primitive_init || ctx->error) {
                return primitive_init;
            }
            if (dot) {
                char* owner_name = 0;
                char* trait_name = 0;
                char* parsed_member_name = 0;
                const char* member_name = 0;
                HirBinding* owner_binding = 0;
                HirType* owner_type = 0;
                HirExprList matched_args;
                memset(&matched_args, 0, sizeof(matched_args));
                if (parse_trait_qualified_callee(expr->as.call.callee, &owner_name, &trait_name, &parsed_member_name)) {
                    member_name = parsed_member_name;
                } else {
                    size_t owner_len = (size_t)(dot - expr->as.call.callee);
                    owner_name = (char*)malloc(owner_len + 1);
                    if (!owner_name) {
                        fail(ctx, "out of memory");
                        return 0;
                    }
                    memcpy(owner_name, expr->as.call.callee, owner_len);
                    owner_name[owner_len] = '\0';
                    member_name = dot + 1;
                }
                owner_binding = lookup_binding(ctx, owner_name);
                if (owner_binding) {
                    HirFunction* method = 0;
                    HirExpr* receiver_arg = 0;
                    HirType* method_owner_type = 0;
                    owner_type = owner_binding->type;
                    receiver_arg = make_binding_expr(owner_binding, expr->line);
                    method_owner_type = instance_method_owner_type(owner_type);
                    method = resolve_method_call_overload(ctx, method_owner_type, member_name, 0, &expr->as.call.args, 1, trait_name, &matched_args);
                    if (method) {
                        receiver_arg = make_instance_method_receiver(ctx, make_binding_expr(owner_binding, expr->line), method_owner_type, expr->line);
                        if (!receiver_arg) {
                            free(owner_name);
                            free(trait_name);
                            free(parsed_member_name);
                            return 0;
                        }
                        out = new_expr(HIR_EXPR_CALL, method->return_type, expr->line);
                        out->as.call.callee = method;
                        expr_list_push(&out->as.call.args, receiver_arg);
                        out->as.call.args.items = (HirExpr**)realloc(out->as.call.args.items, sizeof(HirExpr*) * (matched_args.count + 1));
                        memcpy(out->as.call.args.items + 1, matched_args.items, sizeof(HirExpr*) * matched_args.count);
                        out->as.call.args.count = matched_args.count + 1;
                        out->as.call.args.capacity = matched_args.count + 1;
                        free(owner_name);
                        free(trait_name);
                        free(parsed_member_name);
                        return maybe_wrap_expected_errorable_expr(ctx, out, expected_type, expr->line);
                    }
                    if (ctx->error) {
                        free(owner_name);
                        free(trait_name);
                        free(parsed_member_name);
                        return 0;
                    }
                    {
                        HirBuiltinKind builtin_method = builtin_method_kind(owner_type, member_name, 0);
                        if (builtin_method != HIR_BUILTIN_NONE) {
                            out = new_expr(HIR_EXPR_CALL,
                                           builtin_method == HIR_BUILTIN_EQUAL ? primitive_type(ctx->program, HIR_TYPE_BOOL)
                                                                               : primitive_type(ctx->program, HIR_TYPE_INT),
                                           expr->line);
                            if (!lower_builtin_method_call(ctx, expr->line, builtin_method, owner_type, receiver_arg, &expr->as.call.args, out)) {
                                free(owner_name);
                                free(trait_name);
                                free(parsed_member_name);
                                return 0;
                            }
                            free(owner_name);
                            free(trait_name);
                            free(parsed_member_name);
                            return maybe_wrap_expected_errorable_expr(ctx, out, expected_type, expr->line);
                        }
                    }
                    if (resolve_method_value_overload(ctx, method_owner_type, member_name, 1, trait_name, 0)) {
                        free(owner_name);
                        free(trait_name);
                        free(parsed_member_name);
                        fail(ctx, "static method called through instance");
                        return 0;
                    }
                } else {
                    char* owner_first = strdup(owner_name);
                    char* owner_first_dot = owner_first ? strchr(owner_first, '.') : 0;
                    HirBinding* receiver_root = 0;
                    if (owner_first_dot) {
                        *owner_first_dot = '\0';
                    }
                    receiver_root = owner_first ? lookup_binding(ctx, owner_first) : 0;
                    if (receiver_root) {
                        AstExpr* receiver_ast = qualified_path_to_expr(owner_name, expr->line);
                        HirExpr* receiver_value = receiver_ast ? lower_expr(ctx, receiver_ast) : 0;
                        HirExpr* receiver_arg = 0;
                        HirType* method_owner_type = receiver_value ? instance_method_owner_type(receiver_value->type) : 0;
                        HirFunction* method = method_owner_type ? resolve_method_call_overload(ctx, method_owner_type, member_name, 0, &expr->as.call.args, 1, trait_name, &matched_args) : 0;
                        if (method) {
                            receiver_arg = make_instance_method_receiver(ctx, receiver_value, method_owner_type, expr->line);
                            if (!receiver_arg) {
                                free(owner_name);
                                free(trait_name);
                                free(parsed_member_name);
                                return 0;
                            }
                            out = new_expr(HIR_EXPR_CALL, method->return_type, expr->line);
                            out->as.call.callee = method;
                            expr_list_push(&out->as.call.args, receiver_arg);
                            out->as.call.args.items = (HirExpr**)realloc(out->as.call.args.items, sizeof(HirExpr*) * (matched_args.count + 1));
                            memcpy(out->as.call.args.items + 1, matched_args.items, sizeof(HirExpr*) * matched_args.count);
                            out->as.call.args.count = matched_args.count + 1;
                            out->as.call.args.capacity = matched_args.count + 1;
                            free(owner_name);
                            free(trait_name);
                            free(parsed_member_name);
                            return maybe_wrap_expected_errorable_expr(ctx, out, expected_type, expr->line);
                        }
                        if (ctx->error) {
                            free(owner_name);
                            free(trait_name);
                            free(parsed_member_name);
                            return 0;
                        }
                    }
                    free(owner_first);
                }
                {
                    HirType* named_type = resolve_owner_type_name(ctx, owner_name);
                    if (named_type) {
                        HirFunction* method = resolve_method_call_overload(ctx, named_type, member_name, 1, &expr->as.call.args, 0, trait_name, &matched_args);
                        if (method) {
                            free(owner_name);
                            free(trait_name);
                            free(parsed_member_name);
                            out = new_expr(HIR_EXPR_CALL, method->return_type, expr->line);
                            out->as.call.callee = method;
                            out->as.call.args = matched_args;
                            return out;
                        }
                        if (ctx->error) {
                            free(owner_name);
                            free(trait_name);
                            free(parsed_member_name);
                            return 0;
                        }
                        if (named_type->kind == HIR_TYPE_STRUCT && strcmp(member_name, "init") == 0) {
                            HirStructDecl* init_struct = named_type->struct_decl;
                            if (!init_struct->has_init) {
                                free(owner_name);
                                free(trait_name);
                                free(parsed_member_name);
                                fail(ctx, "struct has no init");
                                return 0;
                            }
                            free(owner_name);
                            free(trait_name);
                            free(parsed_member_name);
                            out = new_expr(HIR_EXPR_CALL, named_type, expr->line);
                            out->as.call.callee = 0;
                            out->as.call.builtin = HIR_BUILTIN_NONE;
                            if (!lower_struct_init_args(ctx, init_struct, &expr->as.call.args, &out->as.call.args, expr->line)) {
                                return 0;
                            }
                            {
                                int fn_index = 0;
                                for (fn_index = 0; fn_index < ctx->program->functions.count; ++fn_index) {
                                    HirFunction* candidate = &ctx->program->functions.items[fn_index];
                                    if (candidate->struct_init_flag &&
                                        candidate->owner_struct == init_struct &&
                                        struct_init_name_matches(candidate, 0) &&
                                        candidate->params.count == out->as.call.args.count) {
                                        HirExprList candidate_args;
                                        const char* saved_error = ctx->error;
                                        memset(&candidate_args, 0, sizeof(candidate_args));
                                        ctx->error = 0;
                                        if (lower_struct_init_args_for_function(ctx, &expr->as.call.args, &candidate_args, candidate)) {
                                            out->as.call.callee = candidate;
                                            ctx->error = saved_error;
                                            break;
                                        }
                                        ctx->error = saved_error;
                                    }
                                }
                            }
                            if (!out->as.call.callee) {
                                fail(ctx, "internal error: missing struct init function");
                                return 0;
                            }
                            out->type = out->as.call.callee->return_type;
                            return maybe_wrap_expected_optional_expr(ctx, out, expected_type, expr->line);
                        }
                        if (named_type->kind == HIR_TYPE_STRUCT &&
                            struct_has_init_name(ctx->program, named_type->struct_decl, member_name)) {
                            HirFunction* init_fn = 0;
                            HirExprList named_args;
                            memset(&named_args, 0, sizeof(named_args));
                            if (!lower_struct_init_args_named(ctx, named_type->struct_decl, member_name, &expr->as.call.args, &named_args, &init_fn, expr->line)) {
                                free(owner_name);
                                free(trait_name);
                                free(parsed_member_name);
                                return 0;
                            }
                            free(owner_name);
                            free(trait_name);
                            free(parsed_member_name);
                            out = new_expr(HIR_EXPR_CALL, init_fn->return_type, expr->line);
                            out->as.call.callee = init_fn;
                            out->as.call.builtin = HIR_BUILTIN_NONE;
                            out->as.call.args = named_args;
                            return maybe_wrap_expected_optional_expr(ctx, out, expected_type, expr->line);
                        }
                        method = resolve_method_call_overload(ctx, named_type, member_name, 0, &expr->as.call.args, 0, trait_name, &matched_args);
                        if (method) {
                            free(owner_name);
                            free(trait_name);
                            free(parsed_member_name);
                            out = new_expr(HIR_EXPR_CALL, method->return_type, expr->line);
                            out->as.call.callee = method;
                            out->as.call.args = matched_args;
                            return maybe_wrap_expected_errorable_expr(ctx, out, expected_type, expr->line);
                        }
                        if (ctx->error) {
                            free(owner_name);
                            free(trait_name);
                            free(parsed_member_name);
                            return 0;
                        }
                        if (named_type->kind == HIR_TYPE_UNION) {
                            HirUnionVariant* variant = find_union_variant(named_type->union_decl, member_name);
                            if (variant) {
                                out = new_expr(HIR_EXPR_VARIANT, named_type, expr->line);
                                out->as.variant.variant = variant;
                                if (variant->payload_type->kind == HIR_TYPE_VOID) {
                                    if (expr->as.call.args.count != 0) {
                                        free(owner_name);
                                        free(trait_name);
                                        free(parsed_member_name);
                                        fail(ctx, "void variant does not accept a payload");
                                        return 0;
                                    }
                                } else {
                                    if (expr->as.call.args.count != 1 || expr->as.call.args.items[0].name) {
                                        free(owner_name);
                                        free(trait_name);
                                        free(parsed_member_name);
                                        fail(ctx, "variant payload required");
                                        return 0;
                                    }
                                    out->as.variant.payload = lower_expr(ctx, expr->as.call.args.items[0].value);
                                    if (!out->as.variant.payload) {
                                        free(owner_name);
                                        free(trait_name);
                                        free(parsed_member_name);
                                        return 0;
                                    }
                                    if (!type_equals(out->as.variant.payload->type, variant->payload_type)) {
                                        free(owner_name);
                                        free(trait_name);
                                        free(parsed_member_name);
                                        fail(ctx, "variant payload type mismatch");
                                        return 0;
                                    }
                                }
                                free(owner_name);
                                free(trait_name);
                                free(parsed_member_name);
                                return out;
                            }
                        }
                    }
                }
                free(owner_name);
                free(trait_name);
                free(parsed_member_name);
            }
            {
                HirExprList matched_args;
                HirFunction* callee = 0;
                memset(&matched_args, 0, sizeof(matched_args));
                callee = resolve_top_level_function_call(ctx, expr->as.call.callee, &expr->as.call.args, &matched_args);
                if (callee) {
                    out = new_expr(HIR_EXPR_CALL, callee->return_type, expr->line);
                    out->as.call.callee = callee;
                    out->as.call.callee_value = 0;
                    out->as.call.builtin = HIR_BUILTIN_NONE;
                    out->as.call.args = matched_args;
                    out = maybe_wrap_expected_errorable_expr(ctx, out, expected_type, expr->line);
                    return maybe_wrap_expected_optional_expr(ctx, out, expected_type, expr->line);
                }
                if (ctx->error) {
                    return 0;
                }
            }
            HirFunction* callee = 0;
            if (!callee) {
                HirStructDecl* init_struct = find_struct(ctx->program, expr->as.call.callee);
                if (init_struct) {
                    if (init_struct->has_init) {
                        int fn_index = 0;
                        out = new_expr(HIR_EXPR_CALL, new_owned_type(ctx->program, HIR_TYPE_STRUCT), expr->line);
                        out->type->struct_decl = init_struct;
                        out->as.call.callee = 0;
                        out->as.call.builtin = HIR_BUILTIN_NONE;
                        if (!lower_struct_init_args(ctx, init_struct, &expr->as.call.args, &out->as.call.args, expr->line)) {
                            return 0;
                        }
                        for (fn_index = 0; fn_index < ctx->program->functions.count; ++fn_index) {
                            HirFunction* candidate = &ctx->program->functions.items[fn_index];
                            HirExprList candidate_args;
                            const char* saved_error = ctx->error;
                            memset(&candidate_args, 0, sizeof(candidate_args));
                            if (!candidate->struct_init_flag || candidate->owner_struct != init_struct) {
                                continue;
                            }
                            if (!struct_init_name_matches(candidate, 0)) {
                                continue;
                            }
                            ctx->error = 0;
                            if (lower_struct_init_args_for_function(ctx, &expr->as.call.args, &candidate_args, candidate)) {
                                out->as.call.callee = candidate;
                                ctx->error = saved_error;
                                break;
                            }
                            ctx->error = saved_error;
                        }
                        if (!out->as.call.callee) {
                            fail(ctx, "internal error: missing struct init function");
                            return 0;
                        }
                        out->type = out->as.call.callee->return_type;
                        return maybe_wrap_expected_optional_expr(ctx, out, expected_type, expr->line);
                    }
                    if (init_struct->record_flag) {
                        fail(ctx, "record construction requires brace syntax");
                        return 0;
                    }
                    fail(ctx, "struct default construction requires brace syntax");
                    return 0;
                }
            }
            if (!callee) {
                HirBinding* callee_binding = lookup_binding(ctx, expr->as.call.callee);
                if (callee_binding && callee_binding->type && callee_binding->type->kind == HIR_TYPE_FUNCTION) {
                    int i = 0;
                    out = new_expr(HIR_EXPR_CALL, callee_binding->type->return_type, expr->line);
                    out->as.call.callee = 0;
                    out->as.call.callee_value = make_binding_expr(callee_binding, expr->line);
                    out->as.call.builtin = HIR_BUILTIN_NONE;
                    if (expr->as.call.args.count != callee_binding->type->tuple_items.count) {
                        fail(ctx, "call argument count mismatch");
                        return 0;
                    }
                    for (i = 0; i < expr->as.call.args.count; ++i) {
                        AstStructFieldInit* ast_arg = &expr->as.call.args.items[i];
                        HirExpr* arg = 0;
                        HirType* param_type = callee_binding->type->tuple_items.items[i];
                        if (ast_arg->name) {
                            fail(ctx, "labeled call arguments are not supported");
                            return 0;
                        }
                        arg = lower_expr_expected(ctx, ast_arg->value, param_type);
                        if (!arg) {
                            return 0;
                        }
                        arg = maybe_decay_array_to_slice(ctx, arg, param_type, ast_arg->line);
                        if (!arg) {
                            return 0;
                        }
                        if (!type_assignment_compatible(arg->type, param_type)) {
                            fail(ctx, "call argument type mismatch");
                            return 0;
                        }
                        expr_list_push(&out->as.call.args, arg);
                    }
                    out = maybe_wrap_expected_errorable_expr(ctx, out, expected_type, expr->line);
                    return maybe_wrap_expected_optional_expr(ctx, out, expected_type, expr->line);
                }
                {
                    static char unknown_function_error[256];
                    snprintf(unknown_function_error, sizeof(unknown_function_error),
                             "unknown function: %s", expr->as.call.callee ? expr->as.call.callee : "<null>");
                    fail(ctx, unknown_function_error);
                }
                return 0;
            }
            {
                static char unknown_function_error[256];
                snprintf(unknown_function_error, sizeof(unknown_function_error),
                         "unknown function: %s", expr->as.call.callee ? expr->as.call.callee : "<null>");
                fail(ctx, unknown_function_error);
            }
            return 0;
        }
        case AST_EXPR_VARIANT: {
            HirEnumDecl* enum_decl = 0;
            HirEnumMember* enum_member = 0;
            HirUnionDecl* union_decl = 0;
            HirType* union_type = 0;
            if (expr->as.variant.union_name &&
                !expr->as.variant.payload &&
                expr->as.variant.bindings.count == 0 &&
                !expr->as.variant.pattern_flag) {
                HirType* owner_type = resolve_owner_type_name(ctx, expr->as.variant.union_name);
                if (owner_type) {
                    HirFunction* method = resolve_method_value_overload(ctx,
                                                                        owner_type,
                                                                        expr->as.variant.variant_name,
                                                                        1,
                                                                        0,
                                                                        expected_type);
                    if (!method && !ctx->error) {
                        method = resolve_method_value_overload(ctx,
                                                               owner_type,
                                                               expr->as.variant.variant_name,
                                                               0,
                                                               0,
                                                               expected_type);
                    }
                    if (method) {
                        out = new_expr(HIR_EXPR_FUNCTION, function_type_from_function(ctx, method), expr->line);
                        out->as.function = method;
                        return out;
                    }
                    if (ctx->error) {
                        return 0;
                    }
                }
            }
            if (!expr->as.variant.union_name && expected_type) {
                if (!expr->as.variant.payload && expr->as.variant.bindings.count == 0 && expected_type->kind == HIR_TYPE_ENUM) {
                    enum_member = find_enum_member_in_decl(expected_type->enum_decl, expr->as.variant.variant_name);
                    if (!enum_member) {
                        fail(ctx, "unknown enum member");
                        return 0;
                    }
                    out = new_expr(HIR_EXPR_ENUM_MEMBER, expected_type, expr->line);
                    out->as.enum_member.member = enum_member;
                    return out;
                }
                if (expected_type->kind == HIR_TYPE_UNION) {
                    HirUnionVariant* expected_variant = find_union_variant(expected_type->union_decl, expr->as.variant.variant_name);
                    if (!expected_variant) {
                        fail(ctx, "unknown union variant");
                        return 0;
                    }
                    if (!expr->as.variant.payload && expr->as.variant.bindings.count == 0) {
                        return make_int_expr(ctx, expected_variant->tag_value, expr->line);
                    }
                    if (expr->as.variant.pattern_flag) {
                        fail(ctx, "variant pattern is not a value expression");
                        return 0;
                    }
                    out = new_expr(HIR_EXPR_VARIANT, expected_type, expr->line);
                    out->as.variant.variant = expected_variant;
                    if (expected_variant->payload_type->kind == HIR_TYPE_VOID) {
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
                    out->as.variant.payload = lower_expr_expected(ctx, expr->as.variant.payload, expected_variant->payload_type);
                    if (!out->as.variant.payload) {
                        return 0;
                    }
                    if (!type_assignment_compatible(out->as.variant.payload->type, expected_variant->payload_type)) {
                        fail(ctx, "variant payload type mismatch");
                        return 0;
                    }
                    return maybe_wrap_expected_optional_expr(ctx, out, expected_type, expr->line);
                }
            }
            if (!expr->as.variant.payload && expr->as.variant.bindings.count == 0) {
                if (!expr->as.variant.union_name) {
                    fail(ctx, "shorthand requires expected type");
                    return 0;
                }
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
            out->as.variant.payload = lower_expr_expected(ctx, expr->as.variant.payload, variant->payload_type);
            if (!out->as.variant.payload) {
                return 0;
            }
            if (!type_assignment_compatible(out->as.variant.payload->type, variant->payload_type)) {
                fail(ctx, "variant payload type mismatch");
                return 0;
            }
            return out;
            }
        }
        case AST_EXPR_FIELD: {
            if (expr->as.field.base &&
                expr->as.field.base->kind == AST_EXPR_NAME) {
                HirType* owner_type = resolve_owner_type_name(ctx, expr->as.field.base->as.name);
                if (owner_type) {
                    out = make_static_field_expr(ctx, expr->as.field.base->as.name, expr->as.field.name, expr->line);
                    if (out) {
                        return maybe_wrap_expected_errorable_expr(ctx, out, expected_type, expr->line);
                    }
                    HirFunction* method = resolve_method_value_overload(ctx, owner_type, expr->as.field.name, 1, expr->as.field.trait_name, expected_type);
                    if (!method && !ctx->error) {
                        method = resolve_method_value_overload(ctx, owner_type, expr->as.field.name, 0, expr->as.field.trait_name, expected_type);
                    }
                    if (method) {
                        out = new_expr(HIR_EXPR_FUNCTION, function_type_from_function(ctx, method), expr->line);
                        out->as.function = method;
                        return out;
                    }
                    if (ctx->error) {
                        return 0;
                    }
                }
            }
            if (expr->as.field.trait_name) {
                fail(ctx, "trait-qualified field access requires a method");
                return 0;
            }
            HirExpr* base = lower_expr(ctx, expr->as.field.base);
            HirType* base_type = 0;
            if (!base) {
                return 0;
            }
            base_type = base->type;
            if ((base_type->kind == HIR_TYPE_REFERENCE || base_type->kind == HIR_TYPE_POINTER) && base_type->array_item && base_type->array_item->kind == HIR_TYPE_STRUCT) {
                base_type = base_type->array_item;
            }
            if (base_type->kind == HIR_TYPE_ENUM && strcmp(expr->as.field.name, "value") == 0) {
                out = new_expr(HIR_EXPR_ENUM_VALUE, primitive_type(ctx->program, HIR_TYPE_INT), expr->line);
                out->as.enum_value.value = base;
                return out;
            }
            if (base_type->kind == HIR_TYPE_UNION && strcmp(expr->as.field.name, "tag") == 0) {
                out = make_union_tag_expr(ctx, base, expr->line);
                return out;
            }
            if (base_type->kind == HIR_TYPE_UNION) {
                HirUnionVariant* variant = find_union_variant(base_type->union_decl, expr->as.field.name);
                if (variant && variant->payload_type->kind != HIR_TYPE_VOID && variant->payload_type->kind != HIR_TYPE_TUPLE) {
                    out = make_union_field_expr(ctx, base, variant, -1, variant->payload_type, expr->line);
                    return out;
                }
            }
            if (base_type->kind == HIR_TYPE_SLICE && strcmp(expr->as.field.name, "length") == 0) {
                out = new_expr(HIR_EXPR_SLICE_LENGTH, primitive_type(ctx->program, HIR_TYPE_INT), expr->line);
                out->as.slice_length.base = base;
                return maybe_wrap_expected_optional_expr(ctx, out, expected_type, expr->line);
            }
            if (base_type->kind == HIR_TYPE_STRUCT) {
                int field_index = -1;
                HirStructField* field = find_struct_field(base_type->struct_decl, expr->as.field.name, &field_index);
                if (!field) {
                    out = make_static_field_expr(ctx, base_type->struct_decl->name, expr->as.field.name, expr->line);
                    if (out) {
                        return maybe_wrap_expected_errorable_expr(ctx, out, expected_type, expr->line);
                    }
                    fail_unknown_field(ctx, base_type->struct_decl->name, expr->as.field.name);
                    return 0;
                }
                out = new_expr(HIR_EXPR_STRUCT_FIELD, field->type, expr->line);
                out->as.struct_field.base = base;
                out->as.struct_field.field = field;
                out->as.struct_field.field_index = field_index;
                out = maybe_auto_deref_pointer_expr(out, expected_type, expr->line);
                if (!out) {
                    return 0;
                }
                return maybe_wrap_expected_optional_expr(ctx, out, expected_type, expr->line);
            }
            fail(ctx, "unknown field");
            return 0;
        }
        case AST_EXPR_STRUCT: {
            HirStructDecl* struct_decl = 0;
            const AstStructDecl* ast_struct = 0;
            if (expr->as.struct_lit.type_name) {
                HirType* named_struct_type = resolve_owner_type_name(ctx, expr->as.struct_lit.type_name);
                if (!named_struct_type || named_struct_type->kind != HIR_TYPE_STRUCT) {
                    fail(ctx, "unknown named type");
                    return 0;
                }
                struct_decl = named_struct_type->struct_decl;
                if (!struct_decl->record_flag && struct_decl->has_init) {
                    fail(ctx, "struct with init requires call syntax");
                    return 0;
                }
                ast_struct = find_ast_struct(ctx->ast, struct_decl->name);
            } else {
                if (!expected_type || expected_type->kind != HIR_TYPE_STRUCT || !expected_type->struct_decl) {
                    fail(ctx, "brace literal requires expected struct or record type");
                    return 0;
                }
                if (!expected_type->struct_decl->record_flag && expected_type->struct_decl->has_init) {
                    fail(ctx, "struct with init requires call syntax");
                    return 0;
                }
                struct_decl = expected_type->struct_decl;
                ast_struct = find_ast_struct(ctx->ast, struct_decl->name);
            }
            out = new_expr(HIR_EXPR_STRUCT, new_owned_type(ctx->program, HIR_TYPE_STRUCT), expr->line);
            out->type->struct_decl = struct_decl;
            if (!lower_default_struct_call_args(ctx, ast_struct, struct_decl, &expr->as.struct_lit.fields, out, expr->line)) {
                return 0;
            }
            return maybe_wrap_expected_optional_expr(ctx, out, expected_type, expr->line);
        }
        case AST_EXPR_BINARY: {
            int arithmetic_op = expr->as.binary.op == AST_BIN_ADD ||
                                expr->as.binary.op == AST_BIN_SUB ||
                                expr->as.binary.op == AST_BIN_MUL ||
                                expr->as.binary.op == AST_BIN_MOD ||
                                expr->as.binary.op == AST_BIN_DIV;
            int logic_op = expr->as.binary.op == AST_BIN_LOGIC_AND ||
                           expr->as.binary.op == AST_BIN_LOGIC_OR;
            if (expr->as.binary.op == AST_BIN_IS) {
                fail(ctx, "is pattern is only supported in if and while conditions");
                return 0;
            }
            if (logic_op) {
                HirType* bool_type = primitive_type(ctx->program, HIR_TYPE_BOOL);
                HirExpr* left = lower_expr_expected(ctx, expr->as.binary.left, bool_type);
                HirExpr* right = 0;
                HirExpr* literal = 0;
                if (!left) {
                    return 0;
                }
                if (left->type->kind != HIR_TYPE_BOOL) {
                    fail(ctx, "logical operation requires Bool operands");
                    return 0;
                }
                right = lower_expr_expected(ctx, expr->as.binary.right, bool_type);
                if (!right) {
                    return 0;
                }
                if (right->type->kind != HIR_TYPE_BOOL) {
                    fail(ctx, "logical operation requires Bool operands");
                    return 0;
                }
                literal = new_expr(HIR_EXPR_BOOL, bool_type, expr->line);
                out = new_expr(HIR_EXPR_IF, bool_type, expr->line);
                out->as.if_expr.cond = left;
                if (expr->as.binary.op == AST_BIN_LOGIC_AND) {
                    literal->as.bool_value = 0;
                    out->as.if_expr.then_expr = right;
                    out->as.if_expr.else_expr = literal;
                } else {
                    literal->as.bool_value = 1;
                    out->as.if_expr.then_expr = literal;
                    out->as.if_expr.else_expr = right;
                }
                return maybe_wrap_expected_optional_expr(ctx, out, expected_type, expr->line);
            }
            int bitwise_op = expr->as.binary.op == AST_BIN_BIT_AND ||
                             expr->as.binary.op == AST_BIN_BIT_OR ||
                             expr->as.binary.op == AST_BIN_BIT_XOR;
            int shift_op = expr->as.binary.op == AST_BIN_SHL ||
                           expr->as.binary.op == AST_BIN_SHR;
            int comparison_op = !arithmetic_op && !bitwise_op && !shift_op;
            HirExpr* left = 0;
            HirExpr* right = 0;
            if ((arithmetic_op || comparison_op || bitwise_op || shift_op) &&
                is_numeric_literal_expr(expr->as.binary.left) &&
                !is_numeric_literal_expr(expr->as.binary.right)) {
                right = lower_expr_value(ctx, expr->as.binary.right);
                if (!right) {
                    return 0;
                }
                left = lower_expr_expected(ctx, expr->as.binary.left, right->type);
                if (!left) {
                    return 0;
                }
            } else if ((arithmetic_op || comparison_op || bitwise_op || shift_op) &&
                       is_numeric_literal_expr(expr->as.binary.right) &&
                       !is_numeric_literal_expr(expr->as.binary.left)) {
                left = lower_expr_value(ctx, expr->as.binary.left);
                if (!left) {
                    return 0;
                }
                right = lower_expr_expected(ctx, expr->as.binary.right, left->type);
                if (!right) {
                    return 0;
                }
            } else if (comparison_op &&
                (is_expected_type_shorthand_expr(expr->as.binary.left) || is_expected_type_null_expr(expr->as.binary.left)) &&
                !is_expected_type_shorthand_expr(expr->as.binary.right) &&
                !is_expected_type_null_expr(expr->as.binary.right)) {
                right = lower_expr_value(ctx, expr->as.binary.right);
                if (!right) {
                    return 0;
                }
                left = lower_expr_expected(ctx, expr->as.binary.left, right->type);
                if (!left) {
                    return 0;
                }
            } else {
                left = lower_expr_value(ctx, expr->as.binary.left);
                if (!left) {
                    return 0;
                }
                if (comparison_op &&
                    (is_expected_type_shorthand_expr(expr->as.binary.right) || is_expected_type_null_expr(expr->as.binary.right))) {
                    right = lower_expr_expected(ctx, expr->as.binary.right, left->type);
                } else {
                    right = lower_expr_value(ctx, expr->as.binary.right);
                }
                if (!right) {
                    return 0;
                }
            }
            out = new_expr(HIR_EXPR_BINARY, primitive_type(ctx->program, HIR_TYPE_INT), expr->line);
            out->as.binary.left = left;
            out->as.binary.right = right;
            switch (expr->as.binary.op) {
                case AST_BIN_ADD: out->as.binary.op = HIR_BIN_ADD; break;
                case AST_BIN_SUB: out->as.binary.op = HIR_BIN_SUB; break;
                case AST_BIN_MUL: out->as.binary.op = HIR_BIN_MUL; break;
                case AST_BIN_MOD: out->as.binary.op = HIR_BIN_MOD; break;
                case AST_BIN_DIV: out->as.binary.op = HIR_BIN_DIV; break;
                case AST_BIN_BIT_AND: out->as.binary.op = HIR_BIN_BIT_AND; break;
                case AST_BIN_BIT_OR: out->as.binary.op = HIR_BIN_BIT_OR; break;
                case AST_BIN_BIT_XOR: out->as.binary.op = HIR_BIN_BIT_XOR; break;
                case AST_BIN_LOGIC_AND: break;
                case AST_BIN_LOGIC_OR: break;
                case AST_BIN_SHL: out->as.binary.op = HIR_BIN_SHL; break;
                case AST_BIN_SHR: out->as.binary.op = HIR_BIN_SHR; break;
                case AST_BIN_EQ: out->as.binary.op = HIR_BIN_EQ; out->type = primitive_type(ctx->program, HIR_TYPE_BOOL); break;
                case AST_BIN_NE: out->as.binary.op = HIR_BIN_NE; out->type = primitive_type(ctx->program, HIR_TYPE_BOOL); break;
                case AST_BIN_LT: out->as.binary.op = HIR_BIN_LT; out->type = primitive_type(ctx->program, HIR_TYPE_BOOL); break;
                case AST_BIN_LE: out->as.binary.op = HIR_BIN_LE; out->type = primitive_type(ctx->program, HIR_TYPE_BOOL); break;
                case AST_BIN_GT: out->as.binary.op = HIR_BIN_GT; out->type = primitive_type(ctx->program, HIR_TYPE_BOOL); break;
                case AST_BIN_GE: out->as.binary.op = HIR_BIN_GE; out->type = primitive_type(ctx->program, HIR_TYPE_BOOL); break;
                case AST_BIN_IS: break;
            }
            if (arithmetic_op) {
                if (!type_equals(left->type, right->type) ||
                    (!is_integer_like_type(left->type) &&
                     !is_float_like_type(left->type))) {
                    fail(ctx, "arithmetic requires matching numeric operands");
                    return 0;
                }
                if (expr->as.binary.op == AST_BIN_MOD && !is_integer_like_type(left->type)) {
                    fail(ctx, "modulo requires integer operands");
                    return 0;
                }
                out->type = left->type;
            } else if (bitwise_op) {
                if (!type_equals(left->type, right->type) ||
                    !is_bitwise_integer_type(left->type)) {
                    fail(ctx, "bitwise operation requires matching integer operands");
                    return 0;
                }
                out->type = left->type;
            } else if (shift_op) {
                if (!is_bitwise_integer_type(left->type) ||
                    !is_bitwise_integer_type(right->type)) {
                    fail(ctx, "shift requires integer operands");
                    return 0;
                }
                out->type = left->type;
            } else {
                if (!type_equals(left->type, right->type)) {
                    fail(ctx, "comparison requires matching operand types");
                    return 0;
                }
                if (left->type->kind == HIR_TYPE_OPTIONAL) {
                    if ((expr->as.binary.op != AST_BIN_EQ && expr->as.binary.op != AST_BIN_NE) ||
                        (!is_expected_type_null_expr(expr->as.binary.left) && !is_expected_type_null_expr(expr->as.binary.right))) {
                        fail(ctx, "comparison operand type unsupported");
                        return 0;
                    }
                } else if (left->type->kind == HIR_TYPE_REFERENCE || left->type->kind == HIR_TYPE_POINTER || left->type->kind == HIR_TYPE_MANY_POINTER) {
                    if (expr->as.binary.op != AST_BIN_EQ && expr->as.binary.op != AST_BIN_NE) {
                        fail(ctx, "comparison operand type unsupported");
                        return 0;
                    }
                } else if (is_float_like_type(left->type)) {
                    /* supported */
                } else if (!is_integer_like_type(left->type) &&
                           left->type->kind != HIR_TYPE_CHARACTER &&
                           left->type->kind != HIR_TYPE_ENUM) {
                    fail(ctx, "comparison operand type unsupported");
                    return 0;
                }
            }
            return out;
        }
        case AST_EXPR_TUPLE: {
            HirType* tuple_type = expr->as.tuple.items.count == 0
                ? primitive_type(ctx->program, HIR_TYPE_VOID)
                : new_owned_type(ctx->program, HIR_TYPE_TUPLE);
            out = new_expr(HIR_EXPR_TUPLE, tuple_type, expr->line);
            for (i = 0; i < expr->as.tuple.items.count; ++i) {
                HirExpr* item = lower_expr_value(ctx, expr->as.tuple.items.items[i]);
                if (!item) {
                    return 0;
                }
                expr_list_push(&out->as.tuple.items, item);
                type_list_push(&tuple_type->tuple_items, item->type);
            }
            return maybe_wrap_expected_optional_expr(ctx, out, expected_type, expr->line);
        }
        case AST_EXPR_ARRAY: {
            HirType* array_type = new_owned_type(ctx->program, HIR_TYPE_ARRAY);
            HirType* array_item_expected = 0;
            out = new_expr(HIR_EXPR_ARRAY, array_type, expr->line);
            array_type->array_length = expr->as.array.items.count;
            if (expected_type && expected_type->kind == HIR_TYPE_ARRAY) {
                array_item_expected = expected_type->array_item;
                array_type->array_item = array_item_expected;
            }
            for (i = 0; i < expr->as.array.items.count; ++i) {
                HirExpr* item = array_item_expected
                                    ? lower_expr_expected(ctx, expr->as.array.items.items[i], array_item_expected)
                                    : lower_expr_value(ctx, expr->as.array.items.items[i]);
                if (!item) {
                    return 0;
                }
                if (i == 0) {
                    array_type->array_item = item->type;
                } else if (!type_assignment_compatible(item->type, array_type->array_item)) {
                    fail(ctx, "array literal items must have matching types");
                    return 0;
                }
                expr_list_push(&out->as.array.items, item);
            }
            if (!array_type->array_item) {
                fail(ctx, "empty array literal is not supported");
                return 0;
            }
            return maybe_wrap_expected_optional_expr(ctx, out, expected_type, expr->line);
        }
        case AST_EXPR_INDEX: {
            HirExpr* base = lower_expr_value(ctx, expr->as.index.base);
            HirExpr* index = 0;
            HirExpr* subscript_call = 0;
            const AstExpr* range_start = 0;
            const AstExpr* range_end = 0;
            if (!base) {
                return 0;
            }
            if ((base->type->kind == HIR_TYPE_REFERENCE || base->type->kind == HIR_TYPE_POINTER) &&
                base->type->array_item &&
                (base->type->array_item->kind == HIR_TYPE_TUPLE ||
                 base->type->array_item->kind == HIR_TYPE_ARRAY ||
                 base->type->array_item->kind == HIR_TYPE_SLICE)) {
                HirExpr* deref = new_expr(HIR_EXPR_DEREF, base->type->array_item, expr->line);
                deref->as.unary.value = base;
                base = deref;
            }
            if (ast_range_bounds(expr->as.index.index, &range_start, &range_end) &&
                (base->type->kind == HIR_TYPE_ARRAY ||
                 base->type->kind == HIR_TYPE_SLICE ||
                 base->type->kind == HIR_TYPE_MANY_POINTER)) {
                HirExpr* start = lower_expr_expected(ctx, range_start, primitive_type(ctx->program, HIR_TYPE_INT));
                HirExpr* end = lower_expr_expected(ctx, range_end, primitive_type(ctx->program, HIR_TYPE_INT));
                HirType* slice_type = new_owned_type(ctx->program, HIR_TYPE_SLICE);
                if (!start || !end) {
                    return 0;
                }
                if (start->type->kind != HIR_TYPE_INT || end->type->kind != HIR_TYPE_INT) {
                    fail(ctx, "slice bounds must be Int");
                    return 0;
                }
                slice_type->mutable_flag = base->type->mutable_flag;
                slice_type->array_item = base->type->array_item;
                out = new_expr(HIR_EXPR_SLICE, slice_type, expr->line);
                out->as.slice.base = base;
                out->as.slice.start = start;
                out->as.slice.end = end;
                return maybe_wrap_expected_optional_expr(ctx, out, expected_type, expr->line);
            }
            if (base->type->kind == HIR_TYPE_TUPLE) {
                index = lower_expr_value(ctx, expr->as.index.index);
                if (!index) {
                    return 0;
                }
                if (expr->as.index.index->kind != AST_EXPR_INT) {
                    fail(ctx, "tuple index must be an integer literal");
                    return 0;
                }
                if (expr->as.index.index->as.int_value < 0 || expr->as.index.index->as.int_value >= base->type->tuple_items.count) {
                    fail(ctx, "tuple index out of bounds");
                    return 0;
                }
                out = new_expr(HIR_EXPR_INDEX, base->type->tuple_items.items[expr->as.index.index->as.int_value], expr->line);
            } else if (base->type->kind == HIR_TYPE_ARRAY || base->type->kind == HIR_TYPE_SLICE || base->type->kind == HIR_TYPE_MANY_POINTER) {
                index = lower_expr_value(ctx, expr->as.index.index);
                if (!index) {
                    return 0;
                }
                if (index->type->kind != HIR_TYPE_INT) {
                    fail(ctx, "array index must be Int");
                    return 0;
                }
                out = new_expr(HIR_EXPR_INDEX, base->type->array_item, expr->line);
            } else {
                subscript_call = lower_subscriptable_get_expr(ctx, base, expr->as.index.index, expected_type, expr->line);
                if (!subscript_call) {
                    if (!ctx->error) {
                        fail(ctx, "indexing currently requires a tuple, array, slice, many-pointer, or SubscriptGet base");
                    }
                    return 0;
                }
                return subscript_call;
            }
            out->as.index.base = base;
            out->as.index.index = index;
            return maybe_wrap_expected_optional_expr(ctx, out, expected_type, expr->line);
        }
        case AST_EXPR_SLICE_LENGTH: {
            HirExpr* base = lower_expr(ctx, expr->as.slice_length.base);
            if (!base) {
                return 0;
            }
            if (base->type->kind != HIR_TYPE_SLICE) {
                fail(ctx, "length requires slice base");
                return 0;
            }
            out = new_expr(HIR_EXPR_SLICE_LENGTH, primitive_type(ctx->program, HIR_TYPE_INT), expr->line);
            out->as.slice_length.base = base;
            return out;
        }
    }
    fail(ctx, "unsupported expression kind");
    ctx->current_line = saved_line;
    return 0;
}

static HirExpr* lower_expr(LowerContext* ctx, const AstExpr* expr) {
    HirExpr* out = lower_expr_expected(ctx, expr, 0);
    return out;
}

static HirStmt* lower_stmt(LowerContext* ctx, const AstStmt* stmt);

static int hir_type_supports_new_default_init(HirType* type) {
    if (!type) {
        return 0;
    }
    switch (type->kind) {
        case HIR_TYPE_INT:
        case HIR_TYPE_I8:
        case HIR_TYPE_I16:
        case HIR_TYPE_I32:
        case HIR_TYPE_I64:
        case HIR_TYPE_U8:
        case HIR_TYPE_U16:
        case HIR_TYPE_U32:
        case HIR_TYPE_U64:
        case HIR_TYPE_F16:
        case HIR_TYPE_F32:
        case HIR_TYPE_F64:
        case HIR_TYPE_FLOAT:
        case HIR_TYPE_DOUBLE:
        case HIR_TYPE_CHARACTER:
        case HIR_TYPE_UINT8:
        case HIR_TYPE_BOOL:
            return 1;
        default:
            return 0;
    }
}

static int hir_expr_is_new_constructible(const HirExpr* expr) {
    if (!expr) {
        return 0;
    }
    switch (expr->kind) {
        case HIR_EXPR_STRUCT:
        case HIR_EXPR_TUPLE:
        case HIR_EXPR_ARRAY:
        case HIR_EXPR_VARIANT:
        case HIR_EXPR_ENUM_MEMBER:
            return 1;
        case HIR_EXPR_CALL:
            if (expr->as.call.builtin == HIR_BUILTIN_ALLOC_ARRAY) {
                return 1;
            }
            return expr->as.call.callee && expr->as.call.callee->struct_init_flag;
        default:
            return 0;
    }
}

static HirExpr* lower_new_primitive_constructor(LowerContext* ctx, const AstExpr* expr) {
    HirType* target_type = 0;
    HirExpr* arg = 0;
    if (!expr) {
        return 0;
    }
    if (expr->kind == AST_EXPR_NAME) {
        target_type = resolve_owner_type_name(ctx, expr->as.name);
        if (!target_type || !hir_type_supports_new_default_init(target_type)) {
            return 0;
        }
        return make_zero_expr(ctx, target_type, expr->line);
    }
    if (expr->kind != AST_EXPR_CALL || expr->as.call.type_args.count != 0) {
        return 0;
    }
    target_type = resolve_owner_type_name(ctx, expr->as.call.callee);
    if (!target_type || !hir_type_supports_new_default_init(target_type)) {
        return 0;
    }
    if (expr->as.call.args.count == 0) {
        return make_zero_expr(ctx, target_type, expr->line);
    }
    if (expr->as.call.args.count != 1 || expr->as.call.args.items[0].name) {
        fail(ctx, "primitive constructor requires zero or one positional argument");
        return 0;
    }
    arg = lower_expr_expected(ctx, expr->as.call.args.items[0].value, target_type);
    if (!arg) {
        return 0;
    }
    if (!type_equals(arg->type, target_type)) {
        fail(ctx, "primitive constructor type mismatch");
        return 0;
    }
    return arg;
}

static HirExpr* lower_primitive_init_constructor(LowerContext* ctx, const AstExpr* expr) {
    HirType* target_type = 0;
    HirExpr* arg = 0;
    HirExpr* out = 0;
    if (!expr || expr->kind != AST_EXPR_CALL || expr->as.call.type_args.count != 0) {
        return 0;
    }
    target_type = resolve_owner_type_name(ctx, expr->as.call.callee);
    if (!target_type || !hir_type_supports_new_default_init(target_type)) {
        return 0;
    }
    if (expr->as.call.args.count == 0) {
        return make_zero_expr(ctx, target_type, expr->line);
    }
    if (expr->as.call.args.count != 1 || expr->as.call.args.items[0].name) {
        fail(ctx, "primitive init requires zero or one positional argument");
        return 0;
    }
    arg = lower_expr(ctx, expr->as.call.args.items[0].value);
    if (!arg) {
        return 0;
    }
    if (type_equals(arg->type, target_type)) {
        return arg;
    }
    if (!is_primitive_init_cast_type(arg->type) || !is_primitive_init_cast_type(target_type)) {
        fail(ctx, "primitive init type mismatch");
        return 0;
    }
    out = new_expr(HIR_EXPR_AS, target_type, expr->line);
    out->as.unary.value = arg;
    return out;
}

static HirExpr* make_zero_expr(LowerContext* ctx, HirType* type, int line) {
    int i = 0;
    HirExpr* expr = 0;
    switch (type->kind) {
        case HIR_TYPE_INT:
        case HIR_TYPE_I8:
        case HIR_TYPE_I16:
        case HIR_TYPE_I32:
        case HIR_TYPE_I64:
        case HIR_TYPE_U8:
        case HIR_TYPE_U16:
        case HIR_TYPE_U32:
        case HIR_TYPE_U64:
        case HIR_TYPE_UINT8:
            expr = new_expr(HIR_EXPR_INT, type, line);
            expr->as.int_value = 0;
            return expr;
        case HIR_TYPE_F16:
        case HIR_TYPE_F32:
        case HIR_TYPE_F64:
        case HIR_TYPE_FLOAT:
        case HIR_TYPE_DOUBLE:
            expr = new_expr(HIR_EXPR_FLOAT, type, line);
            expr->as.float_value = 0.0;
            return expr;
        case HIR_TYPE_CHARACTER:
            expr = new_expr(HIR_EXPR_CHAR, type, line);
            expr->as.char_value = 0;
            return expr;
        case HIR_TYPE_BOOL:
            expr = new_expr(HIR_EXPR_BOOL, type, line);
            expr->as.bool_value = 0;
            return expr;
        case HIR_TYPE_POINTER:
        case HIR_TYPE_MANY_POINTER:
        case HIR_TYPE_SLICE:
        case HIR_TYPE_FUNCTION:
            return new_expr(HIR_EXPR_NULL, type, line);
        case HIR_TYPE_ENUM:
            expr = new_expr(HIR_EXPR_ENUM_MEMBER, type, line);
            if (!type->enum_decl || type->enum_decl->members.count == 0) {
                return 0;
            }
            expr->as.enum_member.member = &type->enum_decl->members.items[0];
            return expr;
        case HIR_TYPE_OPTIONAL:
            return new_expr(HIR_EXPR_NULL, type, line);
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
            expr = new_expr(HIR_EXPR_VARIANT, type, line);
            if (!type->union_decl || type->union_decl->variants.count == 0) {
                return 0;
            }
            expr->as.variant.variant = &type->union_decl->variants.items[0];
            if (expr->as.variant.variant->payload_type->kind != HIR_TYPE_VOID) {
                expr->as.variant.payload = make_zero_expr(ctx, expr->as.variant.variant->payload_type, line);
                if (!expr->as.variant.payload) {
                    return 0;
                }
            }
            return expr;
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
        if (ast_stmt->kind == AST_STMT_GROUP) {
            if (!lower_init_block(ctx, &ast_stmt->as.group_stmt, out_block, self_binding)) {
                return 0;
            }
            continue;
        }
        if (ast_stmt->kind == AST_STMT_RETURN) {
            stmt = new_stmt(HIR_STMT_RETURN, ast_stmt->line);
            if (ast_stmt->as.ret.expr) {
                if (!ctx->current_function->struct_init_failable_flag ||
                    ast_stmt->as.ret.expr->kind != AST_EXPR_NULL) {
                    return fail(ctx, "struct init must not return a value");
                }
                stmt->as.ret.expr = lower_expr_expected(ctx, ast_stmt->as.ret.expr, ctx->current_function->return_type);
                if (!stmt->as.ret.expr) {
                    return 0;
                }
                stmt_list_push(&out_block->stmts, stmt);
                continue;
            }
            stmt->as.ret.expr = make_binding_expr(self_binding, ast_stmt->line);
            if (ctx->current_function->struct_init_failable_flag) {
                stmt->as.ret.expr = wrap_optional_result(ctx, stmt->as.ret.expr, ast_stmt->line);
            }
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
    int saved_line = ctx->current_line;
    ctx->current_line = stmt->line;
    ctx->current_column = stmt->column;
    switch (stmt->kind) {
        case AST_STMT_RETURN:
            if (!stmt->as.ret.expr) {
                if (ctx->current_function->return_type->kind != HIR_TYPE_VOID &&
                    !is_errorable_type(ctx->current_function->return_type)) {
                    fail(ctx, "return type mismatch");
                    return 0;
                }
                if (is_errorable_type(ctx->current_function->return_type) &&
                    errorable_value_type(ctx->current_function->return_type)->kind != HIR_TYPE_VOID) {
                    fail(ctx, "return type mismatch");
                    return 0;
                }
                if (is_errorable_type(ctx->current_function->return_type)) {
                    HirExpr* wrapped = new_expr(HIR_EXPR_VARIANT, ctx->current_function->return_type, stmt->line);
                    wrapped->as.variant.variant = &ctx->current_function->return_type->union_decl->variants.items[0];
                    wrapped->as.variant.payload = 0;
                    out->as.ret.expr = wrapped;
                } else {
                    out->as.ret.expr = 0;
                }
            } else {
                out->as.ret.expr = lower_expr_expected(ctx, stmt->as.ret.expr, ctx->current_function->return_type);
                if (!out->as.ret.expr) {
                    return 0;
                }
                out->as.ret.expr = maybe_decay_array_to_slice(ctx, out->as.ret.expr, ctx->current_function->return_type, stmt->line);
                if (!out->as.ret.expr) {
                    return 0;
                }
                if (is_errorable_type(ctx->current_function->return_type)) {
                    if (!is_errorable_type(out->as.ret.expr->type) &&
                        type_assignment_compatible(out->as.ret.expr->type, errorable_value_type(ctx->current_function->return_type))) {
                        HirExpr* wrapped = new_expr(HIR_EXPR_VARIANT, ctx->current_function->return_type, stmt->line);
                        wrapped->as.variant.variant = &ctx->current_function->return_type->union_decl->variants.items[0];
                        wrapped->as.variant.payload = out->as.ret.expr;
                        out->as.ret.expr = wrapped;
                    }
                }
                if (!type_assignment_compatible(out->as.ret.expr->type, ctx->current_function->return_type)) {
                    fail(ctx, "return type mismatch");
                    return 0;
                }
            }
            ctx->current_line = saved_line;
            return out;
        case AST_STMT_THROW:
            if (!is_errorable_type(ctx->current_function->return_type)) {
                fail(ctx, "throw requires an errorable return type");
                return 0;
            }
            out->as.throw_stmt.expr = lower_expr_expected(ctx, stmt->as.throw_stmt.expr, errorable_error_type(ctx->current_function->return_type));
            if (!out->as.throw_stmt.expr) {
                return 0;
            }
            if (!type_assignment_compatible(out->as.throw_stmt.expr->type, errorable_error_type(ctx->current_function->return_type))) {
                fail(ctx, "throw type mismatch");
                return 0;
            }
            if (ctx->try_scopes.count > 0 &&
                !current_try_catches_error(ctx, out->as.throw_stmt.expr->type)) {
                fail(ctx, "uncaught error type in try block");
                return 0;
            }
            ctx->current_line = saved_line;
            return out;
        case AST_STMT_VAR_DECL: {
            HirBinding* binding = 0;
            HirExpr* init = 0;
            HirType* type = primitive_type(ctx->program, HIR_TYPE_INT);
            if (stmt->as.var_decl.type.kind == AST_TYPE_INFER) {
                if (!stmt->as.var_decl.init) {
                    fail(ctx, "inferred variable declaration requires initializer");
                    return 0;
                }
                init = lower_expr(ctx, stmt->as.var_decl.init);
                if (!init) {
                    return 0;
                }
                type = init->type;
            } else {
                type = lower_type(ctx, &stmt->as.var_decl.type);
                if (!stmt->as.var_decl.init) {
                    fail(ctx, "variable declaration requires initializer");
                    return 0;
                }
                init = lower_expr_expected(ctx, stmt->as.var_decl.init, type);
                if (!init) {
                    return 0;
                }
                init = maybe_decay_array_to_slice(ctx, init, type, stmt->line);
                if (!init) {
                    return 0;
                }
                if (!type_assignment_compatible(init->type, type) && !apply_array_length_inference(type, init->type)) {
                    fail(ctx, "variable initializer type mismatch");
                    return 0;
                }
            }
            binding = new_binding(type, stmt->as.var_decl.type.mutable_flag, stmt->as.var_decl.name, HIR_BINDING_LOCAL, stmt->line);
            if (!bind_in_current_scope(ctx, binding)) {
                return 0;
            }
            binding_list_push(&ctx->current_function->locals, binding);
            out->as.var_decl.binding = binding;
            out->as.var_decl.init = init;
            ctx->current_line = saved_line;
            return out;
        }
        case AST_STMT_ASSIGN: {
            if (stmt->as.assign.target &&
                stmt->as.assign.target->kind == AST_EXPR_INDEX) {
                HirStmt* subscript_stmt = lower_subscriptable_set_stmt(ctx,
                                                                       stmt->as.assign.target,
                                                                       stmt->as.assign.value,
                                                                       stmt->line);
                if (subscript_stmt) {
                    return subscript_stmt;
                }
                if (ctx->error) {
                    return 0;
                }
            }
            if (stmt->as.assign.target->kind == AST_EXPR_FIELD) {
                out->as.assign.target = lower_expr_preserve_pointer(ctx, stmt->as.assign.target);
            } else {
                out->as.assign.target = lower_expr(ctx, stmt->as.assign.target);
            }
            if (!out->as.assign.target) {
                return 0;
            }
            if (out->as.assign.target->kind == HIR_EXPR_BINDING) {
                out->as.assign.binding = out->as.assign.target->as.binding;
            } else if (out->as.assign.target->kind == HIR_EXPR_INDEX) {
                if (out->as.assign.target->as.index.base->type->kind != HIR_TYPE_ARRAY &&
                    out->as.assign.target->as.index.base->type->kind != HIR_TYPE_SLICE &&
                    out->as.assign.target->as.index.base->type->kind != HIR_TYPE_MANY_POINTER) {
                    fail(ctx, "assignment target not found");
                    return 0;
                }
            } else if (stmt->as.assign.target->kind == AST_EXPR_INDEX &&
                       out->as.assign.target->kind == HIR_EXPR_CALL) {
                fail(ctx, "assignment target is immutable");
                return 0;
            } else if (out->as.assign.target->kind == HIR_EXPR_DEREF) {
            } else if (out->as.assign.target->kind != HIR_EXPR_STRUCT_FIELD) {
                fail(ctx, "assignment target not found");
                return 0;
            }
            if (!is_struct_init_self_field_assign(ctx, stmt->as.assign.target) &&
                !is_mutable_assignment_target(out->as.assign.target)) {
                fail(ctx, "assignment target is immutable");
                return 0;
            }
            out->as.assign.value = lower_expr_expected(ctx, stmt->as.assign.value, out->as.assign.target->type);
            if (!out->as.assign.value) {
                return 0;
            }
            out->as.assign.value = maybe_decay_array_to_slice(ctx, out->as.assign.value, out->as.assign.target->type, stmt->line);
            if (!out->as.assign.value) {
                return 0;
            }
            if (!type_assignment_compatible(out->as.assign.value->type, out->as.assign.target->type)) {
                fail(ctx, "assignment type mismatch");
                return 0;
            }
            ctx->current_line = saved_line;
            return out;
        }
        case AST_STMT_IF: {
            Scope inner;
            const char* optional_name = 0;
            int non_null_then = 0;
            if (stmt->as.if_stmt.cond &&
                stmt->as.if_stmt.cond->kind == AST_EXPR_BINARY &&
                stmt->as.if_stmt.cond->as.binary.op == AST_BIN_IS &&
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
                if (!lower_block(ctx, &stmt->as.if_stmt.then_block, &out->as.if_stmt.then_block, 0)) {
                    return 0;
                }
                pop_scope(ctx);
            } else {
                out->as.if_stmt.cond = lower_expr_expected(ctx, stmt->as.if_stmt.cond, primitive_type(ctx->program, HIR_TYPE_BOOL));
                if (!out->as.if_stmt.cond) {
                    return 0;
                }
                if (out->as.if_stmt.cond->type->kind != HIR_TYPE_BOOL) {
                    fail(ctx, "if condition must be Bool");
                    return 0;
                }
                push_scope(ctx, &inner);
                if (optional_null_compare_binding_name(stmt->as.if_stmt.cond, &non_null_then, &optional_name) && non_null_then) {
                    if (!bind_optional_narrow(ctx, optional_name, &out->as.if_stmt.then_block, stmt->line)) {
                        return 0;
                    }
                }
                if (!lower_block(ctx, &stmt->as.if_stmt.then_block, &out->as.if_stmt.then_block, 0)) {
                    return 0;
                }
                pop_scope(ctx);
            }
            if (stmt->as.if_stmt.has_else) {
                Scope else_scope;
                out->as.if_stmt.has_else = 1;
                push_scope(ctx, &else_scope);
                if (optional_null_compare_binding_name(stmt->as.if_stmt.cond, &non_null_then, &optional_name) && !non_null_then) {
                    if (!bind_optional_narrow(ctx, optional_name, &out->as.if_stmt.else_block, stmt->line)) {
                        return 0;
                    }
                }
                if (!lower_block(ctx, &stmt->as.if_stmt.else_block, &out->as.if_stmt.else_block, 0)) {
                    return 0;
                }
                pop_scope(ctx);
            }
            ctx->current_line = saved_line;
            return out;
        }
        case AST_STMT_TRY: {
            TryScope try_scope;
            int i = 0;
            memset(&try_scope, 0, sizeof(try_scope));
            try_scope.defer_start = ctx->active_defers.count;
            if (stmt->as.try_stmt.catches.count == 0) {
                fail(ctx, "try requires at least one catch");
                return 0;
            }
            for (i = 0; i < stmt->as.try_stmt.catches.count; ++i) {
                const AstTryCatch* ast_catch = &stmt->as.try_stmt.catches.items[i];
                HirTryCatch catch_item;
                int j = 0;
                memset(&catch_item, 0, sizeof(catch_item));
                catch_item.error_type = lower_type(ctx, &ast_catch->error_type);
                if (!catch_item.error_type) {
                    return 0;
                }
                for (j = 0; j < try_scope.catches.count; ++j) {
                    if (type_equals(try_scope.catches.items[j].error_type, catch_item.error_type)) {
                        fail(ctx, "duplicate catch type");
                        return 0;
                    }
                }
                catch_item.binding = new_binding(catch_item.error_type, 0, ast_catch->binding_name, HIR_BINDING_LOCAL, ast_catch->line);
                binding_list_push(&ctx->current_function->locals, catch_item.binding);
                try_catch_list_push(&try_scope.catches, catch_item);
                try_catch_list_push(&out->as.try_stmt.catches, catch_item);
            }
            try_scope_list_push(&ctx->try_scopes, try_scope);
            if (!lower_block(ctx, &stmt->as.try_stmt.try_body, &out->as.try_stmt.try_body, 0)) {
                ctx->try_scopes.count -= 1;
                return 0;
            }
            ctx->try_scopes.count -= 1;
            for (i = 0; i < stmt->as.try_stmt.catches.count; ++i) {
                Scope catch_scope;
                HirTryCatch* catch_item = &out->as.try_stmt.catches.items[i];
                push_scope(ctx, &catch_scope);
                if (!bind_in_current_scope(ctx, catch_item->binding)) {
                    return 0;
                }
                if (!lower_block(ctx, &stmt->as.try_stmt.catches.items[i].body, &catch_item->body, 0)) {
                    return 0;
                }
                pop_scope(ctx);
            }
            ctx->current_line = saved_line;
            return out;
        }
        case AST_STMT_EXPR_CATCH: {
            TryScope try_scope;
            HirTryCatch catch_item;
            AstStmt expr_stmt_ast;
            HirStmt* lowered_expr_stmt = 0;
            Scope catch_scope;
            HirExpr* raw_expr = lower_expr(ctx, stmt->as.expr_catch_stmt.expr);
            if (!raw_expr) {
                return 0;
            }
            if (!is_errorable_type(raw_expr->type)) {
                fail(ctx, "catch handler requires errorable expression");
                return 0;
            }
            memset(&try_scope, 0, sizeof(try_scope));
            try_scope.defer_start = ctx->active_defers.count;
            memset(&catch_item, 0, sizeof(catch_item));
            catch_item.error_type = errorable_error_type(raw_expr->type);
            catch_item.binding = new_binding(catch_item.error_type, 0, stmt->as.expr_catch_stmt.binding_name, HIR_BINDING_LOCAL, stmt->line);
            binding_list_push(&ctx->current_function->locals, catch_item.binding);
            try_catch_list_push(&try_scope.catches, catch_item);
            try_catch_list_push(&out->as.try_stmt.catches, catch_item);
            try_scope_list_push(&ctx->try_scopes, try_scope);
            memset(&expr_stmt_ast, 0, sizeof(expr_stmt_ast));
            expr_stmt_ast.kind = AST_STMT_EXPR;
            expr_stmt_ast.line = stmt->line;
            expr_stmt_ast.column = stmt->column;
            expr_stmt_ast.as.expr_stmt.expr = stmt->as.expr_catch_stmt.expr;
            lowered_expr_stmt = lower_stmt(ctx, &expr_stmt_ast);
            ctx->try_scopes.count -= 1;
            if (!lowered_expr_stmt) {
                return 0;
            }
            stmt_list_push(&out->as.try_stmt.try_body.stmts, lowered_expr_stmt);
            push_scope(ctx, &catch_scope);
            if (!bind_in_current_scope(ctx, out->as.try_stmt.catches.items[0].binding)) {
                return 0;
            }
            if (!lower_block(ctx, &stmt->as.expr_catch_stmt.body, &out->as.try_stmt.catches.items[0].body, 0)) {
                return 0;
            }
            pop_scope(ctx);
            ctx->current_line = saved_line;
            return out;
        }
        case AST_STMT_WHILE: {
            Scope inner;
            ctx->loop_depth += 1;
            if (stmt->as.while_stmt.cond &&
                stmt->as.while_stmt.cond->kind == AST_EXPR_BINARY &&
                stmt->as.while_stmt.cond->as.binary.op == AST_BIN_IS &&
                stmt->as.while_stmt.cond->as.binary.right &&
                stmt->as.while_stmt.cond->as.binary.right->kind == AST_EXPR_VARIANT &&
                stmt->as.while_stmt.cond->as.binary.right->as.variant.pattern_flag) {
                HirExpr* value = lower_expr(ctx, stmt->as.while_stmt.cond->as.binary.left);
                if (!value) {
                    return 0;
                }
                push_scope(ctx, &inner);
                if (!lower_variant_pattern_bind(ctx, value, stmt->as.while_stmt.cond->as.binary.right, &out->as.while_stmt.body, &out->as.while_stmt.cond)) {
                    return 0;
                }
                if (!lower_block(ctx, &stmt->as.while_stmt.body, &out->as.while_stmt.body, 1)) {
                    return 0;
                }
                pop_scope(ctx);
            } else {
                out->as.while_stmt.cond = lower_expr_expected(ctx, stmt->as.while_stmt.cond, primitive_type(ctx->program, HIR_TYPE_BOOL));
                if (!out->as.while_stmt.cond) {
                    return 0;
                }
                if (out->as.while_stmt.cond->type->kind != HIR_TYPE_BOOL) {
                    fail(ctx, "while condition must be Bool");
                    return 0;
                }
                push_scope(ctx, &inner);
                if (!lower_block(ctx, &stmt->as.while_stmt.body, &out->as.while_stmt.body, 1)) {
                    return 0;
                }
                pop_scope(ctx);
            }
            ctx->loop_depth -= 1;
            ctx->current_line = saved_line;
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
            binding = new_binding(primitive_type(ctx->program, HIR_TYPE_INT), stmt->as.for_range.type.mutable_flag, stmt->as.for_range.name, HIR_BINDING_LOCAL, stmt->line);
            binding_list_push(&ctx->current_function->locals, binding);
            out->as.for_range.binding = binding;
            out->as.for_range.start = start;
            out->as.for_range.end = end;
            ctx->loop_depth += 1;
            push_scope(ctx, &inner);
            if (!bind_in_current_scope(ctx, binding)) {
                return 0;
            }
            if (!lower_block(ctx, &stmt->as.for_range.body, &out->as.for_range.body, 1)) {
                return 0;
            }
            pop_scope(ctx);
            ctx->loop_depth -= 1;
            ctx->current_line = saved_line;
            return out;
        }
        case AST_STMT_BREAK:
            if (ctx->loop_depth <= 0) {
                fail(ctx, "break used outside loop");
                return 0;
            }
            ctx->current_line = saved_line;
            return out;
        case AST_STMT_CONTINUE:
            if (ctx->loop_depth <= 0) {
                fail(ctx, "continue used outside loop");
                return 0;
            }
            ctx->current_line = saved_line;
            return out;
        case AST_STMT_EXPR:
            out->as.expr_stmt.expr = lower_expr_value(ctx, stmt->as.expr_stmt.expr);
            if (!out->as.expr_stmt.expr) {
                return 0;
            }
            if (is_errorable_type(out->as.expr_stmt.expr->type)) {
                HirType* error_type = errorable_error_type(out->as.expr_stmt.expr->type);
                int try_handles = ctx->try_scopes.count > 0 && current_try_catches_error(ctx, error_type);
                int function_handles = is_errorable_type(ctx->current_function->return_type) &&
                    type_equals(error_type, errorable_error_type(ctx->current_function->return_type));
                if (try_handles || function_handles) {
                    HirExpr* propagated = new_expr(HIR_EXPR_PROPAGATE, errorable_value_type(out->as.expr_stmt.expr->type), stmt->line);
                    propagated->as.propagate.value = out->as.expr_stmt.expr;
                    propagated->as.propagate.result_type = try_handles ? 0 : ctx->current_function->return_type;
                    out->as.expr_stmt.expr = propagated;
                } else if (ctx->try_scopes.count > 0) {
                    fail(ctx, "uncaught error type in try block");
                    return 0;
                } else {
                    fail(ctx, "errorable expression must be handled");
                    return 0;
                }
            }
            ctx->current_line = saved_line;
            return out;
        default:
            fail(ctx, "unsupported statement kind");
            return 0;
    }
}

static int lower_var_decl_coalesce_control(LowerContext* ctx, const AstStmt* stmt, HirBlock* out_block) {
    HirExpr* optional_value = 0;
    HirType* binding_type = 0;
    HirBinding* temp_binding = 0;
    HirBinding* binding = 0;
    HirStmt* temp_decl = 0;
    HirStmt* if_stmt = 0;
    HirStmt* exit_stmt = 0;
    HirStmt* value_decl = 0;
    HirExpr* temp_ref = 0;
    HirExpr* cond = 0;
    HirExpr* init = 0;
    HirExpr* return_value = 0;
    AstCoalesceControlKind control = stmt->as.var_decl.init->as.coalesce_control.control;

    optional_value = lower_expr(ctx, stmt->as.var_decl.init->as.coalesce_control.left);
    if (!optional_value) {
        return 0;
    }
    if (optional_value->type->kind != HIR_TYPE_OPTIONAL) {
        return fail(ctx, "optional coalesce requires optional left operand");
    }
    if (stmt->as.var_decl.type.kind == AST_TYPE_INFER) {
        binding_type = optional_value->type->array_item;
    } else {
        binding_type = lower_type(ctx, &stmt->as.var_decl.type);
        if (!type_assignment_compatible(optional_value->type->array_item, binding_type)) {
            return fail(ctx, "variable initializer type mismatch");
        }
    }

    if (control == AST_COALESCE_RETURN) {
        if (stmt->as.var_decl.init->as.coalesce_control.return_expr) {
            return_value = lower_expr_expected(ctx,
                                               stmt->as.var_decl.init->as.coalesce_control.return_expr,
                                               ctx->current_function->return_type);
            if (!return_value) {
                return 0;
            }
            return_value = maybe_decay_array_to_slice(ctx, return_value, ctx->current_function->return_type, stmt->line);
            if (!return_value) {
                return 0;
            }
            if (!type_assignment_compatible(return_value->type, ctx->current_function->return_type)) {
                return fail(ctx, "return type mismatch");
            }
        } else if (ctx->current_function->return_type->kind != HIR_TYPE_VOID) {
            return fail(ctx, "coalesce return requires value");
        }
    }
    if ((control == AST_COALESCE_BREAK || control == AST_COALESCE_CONTINUE) && ctx->loop_depth <= 0) {
        return fail(ctx, control == AST_COALESCE_BREAK ? "break used outside loop" : "continue used outside loop");
    }

    temp_binding = new_binding(optional_value->type, 0, make_temp_name(ctx), HIR_BINDING_LOCAL, stmt->line);
    binding_list_push(&ctx->current_function->locals, temp_binding);
    temp_decl = new_stmt(HIR_STMT_VAR_DECL, stmt->line);
    temp_decl->as.var_decl.binding = temp_binding;
    temp_decl->as.var_decl.init = optional_value;
    stmt_list_push(&out_block->stmts, temp_decl);

    temp_ref = new_expr(HIR_EXPR_BINDING, temp_binding->type, stmt->line);
    temp_ref->as.binding = temp_binding;
    cond = new_expr(HIR_EXPR_BINARY, primitive_type(ctx->program, HIR_TYPE_BOOL), stmt->line);
    cond->as.binary.op = HIR_BIN_EQ;
    cond->as.binary.left = temp_ref;
    cond->as.binary.right = make_null_expr(temp_binding->type, stmt->line);
    if_stmt = new_stmt(HIR_STMT_IF, stmt->line);
    if_stmt->as.if_stmt.cond = cond;
    if (control == AST_COALESCE_RETURN) {
        emit_deferred_blocks(ctx, &if_stmt->as.if_stmt.then_block, 0);
    } else {
        emit_deferred_blocks(ctx, &if_stmt->as.if_stmt.then_block, nearest_loop_defer_start(ctx));
    }
    exit_stmt = new_stmt(control == AST_COALESCE_RETURN ? HIR_STMT_RETURN :
                         (control == AST_COALESCE_BREAK ? HIR_STMT_BREAK : HIR_STMT_CONTINUE),
                         stmt->line);
    if (control == AST_COALESCE_RETURN) {
        exit_stmt->as.ret.expr = return_value;
    }
    stmt_list_push(&if_stmt->as.if_stmt.then_block.stmts, exit_stmt);
    stmt_list_push(&out_block->stmts, if_stmt);

    binding = new_binding(binding_type, stmt->as.var_decl.type.mutable_flag, stmt->as.var_decl.name, HIR_BINDING_LOCAL, stmt->line);
    if (!bind_in_current_scope(ctx, binding)) {
        return 0;
    }
    binding_list_push(&ctx->current_function->locals, binding);
    temp_ref = new_expr(HIR_EXPR_BINDING, temp_binding->type, stmt->line);
    temp_ref->as.binding = temp_binding;
    init = make_optional_value_expr(ctx, temp_ref, stmt->line);
    if (!init) {
        return 0;
    }
    init = maybe_decay_array_to_slice(ctx, init, binding_type, stmt->line);
    if (!init) {
        return 0;
    }
    if (!type_assignment_compatible(init->type, binding_type) &&
        !apply_array_length_inference(binding_type, init->type)) {
        return fail(ctx, "variable initializer type mismatch");
    }
    value_decl = new_stmt(HIR_STMT_VAR_DECL, stmt->line);
    value_decl->as.var_decl.binding = binding;
    value_decl->as.var_decl.init = init;
    stmt_list_push(&out_block->stmts, value_decl);
    return 1;
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
            if (!type_assignment_compatible(init->type, type)) {
                return fail(ctx, "destructure binding type mismatch");
            }
        }
        binding = new_binding(type, stmt->as.destructure.bindings.items[0].type.mutable_flag, stmt->as.destructure.bindings.items[0].name, HIR_BINDING_LOCAL, stmt->as.destructure.bindings.items[0].line);
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
        HirBinding* temp_binding = new_binding(init->type, 0, temp_name, HIR_BINDING_LOCAL, stmt->line);
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
                if (!type_assignment_compatible(item_type, binding_type)) {
                    return fail(ctx, "destructure binding type mismatch");
                }
            }
            binding = new_binding(binding_type, stmt->as.destructure.bindings.items[i].type.mutable_flag, stmt->as.destructure.bindings.items[i].name, HIR_BINDING_LOCAL, stmt->as.destructure.bindings.items[i].line);
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
    HirUnionVariant* variant = 0;
    HirExpr* cond = 0;
    int i = 0;
    if (value->type->kind == HIR_TYPE_OPTIONAL &&
        (!pattern->as.variant.union_name || strcmp(pattern->as.variant.union_name, "Option") == 0)) {
        HirExpr* some_value = 0;
        if (strcmp(pattern->as.variant.variant_name, "some") != 0) {
            return fail(ctx, "unknown optional pattern");
        }
        cond = new_expr(HIR_EXPR_BINARY, primitive_type(ctx->program, HIR_TYPE_BOOL), pattern->line);
        cond->as.binary.op = HIR_BIN_NE;
        cond->as.binary.left = value;
        cond->as.binary.right = make_null_expr(value->type, pattern->line);
        *cond_out = cond;
        if (pattern->as.variant.bindings.count == 0) {
            return 1;
        }
        if (pattern->as.variant.bindings.count != 1) {
            return fail(ctx, "non-tuple variant pattern arity mismatch");
        }
        some_value = make_optional_value_expr(ctx, value, pattern->line);
        if (!some_value) {
            return 0;
        }
        return lower_pattern_bind(ctx, pattern->as.variant.bindings.items[0], some_value, out_block);
    }
    variant = resolve_variant_expr(ctx, pattern, &union_decl);
    if (!variant) {
        return 0;
    }
    if (value->type->kind != HIR_TYPE_UNION ||
        !value->type->union_decl ||
        !union_decl ||
        !nominal_names_equivalent(value->type->union_decl->name, union_decl->name)) {
        return fail(ctx, "union pattern type mismatch");
    }
    if (value->type->union_decl != union_decl) {
        variant = find_union_variant(value->type->union_decl, pattern->as.variant.variant_name);
        if (!variant) {
            return fail(ctx, "unknown union variant");
        }
        union_decl = value->type->union_decl;
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
    return lower_pattern_bind(ctx, pattern->as.variant.bindings.items[0], make_union_field_expr(ctx, value, variant, -1, variant->payload_type, pattern->line), out_block);
}

static int switch_expr_case_has_bindings(const AstSwitchExprCase* ast_case) {
    if (!ast_case || ast_case->is_else || !ast_case->pattern) {
        return 0;
    }
    return ast_case->pattern->kind == AST_EXPR_VARIANT &&
           ast_case->pattern->as.variant.bindings.count > 0;
}

static int validate_switch_expr_cases(LowerContext* ctx, HirExpr* value, const AstExpr* expr) {
    int i = 0;
    int have_else = 0;
    if (is_errorable_type(value->type)) {
        return fail(ctx, "switch expression does not support errorable values");
    }
    if (value->type->kind == HIR_TYPE_ENUM) {
        int member_count = value->type->enum_decl->members.count;
        int* seen = (int*)calloc((size_t)member_count, sizeof(int));
        int seen_count = 0;
        if (!seen) {
            return fail(ctx, "out of memory");
        }
        for (i = 0; i < expr->as.switch_expr.cases.count; ++i) {
            const AstSwitchExprCase* ast_case = &expr->as.switch_expr.cases.items[i];
            if (ast_case->is_else) {
                if (have_else) {
                    free(seen);
                    return fail(ctx, "duplicate else case");
                }
                have_else = 1;
                continue;
            }
            {
                HirExpr* case_value = lower_expr_expected(ctx, ast_case->pattern, value->type);
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
        for (i = 0; i < expr->as.switch_expr.cases.count; ++i) {
            const AstSwitchExprCase* ast_case = &expr->as.switch_expr.cases.items[i];
            if (ast_case->is_else) {
                if (have_else) {
                    free(seen);
                    return fail(ctx, "duplicate else case");
                }
                have_else = 1;
                continue;
            }
            if (switch_expr_case_has_bindings(ast_case)) {
                free(seen);
                return fail(ctx, "switch expression pattern bindings unsupported");
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
    if (value->type->kind == HIR_TYPE_OPTIONAL) {
        int seen_some = 0;
        for (i = 0; i < expr->as.switch_expr.cases.count; ++i) {
            const AstSwitchExprCase* ast_case = &expr->as.switch_expr.cases.items[i];
            if (ast_case->is_else) {
                if (have_else) {
                    return fail(ctx, "duplicate else case");
                }
                have_else = 1;
                continue;
            }
            if (switch_expr_case_has_bindings(ast_case)) {
                return fail(ctx, "switch expression pattern bindings unsupported");
            }
            if (ast_case->pattern->kind != AST_EXPR_VARIANT ||
                !ast_case->pattern->as.variant.union_name ||
                strcmp(ast_case->pattern->as.variant.union_name, "Option") != 0) {
                return fail(ctx, "switch case type mismatch");
            }
            if (strcmp(ast_case->pattern->as.variant.variant_name, "some") != 0) {
                return fail(ctx, "unknown optional pattern");
            }
            if (seen_some) {
                return fail(ctx, "duplicate switch case");
            }
            seen_some = 1;
        }
        if (!have_else) {
            return fail(ctx, "non-exhaustive optional switch");
        }
        return 1;
    }
    for (i = 0; i < expr->as.switch_expr.cases.count; ++i) {
        const AstSwitchExprCase* ast_case = &expr->as.switch_expr.cases.items[i];
        if (ast_case->is_else) {
            if (have_else) {
                return fail(ctx, "duplicate else case");
            }
            have_else = 1;
            continue;
        }
        {
            HirExpr* case_value = lower_expr_expected(ctx, ast_case->pattern, value->type);
            if (!case_value) {
                return 0;
            }
            if (!type_equals(value->type, case_value->type)) {
                return fail(ctx, "switch case type mismatch");
            }
        }
    }
    if (!have_else) {
        return fail(ctx, "non-exhaustive switch expression");
    }
    return 1;
}

static HirExpr* lower_switch_expr(LowerContext* ctx, const AstExpr* expr) {
    HirExpr* value = lower_expr(ctx, expr->as.switch_expr.value);
    HirExpr* fallback = 0;
    HirType* result_type = 0;
    int i = expr->as.switch_expr.cases.count - 1;
    if (!value) {
        return 0;
    }
    if (!validate_switch_expr_cases(ctx, value, expr)) {
        return 0;
    }
    for (; i >= 0; --i) {
        const AstSwitchExprCase* ast_case = &expr->as.switch_expr.cases.items[i];
        HirExpr* branch_value = 0;
        HirExpr* cond = 0;
        if (ast_case->is_else) {
            fallback = lower_expr(ctx, ast_case->value);
            if (!fallback) {
                return 0;
            }
            if (hir_expr_is_never_exit(fallback)) {
                if (result_type) {
                    fallback->type = result_type;
                }
            } else if (!result_type) {
                result_type = fallback->type;
            } else if (!type_equals(result_type, fallback->type)) {
                return fail(ctx, "switch expression branch type mismatch"), (HirExpr*)0;
            }
            continue;
        }
        branch_value = lower_expr(ctx, ast_case->value);
        if (!branch_value) {
            return 0;
        }
        if (hir_expr_is_never_exit(branch_value)) {
            if (result_type) {
                branch_value->type = result_type;
            }
        } else if (!result_type) {
            result_type = branch_value->type;
        } else if (!type_equals(result_type, branch_value->type)) {
            return fail(ctx, "switch expression branch type mismatch"), (HirExpr*)0;
        }
        if (result_type && hir_expr_is_never_exit(fallback)) {
            fallback->type = result_type;
        }
        if (result_type && result_type->kind == HIR_TYPE_VOID) {
            return fail(ctx, "switch expression aggregate result unsupported"), (HirExpr*)0;
        }
        if ((value->type->kind == HIR_TYPE_UNION || value->type->kind == HIR_TYPE_OPTIONAL) &&
            ast_case->pattern->kind == AST_EXPR_VARIANT) {
            HirBlock dummy;
            memset(&dummy, 0, sizeof(dummy));
            if (!lower_variant_pattern_bind(ctx, value, ast_case->pattern, &dummy, &cond)) {
                return 0;
            }
            if (dummy.stmts.count != 0) {
                return fail(ctx, "switch expression pattern bindings unsupported"), (HirExpr*)0;
            }
        } else {
            HirExpr* case_value = lower_expr_expected(ctx, ast_case->pattern, value->type);
            if (!case_value) {
                return 0;
            }
            if (!type_equals(value->type, case_value->type)) {
                return fail(ctx, "switch case type mismatch"), (HirExpr*)0;
            }
            cond = new_expr(HIR_EXPR_BINARY, primitive_type(ctx->program, HIR_TYPE_BOOL), ast_case->pattern->line);
            cond->as.binary.op = HIR_BIN_EQ;
            cond->as.binary.left = value;
            cond->as.binary.right = case_value;
        }
        if (!fallback) {
            return fail(ctx, "non-exhaustive switch expression"), (HirExpr*)0;
        }
        {
            HirExpr* if_expr = new_expr(HIR_EXPR_IF, result_type ? result_type : branch_value->type, ast_case->value->line);
            if_expr->as.if_expr.cond = cond;
            if_expr->as.if_expr.then_expr = branch_value;
            if_expr->as.if_expr.else_expr = fallback;
            fallback = if_expr;
        }
    }
    return fallback;
}

static int validate_switch_cases(LowerContext* ctx, HirExpr* value, const AstStmt* stmt) {
    int i = 0;
    int have_else = 0;
    if (is_errorable_type(value->type)) {
        int seen_value = 0;
        int seen_error = 0;
        for (i = 0; i < stmt->as.switch_stmt.cases.count; ++i) {
            const AstSwitchCase* ast_case = &stmt->as.switch_stmt.cases.items[i];
            if (ast_case->is_else) {
                if (have_else) {
                    return fail(ctx, "duplicate else case");
                }
                have_else = 1;
                continue;
            }
            if (ast_case->result_case_kind == 1) {
                if (seen_value) {
                    return fail(ctx, "duplicate switch case");
                }
                seen_value = 1;
                continue;
            }
            if (ast_case->result_case_kind == 2) {
                if (seen_error) {
                    return fail(ctx, "duplicate switch case");
                }
                seen_error = 1;
                continue;
            }
            return fail(ctx, "errorable switch requires 'value' or 'error' branches");
        }
        if (!have_else && (!seen_value || !seen_error)) {
            return fail(ctx, "non-exhaustive errorable switch");
        }
        return 1;
    }
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
                HirExpr* case_value = lower_expr_expected(ctx, ast_case->pattern, value->type);
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
    if (value->type->kind == HIR_TYPE_OPTIONAL) {
        int seen_some = 0;
        for (i = 0; i < stmt->as.switch_stmt.cases.count; ++i) {
            const AstSwitchCase* ast_case = &stmt->as.switch_stmt.cases.items[i];
            if (ast_case->is_else) {
                if (have_else) {
                    return fail(ctx, "duplicate else case");
                }
                have_else = 1;
                continue;
            }
            if (ast_case->pattern->kind != AST_EXPR_VARIANT ||
                !ast_case->pattern->as.variant.union_name ||
                strcmp(ast_case->pattern->as.variant.union_name, "Option") != 0) {
                return fail(ctx, "switch case type mismatch");
            }
            if (strcmp(ast_case->pattern->as.variant.variant_name, "some") != 0) {
                return fail(ctx, "unknown optional pattern");
            }
            if (seen_some) {
                return fail(ctx, "duplicate switch case");
            }
            seen_some = 1;
        }
        if (!have_else) {
            return fail(ctx, "non-exhaustive optional switch");
        }
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
            HirExpr* case_value = lower_expr_expected(ctx, ast_case->pattern, value->type);
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
            if (!lower_block(ctx, &ast_case->body, &else_block, 0)) {
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
            if (is_errorable_type(value->type) && ast_case->result_case_kind != 0) {
                int tag_value = ast_case->result_case_kind == 1 ? 0 : 1;
                HirType* payload_type = ast_case->result_case_kind == 1
                    ? errorable_value_type(value->type)
                    : errorable_error_type(value->type);
                HirUnionVariant* variant = &value->type->union_decl->variants.items[tag_value];
                if_stmt->as.if_stmt.cond = new_expr(HIR_EXPR_BINARY, primitive_type(ctx->program, HIR_TYPE_BOOL), stmt->line);
                if_stmt->as.if_stmt.cond->as.binary.op = HIR_BIN_EQ;
                if_stmt->as.if_stmt.cond->as.binary.left = make_union_tag_expr(ctx, value, stmt->line);
                if_stmt->as.if_stmt.cond->as.binary.right = make_int_expr(ctx, tag_value, stmt->line);
                if (ast_case->binding_name) {
                    if (payload_type->kind == HIR_TYPE_VOID) {
                        return fail(ctx, "void result case must not bind a value");
                    }
                    HirBinding* binding = new_binding(payload_type, 0, ast_case->binding_name, HIR_BINDING_LOCAL, ast_case->pattern ? ast_case->pattern->line : stmt->line);
                    HirStmt* decl = 0;
                    if (!bind_in_current_scope(ctx, binding)) {
                        return 0;
                    }
                    binding_list_push(&ctx->current_function->locals, binding);
                    decl = new_stmt(HIR_STMT_VAR_DECL, stmt->line);
                    decl->as.var_decl.binding = binding;
                    decl->as.var_decl.init = make_union_field_expr(ctx, value, variant, -1, payload_type, stmt->line);
                    stmt_list_push(&if_stmt->as.if_stmt.then_block.stmts, decl);
                }
            } else if ((value->type->kind == HIR_TYPE_UNION || value->type->kind == HIR_TYPE_OPTIONAL) &&
                ast_case->pattern->kind == AST_EXPR_VARIANT) {
                if (!lower_variant_pattern_bind(ctx, value, ast_case->pattern, &if_stmt->as.if_stmt.then_block, &if_stmt->as.if_stmt.cond)) {
                    return 0;
                }
            } else {
                HirExpr* case_value = lower_expr_expected(ctx, ast_case->pattern, value->type);
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
            if (!lower_block(ctx, &ast_case->body, &if_stmt->as.if_stmt.then_block, 0)) {
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
            if (!type_assignment_compatible(init->type, type)) {
                return fail(ctx, "binding type mismatch");
            }
        }
        binding = new_binding(type, pattern->type.mutable_flag, pattern->name, HIR_BINDING_LOCAL, pattern->line);
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
    iterable_binding = new_binding(iterable->type, 0, iterable_name, HIR_BINDING_LOCAL, stmt->line);
    binding_list_push(&ctx->current_function->locals, iterable_binding);
    iterable_decl = new_stmt(HIR_STMT_VAR_DECL, stmt->line);
    iterable_decl->as.var_decl.binding = iterable_binding;
    iterable_decl->as.var_decl.init = iterable;
    stmt_list_push(&out_block->stmts, iterable_decl);

    index_name = make_temp_name(ctx);
    index_binding = new_binding(primitive_type(ctx->program, HIR_TYPE_INT), 0, index_name, HIR_BINDING_LOCAL, stmt->line);
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
    if (!lower_block(ctx, &stmt->as.for_each.body, &loop_stmt->as.for_range.body, 1)) {
        pop_scope(ctx);
        return 0;
    }
    pop_scope(ctx);
    stmt_list_push(&out_block->stmts, loop_stmt);
    return 1;
}

static int lower_block(LowerContext* ctx, const AstBlock* ast_block, HirBlock* out_block, int loop_boundary) {
    int i = 0;
    DeferFrame frame;
    int saved_line = ctx->current_line;
    frame.defer_start = ctx->active_defers.count;
    frame.loop_boundary = loop_boundary;
    defer_frame_list_push(&ctx->defer_frames, frame);
    for (i = 0; i < ast_block->stmts.count; ++i) {
        HirStmt* stmt = 0;
        HirStmt* spill_stmt = 0;
        ctx->current_line = ast_block->stmts.items[i]->line;
        if (ast_block->stmts.items[i]->kind == AST_STMT_DEFER) {
            HirBlock deferred_body;
            memset(&deferred_body, 0, sizeof(deferred_body));
            if (defer_body_has_control_flow(&ast_block->stmts.items[i]->as.defer_stmt.body)) {
                return fail(ctx, "defer body cannot contain control flow");
            }
            if (!lower_block(ctx, &ast_block->stmts.items[i]->as.defer_stmt.body, &deferred_body, 0)) {
                return 0;
            }
            defer_block_list_push(&ctx->active_defers, deferred_body);
            continue;
        }
        if (ast_block->stmts.items[i]->kind == AST_STMT_GROUP) {
            if (!lower_block(ctx, &ast_block->stmts.items[i]->as.group_stmt, out_block, loop_boundary)) {
                return 0;
            }
            continue;
        }
        if (ast_block->stmts.items[i]->kind == AST_STMT_VAR_DECL &&
            ast_block->stmts.items[i]->as.var_decl.init &&
            ast_block->stmts.items[i]->as.var_decl.init->kind == AST_EXPR_COALESCE_CONTROL) {
            if (!lower_var_decl_coalesce_control(ctx, ast_block->stmts.items[i], out_block)) {
                return 0;
            }
            continue;
        }
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
        if (stmt->kind == HIR_STMT_THROW && stmt->as.throw_stmt.expr) {
            HirBinding* temp_binding = new_binding(stmt->as.throw_stmt.expr->type, 0, make_temp_name(ctx), HIR_BINDING_LOCAL, stmt->line);
            spill_stmt = new_stmt(HIR_STMT_VAR_DECL, stmt->line);
            binding_list_push(&ctx->current_function->locals, temp_binding);
            spill_stmt->as.var_decl.binding = temp_binding;
            spill_stmt->as.var_decl.init = stmt->as.throw_stmt.expr;
            stmt->as.throw_stmt.expr = make_binding_expr(temp_binding, stmt->line);
        } else if (stmt->kind == HIR_STMT_EXPR &&
                   stmt->as.expr_stmt.expr &&
                   stmt->as.expr_stmt.expr->kind == HIR_EXPR_PROPAGATE &&
                   stmt->as.expr_stmt.expr->as.propagate.value) {
            HirBinding* temp_binding = new_binding(stmt->as.expr_stmt.expr->as.propagate.value->type, 0, make_temp_name(ctx), HIR_BINDING_LOCAL, stmt->line);
            spill_stmt = new_stmt(HIR_STMT_VAR_DECL, stmt->line);
            binding_list_push(&ctx->current_function->locals, temp_binding);
            spill_stmt->as.var_decl.binding = temp_binding;
            spill_stmt->as.var_decl.init = stmt->as.expr_stmt.expr->as.propagate.value;
            stmt->as.expr_stmt.expr->as.propagate.value = make_binding_expr(temp_binding, stmt->line);
        }
        if (spill_stmt) {
            stmt_list_push(&out_block->stmts, spill_stmt);
        }
        if (stmt->kind == HIR_STMT_RETURN) {
            emit_deferred_blocks(ctx, out_block, 0);
        } else if (stmt->kind == HIR_STMT_THROW) {
            int defer_start = (ctx->try_scopes.count > 0 &&
                               current_try_catches_error(ctx, stmt->as.throw_stmt.expr->type))
                ? current_try_defer_start(ctx)
                : 0;
            emit_deferred_blocks(ctx, out_block, defer_start);
        } else if (stmt->kind == HIR_STMT_BREAK || stmt->kind == HIR_STMT_CONTINUE) {
            emit_deferred_blocks(ctx, out_block, nearest_loop_defer_start(ctx));
        } else if (stmt->kind == HIR_STMT_EXPR &&
                   stmt->as.expr_stmt.expr &&
                   stmt->as.expr_stmt.expr->kind == HIR_EXPR_PROPAGATE) {
            int defer_start = stmt->as.expr_stmt.expr->as.propagate.result_type ? 0 : current_try_defer_start(ctx);
            emit_deferred_blocks(ctx, out_block, defer_start);
        }
        stmt_list_push(&out_block->stmts, stmt);
    }
    emit_deferred_blocks(ctx, out_block, frame.defer_start);
    ctx->active_defers.count = frame.defer_start;
    ctx->defer_frames.count -= 1;
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
        hir_global.binding = new_binding(type, ast_global->type.mutable_flag, ast_global->name, HIR_BINDING_GLOBAL, ast_global->line);
        hir_global.extern_flag = ast_global->extern_flag;
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
            int k = 0;
            memset(&member, 0, sizeof(member));
            for (k = 0; k < j; ++k) {
                if (strcmp(ctx->ast->enums.items[i].members.items[k].name,
                           ctx->ast->enums.items[i].members.items[j].name) == 0) {
                    return fail(ctx, "duplicate enum member");
                }
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
        struct_decl.record_flag = ast_struct->record_flag;
        struct_decl.init_count = ast_struct->init_overloads.count;
        struct_decl.has_init = ast_struct->init_overloads.count > 0;
        struct_decl.from_string_literal = ast_struct_declares_trait(ast_struct, "FromStringLiteral");
        struct_decl.has_deinit = ast_struct->has_deinit;
        if (ast_struct->has_deinit) {
            struct_decl.deinit_name = make_struct_deinit_name(ast_struct->name);
        }
        struct_list_push(&ctx->program->structs, struct_decl);
        hashmap_set(&ctx->program->type_name_map, ast_struct->name, (void*)1);
    }
    rebuild_struct_map(ctx->program);
    return 1;
}

static int register_unions(LowerContext* ctx) {
    int i = 0;
    for (i = 0; i < ctx->ast->unions.count; ++i) {
        HirUnionDecl union_decl;
        const AstUnionDecl* ast_union = &ctx->ast->unions.items[i];
        memset(&union_decl, 0, sizeof(union_decl));
        if (ast_union->tag_name && !hashmap_contains(&ctx->program->type_name_map, ast_union->tag_name)) {
            return fail(ctx, "unknown union tag enum");
        }
        if (hashmap_contains(&ctx->program->union_name_map, ast_union->name) || hashmap_contains(&ctx->program->type_name_map, ast_union->name)) {
            return fail(ctx, "duplicate union name");
        }
        union_decl.name = ast_union->name;
        union_decl.tag_name = ast_union->tag_name;
        union_list_push(&ctx->program->unions, union_decl);
        hashmap_set(&ctx->program->union_name_map, union_decl.name, (void*)1);
        hashmap_set(&ctx->program->type_name_map, union_decl.name, (void*)1);
    }
    rebuild_union_map(ctx->program);
    return 1;
}

static int resolve_struct_fields(LowerContext* ctx) {
    int i = 0;
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
            if (!field.type) {
                return 0;
            }
            field.mutable_flag = ast_struct->fields.items[j].type.mutable_flag;
            struct_field_list_push(&struct_decl->fields, field);
        }
    }
    return 1;
}

static int resolve_union_variants(LowerContext* ctx) {
    int i = 0;
    for (i = 0; i < ctx->ast->unions.count; ++i) {
        const AstUnionDecl* ast_union = &ctx->ast->unions.items[i];
        HirUnionDecl* union_decl = &ctx->program->unions.items[i];
        int j = 0;
        for (j = 0; j < ast_union->variants.count; ++j) {
            HirUnionVariant variant;
            memset(&variant, 0, sizeof(variant));
            variant.name = ast_union->variants.items[j].name;
            variant.tag_value = j;
            variant.payload_type = lower_type(ctx, &ast_union->variants.items[j].type);
            if (!variant.payload_type) {
                return 0;
            }
            if (type_size_bytes(variant.payload_type) > union_decl->payload_size) {
                union_decl->payload_size = type_size_bytes(variant.payload_type);
            }
            if (type_alignment_bytes(variant.payload_type) > union_decl->payload_align) {
                union_decl->payload_align = type_alignment_bytes(variant.payload_type);
            }
            union_variant_list_push(&union_decl->variants, variant);
        }
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
        int init_index = 0;
        for (init_index = 0; init_index < ast_struct->init_overloads.count; ++init_index) {
            memset(&hir_fn, 0, sizeof(hir_fn));
            return_type = new_owned_type(ctx->program, HIR_TYPE_STRUCT);
            return_type->struct_decl = struct_decl;
            if (ast_struct->init_overloads.items[init_index].failable_flag) {
                return_type = make_optional_type(ctx, return_type);
                hir_fn.struct_init_failable_flag = 1;
            }
            hir_fn.return_type = return_type;
            hir_fn.name = make_struct_init_name(struct_decl->name, init_index);
            hir_fn.source_name = ast_struct->init_overloads.items[init_index].name;
            hir_fn.line = ast_struct->init_overloads.items[init_index].line;
            hir_fn.struct_init_flag = 1;
            hir_fn.struct_init_index = init_index;
            hir_fn.owner_struct = struct_decl;
            for (param_index = 0; param_index < ast_struct->init_overloads.items[init_index].params.count; ++param_index) {
                HirBinding* param_binding = new_binding(lower_type(ctx, &ast_struct->init_overloads.items[init_index].params.items[param_index].type), ast_struct->init_overloads.items[init_index].params.items[param_index].type.mutable_flag, ast_struct->init_overloads.items[init_index].params.items[param_index].name, HIR_BINDING_PARAM, ast_struct->init_overloads.items[init_index].params.items[param_index].line);
                param_binding->label = ast_struct->init_overloads.items[init_index].params.items[param_index].label;
                param_binding->default_value = ast_struct->init_overloads.items[init_index].params.items[param_index].default_value;
                binding_list_push(&hir_fn.params, param_binding);
            }
            {
                int fn_index = 0;
                for (fn_index = 0; fn_index < ctx->program->functions.count; ++fn_index) {
                    if (function_signature_conflicts(&ctx->program->functions.items[fn_index], &hir_fn)) {
                        return fail(ctx, "duplicate function");
                    }
                }
            }
            function_list_push(&ctx->program->functions, hir_fn);
            hashmap_set(&ctx->program->function_map, hir_fn.name, (void*)1);
        }
    }
    rebuild_function_map(ctx->program);
    return 1;
}

static int register_struct_deinit_functions(LowerContext* ctx) {
    int i = 0;
    for (i = 0; i < ctx->program->structs.count; ++i) {
        const AstStructDecl* ast_struct = &ctx->ast->structs.items[i];
        HirStructDecl* struct_decl = &ctx->program->structs.items[i];
        HirFunction hir_fn;
        HirType* self_type = 0;
        HirBinding* self_binding = 0;
        if (!ast_struct->has_deinit) {
            continue;
        }
        memset(&hir_fn, 0, sizeof(hir_fn));
        hir_fn.return_type = primitive_type(ctx->program, HIR_TYPE_VOID);
        hir_fn.name = struct_decl->deinit_name;
        hir_fn.source_name = "deinit";
        hir_fn.line = ast_struct->deinit_line;
        hir_fn.struct_deinit_flag = 1;
        hir_fn.owner_struct = struct_decl;
        self_type = new_owned_type(ctx->program, HIR_TYPE_STRUCT);
        self_type->struct_decl = struct_decl;
        self_binding = new_binding(self_type, 1, "self", HIR_BINDING_PARAM, ast_struct->deinit_line);
        binding_list_push(&hir_fn.params, self_binding);
        {
            int fn_index = 0;
            for (fn_index = 0; fn_index < ctx->program->functions.count; ++fn_index) {
                if (function_signature_conflicts(&ctx->program->functions.items[fn_index], &hir_fn)) {
                    return fail(ctx, "duplicate function");
                }
            }
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
        HirFunction* saved_current_function = 0;
        int param_index = 0;
        memset(&hir_fn, 0, sizeof(hir_fn));
        if (!ast_fn->method_flag && hashmap_contains(&ctx->program->type_name_map, ast_fn->name)) {
            return fail(ctx, "type and function name conflict");
        }
        hir_fn.name = ast_fn->name;
        hir_fn.source_name = ast_fn->name;
        hir_fn.line = ast_fn->line;
        hir_fn.extern_flag = ast_fn->extern_flag;
        hir_fn.public_flag = ast_fn->public_flag;
        hir_fn.method_flag = ast_fn->method_flag;
        hir_fn.static_method_flag = ast_fn->static_method_flag;
        if (ast_fn->method_flag) {
            hir_fn.method_name = ast_fn->name;
            hir_fn.receiver_type = find_named_owner_type(ctx->program, ast_fn->owner_type_name);
            if (!hir_fn.receiver_type) {
                return fail(ctx, "unknown named type");
            }
            if (hir_fn.receiver_type->kind == HIR_TYPE_STRUCT &&
                ast_fn->static_method_flag &&
                strcmp(ast_fn->name, "init") == 0) {
                return fail(ctx, "static init not allowed");
            }
            if (hir_fn.receiver_type->kind == HIR_TYPE_STRUCT &&
                find_struct_field(hir_fn.receiver_type->struct_decl, ast_fn->name, 0)) {
                return fail(ctx, "struct field and method name conflict");
            }
            hir_fn.name = make_method_name(ast_fn->owner_type_name, ast_fn->name, ast_fn->static_method_flag);
        }
        saved_current_function = ctx->current_function;
        ctx->current_function = &hir_fn;
        hir_fn.return_type = lower_type(ctx, &ast_fn->return_type);
        ctx->current_function = saved_current_function;
        if (ast_fn->method_flag && !ast_fn->static_method_flag) {
            HirType* self_type = hir_fn.receiver_type;
            if (hir_fn.receiver_type->kind == HIR_TYPE_STRUCT) {
                self_type = reference_to_type(ctx, hir_fn.receiver_type);
            }
            HirBinding* self_binding = new_binding(self_type, 1, "self", HIR_BINDING_PARAM, ast_fn->line);
            binding_list_push(&hir_fn.params, self_binding);
        }
        for (param_index = 0; param_index < ast_fn->params.count; ++param_index) {
            HirType* param_type = 0;
            saved_current_function = ctx->current_function;
            ctx->current_function = &hir_fn;
            param_type = lower_type(ctx, &ast_fn->params.items[param_index].type);
            ctx->current_function = saved_current_function;
            HirBinding* param_binding = new_binding(param_type, ast_fn->params.items[param_index].type.mutable_flag, ast_fn->params.items[param_index].name, HIR_BINDING_PARAM, ast_fn->params.items[param_index].line);
            param_binding->label = ast_fn->params.items[param_index].label;
            param_binding->default_value = ast_fn->params.items[param_index].default_value;
            binding_list_push(&hir_fn.params, param_binding);
        }
        for (param_index = 0; param_index < ctx->program->functions.count; ++param_index) {
            if (function_signature_conflicts(&ctx->program->functions.items[param_index], &hir_fn)) {
                return fail(ctx, "duplicate function");
            }
        }
        if (hashmap_contains(&ctx->program->function_map, hir_fn.name)) {
            char* unique_name = make_unique_function_name(ctx->program, hir_fn.name);
            if (!unique_name) {
                return fail(ctx, "out of memory");
            }
            hir_fn.name = unique_name;
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
        if (ctx->ast->globals.items[i].extern_flag) {
            hir_global->init = 0;
            continue;
        }
        hir_global->init = lower_expr_expected(ctx, ctx->ast->globals.items[i].init, hir_global->binding->type);
        if (!hir_global->init) {
            return 0;
        }
        hir_global->init = maybe_decay_array_to_slice(ctx, hir_global->init, hir_global->binding->type, ctx->ast->globals.items[i].line);
        if (!hir_global->init) {
            return 0;
        }
        if (ctx->ast->globals.items[i].type.kind == AST_TYPE_INFER) {
            hir_global->binding->type = hir_global->init->type;
        } else if (!type_assignment_compatible(hir_global->init->type, hir_global->binding->type) &&
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
            const AstStructInitDecl* ast_init = 0;
            HirBinding* self_binding = 0;
            HirType* self_type = 0;
            HirStmt* self_decl = 0;
            int* field_state = 0;
            int j = 0;
            if (!ast_struct ||
                ctx->current_function->struct_init_index < 0 ||
                ctx->current_function->struct_init_index >= ast_struct->init_overloads.count) {
                return fail(ctx, "internal error: missing struct init metadata");
            }
            ast_init = &ast_struct->init_overloads.items[ctx->current_function->struct_init_index];
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
                } else if (ctx->current_function->owner_struct->fields.items[j].type->kind == HIR_TYPE_OPTIONAL) {
                    field_state[j] = 2;
                }
            }
            if (!validate_struct_init_block(ctx, ctx->current_function->owner_struct, &ast_init->body, field_state)) {
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
            self_type = new_owned_type(ctx->program, HIR_TYPE_STRUCT);
            self_type->struct_decl = ctx->current_function->owner_struct;
            self_binding = new_binding(self_type, 1, "self", HIR_BINDING_LOCAL, ast_init->line);
            binding_list_push(&ctx->current_function->locals, self_binding);
            if (!bind_in_current_scope(ctx, self_binding)) {
                return 0;
            }
            self_decl = new_stmt(HIR_STMT_VAR_DECL, ast_init->line);
            self_decl->as.var_decl.binding = self_binding;
            self_decl->as.var_decl.init = make_zero_expr(ctx, self_type, ast_init->line);
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
            if (!lower_init_block(ctx, &ast_init->body, &ctx->current_function->body, self_binding)) {
                return 0;
            }
            if (ctx->current_function->body.stmts.count == 0 ||
                ctx->current_function->body.stmts.items[ctx->current_function->body.stmts.count - 1]->kind != HIR_STMT_RETURN) {
                HirStmt* ret = new_stmt(HIR_STMT_RETURN, ast_init->line);
                ret->as.ret.expr = make_binding_expr(self_binding, ast_init->line);
                if (ctx->current_function->struct_init_failable_flag) {
                    ret->as.ret.expr = wrap_optional_result(ctx, ret->as.ret.expr, ast_init->line);
                }
                stmt_list_push(&ctx->current_function->body.stmts, ret);
            }
        } else if (ctx->current_function->struct_deinit_flag) {
            const AstStructDecl* ast_struct = find_ast_struct(ctx->ast, ctx->current_function->owner_struct->name);
            int param_index = 0;
            if (!ast_struct) {
                return fail(ctx, "internal error: missing struct deinit metadata");
            }
            for (param_index = 0; param_index < ctx->current_function->params.count; ++param_index) {
                if (!bind_in_current_scope(ctx, ctx->current_function->params.items[param_index])) {
                    return 0;
                }
            }
            if (!lower_block(ctx, &ast_struct->deinit_body, &ctx->current_function->body, 0)) {
                return 0;
            }
            if (ctx->current_function->body.stmts.count == 0 ||
                ctx->current_function->body.stmts.items[ctx->current_function->body.stmts.count - 1]->kind != HIR_STMT_RETURN) {
                HirStmt* ret = new_stmt(HIR_STMT_RETURN, ast_struct->deinit_line);
                ret->as.ret.expr = 0;
                stmt_list_push(&ctx->current_function->body.stmts, ret);
            }
        } else {
            if (ctx->ast->functions.items[user_index].extern_flag) {
                user_index += 1;
                pop_scope(ctx);
                continue;
            }
            int param_index = 0;
            for (param_index = 0; param_index < ctx->current_function->params.count; ++param_index) {
                if (!bind_in_current_scope(ctx, ctx->current_function->params.items[param_index])) {
                    return 0;
                }
            }
            if (!lower_block(ctx, &ctx->ast->functions.items[user_index].body, &ctx->current_function->body, 0)) {
                return 0;
            }
            user_index += 1;
        }
        pop_scope(ctx);
    }
    return 1;
}

int lower_ast_to_hir(const AstProgram* ast, HirProgram* hir, const char** error, int* error_line, int* error_column) {
    LowerContext ctx;
    memset(hir, 0, sizeof(*hir));
    memset(&ctx, 0, sizeof(ctx));
    hir->int_type.kind = HIR_TYPE_INT;
    hir->i8_type.kind = HIR_TYPE_I8;
    hir->i16_type.kind = HIR_TYPE_I16;
    hir->i32_type.kind = HIR_TYPE_I32;
    hir->i64_type.kind = HIR_TYPE_I64;
    hir->u8_type.kind = HIR_TYPE_U8;
    hir->u16_type.kind = HIR_TYPE_U16;
    hir->u32_type.kind = HIR_TYPE_U32;
    hir->u64_type.kind = HIR_TYPE_U64;
    hir->f16_type.kind = HIR_TYPE_F16;
    hir->f32_type.kind = HIR_TYPE_F32;
    hir->f64_type.kind = HIR_TYPE_F64;
    hir->float_type.kind = HIR_TYPE_FLOAT;
    hir->double_type.kind = HIR_TYPE_DOUBLE;
    hir->character_type.kind = HIR_TYPE_CHARACTER;
    hir->uint8_type.kind = HIR_TYPE_UINT8;
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
    g_active_lower_ctx = &ctx;
    if (!register_enums(&ctx) || !register_structs(&ctx) || !register_unions(&ctx) || !resolve_struct_fields(&ctx) || !resolve_union_variants(&ctx) || !register_globals(&ctx) || !register_struct_init_functions(&ctx) || !register_struct_deinit_functions(&ctx) || !register_functions(&ctx) || !lower_globals(&ctx) || !lower_functions(&ctx)) {
        *error = ctx.error;
        if (error_line) {
            *error_line = ctx.error_line;
        }
        if (error_column) {
            *error_column = ctx.error_column;
        }
        g_active_lower_ctx = 0;
        return 0;
    }
    if (error_line) {
        *error_line = 0;
    }
    if (error_column) {
        *error_column = 0;
    }
    g_active_lower_ctx = 0;
    return 1;
}
