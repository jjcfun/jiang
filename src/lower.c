#include "lower.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static JirModule* current_jir_mod = NULL;
static JirFunction* current_jir_func = NULL;
static Arena* jir_arena = NULL;
static HIRNode* current_hir_root = NULL;
static HIRNode* current_self_owner = NULL;

static Token jir_enum_constant_name(Token enum_name, Token member_name) {
    if (enum_name.length == 0) return member_name;
    size_t len = enum_name.length + 1 + member_name.length;
    char* buf = arena_alloc(jir_arena, len + 1);
    memcpy(buf, enum_name.start, enum_name.length);
    buf[enum_name.length] = '_';
    memcpy(buf + enum_name.length + 1, member_name.start, member_name.length);
    buf[len] = '\0';
    Token tok = {TOKEN_IDENTIFIER, buf, len, enum_name.line};
    return tok;
}
static uint32_t loop_break_labels[64];
static uint32_t loop_continue_labels[64];
static int loop_depth = 0;
static TypeExpr lower_int_type = {
    .kind = TYPE_BASE,
    .as.base_type = { TOKEN_IDENTIFIER, "Int", 3, 0 }
};
static TypeExpr lower_void_type = {
    .kind = TYPE_BASE,
    .as.base_type = { TOKEN_IDENTIFIER, "void", 4, 0 }
};
static TypeExpr lower_bool_type = {
    .kind = TYPE_BASE,
    .as.base_type = { TOKEN_IDENTIFIER, "Bool", 4, 0 }
};

static int lower_expr(HIRNode* node);
static JirReg lower_get_or_create_local(Token name, TypeExpr* type);
static TypeExpr* lower_unwrap_type(TypeExpr* type);
static int lower_call_expr(HIRNode* node, bool discard_result);
static void lower_emit_function_body(HIRNode* decl, Token emitted_name, TypeExpr* return_type, TypeExpr* self_type, bool self_is_param, HIRNode* self_owner);

static TypeExpr* lower_make_pointer_type(TypeExpr* element) {
    TypeExpr* t = arena_alloc(jir_arena, sizeof(TypeExpr));
    t->kind = TYPE_POINTER;
    t->as.pointer.element = element;
    return t;
}

static TypeExpr* lower_make_base_type_from_token(Token name) {
    TypeExpr* t = arena_alloc(jir_arena, sizeof(TypeExpr));
    t->kind = TYPE_BASE;
    t->as.base_type = name;
    return t;
}

static TypeExpr* lower_resolved_type(TypeExpr* declared, TypeExpr* inferred) {
    TypeExpr* type = declared;
    if (!type) return inferred;
    type = lower_unwrap_type(type);
    if (type &&
        type->kind == TYPE_BASE &&
        type->as.base_type.length == 1 &&
        type->as.base_type.start[0] == '_') {
        return inferred;
    }
    return declared;
}

static TypeExpr* lower_unwrap_type(TypeExpr* type) {
    while (type && (type->kind == TYPE_NULLABLE || type->kind == TYPE_MUTABLE)) {
        type = (type->kind == TYPE_NULLABLE) ? type->as.nullable.element : type->as.mutable.element;
    }
    return type;
}

static bool lower_is_void_type(TypeExpr* type) {
    return type &&
           ((type->kind == TYPE_TUPLE && type->as.tuple.count == 0) ||
            (type->kind == TYPE_BASE &&
             ((type->as.base_type.length == 2 &&
               strncmp(type->as.base_type.start, "()", 2) == 0) ||
              (type->as.base_type.length == 4 &&
               strncmp(type->as.base_type.start, "void", 4) == 0))));
}

static JirReg lower_find_local(Token name) {
    for (size_t i = 0; i < current_jir_func->local_count; i++) {
        JirLocal* local = &current_jir_func->locals[i];
        if (strncmp(local->name, name.start, name.length) == 0 && local->name[name.length] == '\0') {
            return (JirReg)i;
        }
    }
    return -1;
}

static Token lower_make_token_from_cstr(const char* text) {
    size_t len = strlen(text);
    char* buf = arena_alloc(jir_arena, len + 1);
    memcpy(buf, text, len + 1);
    Token token = { TOKEN_IDENTIFIER, buf, (int)len, 0 };
    return token;
}

static Token lower_join_tokens(Token left, const char* sep, Token right) {
    size_t sep_len = strlen(sep);
    size_t len = left.length + sep_len + right.length;
    char* buf = arena_alloc(jir_arena, len + 1);
    memcpy(buf, left.start, left.length);
    memcpy(buf + left.length, sep, sep_len);
    memcpy(buf + left.length + sep_len, right.start, right.length);
    buf[len] = '\0';
    Token token = { TOKEN_IDENTIFIER, buf, (int)len, left.line };
    return token;
}

static bool lower_is_static_struct_method(HIRNode* method) {
    return method &&
           method->kind == HIR_FUNC_DECL &&
           method->token.length == 4 &&
           strncmp(method->token.start, "open", 4) == 0;
}

typedef struct {
    HIRNode* union_decl;
    HIRNode* variant_decl;
    TypeExpr* variant_type;
    Token variant_name;
    Token tag_name;
} LowerVariantInfo;

static HIRNode* lower_skip_declared_type(HIRNode* node) {
    HIRNode* child = hir_first_child(node);
    if (node && node->declared_type && hir_is_type_node(child)) return hir_next_sibling(child);
    return child;
}

static HIRNode* lower_last_child(HIRNode* node) {
    return node ? node->last_child : NULL;
}

static HIRNode* lower_find_union_decl(TypeExpr* type) {
    if (!current_hir_root || !type || type->kind != TYPE_BASE) return NULL;
    for (HIRNode* stmt = hir_first_child(current_hir_root); stmt; stmt = hir_next_sibling(stmt)) {
        if (stmt->kind != HIR_UNION_DECL) continue;
        if (stmt->token.length == type->as.base_type.length &&
            strncmp(stmt->token.start, type->as.base_type.start, type->as.base_type.length) == 0) {
            return stmt;
        }
    }
    return NULL;
}

