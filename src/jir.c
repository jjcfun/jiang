#include "lower.h"
#include "vec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define jir_inst_list_push(list, inst) VEC_PUSH((list), (inst))
#define jir_block_list_push(list, block) VEC_PUSH((list), (block))
#define jir_function_list_push(list, fn) VEC_PUSH((list), (fn))
#define jir_global_list_push(list, global) VEC_PUSH((list), (global))
#define jir_expr_list_push(list, expr) VEC_PUSH((list), (expr))
#define jir_field_init_list_push(list, init) VEC_PUSH((list), (init))
#define jir_type_list_push(list, type) VEC_PUSH((list), (type))
#define jir_binding_list_push(list, binding) VEC_PUSH((list), (binding))
#define jir_struct_field_decl_list_push(list, field) VEC_PUSH((list), (field))
#define jir_try_handler_list_push(list, handler) VEC_PUSH((list), (handler))
#define jir_try_scope_list_push(list, scope) VEC_PUSH((list), (scope))

typedef struct JirTypeMapEntry {
    const HirType* hir;
    JirType* jir;
} JirTypeMapEntry;

typedef struct JirTypeMapEntryList {
    JirTypeMapEntry* items;
    int count;
    int capacity;
} JirTypeMapEntryList;

typedef struct JirBindingMapEntry {
    const HirBinding* hir;
    JirBinding* jir;
} JirBindingMapEntry;

typedef struct JirBindingMapEntryList {
    JirBindingMapEntry* items;
    int count;
    int capacity;
} JirBindingMapEntryList;

typedef struct JirLoopTarget {
    int continue_index;
    int break_index;
} JirLoopTarget;

typedef struct JirLoopTargetList {
    JirLoopTarget* items;
    int count;
    int capacity;
} JirLoopTargetList;

typedef struct LowerJirContext {
    const HirProgram* program;
    JirProgram* jir_program;
    JirLoweredFunction* function;
    int next_block_id;
    int active_try_scope;
    JirLoopTargetList loops;
    const char** error;
    int* error_line;
    int* error_column;
} LowerJirContext;

#define jir_loop_list_push(list, item) VEC_PUSH((list), (item))
#define jir_type_map_push(list, item) VEC_PUSH((list), (item))
#define jir_binding_map_push(list, item) VEC_PUSH((list), (item))

static char* dup_text(const char* text) {
    size_t len = strlen(text);
    char* copy = (char*)malloc(len + 1);
    if (!copy) {
        return 0;
    }
    memcpy(copy, text, len + 1);
    return copy;
}

static JirTypeMapEntryList g_type_map;
static JirBindingMapEntryList g_binding_map;

static int hir_is_errorable_type(const HirType* type) {
    return type && type->kind == HIR_TYPE_UNION && type->union_decl && type->union_decl->errorable_flag;
}

static int hir_type_equivalent(const HirType* left, const HirType* right) {
    int i = 0;
    if (left == right) {
        return 1;
    }
    if (!left || !right) {
        return 0;
    }
    if (left->kind != right->kind || left->mutable_flag != right->mutable_flag || left->array_length != right->array_length) {
        return 0;
    }
    switch (left->kind) {
        case HIR_TYPE_ENUM:
            return left->enum_decl == right->enum_decl;
        case HIR_TYPE_STRUCT:
            return left->struct_decl == right->struct_decl;
        case HIR_TYPE_UNION:
            return left->union_decl == right->union_decl;
        case HIR_TYPE_SLICE:
        case HIR_TYPE_REFERENCE:
        case HIR_TYPE_POINTER:
        case HIR_TYPE_MANY_POINTER:
        case HIR_TYPE_OPTIONAL:
        case HIR_TYPE_ARRAY:
            return hir_type_equivalent(left->array_item, right->array_item);
        case HIR_TYPE_FUNCTION:
            if (!hir_type_equivalent(left->return_type, right->return_type) ||
                left->tuple_items.count != right->tuple_items.count) {
                return 0;
            }
            for (i = 0; i < left->tuple_items.count; ++i) {
                if (!hir_type_equivalent(left->tuple_items.items[i], right->tuple_items.items[i])) {
                    return 0;
                }
            }
            return 1;
        case HIR_TYPE_TUPLE:
            if (left->tuple_items.count != right->tuple_items.count) {
                return 0;
            }
            for (i = 0; i < left->tuple_items.count; ++i) {
                if (!hir_type_equivalent(left->tuple_items.items[i], right->tuple_items.items[i])) {
                    return 0;
                }
            }
            return 1;
        default:
            return 1;
    }
}

static const HirType* hir_errorable_error_type(const HirType* type) {
    if (!hir_is_errorable_type(type) || type->union_decl->variants.count < 2) {
        return 0;
    }
    return type->union_decl->variants.items[1].payload_type;
}

static JirTypeKind jir_type_kind(HirTypeKind kind) {
    return (JirTypeKind)kind;
}

static JirBindingKind jir_binding_kind(HirBindingKind kind) {
    return (JirBindingKind)kind;
}

static JirExprKind jir_expr_kind(HirExprKind kind) {
    switch (kind) {
        case HIR_EXPR_INT: return JIR_EXPR_INT;
        case HIR_EXPR_FLOAT: return JIR_EXPR_FLOAT;
        case HIR_EXPR_CHAR: return JIR_EXPR_CHAR;
        case HIR_EXPR_BOOL: return JIR_EXPR_BOOL;
        case HIR_EXPR_CSTRING: return JIR_EXPR_CSTRING;
        case HIR_EXPR_NULL: return JIR_EXPR_NULL;
        case HIR_EXPR_OPTIONAL_SOME: return JIR_EXPR_OPTIONAL_SOME;
        case HIR_EXPR_BINDING: return JIR_EXPR_BINDING;
        case HIR_EXPR_FUNCTION: return JIR_EXPR_FUNCTION;
        case HIR_EXPR_AS: return JIR_EXPR_AS;
        case HIR_EXPR_ADDR: return JIR_EXPR_ADDR;
        case HIR_EXPR_DEREF: return JIR_EXPR_DEREF;
        case HIR_EXPR_NEW: return JIR_EXPR_NEW;
        case HIR_EXPR_FREE: return JIR_EXPR_FREE;
        case HIR_EXPR_BIT_NOT: return JIR_EXPR_BIT_NOT;
        case HIR_EXPR_BINARY: return JIR_EXPR_BINARY;
        case HIR_EXPR_COALESCE: return JIR_EXPR_COALESCE;
        case HIR_EXPR_CATCH_FALLBACK: return JIR_EXPR_CATCH_FALLBACK;
        case HIR_EXPR_CATCH_HANDLER: return JIR_EXPR_CATCH_HANDLER;
        case HIR_EXPR_BLOCK: return JIR_EXPR_BLOCK;
        case HIR_EXPR_IF: return JIR_EXPR_IF;
        case HIR_EXPR_TERNARY: return JIR_EXPR_TERNARY;
        case HIR_EXPR_CALL: return JIR_EXPR_CALL;
        case HIR_EXPR_PROPAGATE: return JIR_EXPR_PROPAGATE;
        case HIR_EXPR_ENUM_MEMBER: return JIR_EXPR_ENUM_MEMBER;
        case HIR_EXPR_VARIANT: return JIR_EXPR_VARIANT;
        case HIR_EXPR_ENUM_VALUE: return JIR_EXPR_ENUM_VALUE;
        case HIR_EXPR_UNION_TAG: return JIR_EXPR_UNION_TAG;
        case HIR_EXPR_UNION_FIELD: return JIR_EXPR_UNION_FIELD;
        case HIR_EXPR_OPTIONAL_VALUE: return JIR_EXPR_OPTIONAL_VALUE;
        case HIR_EXPR_STRUCT: return JIR_EXPR_STRUCT;
        case HIR_EXPR_STRUCT_FIELD: return JIR_EXPR_STRUCT_FIELD;
        case HIR_EXPR_TUPLE: return JIR_EXPR_TUPLE;
        case HIR_EXPR_ARRAY: return JIR_EXPR_ARRAY;
        case HIR_EXPR_INDEX: return JIR_EXPR_INDEX;
        case HIR_EXPR_SLICE: return JIR_EXPR_SLICE;
        case HIR_EXPR_SLICE_LENGTH: return JIR_EXPR_SLICE_LENGTH;
    }
    return JIR_EXPR_INT;
}