static HIRNode* lower_find_enum_decl(TypeExpr* type) {
    if (!current_hir_root || !type || type->kind != TYPE_BASE) return NULL;
    for (HIRNode* stmt = hir_first_child(current_hir_root); stmt; stmt = hir_next_sibling(stmt)) {
        if (stmt->kind != HIR_ENUM_DECL) continue;
        if (stmt->token.length == type->as.base_type.length &&
            strncmp(stmt->token.start, type->as.base_type.start, type->as.base_type.length) == 0) {
            return stmt;
        }
    }
    return NULL;
}

static Token lower_infer_enum_name_from_member(Token member_name) {
    if (!current_hir_root) return (Token){0};
    for (HIRNode* stmt = hir_first_child(current_hir_root); stmt; stmt = hir_next_sibling(stmt)) {
        if (stmt->kind != HIR_ENUM_DECL) continue;
        for (size_t i = 0; i < stmt->token_count; i++) {
            Token candidate = stmt->tokens[i];
            if (candidate.length == member_name.length &&
                strncmp(candidate.start, member_name.start, member_name.length) == 0) {
                return stmt->token;
            }
        }
    }
    return (Token){0};
}

static HIRNode* lower_find_struct_field_decl(HIRNode* owner, Token name) {
    if (!owner || owner->kind != HIR_STRUCT_DECL) return NULL;
    for (HIRNode* member = hir_first_child(owner); member; member = hir_next_sibling(member)) {
        if (member->kind != HIR_VAR_DECL) continue;
        if (member->token.length == name.length &&
            strncmp(member->token.start, name.start, name.length) == 0) {
            return member;
        }
    }
    return NULL;
}

static bool lower_resolve_variant_info(HIRNode* switch_expr, HIRNode* pattern, LowerVariantInfo* info) {
    if (!switch_expr || !pattern || pattern->kind != HIR_FUNC_CALL) {
        return false;
    }

    HIRNode* callee = hir_child_at(pattern, 0);
    if (!callee || callee->kind != HIR_MEMBER_ACCESS) return false;

    TypeExpr* switch_type = switch_expr->evaluated_type;
    if (!switch_type || switch_type->kind != TYPE_BASE) return false;

    HIRNode* union_decl = lower_find_union_decl(switch_type);
    if (!union_decl) return false;

    Token variant_name = callee->token;
    HIRNode* variant_decl = NULL;
    for (HIRNode* member = hir_first_child(union_decl); member; member = hir_next_sibling(member)) {
        if (member->kind != HIR_VAR_DECL) continue;
        if (member->token.length == variant_name.length &&
            strncmp(member->token.start, variant_name.start, variant_name.length) == 0) {
            variant_decl = member;
            break;
        }
    }
    if (!variant_decl) return false;

    char tag_buf[160];
    if (union_decl->token2.length > 0) {
        snprintf(tag_buf, sizeof(tag_buf), "%.*s_%.*s",
                 (int)union_decl->token2.length, union_decl->token2.start,
                 (int)variant_name.length, variant_name.start);
    } else {
        snprintf(tag_buf, sizeof(tag_buf), "%.*sTag_%.*s",
                 (int)union_decl->token.length, union_decl->token.start,
                 (int)variant_name.length, variant_name.start);
    }

    info->union_decl = union_decl;
    info->variant_decl = variant_decl;
    info->variant_type = variant_decl->declared_type;
    info->variant_name = variant_name;
    info->tag_name = lower_make_token_from_cstr(tag_buf);
    return true;
}

static void lower_emit_variant_bindings(JirReg switch_reg, HIRNode* pattern, const LowerVariantInfo* info) {
    if (!pattern || pattern->kind != HIR_FUNC_CALL || !info || !info->variant_decl) return;

    JirReg variant_reg = jir_alloc_temp(current_jir_func, info->variant_type, jir_arena);
    int variant_idx = jir_emit(current_jir_func, JIR_OP_MEMBER_ACCESS, variant_reg, switch_reg, -1, jir_arena);
    current_jir_func->insts[variant_idx].payload.name = info->variant_name;

    size_t binding_index = 0;
    for (HIRNode* binding = hir_next_sibling(hir_child_at(pattern, 0)); binding; binding = hir_next_sibling(binding), binding_index++) {
        if (binding->kind != HIR_PATTERN) continue;

        TypeExpr* binding_type = binding->evaluated_type ? binding->evaluated_type : binding->declared_type;
        JirReg source_reg = variant_reg;
        if (info->variant_type && info->variant_type->kind == TYPE_TUPLE) {
            if ((!binding_type ||
                 (binding_type->kind == TYPE_BASE &&
                  binding_type->as.base_type.length == 1 &&
                  binding_type->as.base_type.start[0] == '_')) &&
                binding_index < info->variant_type->as.tuple.count) {
                binding_type = info->variant_type->as.tuple.elements[binding_index];
            }
            source_reg = jir_alloc_temp(current_jir_func, binding_type, jir_arena);
            int field_idx = jir_emit(current_jir_func, JIR_OP_EXTRACT_FIELD, source_reg, variant_reg, -1, jir_arena);
            current_jir_func->insts[field_idx].payload.int_val = (int64_t)binding_index;
        } else if (!binding_type ||
                   (binding_type->kind == TYPE_BASE &&
                    binding_type->as.base_type.length == 1 &&
                    binding_type->as.base_type.start[0] == '_')) {
            binding_type = info->variant_type;
        }

        JirReg target_reg = lower_get_or_create_local(binding->token, binding_type);
        jir_emit(current_jir_func, JIR_OP_STORE, target_reg, source_reg, -1, jir_arena);
    }
}

static bool lower_call_has_pattern_args(HIRNode* call) {
    if (!call || call->kind != HIR_FUNC_CALL) return false;
    for (HIRNode* arg = hir_next_sibling(hir_child_at(call, 0)); arg; arg = hir_next_sibling(arg)) {
        if (arg->kind == HIR_PATTERN) return true;
    }
    return false;
}

static int lower_emit_enum_access_with_name(HIRNode* node, Token enum_name) {
    int r = jir_alloc_temp(current_jir_func,
                           node->evaluated_type ? node->evaluated_type : lower_make_base_type_from_token(enum_name),
                           jir_arena);
    int idx = jir_emit(current_jir_func, JIR_OP_LOAD_SYM, r, -1, -1, jir_arena);
    current_jir_func->insts[idx].payload.name = jir_enum_constant_name(enum_name, node->token2);
    return r;
}

static JirReg lower_get_or_create_local(Token name, TypeExpr* type) {
    JirReg existing = lower_find_local(name);
    if (existing >= 0) return existing;
    return jir_alloc_local(current_jir_func, name, type, false, jir_arena);
}

static void lower_stmt(HIRNode* node) {
    if (!node) return;
    switch (node->kind) {
        case HIR_VAR_DECL: {
            HIRNode* init = lower_skip_declared_type(node);
            TypeExpr* t = lower_resolved_type(node->declared_type, init ? init->evaluated_type : NULL);
            int r_var = lower_get_or_create_local(node->token, t);
            if (init) {
                int r_val = (init->kind == HIR_ENUM_ACCESS &&
                             init->token.length == 0 &&
                             t && t->kind == TYPE_BASE)
                                ? lower_emit_enum_access_with_name(init, t->as.base_type)
                                : lower_expr(init);
                jir_emit(current_jir_func, JIR_OP_STORE, r_var, r_val >= 0 ? r_val : 0, -1, jir_arena);
            }
            break;
        }
        case HIR_IF_STMT: {
            HIRNode* condition = hir_child_at(node, 0);
            HIRNode* then_branch = hir_child_at(node, 1);
            HIRNode* else_branch = hir_child_at(node, 2);
            int r_cond = lower_expr(condition);
            uint32_t l_else = jir_reserve_label(current_jir_func);
            uint32_t l_end = jir_reserve_label(current_jir_func);
            int idx = jir_emit(current_jir_func, JIR_OP_JUMP_IF_FALSE, -1, r_cond >= 0 ? r_cond : 0, -1, jir_arena);
            current_jir_func->insts[idx].payload.label_id = l_else;
            lower_stmt(then_branch);
            int idx2 = jir_emit(current_jir_func, JIR_OP_JUMP, -1, -1, -1, jir_arena);
            current_jir_func->insts[idx2].payload.label_id = l_end;
            jir_emit_label(current_jir_func, l_else, jir_arena);
            if (else_branch) lower_stmt(else_branch);
            jir_emit_label(current_jir_func, l_end, jir_arena);
            break;
        }
        case HIR_WHILE_STMT: {
            HIRNode* condition = hir_child_at(node, 0);
            HIRNode* body = hir_child_at(node, 1);
            uint32_t l_start = jir_reserve_label(current_jir_func);
            uint32_t l_end = jir_reserve_label(current_jir_func);
            loop_break_labels[loop_depth] = l_end;
            loop_continue_labels[loop_depth] = l_start;
            loop_depth++;
            jir_emit_label(current_jir_func, l_start, jir_arena);
            int r_cond = lower_expr(condition);
            int idx = jir_emit(current_jir_func, JIR_OP_JUMP_IF_FALSE, -1, r_cond >= 0 ? r_cond : 0, -1, jir_arena);
            current_jir_func->insts[idx].payload.label_id = l_end;
            lower_stmt(body);
            int idx2 = jir_emit(current_jir_func, JIR_OP_JUMP, -1, -1, -1, jir_arena);
            current_jir_func->insts[idx2].payload.label_id = l_start;
            jir_emit_label(current_jir_func, l_end, jir_arena);
            loop_depth--;
            break;
        }
        case HIR_FOR_STMT: {
            HIRNode* pattern = hir_child_at(node, 0);
            HIRNode* iterable = hir_child_at(node, 1);
            HIRNode* body = hir_child_at(node, 2);
            if (iterable && iterable->kind == HIR_RANGE_EXPR) {
                if (pattern && pattern->kind == HIR_BINDING_LIST && pattern->child_count > 0) {
                    pattern = hir_child_at(pattern, 0);
                }
                if (pattern && pattern->kind == HIR_PATTERN) {
                    JirReg start_reg = lower_expr(hir_child_at(iterable, 0));
                    JirReg end_reg = lower_expr(hir_child_at(iterable, 1));
                    TypeExpr* pattern_type = pattern->evaluated_type ? pattern->evaluated_type : pattern->declared_type;
                    JirReg iter_reg = lower_get_or_create_local(pattern->token, pattern_type);
                    jir_emit(current_jir_func, JIR_OP_STORE, iter_reg, start_reg, -1, jir_arena);

                    uint32_t l_start = jir_reserve_label(current_jir_func);
                    uint32_t l_continue = jir_reserve_label(current_jir_func);
                    uint32_t l_end = jir_reserve_label(current_jir_func);
                    loop_break_labels[loop_depth] = l_end;
                    loop_continue_labels[loop_depth] = l_continue;
                    loop_depth++;

                    jir_emit_label(current_jir_func, l_start, jir_arena);
                    JirReg cond_reg = jir_alloc_temp(current_jir_func, pattern_type, jir_arena);
                    jir_emit(current_jir_func, JIR_OP_CMP_LT, cond_reg, iter_reg, end_reg, jir_arena);
                    int idx = jir_emit(current_jir_func, JIR_OP_JUMP_IF_FALSE, -1, cond_reg, -1, jir_arena);
                    current_jir_func->insts[idx].payload.label_id = l_end;

                    lower_stmt(body);

                    jir_emit_label(current_jir_func, l_continue, jir_arena);
                    JirReg one_reg = jir_alloc_temp(current_jir_func, pattern_type, jir_arena);
                    int one_idx = jir_emit(current_jir_func, JIR_OP_CONST_INT, one_reg, -1, -1, jir_arena);
                    current_jir_func->insts[one_idx].payload.int_val = 1;
                    JirReg next_reg = jir_alloc_temp(current_jir_func, pattern_type, jir_arena);
                    jir_emit(current_jir_func, JIR_OP_ADD, next_reg, iter_reg, one_reg, jir_arena);
                    jir_emit(current_jir_func, JIR_OP_STORE, iter_reg, next_reg, -1, jir_arena);
                    int back_idx = jir_emit(current_jir_func, JIR_OP_JUMP, -1, -1, -1, jir_arena);
                    current_jir_func->insts[back_idx].payload.label_id = l_start;
                    jir_emit_label(current_jir_func, l_end, jir_arena);
                    loop_depth--;
                }
            }
            break;
        }
        case HIR_SWITCH_STMT: {
            HIRNode* switch_expr = hir_child_at(node, 0);
            JirReg switch_reg = lower_expr(switch_expr);
            uint32_t end_label = jir_reserve_label(current_jir_func);
            uint32_t next_label = 0;

            for (HIRNode* case_node = hir_next_sibling(switch_expr);
                 case_node && case_node->kind == HIR_SWITCH_CASE;
                 case_node = hir_next_sibling(case_node)) {
                LowerVariantInfo info = {0};
                HIRNode* pattern = hir_child_at(case_node, 0);
                HIRNode* body = hir_child_at(case_node, 1);
                bool has_variant = lower_resolve_variant_info(switch_expr, pattern, &info);

                JirReg cond_reg = -1;
                if (has_variant) {
                    JirReg tag_reg = jir_alloc_temp(current_jir_func, &lower_int_type, jir_arena);
                    jir_emit(current_jir_func, JIR_OP_GET_TAG, tag_reg, switch_reg, -1, jir_arena);
                    JirReg expected_reg = jir_alloc_temp(current_jir_func, &lower_int_type, jir_arena);
                    int load_idx = jir_emit(current_jir_func, JIR_OP_LOAD_SYM, expected_reg, -1, -1, jir_arena);
                    current_jir_func->insts[load_idx].payload.name = info.tag_name;
                    cond_reg = jir_alloc_temp(current_jir_func, &lower_int_type, jir_arena);
                    jir_emit(current_jir_func, JIR_OP_CMP_EQ, cond_reg, tag_reg, expected_reg, jir_arena);
                } else {
                    cond_reg = lower_expr(pattern);
                }

                next_label = jir_reserve_label(current_jir_func);
                int jmp_idx = jir_emit(current_jir_func, JIR_OP_JUMP_IF_FALSE, -1, cond_reg, -1, jir_arena);
                current_jir_func->insts[jmp_idx].payload.label_id = next_label;

                if (has_variant) {
                    lower_emit_variant_bindings(switch_reg, pattern, &info);
                }
                lower_stmt(body);
                int end_idx = jir_emit(current_jir_func, JIR_OP_JUMP, -1, -1, -1, jir_arena);
                current_jir_func->insts[end_idx].payload.label_id = end_label;
                jir_emit_label(current_jir_func, next_label, jir_arena);
            }

            HIRNode* else_branch = lower_last_child(node);
            if (else_branch && else_branch->kind != HIR_SWITCH_CASE) {
                lower_stmt(else_branch);
            }
            jir_emit_label(current_jir_func, end_label, jir_arena);
            break;
        }
        case HIR_RETURN_STMT: {
            HIRNode* expr = hir_child_at(node, 0);
            int r = -1;
            if (expr && !lower_is_void_type(expr->evaluated_type)) {
                r = lower_expr(expr);
            }
            jir_emit(current_jir_func, JIR_OP_RETURN, -1, r >= 0 ? r : -1, -1, jir_arena);
            break;
        }
        case HIR_BINDING_ASSIGN: {
            HIRNode* bindings = hir_child_at(node, 0);
            HIRNode* value = hir_child_at(node, 1);
            JirReg value_reg = lower_expr(value);
            size_t i = 0;
            for (HIRNode* binding = hir_first_child(bindings); binding; binding = hir_next_sibling(binding), i++) {
                TypeExpr* binding_type = binding->evaluated_type ? binding->evaluated_type : binding->declared_type;
                JirReg target_reg = lower_get_or_create_local(binding->token, binding_type);
                JirReg field_reg = jir_alloc_temp(current_jir_func, binding_type, jir_arena);
                int field_idx = jir_emit(current_jir_func, JIR_OP_EXTRACT_FIELD, field_reg, value_reg, -1, jir_arena);
                current_jir_func->insts[field_idx].payload.int_val = (int64_t)i;
                jir_emit(current_jir_func, JIR_OP_STORE, target_reg, field_reg, -1, jir_arena);
            }
            break;
        }
        case HIR_BREAK_STMT: {
            if (loop_depth > 0) {
                int idx = jir_emit(current_jir_func, JIR_OP_JUMP, -1, -1, -1, jir_arena);
                current_jir_func->insts[idx].payload.label_id = loop_break_labels[loop_depth - 1];
            }
            break;
        }
        case HIR_CONTINUE_STMT: {
            if (loop_depth > 0) {
                int idx = jir_emit(current_jir_func, JIR_OP_JUMP, -1, -1, -1, jir_arena);
                current_jir_func->insts[idx].payload.label_id = loop_continue_labels[loop_depth - 1];
            }
            break;
        }
        case HIR_BLOCK:
        case HIR_PROGRAM:
            for (HIRNode* child = hir_first_child(node); child; child = hir_next_sibling(child)) lower_stmt(child);
            break;
        case HIR_FUNC_CALL:
            lower_call_expr(node, true);
            break;
        default: lower_expr(node); break;
    }
}