static JirBinaryOp jir_binary_op(HirBinaryOp op) {
    return (JirBinaryOp)op;
}

static JirBuiltinKind jir_builtin_kind(HirBuiltinKind kind) {
    return (JirBuiltinKind)kind;
}

static JirType* find_lowered_type(const HirType* hir_type) {
    int i = 0;
    for (i = 0; i < g_type_map.count; ++i) {
        if (hir_type_equivalent(g_type_map.items[i].hir, hir_type)) {
            return g_type_map.items[i].jir;
        }
    }
    return 0;
}

static JirBinding* find_lowered_binding(const HirBinding* hir_binding) {
    int i = 0;
    for (i = 0; i < g_binding_map.count; ++i) {
        if (g_binding_map.items[i].hir == hir_binding) {
            return g_binding_map.items[i].jir;
        }
    }
    return 0;
}

static JirType* lower_type(const HirType* hir_type, const char** error) {
    JirType* jir_type = 0;
    int i = 0;
    if (!hir_type) {
        return 0;
    }
    jir_type = find_lowered_type(hir_type);
    if (jir_type) {
        return jir_type;
    }
    jir_type = (JirType*)calloc(1, sizeof(JirType));
    if (!jir_type) {
        *error = "out of memory";
        return 0;
    }
    jir_type->kind = jir_type_kind(hir_type->kind);
    jir_type->mutable_flag = hir_type->mutable_flag;
    jir_type->array_length = hir_type->array_length;
    jir_type->errorable_flag = hir_is_errorable_type(hir_type);
    if (hir_type->return_type) {
        jir_type->return_type = lower_type(hir_type->return_type, error);
        if (!jir_type->return_type) {
            return 0;
        }
    }
    if (hir_type->union_decl) {
        jir_type->union_payload_size = hir_type->union_decl->payload_size;
        jir_type->union_payload_align = hir_type->union_decl->payload_align;
    }
    {
        JirTypeMapEntry entry;
        entry.hir = hir_type;
        entry.jir = jir_type;
        jir_type_map_push(&g_type_map, entry);
    }
    if (hir_type->array_item) {
        jir_type->array_item = lower_type(hir_type->array_item, error);
        if (!jir_type->array_item) {
            return 0;
        }
    }
    for (i = 0; i < hir_type->tuple_items.count; ++i) {
        JirType* item = lower_type(hir_type->tuple_items.items[i], error);
        if (!item) {
            return 0;
        }
        jir_type_list_push(&jir_type->tuple_items, item);
    }
    if (hir_type->struct_decl) {
        for (i = 0; i < hir_type->struct_decl->fields.count; ++i) {
            JirStructFieldDecl field;
            memset(&field, 0, sizeof(field));
            field.name = hir_type->struct_decl->fields.items[i].name;
            field.type = lower_type(hir_type->struct_decl->fields.items[i].type, error);
            field.mutable_flag = hir_type->struct_decl->fields.items[i].mutable_flag;
            if (!field.type) {
                return 0;
            }
            jir_struct_field_decl_list_push(&jir_type->struct_fields, field);
        }
        jir_type->struct_has_deinit = hir_type->struct_decl->has_deinit;
        jir_type->struct_deinit_name = hir_type->struct_decl->deinit_name;
    }
    return jir_type;
}

static JirBinding* lower_binding(const HirBinding* hir_binding, const char** error) {
    JirBinding* jir_binding = 0;
    if (!hir_binding) {
        return 0;
    }
    jir_binding = find_lowered_binding(hir_binding);
    if (jir_binding) {
        return jir_binding;
    }
    jir_binding = (JirBinding*)calloc(1, sizeof(JirBinding));
    if (!jir_binding) {
        *error = "out of memory";
        return 0;
    }
    jir_binding->type = lower_type(hir_binding->type, error);
    if (!jir_binding->type) {
        return 0;
    }
    jir_binding->name = hir_binding->name;
    jir_binding->mutable_flag = hir_binding->mutable_flag;
    jir_binding->kind = jir_binding_kind(hir_binding->kind);
    jir_binding->line = hir_binding->line;
    {
        JirBindingMapEntry entry;
        entry.hir = hir_binding;
        entry.jir = jir_binding;
        jir_binding_map_push(&g_binding_map, entry);
    }
    return jir_binding;
}

static JirExpr* lower_expr(JirProgram* program, const HirExpr* expr, const char** error, int* error_line, int* error_column);
static JirExpr* desugar_expr(JirExpr* expr);
static JirExpr* desugar_lvalue_expr(JirExpr* expr);
static JirExpr* make_int_expr(int64_t value, JirType* type, int line);
static JirExpr* make_extract_expr(JirExtractKind kind, JirExpr* base, int field_index, JirType* type, JirType* payload_type, int line);
static int append_simple_inst(JirProgram* program, JirBasicBlock* block, const HirStmt* stmt, const char** error, int* error_line, int* error_column);

static JirExpr* new_expr(JirExprKind kind, JirType* type, int line) {
    JirExpr* expr = (JirExpr*)calloc(1, sizeof(JirExpr));
    if (!expr) {
        return 0;
    }
    expr->kind = kind;
    expr->type = type;
    expr->line = line;
    return expr;
}

static JirExpr* make_extract_expr(JirExtractKind kind, JirExpr* base, int field_index, JirType* type, JirType* payload_type, int line) {
    JirExpr* expr = new_expr(JIR_EXPR_EXTRACT, type, line);
    if (!expr) {
        return 0;
    }
    expr->as.extract.kind = kind;
    expr->as.extract.base = base;
    expr->as.extract.field_index = field_index;
    expr->as.extract.payload_type = payload_type;
    return expr;
}

static void sort_struct_fields(JirStructFieldInitList* fields) {
    int i = 0;
    int j = 0;
    for (i = 0; i < fields->count; ++i) {
        for (j = i + 1; j < fields->count; ++j) {
            if (fields->items[j].field_index < fields->items[i].field_index) {
                JirStructFieldInit tmp = fields->items[i];
                fields->items[i] = fields->items[j];
                fields->items[j] = tmp;
            }
        }
    }
}

static JirExpr* desugar_expr_list(JirExprList* list) {
    int i = 0;
    for (i = 0; i < list->count; ++i) {
        list->items[i] = desugar_expr(list->items[i]);
        if (!list->items[i]) {
            return 0;
        }
    }
    return list->count > 0 ? list->items[0] : (JirExpr*)1;
}

static JirExpr* desugar_lvalue_expr(JirExpr* expr) {
    if (!expr) {
        return 0;
    }
    switch (expr->kind) {
        case JIR_EXPR_BINDING:
            return expr;
        case JIR_EXPR_STRUCT_FIELD:
            expr->as.struct_field.base = desugar_lvalue_expr(expr->as.struct_field.base);
            return expr->as.struct_field.base ? expr : 0;
        case JIR_EXPR_INDEX:
            expr->as.index.base = desugar_lvalue_expr(expr->as.index.base);
            expr->as.index.index = desugar_expr(expr->as.index.index);
            return expr->as.index.base && expr->as.index.index ? expr : 0;
        default:
            return desugar_expr(expr);
    }
}