static int lower_call_expr(HIRNode* node, bool discard_result) {
    int args[32];
    size_t arg_count = 0;
    HIRNode* callee = hir_child_at(node, 0);
    for (HIRNode* arg = hir_next_sibling(callee); arg; arg = hir_next_sibling(arg)) args[arg_count++] = lower_expr(arg);
    TypeExpr* call_type = node->evaluated_type;
    if (!call_type && callee) call_type = callee->evaluated_type;
    if (!call_type &&
        callee &&
        callee->symbol &&
        callee->symbol->kind == SYM_FUNC) {
        call_type = callee->symbol->type;
    }
    int r = (discard_result || lower_is_void_type(call_type)) ? -1 : jir_alloc_temp(current_jir_func, call_type, jir_arena);
    Token callee_name = {TOKEN_IDENTIFIER, "anonymous", 9, 0};
    if (callee->kind == HIR_IDENTIFIER) {
        callee_name = callee->token;
        if (callee->symbol && callee->symbol->kind == SYM_STRUCT) {
            char name_buf[160];
            snprintf(name_buf, sizeof(name_buf), "%.*s_new",
                     (int)callee_name.length, callee_name.start);
            callee_name = lower_make_token_from_cstr(name_buf);
        }
    } else if (callee->kind == HIR_MEMBER_ACCESS) {
        HIRNode* obj = hir_child_at(callee, 0);
        Token member = callee->token;
        if (obj->symbol && obj->symbol->kind == SYM_NAMESPACE) {
            callee_name = member;
        } else if (obj->symbol && obj->symbol->kind == SYM_STRUCT &&
                   obj->symbol->type && obj->symbol->type->kind == TYPE_BASE) {
            Token type_name = obj->symbol->type->as.base_type;
            if (type_name.length > 0) {
                char name_buf[160];
                snprintf(name_buf, sizeof(name_buf), "%.*s_%.*s",
                         (int)type_name.length, type_name.start,
                         (int)member.length, member.start);
                callee_name = lower_make_token_from_cstr(name_buf);
            }
        } else {
            TypeExpr* obj_type = lower_unwrap_type(obj->evaluated_type);
            bool is_ptr = obj_type && obj_type->kind == TYPE_POINTER;
            TypeExpr* owner_type = is_ptr ? lower_unwrap_type(obj_type->as.pointer.element) : obj_type;
            if (owner_type && owner_type->kind == TYPE_BASE) {
                char name_buf[160];
                snprintf(name_buf, sizeof(name_buf), "%.*s_%.*s",
                         (int)owner_type->as.base_type.length, owner_type->as.base_type.start,
                         (int)member.length, member.start);
                callee_name = lower_make_token_from_cstr(name_buf);

                int self_reg = lower_expr(obj);
                if (!is_ptr) {
                    int addr_reg = jir_alloc_temp(current_jir_func, lower_make_pointer_type(owner_type), jir_arena);
                    jir_emit(current_jir_func, JIR_OP_ADDR_OF, addr_reg, self_reg, -1, jir_arena);
                    self_reg = addr_reg;
                }
                for (size_t i = arg_count; i > 0; i--) args[i] = args[i - 1];
                args[0] = self_reg;
                arg_count++;
            }
        }
    }
    jir_emit_call(current_jir_func, r, callee_name, args, arg_count, jir_arena);
    return r;
}

static int lower_expr(HIRNode* node) {
    if (!node) return -1;
    switch (node->kind) {
        case HIR_LITERAL_NUMBER: {
            TypeExpr* number_type = node->evaluated_type ? node->evaluated_type : &lower_int_type;
            int r = jir_alloc_temp(current_jir_func, number_type, jir_arena);
            if (number_type &&
                number_type->kind == TYPE_BASE &&
                number_type->as.base_type.length == 3 &&
                strncmp(number_type->as.base_type.start, "Int", 3) == 0) {
                int idx = jir_emit(current_jir_func, JIR_OP_CONST_INT, r, -1, -1, jir_arena);
                current_jir_func->insts[idx].payload.int_val = (int64_t)node->number_value;
            } else {
                int idx = jir_emit(current_jir_func, JIR_OP_CONST_FLOAT, r, -1, -1, jir_arena);
                current_jir_func->insts[idx].payload.float_val = node->number_value;
            }
            return r;
        }
        case HIR_LITERAL_STRING: {
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int len = (int)node->token.length; const char* start = node->token.start;
            if (len >= 2 && start[0] == '"') { start++; len -= 2; }
            char* s = malloc(len + 1); memcpy(s, start, len); s[len] = '\0';
            int idx = jir_emit(current_jir_func, JIR_OP_CONST_STR, r, -1, -1, jir_arena);
            current_jir_func->insts[idx].payload.str_val = s;
            return r;
        }
        case HIR_IDENTIFIER: {
            JirReg existing = lower_find_local(node->token);
            if (existing >= 0) return existing;
            if (current_self_owner) {
                HIRNode* field = lower_find_struct_field_decl(current_self_owner, node->token);
                if (field) {
                    Token self_name = { TOKEN_IDENTIFIER, "self", 4, node->line };
                    JirReg self_reg = lower_find_local(self_name);
                    int r = jir_alloc_temp(current_jir_func,
                                           lower_resolved_type(field->declared_type, field->evaluated_type),
                                           jir_arena);
                    int idx = jir_emit(current_jir_func, JIR_OP_MEMBER_ACCESS, r, self_reg, -1, jir_arena);
                    current_jir_func->insts[idx].payload.name = node->token;
                    return r;
                }
            }
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int idx = jir_emit(current_jir_func, JIR_OP_LOAD_SYM, r, -1, -1, jir_arena);
            current_jir_func->insts[idx].payload.name = node->token;
            return r;
        }
        case HIR_BINARY_EXPR: {
            HIRNode* left = hir_child_at(node, 0);
            HIRNode* right = hir_child_at(node, 1);
            if ((node->op == TOKEN_EQUAL_EQUAL || node->op == TOKEN_BANG_EQUAL) &&
                right && right->kind == HIR_FUNC_CALL && lower_call_has_pattern_args(right)) {
                LowerVariantInfo info = {0};
                JirReg left_reg = lower_expr(left);
                JirReg result_reg = jir_alloc_temp(current_jir_func, &lower_bool_type, jir_arena);
                int init_idx = jir_emit(current_jir_func, JIR_OP_CONST_INT, result_reg, -1, -1, jir_arena);
                current_jir_func->insts[init_idx].payload.int_val = (node->op == TOKEN_BANG_EQUAL) ? 1 : 0;

                if (lower_resolve_variant_info(left, right, &info)) {
                    JirReg tag_reg = jir_alloc_temp(current_jir_func, &lower_int_type, jir_arena);
                    jir_emit(current_jir_func, JIR_OP_GET_TAG, tag_reg, left_reg, -1, jir_arena);
                    JirReg expected_reg = jir_alloc_temp(current_jir_func, &lower_int_type, jir_arena);
                    int load_idx = jir_emit(current_jir_func, JIR_OP_LOAD_SYM, expected_reg, -1, -1, jir_arena);
                    current_jir_func->insts[load_idx].payload.name = info.tag_name;
                    JirReg cond_reg = jir_alloc_temp(current_jir_func, &lower_bool_type, jir_arena);
                    jir_emit(current_jir_func, JIR_OP_CMP_EQ, cond_reg, tag_reg, expected_reg, jir_arena);
                    uint32_t end_label = jir_reserve_label(current_jir_func);
                    int jump_idx = jir_emit(current_jir_func, JIR_OP_JUMP_IF_FALSE, -1, cond_reg, -1, jir_arena);
                    current_jir_func->insts[jump_idx].payload.label_id = end_label;
                    lower_emit_variant_bindings(left_reg, right, &info);
                    int set_idx = jir_emit(current_jir_func, JIR_OP_CONST_INT, result_reg, -1, -1, jir_arena);
                    current_jir_func->insts[set_idx].payload.int_val = (node->op == TOKEN_BANG_EQUAL) ? 0 : 1;
                    jir_emit_label(current_jir_func, end_label, jir_arena);
                    return result_reg;
                }
            }
            if ((node->op == TOKEN_EQUAL_EQUAL || node->op == TOKEN_BANG_EQUAL) &&
                right && right->kind == HIR_MEMBER_ACCESS) {
                HIRNode* object = hir_child_at(right, 0);
                TypeExpr* left_type = lower_unwrap_type(left->evaluated_type);
                HIRNode* union_decl = lower_find_union_decl(left_type);
                if (object && object->kind == HIR_IDENTIFIER && union_decl &&
                    object->token.length == union_decl->token.length &&
                    strncmp(object->token.start, union_decl->token.start, union_decl->token.length) == 0) {
                    char tag_buf[160];
                    if (union_decl->token2.length > 0) {
                        snprintf(tag_buf, sizeof(tag_buf), "%.*s_%.*s",
                                 (int)union_decl->token2.length, union_decl->token2.start,
                                 (int)right->token.length, right->token.start);
                    } else {
                        snprintf(tag_buf, sizeof(tag_buf), "%.*sTag_%.*s",
                                 (int)union_decl->token.length, union_decl->token.start,
                                 (int)right->token.length, right->token.start);
                    }
                    JirReg left_reg = lower_expr(left);
                    JirReg tag_reg = jir_alloc_temp(current_jir_func, &lower_int_type, jir_arena);
                    jir_emit(current_jir_func, JIR_OP_GET_TAG, tag_reg, left_reg, -1, jir_arena);
                    JirReg expected_reg = jir_alloc_temp(current_jir_func, &lower_int_type, jir_arena);
                    int load_idx = jir_emit(current_jir_func, JIR_OP_LOAD_SYM, expected_reg, -1, -1, jir_arena);
                    current_jir_func->insts[load_idx].payload.name = lower_make_token_from_cstr(tag_buf);
                    JirReg result_reg = jir_alloc_temp(current_jir_func, &lower_bool_type, jir_arena);
                    jir_emit(current_jir_func,
                             node->op == TOKEN_EQUAL_EQUAL ? JIR_OP_CMP_EQ : JIR_OP_CMP_NEQ,
                             result_reg, tag_reg, expected_reg, jir_arena);
                    return result_reg;
                }
            }
            if (node->op == TOKEN_EQUAL) {
                int r_right = lower_expr(right);
                if (left->kind == HIR_IDENTIFIER) {
                    JirReg dest = lower_find_local(left->token);
                    if (dest >= 0) {
                        jir_emit(current_jir_func, JIR_OP_STORE, dest, r_right >= 0 ? r_right : 0, -1, jir_arena);
                        return dest;
                    }
                    if (current_self_owner && lower_find_struct_field_decl(current_self_owner, left->token)) {
                        Token self_name = { TOKEN_IDENTIFIER, "self", 4, left->line };
                        JirReg self_reg = lower_find_local(self_name);
                        int idx = jir_emit(current_jir_func, JIR_OP_STORE_MEMBER, self_reg, r_right >= 0 ? r_right : 0, -1, jir_arena);
                        current_jir_func->insts[idx].payload.name = left->token;
                        return r_right;
                    }
                    int idx = jir_emit(current_jir_func, JIR_OP_STORE_SYM, -1, r_right >= 0 ? r_right : 0, -1, jir_arena);
                    current_jir_func->insts[idx].payload.name = left->token;
                    return r_right;
                } else if (left->kind == HIR_MEMBER_ACCESS) {
                    int r_obj = lower_expr(hir_child_at(left, 0));
                    int idx = jir_emit(current_jir_func, JIR_OP_STORE_MEMBER, r_obj, r_right, -1, jir_arena);
                    current_jir_func->insts[idx].payload.name = left->token;
                    return r_right;
                } else if (left->kind == HIR_INDEX_EXPR) {
                    int r_obj = lower_expr(hir_child_at(left, 0));
                    int r_idx = lower_expr(hir_child_at(left, 1));
                    jir_emit(current_jir_func, JIR_OP_STORE_MEMBER, r_obj, r_right, r_idx, jir_arena);
                    return r_right;
                }
            }
            int r1 = (left->kind == HIR_ENUM_ACCESS &&
                      left->token.length == 0 &&
                      right->evaluated_type && right->evaluated_type->kind == TYPE_BASE)
                         ? lower_emit_enum_access_with_name(left, right->evaluated_type->as.base_type)
                         : lower_expr(left);
            int r2 = (right->kind == HIR_ENUM_ACCESS &&
                      right->token.length == 0 &&
                      left->evaluated_type && left->evaluated_type->kind == TYPE_BASE)
                         ? lower_emit_enum_access_with_name(right, left->evaluated_type->as.base_type)
                         : lower_expr(right);
            TypeExpr* result_type =
                (node->op == TOKEN_EQUAL_EQUAL || node->op == TOKEN_BANG_EQUAL ||
                 node->op == TOKEN_LESS || node->op == TOKEN_GREATER ||
                 node->op == TOKEN_LESS_EQUAL || node->op == TOKEN_GREATER_EQUAL)
                    ? &lower_bool_type
                    : node->evaluated_type;
            int r = jir_alloc_temp(current_jir_func, result_type, jir_arena);
            JirOp op;
            switch (node->op) {
                case TOKEN_PLUS: op = JIR_OP_ADD; break;
                case TOKEN_MINUS: op = JIR_OP_SUB; break;
                case TOKEN_STAR: op = JIR_OP_MUL; break;
                case TOKEN_SLASH: op = JIR_OP_DIV; break;
                case TOKEN_EQUAL_EQUAL: op = JIR_OP_CMP_EQ; break;
                case TOKEN_BANG_EQUAL: op = JIR_OP_CMP_NEQ; break;
                case TOKEN_LESS: op = JIR_OP_CMP_LT; break;
                case TOKEN_GREATER: op = JIR_OP_CMP_GT; break;
                case TOKEN_LESS_EQUAL: op = JIR_OP_CMP_LTE; break;
                case TOKEN_GREATER_EQUAL: op = JIR_OP_CMP_GTE; break;
                default: op = JIR_OP_ADD; break;
            }
            jir_emit(current_jir_func, op, r, r1 >= 0 ? r1 : 0, r2 >= 0 ? r2 : 0, jir_arena);
            return r;
        }
        case HIR_FUNC_CALL: {
            return lower_call_expr(node, false);
        }
        case HIR_ARRAY_LITERAL: {
            int args[64];
            size_t i = 0;
            for (HIRNode* elem = lower_skip_declared_type(node); elem; elem = hir_next_sibling(elem), i++) args[i] = lower_expr(elem);
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int idx = jir_emit(current_jir_func, JIR_OP_ARRAY_INIT, r, -1, -1, jir_arena);
            if (i > 0) {
                current_jir_func->insts[idx].call_args = malloc(sizeof(int) * i);
                for(size_t j=0; j<i; j++) current_jir_func->insts[idx].call_args[j] = args[j] >= 0 ? args[j] : 0;
                current_jir_func->insts[idx].call_arg_count = i;
            }
            return r;
        }
        case HIR_TUPLE_LITERAL: {
            int args[64];
            size_t i = 0;
            for (HIRNode* elem = hir_first_child(node); elem; elem = hir_next_sibling(elem), i++) args[i] = lower_expr(elem);
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int idx = jir_emit(current_jir_func, JIR_OP_ARRAY_INIT, r, -1, -1, jir_arena);
            if (i > 0) {
                current_jir_func->insts[idx].call_args = malloc(sizeof(int) * i);
                for (size_t j = 0; j < i; j++) current_jir_func->insts[idx].call_args[j] = args[j];
                current_jir_func->insts[idx].call_arg_count = i;
            }
            return r;
        }
        case HIR_STRUCT_INIT_EXPR: {
            int args[64];
            size_t count = 0;
            for (HIRNode* field = lower_skip_declared_type(node); field; field = hir_next_sibling(field)) {
                args[count++] = lower_expr(field);
            }
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int idx = jir_emit(current_jir_func, JIR_OP_STRUCT_INIT, r, -1, -1, jir_arena);
            current_jir_func->insts[idx].payload.name = node->declared_type->as.base_type;
            if (count > 0) {
                current_jir_func->insts[idx].call_args = malloc(sizeof(int) * count);
                current_jir_func->insts[idx].field_names = malloc(sizeof(Token) * count);
                for (size_t i = 0; i < count; i++) {
                    current_jir_func->insts[idx].call_args[i] = args[i];
                    current_jir_func->insts[idx].field_names[i] = node->tokens[i];
                }
                current_jir_func->insts[idx].call_arg_count = count;
                current_jir_func->insts[idx].field_name_count = count;
            }
            return r;
        }
        case HIR_INDEX_EXPR: {
            HIRNode* object = hir_child_at(node, 0);
            HIRNode* index = hir_child_at(node, 1);
            int r_obj = lower_expr(object);
            if (index && index->kind == HIR_RANGE_EXPR) {
                int r_start = hir_child_at(index, 0) ? lower_expr(hir_child_at(index, 0)) : -1;
                int r_end = hir_child_at(index, 1) ? lower_expr(hir_child_at(index, 1)) : -1;
                int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
                int idx = jir_emit(current_jir_func, JIR_OP_MAKE_SLICE, r, r_obj >= 0 ? r_obj : 0, -1, jir_arena);
                if (r_start >= 0) current_jir_func->insts[idx].src2 = r_start;
                if (r_end >= 0) {
                    current_jir_func->insts[idx].call_args = malloc(sizeof(int));
                    current_jir_func->insts[idx].call_args[0] = r_end;
                    current_jir_func->insts[idx].call_arg_count = 1;
                }
                return r;
            }
            int r_idx = lower_expr(index);
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            jir_emit(current_jir_func, JIR_OP_MEMBER_ACCESS, r, r_obj >= 0 ? r_obj : 0, r_idx >= 0 ? r_idx : 0, jir_arena);
            return r;
        }
        case HIR_NEW_EXPR: {
            int r_val = lower_expr(hir_child_at(node, 0));
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            jir_emit(current_jir_func, JIR_OP_MALLOC, r, -1, -1, jir_arena);
            jir_emit(current_jir_func, JIR_OP_STORE_PTR, r, r_val >= 0 ? r_val : 0, -1, jir_arena);
            return r;
        }
        case HIR_MEMBER_ACCESS: {
            HIRNode* object = hir_child_at(node, 0);
            if (object && object->symbol && object->symbol->kind == SYM_NAMESPACE) {
                int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
                int idx = jir_emit(current_jir_func, JIR_OP_LOAD_SYM, r, -1, -1, jir_arena);
                current_jir_func->insts[idx].payload.name = node->token;
                return r;
            }
            if (object &&
                lower_find_enum_decl(lower_unwrap_type(object->evaluated_type)) &&
                node->token.length == 5 &&
                strncmp(node->token.start, "value", 5) == 0) {
                return lower_expr(object);
            }
            if (object &&
                object->kind == HIR_UNARY_EXPR &&
                object->op == TOKEN_DOLLAR &&
                node->token.length == 3 &&
                strncmp(node->token.start, "tag", 3) == 0) {
                int r_obj = lower_expr(hir_child_at(object, 0));
                int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
                jir_emit(current_jir_func, JIR_OP_GET_TAG, r, r_obj, -1, jir_arena);
                return r;
            }
            int r_obj = lower_expr(object);
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int idx = jir_emit(current_jir_func, JIR_OP_MEMBER_ACCESS, r, r_obj >= 0 ? r_obj : 0, -1, jir_arena);
            current_jir_func->insts[idx].payload.name = node->token;
            return r;
        }
        case HIR_UNARY_EXPR: {
            HIRNode* right = hir_child_at(node, 0);
            if (node->op == TOKEN_STAR) {
                int r_right = lower_expr(right);
                int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
                jir_emit(current_jir_func, JIR_OP_DEREF, r, r_right, -1, jir_arena);
                return r;
            }
            if (node->op == TOKEN_DOLLAR) {
                return lower_expr(right);
            }
            return lower_expr(right);
        }
        case HIR_ENUM_ACCESS: {
            Token enum_name = node->token;
            if (enum_name.length == 0 && node->evaluated_type && node->evaluated_type->kind == TYPE_BASE) {
                enum_name = node->evaluated_type->as.base_type;
            }
            if (enum_name.length == 0) {
                enum_name = lower_infer_enum_name_from_member(node->token2);
            }
            TypeExpr* enum_type = node->evaluated_type;
            if (!enum_type && enum_name.length > 0) enum_type = lower_make_base_type_from_token(enum_name);
            int r = jir_alloc_temp(current_jir_func, enum_type, jir_arena);
            int idx = jir_emit(current_jir_func, JIR_OP_LOAD_SYM, r, -1, -1, jir_arena);
            current_jir_func->insts[idx].payload.name =
                jir_enum_constant_name(enum_name, node->token2);
            return r;
        }
        default: return -1;
    }
}