static JirExpr* desugar_expr(JirExpr* expr) {
    int i = 0;
    if (!expr) {
        return 0;
    }
    switch (expr->kind) {
        case JIR_EXPR_INT:
        case JIR_EXPR_FLOAT:
        case JIR_EXPR_CHAR:
        case JIR_EXPR_BOOL:
        case JIR_EXPR_CSTRING:
        case JIR_EXPR_NULL:
        case JIR_EXPR_BINDING:
        case JIR_EXPR_FUNCTION:
        case JIR_EXPR_ENUM_MEMBER:
            return expr;
        case JIR_EXPR_ADDR:
        case JIR_EXPR_DEREF:
        case JIR_EXPR_NEW:
        case JIR_EXPR_FREE:
        case JIR_EXPR_BIT_NOT:
        case JIR_EXPR_OPTIONAL_SOME:
            expr->as.unary.value = desugar_expr(expr->as.unary.value);
            return expr->as.unary.value ? expr : 0;
        case JIR_EXPR_PROPAGATE:
            expr->as.propagate.value = desugar_expr(expr->as.propagate.value);
            return expr->as.propagate.value ? expr : 0;
        case JIR_EXPR_BINARY:
            expr->as.binary.left = desugar_expr(expr->as.binary.left);
            expr->as.binary.right = desugar_expr(expr->as.binary.right);
            return expr->as.binary.left && expr->as.binary.right ? expr : 0;
        case JIR_EXPR_COALESCE:
            expr->as.coalesce.left = desugar_expr(expr->as.coalesce.left);
            expr->as.coalesce.right = desugar_expr(expr->as.coalesce.right);
            return expr->as.coalesce.left && expr->as.coalesce.right ? expr : 0;
        case JIR_EXPR_CATCH_FALLBACK:
            expr->as.catch_fallback.left = desugar_expr(expr->as.catch_fallback.left);
            expr->as.catch_fallback.fallback = desugar_expr(expr->as.catch_fallback.fallback);
            return expr->as.catch_fallback.left && expr->as.catch_fallback.fallback ? expr : 0;
        case JIR_EXPR_CATCH_HANDLER:
            expr->as.catch_handler.left = desugar_expr(expr->as.catch_handler.left);
            expr->as.catch_handler.handler = desugar_expr(expr->as.catch_handler.handler);
            return expr->as.catch_handler.left && expr->as.catch_handler.handler ? expr : 0;
        case JIR_EXPR_BLOCK:
            if (expr->as.block_expr.insts) {
                for (i = 0; i < expr->as.block_expr.insts->count; ++i) {
                    if (expr->as.block_expr.insts->items[i].expr) {
                        expr->as.block_expr.insts->items[i].expr = desugar_expr(expr->as.block_expr.insts->items[i].expr);
                        if (!expr->as.block_expr.insts->items[i].expr) {
                            return 0;
                        }
                    }
                    if (expr->as.block_expr.insts->items[i].target) {
                        expr->as.block_expr.insts->items[i].target = desugar_lvalue_expr(expr->as.block_expr.insts->items[i].target);
                        if (!expr->as.block_expr.insts->items[i].target) {
                            return 0;
                        }
                    }
                    if (expr->as.block_expr.insts->items[i].value) {
                        expr->as.block_expr.insts->items[i].value = desugar_expr(expr->as.block_expr.insts->items[i].value);
                        if (!expr->as.block_expr.insts->items[i].value) {
                            return 0;
                        }
                    }
                }
            }
            expr->as.block_expr.value = desugar_expr(expr->as.block_expr.value);
            return expr->as.block_expr.value ? expr : 0;
        case JIR_EXPR_IF:
            expr->as.if_expr.cond = desugar_expr(expr->as.if_expr.cond);
            expr->as.if_expr.then_expr = desugar_expr(expr->as.if_expr.then_expr);
            expr->as.if_expr.else_expr = desugar_expr(expr->as.if_expr.else_expr);
            return expr->as.if_expr.cond && expr->as.if_expr.then_expr && expr->as.if_expr.else_expr ? expr : 0;
        case JIR_EXPR_TERNARY:
            expr->as.ternary.cond = desugar_expr(expr->as.ternary.cond);
            expr->as.ternary.then_expr = desugar_expr(expr->as.ternary.then_expr);
            expr->as.ternary.else_expr = desugar_expr(expr->as.ternary.else_expr);
            return expr->as.ternary.cond && expr->as.ternary.then_expr && expr->as.ternary.else_expr ? expr : 0;
        case JIR_EXPR_CALL:
            if (expr->as.call.callee_value) {
                expr->as.call.callee_value = desugar_expr(expr->as.call.callee_value);
                if (!expr->as.call.callee_value) {
                    return 0;
                }
            }
            if (!desugar_expr_list(&expr->as.call.args)) {
                return 0;
            }
            if (expr->as.call.callee && expr->as.call.callee->struct_init_flag) {
                JirExpr* init_expr = new_expr(JIR_EXPR_STRUCT_INIT, expr->type, expr->line);
                if (!init_expr) {
                    return 0;
                }
                init_expr->as.struct_init.init = expr->as.call.callee;
                init_expr->as.struct_init.args = expr->as.call.args;
                return init_expr;
            }
            return expr;
        case JIR_EXPR_STRUCT_INIT:
            return desugar_expr_list(&expr->as.struct_init.args) ? expr : 0;
        case JIR_EXPR_VARIANT:
            if (expr->as.variant.payload) {
                expr->as.variant.payload = desugar_expr(expr->as.variant.payload);
                if (!expr->as.variant.payload) {
                    return 0;
                }
            }
            {
                JirExpr* packed = new_expr(JIR_EXPR_UNION_PACK, expr->type, expr->line);
                int i = 0;
                if (!packed) {
                    return 0;
                }
                packed->as.union_pack.tag_value = expr->as.variant.tag_value;
                packed->as.union_pack.payload = expr->as.variant.payload;
                return packed;
            }
        case JIR_EXPR_UNION_PACK:
            if (expr->as.union_pack.payload) {
                expr->as.union_pack.payload = desugar_expr(expr->as.union_pack.payload);
                if (!expr->as.union_pack.payload) {
                    return 0;
                }
            }
            return expr;
        case JIR_EXPR_EXTRACT:
            expr->as.extract.base = desugar_expr(expr->as.extract.base);
            if (!expr->as.extract.base) {
                return 0;
            }
            return expr;
        case JIR_EXPR_ENUM_VALUE:
            expr->as.enum_value.value = desugar_expr(expr->as.enum_value.value);
            return expr->as.enum_value.value;
        case JIR_EXPR_UNION_TAG:
            expr->as.union_tag.value = desugar_expr(expr->as.union_tag.value);
            if (!expr->as.union_tag.value) {
                return 0;
            }
            if (expr->as.union_tag.value->kind == JIR_EXPR_UNION_PACK) {
                return make_int_expr(expr->as.union_tag.value->as.union_pack.tag_value, expr->type, expr->line);
            }
            return make_extract_expr(JIR_EXTRACT_UNION_TAG, expr->as.union_tag.value, 0, expr->type, 0, expr->line);
        case JIR_EXPR_UNION_FIELD:
            expr->as.union_field.value = desugar_expr(expr->as.union_field.value);
            if (!expr->as.union_field.value) {
                return 0;
            }
            if (expr->as.union_field.value->kind == JIR_EXPR_UNION_PACK) {
                JirExpr* payload = expr->as.union_field.value->as.union_pack.payload;
                if (expr->as.union_field.field_index < 0) {
                    return payload;
                }
                if (!payload || payload->kind != JIR_EXPR_TUPLE) {
                    return 0;
                }
                if (expr->as.union_field.field_index >= payload->as.tuple.items.count) {
                    return 0;
                }
                return payload->as.tuple.items.items[expr->as.union_field.field_index];
            }
            return make_extract_expr(JIR_EXTRACT_UNION_PAYLOAD, expr->as.union_field.value, expr->as.union_field.field_index, expr->type, expr->as.union_field.payload_type, expr->line);
        case JIR_EXPR_OPTIONAL_VALUE:
            expr->as.optional_value.value = desugar_expr(expr->as.optional_value.value);
            if (!expr->as.optional_value.value) {
                return 0;
            }
            if (expr->as.optional_value.value->kind == JIR_EXPR_OPTIONAL_SOME) {
                return expr->as.optional_value.value->as.unary.value;
            }
            return make_extract_expr(JIR_EXTRACT_OPTIONAL_VALUE, expr->as.optional_value.value, 0, expr->type, 0, expr->line);
        case JIR_EXPR_STRUCT:
            for (i = 0; i < expr->as.struct_lit.fields.count; ++i) {
                expr->as.struct_lit.fields.items[i].value = desugar_expr(expr->as.struct_lit.fields.items[i].value);
                if (!expr->as.struct_lit.fields.items[i].value) {
                    return 0;
                }
            }
            sort_struct_fields(&expr->as.struct_lit.fields);
            return expr;
        case JIR_EXPR_STRUCT_FIELD:
            expr->as.struct_field.base = desugar_expr(expr->as.struct_field.base);
            if (!expr->as.struct_field.base) {
                return 0;
            }
            if (expr->as.struct_field.base->type->kind == JIR_TYPE_REFERENCE || expr->as.struct_field.base->type->kind == JIR_TYPE_POINTER) {
                return expr;
            }
            if (expr->as.struct_field.base->kind == JIR_EXPR_STRUCT) {
                for (i = 0; i < expr->as.struct_field.base->as.struct_lit.fields.count; ++i) {
                    if (expr->as.struct_field.base->as.struct_lit.fields.items[i].field_index == expr->as.struct_field.field_index) {
                        return expr->as.struct_field.base->as.struct_lit.fields.items[i].value;
                    }
                }
                return 0;
            }
            return make_extract_expr(JIR_EXTRACT_STRUCT_FIELD, expr->as.struct_field.base, expr->as.struct_field.field_index, expr->type, 0, expr->line);
        case JIR_EXPR_TUPLE:
            return desugar_expr_list(&expr->as.tuple.items) ? expr : 0;
        case JIR_EXPR_ARRAY:
            return desugar_expr_list(&expr->as.array.items) ? expr : 0;
        case JIR_EXPR_INDEX:
            expr->as.index.base = desugar_expr(expr->as.index.base);
            expr->as.index.index = desugar_expr(expr->as.index.index);
            if (!expr->as.index.base || !expr->as.index.index) {
                return 0;
            }
            if (expr->as.index.index->kind == JIR_EXPR_INT) {
                int index = (int)expr->as.index.index->as.int_value;
                if (expr->as.index.base->kind == JIR_EXPR_TUPLE) {
                    if (index < 0 || index >= expr->as.index.base->as.tuple.items.count) {
                        return 0;
                    }
                    return expr->as.index.base->as.tuple.items.items[index];
                }
                if (expr->as.index.base->kind == JIR_EXPR_ARRAY) {
                    if (index < 0 || index >= expr->as.index.base->as.array.items.count) {
                        return 0;
                    }
                    return expr->as.index.base->as.array.items.items[index];
                }
                if (expr->as.index.base->type->kind == JIR_TYPE_TUPLE) {
                    return make_extract_expr(JIR_EXTRACT_TUPLE_ITEM, expr->as.index.base, index, expr->type, 0, expr->line);
                }
                if (expr->as.index.base->type->kind == JIR_TYPE_ARRAY) {
                    return make_extract_expr(JIR_EXTRACT_ARRAY_ITEM, expr->as.index.base, index, expr->type, 0, expr->line);
                }
            }
            return expr;
        case JIR_EXPR_SLICE:
            expr->as.slice.base = desugar_expr(expr->as.slice.base);
            if (expr->as.slice.start) {
                expr->as.slice.start = desugar_expr(expr->as.slice.start);
                if (!expr->as.slice.start) {
                    return 0;
                }
            }
            if (expr->as.slice.end) {
                expr->as.slice.end = desugar_expr(expr->as.slice.end);
                if (!expr->as.slice.end) {
                    return 0;
                }
            }
            return expr->as.slice.base ? expr : 0;
        case JIR_EXPR_SLICE_LENGTH:
            expr->as.slice_length.base = desugar_expr(expr->as.slice_length.base);
            return expr->as.slice_length.base ? expr : 0;
        default:
            return expr;
    }
}