static void lower_emit_function_body(HIRNode* decl, Token emitted_name, TypeExpr* return_type, TypeExpr* self_type, bool self_is_param, HIRNode* self_owner) {
    HIRNode* saved_self_owner = current_self_owner;
    JirFunction* f = jir_function_new(current_jir_mod, emitted_name);
    f->return_type = return_type;
    current_jir_func = f;
    current_self_owner = self_owner;

    HIRNode* body = lower_last_child(decl);
    if (self_is_param && self_type) {
        Token self_name = { TOKEN_IDENTIFIER, "self", 4, decl->line };
        jir_alloc_local(f, self_name, self_type, true, jir_arena);
    }
    for (HIRNode* p = lower_skip_declared_type(decl); p && p != body; p = hir_next_sibling(p)) {
        jir_alloc_local(f, p->token,
                        lower_resolved_type(p->declared_type, p->evaluated_type),
                        true, jir_arena);
    }
    lower_stmt(body);
    current_self_owner = saved_self_owner;
}

JirModule* lower_hir_to_jir(HIRModule* module, Arena* arena) {
    if (!module || !hir_root(module)) return NULL;
    jir_arena = arena;
    current_hir_root = hir_root(module);
    JirModule* mod = jir_module_new(arena);
    current_jir_mod = mod;
    for (HIRNode* child = hir_first_child(hir_root(module)); child; child = hir_next_sibling(child)) {
        if (child->kind == HIR_FUNC_DECL) {
            lower_emit_function_body(child, child->token, child->declared_type, NULL, false, NULL);
        } else if (child->kind == HIR_STRUCT_DECL) {
            TypeExpr* owner_type = lower_make_base_type_from_token(child->token);
            TypeExpr* self_type = lower_make_pointer_type(owner_type);
            for (HIRNode* member = hir_first_child(child); member; member = hir_next_sibling(member)) {
                if (member->kind == HIR_FUNC_DECL) {
                    Token name = lower_join_tokens(child->token, "_", member->token);
                    bool is_static = lower_is_static_struct_method(member);
                    lower_emit_function_body(member, name, member->declared_type, is_static ? NULL : self_type, !is_static, is_static ? NULL : child);
                } else if (member->kind == HIR_INIT_DECL) {
                    Token init_name = lower_join_tokens(child->token, "_", lower_make_token_from_cstr("init"));
                    lower_emit_function_body(member, init_name, &lower_void_type, self_type, true, child);
                }
            }
        }
    }
    Token main_name = {TOKEN_IDENTIFIER, "module_main", 11, 0};
    JirFunction* main_f = jir_function_new(mod, main_name);
    current_jir_func = main_f;
    for (HIRNode* child = hir_first_child(hir_root(module)); child; child = hir_next_sibling(child)) {
        if (child->kind != HIR_FUNC_DECL && child->kind != HIR_STRUCT_DECL && child->kind != HIR_UNION_DECL && child->kind != HIR_IMPORT) {
            lower_stmt(child);
        }
    }
    return mod;
}