static int desugar_program(JirProgram* program) {
    int i = 0;
    int j = 0;
    for (i = 0; i < program->globals.count; ++i) {
        if (program->globals.items[i].extern_flag) {
            continue;
        }
        program->globals.items[i].init = desugar_expr(program->globals.items[i].init);
        if (!program->globals.items[i].init) {
            return 0;
        }
    }
    for (i = 0; i < program->lowered_functions.count; ++i) {
        for (j = 0; j < program->lowered_functions.items[i].blocks.count; ++j) {
            JirBasicBlock* block = &program->lowered_functions.items[i].blocks.items[j];
            int k = 0;
            for (k = 0; k < block->insts.count; ++k) {
                if (block->insts.items[k].expr) {
                    block->insts.items[k].expr = desugar_expr(block->insts.items[k].expr);
                    if (!block->insts.items[k].expr) {
                        return 0;
                    }
                }
                if (block->insts.items[k].target) {
                    block->insts.items[k].target = desugar_lvalue_expr(block->insts.items[k].target);
                    if (!block->insts.items[k].target) {
                        return 0;
                    }
                }
                if (block->insts.items[k].value) {
                    block->insts.items[k].value = desugar_expr(block->insts.items[k].value);
                    if (!block->insts.items[k].value) {
                        return 0;
                    }
                }
            }
            if (block->term.cond) {
                block->term.cond = desugar_expr(block->term.cond);
                if (!block->term.cond) {
                    return 0;
                }
            }
            if (block->term.value) {
                block->term.value = desugar_expr(block->term.value);
                if (!block->term.value) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

static JirFunction* find_jir_function(JirProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        if (strcmp(program->functions.items[i].name, name) == 0) {
            return &program->functions.items[i];
        }
    }
    return 0;
}

static int lower_expr_list(JirProgram* program, const HirExprList* hir_list, JirExprList* jir_list, const char** error, int* error_line, int* error_column) {
    int i = 0;
    memset(jir_list, 0, sizeof(*jir_list));
    for (i = 0; i < hir_list->count; ++i) {
        JirExpr* item = lower_expr(program, hir_list->items[i], error, error_line, error_column);
        if (!item) {
            return 0;
        }
        jir_expr_list_push(jir_list, item);
    }
    return 1;
}

static JirExpr* lower_expr(JirProgram* program, const HirExpr* expr, const char** error, int* error_line, int* error_column) {
    JirExpr* out = 0;
    JirType* type = 0;
    int i = 0;
    if (!expr) {
        return 0;
    }
    if (error_line) {
        *error_line = expr->line;
    }
    if (error_column) {
        *error_column = expr->column;
    }
    type = lower_type(expr->type, error);
    if (!type) {
        return 0;
    }
    out = new_expr(jir_expr_kind(expr->kind), type, expr->line);
    if (!out) {
        *error = "out of memory";
        return 0;
    }
    out->column = expr->column;
    switch (expr->kind) {
        case HIR_EXPR_INT:
            out->as.int_value = expr->as.int_value;
            return out;
        case HIR_EXPR_FLOAT:
            out->as.float_value = expr->as.float_value;
            return out;
        case HIR_EXPR_CHAR:
            out->as.char_value = expr->as.char_value;
            return out;
        case HIR_EXPR_BOOL:
            out->as.bool_value = expr->as.bool_value;
            return out;
        case HIR_EXPR_CSTRING:
            out->as.cstring_lit.text = strdup(expr->as.cstring_lit.text);
            out->as.cstring_lit.length = expr->as.cstring_lit.length;
            return out;
        case HIR_EXPR_NULL:
            return out;
        case HIR_EXPR_OPTIONAL_SOME:
            out->as.unary.value = lower_expr(program, expr->as.unary.value, error, error_line, error_column);
            return out->as.unary.value ? out : 0;
        case HIR_EXPR_BINDING:
            out->as.binding = lower_binding(expr->as.binding, error);
            return out;
        case HIR_EXPR_FUNCTION:
            out->as.function = find_jir_function(program, expr->as.function->name);
            if (!out->as.function) {
                *error = "missing JIR function";
                return 0;
            }
            return out;
        case HIR_EXPR_AS:
        case HIR_EXPR_ADDR:
        case HIR_EXPR_DEREF:
        case HIR_EXPR_NEW:
        case HIR_EXPR_FREE:
        case HIR_EXPR_BIT_NOT:
            out->as.unary.value = lower_expr(program, expr->as.unary.value, error, error_line, error_column);
            return out->as.unary.value ? out : 0;
        case HIR_EXPR_PROPAGATE:
            out->as.propagate.value = lower_expr(program, expr->as.propagate.value, error, error_line, error_column);
            out->as.propagate.error_type = lower_type(hir_errorable_error_type(expr->as.propagate.value->type), error);
            out->as.propagate.result_type = expr->as.propagate.result_type
                ? lower_type(expr->as.propagate.result_type, error)
                : 0;
            return out->as.propagate.value && out->as.propagate.error_type &&
                       (!expr->as.propagate.result_type || out->as.propagate.result_type)
                ? out
                : 0;
        case HIR_EXPR_BINARY:
            out->as.binary.op = jir_binary_op(expr->as.binary.op);
            out->as.binary.left = lower_expr(program, expr->as.binary.left, error, error_line, error_column);
            out->as.binary.right = lower_expr(program, expr->as.binary.right, error, error_line, error_column);
            return out->as.binary.left && out->as.binary.right ? out : 0;
        case HIR_EXPR_COALESCE:
            out->as.coalesce.left = lower_expr(program, expr->as.coalesce.left, error, error_line, error_column);
            out->as.coalesce.right = lower_expr(program, expr->as.coalesce.right, error, error_line, error_column);
            return out->as.coalesce.left && out->as.coalesce.right ? out : 0;
        case HIR_EXPR_CATCH_FALLBACK:
            out->as.catch_fallback.left = lower_expr(program, expr->as.catch_fallback.left, error, error_line, error_column);
            out->as.catch_fallback.fallback = lower_expr(program, expr->as.catch_fallback.fallback, error, error_line, error_column);
            return out->as.catch_fallback.left && out->as.catch_fallback.fallback ? out : 0;
        case HIR_EXPR_CATCH_HANDLER:
            out->as.catch_handler.left = lower_expr(program, expr->as.catch_handler.left, error, error_line, error_column);
            out->as.catch_handler.binding = lower_binding(expr->as.catch_handler.binding, error);
            out->as.catch_handler.handler = lower_expr(program, expr->as.catch_handler.handler, error, error_line, error_column);
            return out->as.catch_handler.left && out->as.catch_handler.binding && out->as.catch_handler.handler ? out : 0;
        case HIR_EXPR_BLOCK: {
            JirBasicBlock temp_block;
            memset(&temp_block, 0, sizeof(temp_block));
            out->as.block_expr.insts = (JirInstList*)calloc(1, sizeof(JirInstList));
            if (!out->as.block_expr.insts) {
                *error = "out of memory";
                return 0;
            }
            for (i = 0; i < expr->as.block_expr.body->stmts.count; ++i) {
                if (!append_simple_inst(program, &temp_block, expr->as.block_expr.body->stmts.items[i], error, error_line, error_column)) {
                    return 0;
                }
            }
            *out->as.block_expr.insts = temp_block.insts;
            out->as.block_expr.value = lower_expr(program, expr->as.block_expr.value, error, error_line, error_column);
            return out->as.block_expr.value ? out : 0;
        }
        case HIR_EXPR_IF:
            out->as.if_expr.cond = lower_expr(program, expr->as.if_expr.cond, error, error_line, error_column);
            out->as.if_expr.then_expr = lower_expr(program, expr->as.if_expr.then_expr, error, error_line, error_column);
            out->as.if_expr.else_expr = lower_expr(program, expr->as.if_expr.else_expr, error, error_line, error_column);
            return out->as.if_expr.cond && out->as.if_expr.then_expr && out->as.if_expr.else_expr ? out : 0;
        case HIR_EXPR_TERNARY:
            out->as.ternary.cond = lower_expr(program, expr->as.ternary.cond, error, error_line, error_column);
            out->as.ternary.then_expr = lower_expr(program, expr->as.ternary.then_expr, error, error_line, error_column);
            out->as.ternary.else_expr = lower_expr(program, expr->as.ternary.else_expr, error, error_line, error_column);
            return out->as.ternary.cond && out->as.ternary.then_expr && out->as.ternary.else_expr ? out : 0;
        case HIR_EXPR_CALL:
            out->as.call.callee = expr->as.call.callee ? find_jir_function(program, expr->as.call.callee->name) : 0;
            out->as.call.callee_value = expr->as.call.callee_value ? lower_expr(program, expr->as.call.callee_value, error, error_line, error_column) : 0;
            out->as.call.builtin = jir_builtin_kind(expr->as.call.builtin);
            if (expr->as.call.callee && !out->as.call.callee) {
                *error = "missing JIR callee";
                return 0;
            }
            if (expr->as.call.callee_value && !out->as.call.callee_value) {
                return 0;
            }
            return lower_expr_list(program, &expr->as.call.args, &out->as.call.args, error, error_line, error_column) ? out : 0;
        case HIR_EXPR_ENUM_MEMBER:
            out->as.enum_member.value = expr->as.enum_member.member->value;
            return out;
        case HIR_EXPR_VARIANT:
            out->as.variant.tag_value = expr->as.variant.variant->tag_value;
            out->as.variant.payload = expr->as.variant.payload ? lower_expr(program, expr->as.variant.payload, error, error_line, error_column) : 0;
            return expr->as.variant.payload && !out->as.variant.payload ? 0 : out;
        case HIR_EXPR_ENUM_VALUE:
            out->as.enum_value.value = lower_expr(program, expr->as.enum_value.value, error, error_line, error_column);
            return out->as.enum_value.value ? out : 0;
        case HIR_EXPR_UNION_TAG:
            out->as.union_tag.value = lower_expr(program, expr->as.union_tag.value, error, error_line, error_column);
            return out->as.union_tag.value ? out : 0;
        case HIR_EXPR_UNION_FIELD:
            out->as.union_field.value = lower_expr(program, expr->as.union_field.value, error, error_line, error_column);
            out->as.union_field.field_index = expr->as.union_field.field_index;
            out->as.union_field.payload_type = lower_type(expr->as.union_field.variant->payload_type, error);
            return out->as.union_field.value ? out : 0;
        case HIR_EXPR_OPTIONAL_VALUE:
            out->as.optional_value.value = lower_expr(program, expr->as.optional_value.value, error, error_line, error_column);
            return out->as.optional_value.value ? out : 0;
        case HIR_EXPR_STRUCT:
            for (i = 0; i < expr->as.struct_lit.fields.count; ++i) {
                JirStructFieldInit init;
                memset(&init, 0, sizeof(init));
                init.field_index = (int)(expr->as.struct_lit.fields.items[i].field - expr->type->struct_decl->fields.items);
                init.value = lower_expr(program, expr->as.struct_lit.fields.items[i].value, error, error_line, error_column);
                if (!init.value) {
                    return 0;
                }
                jir_field_init_list_push(&out->as.struct_lit.fields, init);
            }
            return out;
        case HIR_EXPR_STRUCT_FIELD:
            out->as.struct_field.base = lower_expr(program, expr->as.struct_field.base, error, error_line, error_column);
            out->as.struct_field.field_index = expr->as.struct_field.field_index;
            return out->as.struct_field.base ? out : 0;
        case HIR_EXPR_TUPLE:
            return lower_expr_list(program, &expr->as.tuple.items, &out->as.tuple.items, error, error_line, error_column) ? out : 0;
        case HIR_EXPR_ARRAY:
            return lower_expr_list(program, &expr->as.array.items, &out->as.array.items, error, error_line, error_column) ? out : 0;
        case HIR_EXPR_INDEX:
            out->as.index.base = lower_expr(program, expr->as.index.base, error, error_line, error_column);
            out->as.index.index = lower_expr(program, expr->as.index.index, error, error_line, error_column);
            return out->as.index.base && out->as.index.index ? out : 0;
        case HIR_EXPR_SLICE:
            out->as.slice.base = lower_expr(program, expr->as.slice.base, error, error_line, error_column);
            if (expr->as.slice.start) {
                out->as.slice.start = lower_expr(program, expr->as.slice.start, error, error_line, error_column);
                if (!out->as.slice.start) {
                    return 0;
                }
            }
            if (expr->as.slice.end) {
                out->as.slice.end = lower_expr(program, expr->as.slice.end, error, error_line, error_column);
                if (!out->as.slice.end) {
                    return 0;
                }
            }
            return out->as.slice.base ? out : 0;
        case HIR_EXPR_SLICE_LENGTH:
            out->as.slice_length.base = lower_expr(program, expr->as.slice_length.base, error, error_line, error_column);
            return out->as.slice_length.base ? out : 0;
    }
    *error = "unsupported JIR expr";
    return 0;
}

static JirExpr* make_binding_expr(JirBinding* binding, int line) {
    JirExpr* expr = new_expr(JIR_EXPR_BINDING, binding->type, line);
    if (!expr) {
        return 0;
    }
    expr->as.binding = binding;
    return expr;
}

static JirExpr* make_int_expr(int64_t value, JirType* type, int line) {
    JirExpr* expr = new_expr(JIR_EXPR_INT, type, line);
    if (!expr) {
        return 0;
    }
    expr->as.int_value = value;
    return expr;
}

static JirExpr* make_binary_expr(JirBinaryOp op, JirExpr* left, JirExpr* right, JirType* type, int line) {
    JirExpr* expr = new_expr(JIR_EXPR_BINARY, type, line);
    if (!expr) {
        return 0;
    }
    expr->as.binary.op = op;
    expr->as.binary.left = left;
    expr->as.binary.right = right;
    return expr;
}

static char* make_block_name(LowerJirContext* ctx, const char* prefix) {
    char buffer[64];
    int len = snprintf(buffer, sizeof(buffer), "%s%d", prefix, ctx->next_block_id++);
    char* name = (char*)malloc((size_t)len + 1);
    if (!name) {
        return 0;
    }
    memcpy(name, buffer, (size_t)len + 1);
    return name;
}

static int append_block(LowerJirContext* ctx, const char* prefix) {
    JirBasicBlock block;
    memset(&block, 0, sizeof(block));
    block.name = make_block_name(ctx, prefix);
    if (!block.name) {
        *ctx->error = "out of memory";
        return -1;
    }
    block.term.kind = JIR_TERM_FALLTHROUGH;
    block.active_try_scope = ctx->active_try_scope;
    jir_block_list_push(&ctx->function->blocks, block);
    return ctx->function->blocks.count - 1;
}

static int append_try_scope(LowerJirContext* ctx) {
    JirTryScope scope;
    memset(&scope, 0, sizeof(scope));
    jir_try_scope_list_push(&ctx->function->try_scopes, scope);
    return ctx->function->try_scopes.count - 1;
}

static void set_block_ref(JirBasicBlockRef* ref, JirLoweredFunction* function, int index) {
    ref->index = index;
    ref->name = function->blocks.items[index].name;
}

static int append_simple_inst(JirProgram* program, JirBasicBlock* block, const HirStmt* stmt, const char** error, int* error_line, int* error_column) {
    JirInst inst;
    memset(&inst, 0, sizeof(inst));
    inst.line = stmt->line;
    switch (stmt->kind) {
        case HIR_STMT_VAR_DECL:
            inst.kind = JIR_INST_VAR_DECL;
            inst.binding = lower_binding(stmt->as.var_decl.binding, error);
            inst.value = lower_expr(program, stmt->as.var_decl.init, error, error_line, error_column);
            if (!inst.binding || !inst.value) {
                return 0;
            }
            break;
        case HIR_STMT_ASSIGN:
            inst.kind = JIR_INST_ASSIGN;
            inst.binding = stmt->as.assign.binding ? lower_binding(stmt->as.assign.binding, error) : 0;
            inst.target = stmt->as.assign.target ? lower_expr(program, stmt->as.assign.target, error, error_line, error_column) : 0;
            inst.value = lower_expr(program, stmt->as.assign.value, error, error_line, error_column);
            if ((stmt->as.assign.target && !inst.target) || !inst.value) {
                return 0;
            }
            break;
        case HIR_STMT_EXPR:
            inst.kind = JIR_INST_EXPR;
            inst.expr = lower_expr(program, stmt->as.expr_stmt.expr, error, error_line, error_column);
            if (!inst.expr) {
                return 0;
            }
            break;
        default:
            *error = "unsupported JIR simple statement";
            return 0;
    }
    jir_inst_list_push(&block->insts, inst);
    return 1;
}

static int block_is_terminated(const JirBasicBlock* block) {
    return block->term.kind != JIR_TERM_FALLTHROUGH;
}

static int lower_hir_block(LowerJirContext* ctx, const HirBlock* hir_block, int block_index, int* out_end_index);

static int lower_if_stmt(LowerJirContext* ctx, const HirStmt* stmt, int block_index, int* next_index) {
    int then_index = append_block(ctx, "if.then.");
    int else_index = stmt->as.if_stmt.has_else ? append_block(ctx, "if.else.") : -1;
    int merge_index = append_block(ctx, "if.end.");
    if (then_index < 0 || merge_index < 0 || (stmt->as.if_stmt.has_else && else_index < 0)) {
        return 0;
    }
    if (ctx->error_line) {
        *ctx->error_line = stmt->line;
    }
    if (ctx->error_column) {
        *ctx->error_column = stmt->column;
    }
    ctx->function->blocks.items[block_index].term.kind = JIR_TERM_COND_BRANCH;
    ctx->function->blocks.items[block_index].term.cond = lower_expr(ctx->jir_program, stmt->as.if_stmt.cond, ctx->error, ctx->error_line, ctx->error_column);
    if (!ctx->function->blocks.items[block_index].term.cond) {
        return 0;
    }
    set_block_ref(&ctx->function->blocks.items[block_index].term.then_target, ctx->function, then_index);
    set_block_ref(&ctx->function->blocks.items[block_index].term.else_target, ctx->function, stmt->as.if_stmt.has_else ? else_index : merge_index);
    if (!lower_hir_block(ctx, &stmt->as.if_stmt.then_block, then_index, &then_index)) {
        return 0;
    }
    if (!block_is_terminated(&ctx->function->blocks.items[then_index])) {
        ctx->function->blocks.items[then_index].term.kind = JIR_TERM_BRANCH;
        set_block_ref(&ctx->function->blocks.items[then_index].term.then_target, ctx->function, merge_index);
    }
    if (stmt->as.if_stmt.has_else) {
        if (!lower_hir_block(ctx, &stmt->as.if_stmt.else_block, else_index, &else_index)) {
            return 0;
        }
        if (!block_is_terminated(&ctx->function->blocks.items[else_index])) {
            ctx->function->blocks.items[else_index].term.kind = JIR_TERM_BRANCH;
            set_block_ref(&ctx->function->blocks.items[else_index].term.then_target, ctx->function, merge_index);
        }
    }
    *next_index = merge_index;
    return 1;
}

static int lower_try_stmt(LowerJirContext* ctx, const HirStmt* stmt, int block_index, int* next_index) {
    int previous_scope = ctx->active_try_scope;
    int try_scope_index = 0;
    int try_body_index = -1;
    int merge_index = -1;
    int i = 0;
    int try_end_index = -1;

    ctx->active_try_scope = previous_scope;
    merge_index = append_block(ctx, "try.end.");
    if (merge_index < 0) {
        return 0;
    }
    try_scope_index = append_try_scope(ctx);
    if (try_scope_index < 0) {
        return 0;
    }
    for (i = 0; i < stmt->as.try_stmt.catches.count; ++i) {
        int catch_index = append_block(ctx, "try.catch.");
        JirTryHandler handler;
        if (catch_index < 0) {
            return 0;
        }
        memset(&handler, 0, sizeof(handler));
        handler.error_type = lower_type(stmt->as.try_stmt.catches.items[i].error_type, ctx->error);
        handler.binding = lower_binding(stmt->as.try_stmt.catches.items[i].binding, ctx->error);
        if (!handler.error_type || !handler.binding) {
            return 0;
        }
        set_block_ref(&handler.catch_target, ctx->function, catch_index);
        jir_try_handler_list_push(&ctx->function->try_scopes.items[try_scope_index].handlers, handler);
    }

    ctx->active_try_scope = try_scope_index;
    try_body_index = append_block(ctx, "try.body.");
    if (try_body_index < 0) {
        return 0;
    }
    ctx->function->blocks.items[block_index].term.kind = JIR_TERM_BRANCH;
    set_block_ref(&ctx->function->blocks.items[block_index].term.then_target, ctx->function, try_body_index);
    if (!lower_hir_block(ctx, &stmt->as.try_stmt.try_body, try_body_index, &try_end_index)) {
        return 0;
    }
    if (!block_is_terminated(&ctx->function->blocks.items[try_end_index])) {
        ctx->function->blocks.items[try_end_index].term.kind = JIR_TERM_BRANCH;
        set_block_ref(&ctx->function->blocks.items[try_end_index].term.then_target, ctx->function, merge_index);
    }

    ctx->active_try_scope = previous_scope;
    for (i = 0; i < stmt->as.try_stmt.catches.count; ++i) {
        int catch_index = ctx->function->try_scopes.items[try_scope_index].handlers.items[i].catch_target.index;
        int catch_end_index = catch_index;
        if (!lower_hir_block(ctx, &stmt->as.try_stmt.catches.items[i].body, catch_index, &catch_end_index)) {
            return 0;
        }
        if (!block_is_terminated(&ctx->function->blocks.items[catch_end_index])) {
            ctx->function->blocks.items[catch_end_index].term.kind = JIR_TERM_BRANCH;
            set_block_ref(&ctx->function->blocks.items[catch_end_index].term.then_target, ctx->function, merge_index);
        }
    }
    *next_index = merge_index;
    return 1;
}

static int lower_while_stmt(LowerJirContext* ctx, const HirStmt* stmt, int block_index, int* next_index) {
    int cond_index = append_block(ctx, "while.cond.");
    int body_index = append_block(ctx, "while.body.");
    int end_index = append_block(ctx, "while.end.");
    JirLoopTarget loop_target;
    if (cond_index < 0 || body_index < 0 || end_index < 0) {
        return 0;
    }
    if (ctx->error_line) {
        *ctx->error_line = stmt->line;
    }
    if (ctx->error_column) {
        *ctx->error_column = stmt->column;
    }
    ctx->function->blocks.items[block_index].term.kind = JIR_TERM_BRANCH;
    set_block_ref(&ctx->function->blocks.items[block_index].term.then_target, ctx->function, cond_index);
    ctx->function->blocks.items[cond_index].term.kind = JIR_TERM_COND_BRANCH;
    ctx->function->blocks.items[cond_index].term.cond = lower_expr(ctx->jir_program, stmt->as.while_stmt.cond, ctx->error, ctx->error_line, ctx->error_column);
    if (!ctx->function->blocks.items[cond_index].term.cond) {
        return 0;
    }
    set_block_ref(&ctx->function->blocks.items[cond_index].term.then_target, ctx->function, body_index);
    set_block_ref(&ctx->function->blocks.items[cond_index].term.else_target, ctx->function, end_index);
    loop_target.continue_index = cond_index;
    loop_target.break_index = end_index;
    jir_loop_list_push(&ctx->loops, loop_target);
    if (!lower_hir_block(ctx, &stmt->as.while_stmt.body, body_index, &body_index)) {
        return 0;
    }
    ctx->loops.count -= 1;
    if (!block_is_terminated(&ctx->function->blocks.items[body_index])) {
        ctx->function->blocks.items[body_index].term.kind = JIR_TERM_BRANCH;
        set_block_ref(&ctx->function->blocks.items[body_index].term.then_target, ctx->function, cond_index);
    }
    *next_index = end_index;
    return 1;
}

static int lower_for_stmt(LowerJirContext* ctx, const HirStmt* stmt, int block_index, int* next_index) {
    int cond_index = append_block(ctx, "for.cond.");
    int body_index = append_block(ctx, "for.body.");
    int step_index = append_block(ctx, "for.step.");
    int end_index = append_block(ctx, "for.end.");
    JirInst decl;
    JirInst step;
    JirLoopTarget loop_target;
    JirExpr* cond = 0;
    if (cond_index < 0 || body_index < 0 || step_index < 0 || end_index < 0) {
        return 0;
    }
    if (ctx->error_line) {
        *ctx->error_line = stmt->line;
    }
    if (ctx->error_column) {
        *ctx->error_column = stmt->column;
    }
    memset(&decl, 0, sizeof(decl));
    decl.kind = JIR_INST_VAR_DECL;
    decl.line = stmt->line;
    decl.binding = lower_binding(stmt->as.for_range.binding, ctx->error);
    decl.value = lower_expr(ctx->jir_program, stmt->as.for_range.start, ctx->error, ctx->error_line, ctx->error_column);
    if (!decl.binding || !decl.value) {
        return 0;
    }
    jir_inst_list_push(&ctx->function->blocks.items[block_index].insts, decl);
    ctx->function->blocks.items[block_index].term.kind = JIR_TERM_BRANCH;
    set_block_ref(&ctx->function->blocks.items[block_index].term.then_target, ctx->function, cond_index);
    cond = make_binary_expr(
        JIR_BIN_LT,
        make_binding_expr(decl.binding, stmt->line),
        lower_expr(ctx->jir_program, stmt->as.for_range.end, ctx->error, ctx->error_line, ctx->error_column),
        lower_type(&ctx->program->bool_type, ctx->error),
        stmt->line);
    if (!cond || !cond->as.binary.right) {
        if (ctx->error_line) {
            *ctx->error_line = stmt->line;
        }
        *ctx->error = "out of memory";
        return 0;
    }
    ctx->function->blocks.items[cond_index].term.kind = JIR_TERM_COND_BRANCH;
    ctx->function->blocks.items[cond_index].term.cond = cond;
    set_block_ref(&ctx->function->blocks.items[cond_index].term.then_target, ctx->function, body_index);
    set_block_ref(&ctx->function->blocks.items[cond_index].term.else_target, ctx->function, end_index);
    loop_target.continue_index = step_index;
    loop_target.break_index = end_index;
    jir_loop_list_push(&ctx->loops, loop_target);
    if (!lower_hir_block(ctx, &stmt->as.for_range.body, body_index, &body_index)) {
        return 0;
    }
    ctx->loops.count -= 1;
    if (!block_is_terminated(&ctx->function->blocks.items[body_index])) {
        ctx->function->blocks.items[body_index].term.kind = JIR_TERM_BRANCH;
        set_block_ref(&ctx->function->blocks.items[body_index].term.then_target, ctx->function, step_index);
    }
    memset(&step, 0, sizeof(step));
    step.kind = JIR_INST_ASSIGN;
    step.line = stmt->line;
    step.binding = decl.binding;
    step.target = make_binding_expr(decl.binding, stmt->line);
    step.value = make_binary_expr(
        JIR_BIN_ADD,
        make_binding_expr(decl.binding, stmt->line),
        make_int_expr(1, decl.binding->type, stmt->line),
        decl.binding->type,
        stmt->line);
    if (!step.target || !step.value) {
        if (ctx->error_line) {
            *ctx->error_line = stmt->line;
        }
        *ctx->error = "out of memory";
        return 0;
    }
    jir_inst_list_push(&ctx->function->blocks.items[step_index].insts, step);
    ctx->function->blocks.items[step_index].term.kind = JIR_TERM_BRANCH;
    set_block_ref(&ctx->function->blocks.items[step_index].term.then_target, ctx->function, cond_index);
    *next_index = end_index;
    return 1;
}

static int lower_hir_block(LowerJirContext* ctx, const HirBlock* hir_block, int block_index, int* out_end_index) {
    int i = 0;
    int current_index = block_index;
    for (i = 0; i < hir_block->stmts.count; ++i) {
        const HirStmt* stmt = hir_block->stmts.items[i];
        JirBasicBlock* block = &ctx->function->blocks.items[current_index];
        if (block_is_terminated(block)) {
            break;
        }
        switch (stmt->kind) {
            case HIR_STMT_IF:
                if (!lower_if_stmt(ctx, stmt, current_index, &current_index)) {
                    return 0;
                }
                break;
            case HIR_STMT_TRY:
                if (!lower_try_stmt(ctx, stmt, current_index, &current_index)) {
                    return 0;
                }
                break;
            case HIR_STMT_WHILE:
                if (!lower_while_stmt(ctx, stmt, current_index, &current_index)) {
                    return 0;
                }
                break;
            case HIR_STMT_FOR_RANGE:
                if (!lower_for_stmt(ctx, stmt, current_index, &current_index)) {
                    return 0;
                }
                break;
            case HIR_STMT_BREAK:
                if (ctx->loops.count <= 0) {
                    if (ctx->error_line) {
                        *ctx->error_line = stmt->line;
                    }
                    *ctx->error = "invalid JIR break lowering";
                    return 0;
                }
                block->term.kind = JIR_TERM_BRANCH;
                set_block_ref(&block->term.then_target, ctx->function, ctx->loops.items[ctx->loops.count - 1].break_index);
                break;
            case HIR_STMT_CONTINUE:
                if (ctx->loops.count <= 0) {
                    if (ctx->error_line) {
                        *ctx->error_line = stmt->line;
                    }
                    *ctx->error = "invalid JIR continue lowering";
                    return 0;
                }
                block->term.kind = JIR_TERM_BRANCH;
                set_block_ref(&block->term.then_target, ctx->function, ctx->loops.items[ctx->loops.count - 1].continue_index);
                break;
            case HIR_STMT_RETURN:
                block->term.kind = JIR_TERM_RETURN;
                block->term.value = stmt->as.ret.expr ? lower_expr(ctx->jir_program, stmt->as.ret.expr, ctx->error, ctx->error_line, ctx->error_column) : 0;
                if (stmt->as.ret.expr && !block->term.value) {
                    return 0;
                }
                break;
            case HIR_STMT_THROW:
                block->term.kind = JIR_TERM_THROW;
                block->term.value = stmt->as.throw_stmt.expr ? lower_expr(ctx->jir_program, stmt->as.throw_stmt.expr, ctx->error, ctx->error_line, ctx->error_column) : 0;
                if (!block->term.value) {
                    return 0;
                }
                break;
            default:
                if (!append_simple_inst(ctx->jir_program, block, stmt, ctx->error, ctx->error_line, ctx->error_column)) {
                    return 0;
                }
                break;
        }
    }
    *out_end_index = current_index;
    return 1;
}

static int lower_function_skeleton(const HirProgram* hir_program, JirProgram* jir_program, const HirFunction* hir_fn, JirFunction* function_info, JirLoweredFunction* jir_fn, const char** error, int* error_line, int* error_column) {
    LowerJirContext ctx;
    int entry_index = 0;
    memset(jir_fn, 0, sizeof(*jir_fn));
    memset(&ctx, 0, sizeof(ctx));
    jir_fn->name = dup_text(hir_fn->name);
    jir_fn->function = function_info;
    if (!jir_fn->name) {
        *error = "out of memory";
        return 0;
    }
    ctx.function = jir_fn;
    ctx.error = error;
    ctx.error_line = error_line;
    ctx.error_column = error_column;
    ctx.program = hir_program;
    ctx.jir_program = jir_program;
    ctx.active_try_scope = -1;
    entry_index = append_block(&ctx, "entry.");
    if (entry_index < 0) {
        return 0;
    }
    return lower_hir_block(&ctx, &hir_fn->body, entry_index, &entry_index);
}

static int lower_function_info(const HirFunction* hir_fn, JirFunction* jir_fn, const char** error) {
    int i = 0;
    memset(jir_fn, 0, sizeof(*jir_fn));
    jir_fn->return_type = lower_type(hir_fn->return_type, error);
    if (!jir_fn->return_type) {
        return 0;
    }
    jir_fn->name = hir_fn->name;
    jir_fn->method_name = hir_fn->method_name;
    for (i = 0; i < hir_fn->params.count; ++i) {
        JirBinding* binding = lower_binding(hir_fn->params.items[i], error);
        if (!binding) {
            return 0;
        }
        jir_binding_list_push(&jir_fn->params, binding);
    }
    for (i = 0; i < hir_fn->locals.count; ++i) {
        JirBinding* binding = lower_binding(hir_fn->locals.items[i], error);
        if (!binding) {
            return 0;
        }
        jir_binding_list_push(&jir_fn->locals, binding);
    }
    jir_fn->struct_init_flag = hir_fn->struct_init_flag;
    jir_fn->struct_deinit_flag = hir_fn->struct_deinit_flag;
    jir_fn->extern_flag = hir_fn->extern_flag;
    jir_fn->method_flag = hir_fn->method_flag;
    jir_fn->static_method_flag = hir_fn->static_method_flag;
    jir_fn->owner_name = hir_fn->owner_struct ? hir_fn->owner_struct->name : 0;
    jir_fn->receiver_type = hir_fn->receiver_type ? lower_type(hir_fn->receiver_type, error) : 0;
    if (hir_fn->receiver_type && !jir_fn->receiver_type) {
        return 0;
    }
    jir_fn->line = hir_fn->line;
    return 1;
}

int lower_hir_to_jir(const HirProgram* hir, JirProgram* jir, const char** error, int* error_line, int* error_column) {
    int i = 0;
    memset(jir, 0, sizeof(*jir));
    memset(&g_type_map, 0, sizeof(g_type_map));
    memset(&g_binding_map, 0, sizeof(g_binding_map));
    for (i = 0; i < hir->globals.count; ++i) {
        JirGlobal global;
        memset(&global, 0, sizeof(global));
        global.binding = lower_binding(hir->globals.items[i].binding, error);
        global.extern_flag = hir->globals.items[i].extern_flag;
        if (error_line) {
            *error_line = hir->globals.items[i].line;
        }
        if (error_column) {
            *error_column = hir->globals.items[i].init ? hir->globals.items[i].init->column : 0;
        }
        global.init = hir->globals.items[i].init ? lower_expr(jir, hir->globals.items[i].init, error, error_line, error_column) : 0;
        global.line = hir->globals.items[i].line;
        if (!global.binding || (!global.extern_flag && !global.init)) {
            return 0;
        }
        jir_global_list_push(&jir->globals, global);
    }
    for (i = 0; i < hir->functions.count; ++i) {
        JirFunction function_info;
        if (!lower_function_info(&hir->functions.items[i], &function_info, error)) {
            return 0;
        }
        jir_function_list_push(&jir->functions, function_info);
    }
    for (i = 0; i < hir->functions.count; ++i) {
        JirLoweredFunction lowered;
        if (hir->functions.items[i].extern_flag) {
            continue;
        }
        if (!lower_function_skeleton(hir, jir, &hir->functions.items[i], &jir->functions.items[i], &lowered, error, error_line, error_column)) {
            return 0;
        }
        jir_function_list_push(&jir->lowered_functions, lowered);
    }
    if (!desugar_program(jir)) {
        *error = "failed to desugar JIR";
        if (error_line) {
            *error_line = 0;
        }
        if (error_column) {
            *error_column = 0;
        }
        return 0;
    }
    if (error_line) {
        *error_line = 0;
    }
    if (error_column) {
        *error_column = 0;
    }
    return 1;
}
