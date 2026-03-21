#include "lower.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static JirModule* current_jir_mod = NULL;
static JirFunction* current_jir_func = NULL;
static Arena* jir_arena = NULL;
static ASTNode* current_ast_root = NULL;
static uint32_t loop_break_labels[64];
static uint32_t loop_continue_labels[64];
static int loop_depth = 0;
static TypeExpr lower_int_type = {
    .kind = TYPE_BASE,
    .as.base_type = { TOKEN_IDENTIFIER, "Int", 3, 0 }
};

static int lower_expr(ASTNode* node);
static JirReg lower_get_or_create_local(Token name, TypeExpr* type);

static bool lower_is_void_type(TypeExpr* type) {
    return type &&
           type->kind == TYPE_BASE &&
           type->as.base_type.length == 2 &&
           strncmp(type->as.base_type.start, "()", 2) == 0;
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

typedef struct {
    ASTNode* union_decl;
    ASTNode* variant_decl;
    TypeExpr* variant_type;
    Token variant_name;
    Token tag_name;
} LowerVariantInfo;

static ASTNode* lower_find_union_decl(TypeExpr* type) {
    if (!current_ast_root || !type || type->kind != TYPE_BASE) return NULL;
    for (size_t i = 0; i < current_ast_root->as.block.count; i++) {
        ASTNode* stmt = current_ast_root->as.block.statements[i];
        if (stmt->type != AST_UNION_DECL) continue;
        if (stmt->as.union_decl.name.length == type->as.base_type.length &&
            strncmp(stmt->as.union_decl.name.start, type->as.base_type.start, type->as.base_type.length) == 0) {
            return stmt;
        }
    }
    return NULL;
}

static bool lower_resolve_variant_info(ASTNode* switch_expr, ASTNode* pattern, LowerVariantInfo* info) {
    if (!switch_expr || !pattern || pattern->type != AST_FUNC_CALL || !pattern->as.func_call.callee ||
        pattern->as.func_call.callee->type != AST_MEMBER_ACCESS) {
        return false;
    }

    TypeExpr* switch_type = switch_expr->evaluated_type;
    if (!switch_type || switch_type->kind != TYPE_BASE) return false;

    ASTNode* union_decl = lower_find_union_decl(switch_type);
    if (!union_decl) return false;

    Token variant_name = pattern->as.func_call.callee->as.member_access.member;
    ASTNode* variant_decl = NULL;
    for (size_t i = 0; i < union_decl->as.union_decl.member_count; i++) {
        ASTNode* member = union_decl->as.union_decl.members[i];
        if (member->as.var_decl.name.length == variant_name.length &&
            strncmp(member->as.var_decl.name.start, variant_name.start, variant_name.length) == 0) {
            variant_decl = member;
            break;
        }
    }
    if (!variant_decl) return false;

    char tag_buf[160];
    if (union_decl->as.union_decl.tag_enum.length > 0) {
        snprintf(tag_buf, sizeof(tag_buf), "%.*s_%.*s",
                 (int)union_decl->as.union_decl.tag_enum.length, union_decl->as.union_decl.tag_enum.start,
                 (int)variant_name.length, variant_name.start);
    } else {
        snprintf(tag_buf, sizeof(tag_buf), "%.*sTag_%.*s",
                 (int)union_decl->as.union_decl.name.length, union_decl->as.union_decl.name.start,
                 (int)variant_name.length, variant_name.start);
    }

    info->union_decl = union_decl;
    info->variant_decl = variant_decl;
    info->variant_type = variant_decl->as.var_decl.type;
    info->variant_name = variant_name;
    info->tag_name = lower_make_token_from_cstr(tag_buf);
    return true;
}

static void lower_emit_variant_bindings(JirReg switch_reg, ASTNode* pattern, const LowerVariantInfo* info) {
    if (!pattern || pattern->type != AST_FUNC_CALL || !info || !info->variant_decl) return;

    JirReg variant_reg = jir_alloc_temp(current_jir_func, info->variant_type, jir_arena);
    int variant_idx = jir_emit(current_jir_func, JIR_OP_MEMBER_ACCESS, variant_reg, switch_reg, -1, jir_arena);
    current_jir_func->insts[variant_idx].payload.name = info->variant_name;

    for (size_t i = 0; i < pattern->as.func_call.arg_count; i++) {
        ASTNode* binding = pattern->as.func_call.args[i];
        if (binding->type != AST_PATTERN) continue;

        TypeExpr* binding_type = binding->evaluated_type ? binding->evaluated_type : binding->as.pattern.type;
        JirReg source_reg = variant_reg;
        if (info->variant_type && info->variant_type->kind == TYPE_TUPLE) {
            source_reg = jir_alloc_temp(current_jir_func, binding_type, jir_arena);
            int field_idx = jir_emit(current_jir_func, JIR_OP_EXTRACT_FIELD, source_reg, variant_reg, -1, jir_arena);
            current_jir_func->insts[field_idx].payload.int_val = (int64_t)i;
        }

        JirReg target_reg = lower_get_or_create_local(binding->as.pattern.name, binding_type);
        jir_emit(current_jir_func, JIR_OP_STORE, target_reg, source_reg, -1, jir_arena);
    }
}

static JirReg lower_get_or_create_local(Token name, TypeExpr* type) {
    JirReg existing = lower_find_local(name);
    if (existing >= 0) return existing;
    return jir_alloc_local(current_jir_func, name, type, false, jir_arena);
}

static void lower_stmt(ASTNode* node) {
    if (!node) return;
    switch (node->type) {
        case AST_VAR_DECL: {
            int r_val = lower_expr(node->as.var_decl.initializer);
            TypeExpr* t = node->as.var_decl.type ? node->as.var_decl.type : (node->as.var_decl.initializer ? node->as.var_decl.initializer->evaluated_type : NULL);
            int r_var = lower_get_or_create_local(node->as.var_decl.name, t);
            jir_emit(current_jir_func, JIR_OP_STORE, r_var, r_val >= 0 ? r_val : 0, -1, jir_arena);
            break;
        }
        case AST_IF_STMT: {
            int r_cond = lower_expr(node->as.if_stmt.condition);
            uint32_t l_else = jir_reserve_label(current_jir_func);
            uint32_t l_end = jir_reserve_label(current_jir_func);
            int idx = jir_emit(current_jir_func, JIR_OP_JUMP_IF_FALSE, -1, r_cond >= 0 ? r_cond : 0, -1, jir_arena);
            current_jir_func->insts[idx].payload.label_id = l_else;
            lower_stmt(node->as.if_stmt.then_branch);
            int idx2 = jir_emit(current_jir_func, JIR_OP_JUMP, -1, -1, -1, jir_arena);
            current_jir_func->insts[idx2].payload.label_id = l_end;
            jir_emit_label(current_jir_func, l_else, jir_arena);
            if (node->as.if_stmt.else_branch) lower_stmt(node->as.if_stmt.else_branch);
            jir_emit_label(current_jir_func, l_end, jir_arena);
            break;
        }
        case AST_WHILE_STMT: {
            uint32_t l_start = jir_reserve_label(current_jir_func);
            uint32_t l_end = jir_reserve_label(current_jir_func);
            loop_break_labels[loop_depth] = l_end;
            loop_continue_labels[loop_depth] = l_start;
            loop_depth++;
            jir_emit_label(current_jir_func, l_start, jir_arena);
            int r_cond = lower_expr(node->as.while_stmt.condition);
            int idx = jir_emit(current_jir_func, JIR_OP_JUMP_IF_FALSE, -1, r_cond >= 0 ? r_cond : 0, -1, jir_arena);
            current_jir_func->insts[idx].payload.label_id = l_end;
            lower_stmt(node->as.while_stmt.body);
            int idx2 = jir_emit(current_jir_func, JIR_OP_JUMP, -1, -1, -1, jir_arena);
            current_jir_func->insts[idx2].payload.label_id = l_start;
            jir_emit_label(current_jir_func, l_end, jir_arena);
            loop_depth--;
            break;
        }
        case AST_FOR_STMT: {
            if (node->as.for_stmt.iterable && node->as.for_stmt.iterable->type == AST_RANGE_EXPR) {
                ASTNode* pattern = node->as.for_stmt.pattern;
                if (pattern && pattern->type == AST_BINDING_LIST && pattern->as.binding_list.count > 0) {
                    pattern = pattern->as.binding_list.items[0];
                }
                if (pattern && pattern->type == AST_PATTERN) {
                    JirReg start_reg = lower_expr(node->as.for_stmt.iterable->as.range.start);
                    JirReg end_reg = lower_expr(node->as.for_stmt.iterable->as.range.end);
                    TypeExpr* pattern_type = pattern->evaluated_type ? pattern->evaluated_type : pattern->as.pattern.type;
                    JirReg iter_reg = lower_get_or_create_local(pattern->as.pattern.name, pattern_type);
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

                    lower_stmt(node->as.for_stmt.body);

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
        case AST_SWITCH_STMT: {
            JirReg switch_reg = lower_expr(node->as.switch_stmt.expression);
            uint32_t end_label = jir_reserve_label(current_jir_func);
            uint32_t next_label = 0;

            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                ASTNode* case_node = node->as.switch_stmt.cases[i];
                LowerVariantInfo info = {0};
                bool has_variant = lower_resolve_variant_info(node->as.switch_stmt.expression,
                                                              case_node->as.switch_case.pattern, &info);

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
                    cond_reg = lower_expr(case_node->as.switch_case.pattern);
                }

                next_label = jir_reserve_label(current_jir_func);
                int jmp_idx = jir_emit(current_jir_func, JIR_OP_JUMP_IF_FALSE, -1, cond_reg, -1, jir_arena);
                current_jir_func->insts[jmp_idx].payload.label_id = next_label;

                if (has_variant) {
                    lower_emit_variant_bindings(switch_reg, case_node->as.switch_case.pattern, &info);
                }
                lower_stmt(case_node->as.switch_case.body);
                int end_idx = jir_emit(current_jir_func, JIR_OP_JUMP, -1, -1, -1, jir_arena);
                current_jir_func->insts[end_idx].payload.label_id = end_label;
                jir_emit_label(current_jir_func, next_label, jir_arena);
            }

            if (node->as.switch_stmt.else_branch) {
                lower_stmt(node->as.switch_stmt.else_branch);
            }
            jir_emit_label(current_jir_func, end_label, jir_arena);
            break;
        }
        case AST_RETURN_STMT: {
            int r = lower_expr(node->as.return_stmt.expression);
            jir_emit(current_jir_func, JIR_OP_RETURN, -1, r >= 0 ? r : -1, -1, jir_arena);
            break;
        }
        case AST_BINDING_ASSIGN: {
            JirReg value_reg = lower_expr(node->as.binding_assign.value);
            for (size_t i = 0; i < node->as.binding_assign.bindings->as.binding_list.count; i++) {
                ASTNode* binding = node->as.binding_assign.bindings->as.binding_list.items[i];
                TypeExpr* binding_type = binding->evaluated_type ? binding->evaluated_type : binding->as.pattern.type;
                JirReg target_reg = lower_get_or_create_local(binding->as.pattern.name, binding_type);
                JirReg field_reg = jir_alloc_temp(current_jir_func, binding_type, jir_arena);
                int field_idx = jir_emit(current_jir_func, JIR_OP_EXTRACT_FIELD, field_reg, value_reg, -1, jir_arena);
                current_jir_func->insts[field_idx].payload.int_val = (int64_t)i;
                jir_emit(current_jir_func, JIR_OP_STORE, target_reg, field_reg, -1, jir_arena);
            }
            break;
        }
        case AST_BREAK_STMT: {
            if (loop_depth > 0) {
                int idx = jir_emit(current_jir_func, JIR_OP_JUMP, -1, -1, -1, jir_arena);
                current_jir_func->insts[idx].payload.label_id = loop_break_labels[loop_depth - 1];
            }
            break;
        }
        case AST_CONTINUE_STMT: {
            if (loop_depth > 0) {
                int idx = jir_emit(current_jir_func, JIR_OP_JUMP, -1, -1, -1, jir_arena);
                current_jir_func->insts[idx].payload.label_id = loop_continue_labels[loop_depth - 1];
            }
            break;
        }
        case AST_BLOCK:
            for (size_t i = 0; i < node->as.block.count; i++) lower_stmt(node->as.block.statements[i]);
            break;
        default: lower_expr(node); break;
    }
}

static int lower_expr(ASTNode* node) {
    if (!node) return -1;
    switch (node->type) {
        case AST_LITERAL_NUMBER: {
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int idx = jir_emit(current_jir_func, JIR_OP_CONST_INT, r, -1, -1, jir_arena);
            current_jir_func->insts[idx].payload.int_val = (int64_t)node->as.number.value;
            return r;
        }
        case AST_LITERAL_STRING: {
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int len = (int)node->as.string.value.length; const char* start = node->as.string.value.start;
            if (len >= 2 && start[0] == '"') { start++; len -= 2; }
            char* s = malloc(len + 1); memcpy(s, start, len); s[len] = '\0';
            int idx = jir_emit(current_jir_func, JIR_OP_CONST_STR, r, -1, -1, jir_arena);
            current_jir_func->insts[idx].payload.str_val = s;
            return r;
        }
        case AST_IDENTIFIER: {
            JirReg existing = lower_find_local(node->as.identifier.name);
            if (existing >= 0) return existing;
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int idx = jir_emit(current_jir_func, JIR_OP_LOAD_SYM, r, -1, -1, jir_arena);
            current_jir_func->insts[idx].payload.name = node->as.identifier.name;
            return r;
        }
        case AST_BINARY_EXPR: {
            if (node->as.binary.op == TOKEN_EQUAL) {
                ASTNode* left = node->as.binary.left;
                int r_right = lower_expr(node->as.binary.right);
                if (left->type == AST_IDENTIFIER) {
                    JirReg dest = lower_find_local(left->as.identifier.name);
                    if (dest >= 0) {
                        jir_emit(current_jir_func, JIR_OP_STORE, dest, r_right >= 0 ? r_right : 0, -1, jir_arena);
                        return dest;
                    }
                    int idx = jir_emit(current_jir_func, JIR_OP_STORE_SYM, -1, r_right >= 0 ? r_right : 0, -1, jir_arena);
                    current_jir_func->insts[idx].payload.name = left->as.identifier.name;
                    return r_right;
                } else if (left->type == AST_MEMBER_ACCESS) {
                    int r_obj = lower_expr(left->as.member_access.object);
                    int idx = jir_emit(current_jir_func, JIR_OP_STORE_MEMBER, r_obj, r_right, -1, jir_arena);
                    current_jir_func->insts[idx].payload.name = left->as.member_access.member;
                    return r_right;
                } else if (left->type == AST_INDEX_EXPR) {
                    int r_obj = lower_expr(left->as.index_expr.object);
                    int r_idx = lower_expr(left->as.index_expr.index);
                    jir_emit(current_jir_func, JIR_OP_STORE_MEMBER, r_obj, r_right, r_idx, jir_arena);
                    return r_right;
                }
            }
            int r1 = lower_expr(node->as.binary.left); int r2 = lower_expr(node->as.binary.right);
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            JirOp op;
            switch (node->as.binary.op) {
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
        case AST_FUNC_CALL: {
            int args[32]; for (size_t i = 0; i < node->as.func_call.arg_count; i++) args[i] = lower_expr(node->as.func_call.args[i]);
            int r = lower_is_void_type(node->evaluated_type) ? -1 : jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            Token callee_name = {TOKEN_IDENTIFIER, "anonymous", 9, 0};
            if (node->as.func_call.callee->type == AST_IDENTIFIER) {
                callee_name = node->as.func_call.callee->as.identifier.name;
            } else if (node->as.func_call.callee->type == AST_MEMBER_ACCESS) {
                ASTNode* obj = node->as.func_call.callee->as.member_access.object;
                Token member = node->as.func_call.callee->as.member_access.member;
                if (obj->symbol && obj->symbol->kind == SYM_NAMESPACE) {
                    callee_name = member;
                } else if ((obj->symbol && obj->symbol->kind == SYM_STRUCT && obj->symbol->type && obj->symbol->type->kind == TYPE_BASE) ||
                           (obj->type == AST_IDENTIFIER && obj->evaluated_type && obj->evaluated_type->kind == TYPE_BASE)) {
                    Token type_name = {0};
                    if (obj->symbol && obj->symbol->type && obj->symbol->type->kind == TYPE_BASE) {
                        type_name = obj->symbol->type->as.base_type;
                    } else if (obj->type == AST_IDENTIFIER && obj->evaluated_type && obj->evaluated_type->kind == TYPE_BASE) {
                        type_name = obj->evaluated_type->as.base_type;
                    }
                    if (type_name.length > 0) {
                        char name_buf[160];
                        snprintf(name_buf, sizeof(name_buf), "%.*s_%.*s",
                                 (int)type_name.length, type_name.start,
                                 (int)member.length, member.start);
                        callee_name = lower_make_token_from_cstr(name_buf);
                    }
                }
            }
            jir_emit_call(current_jir_func, r, callee_name, args, node->as.func_call.arg_count, jir_arena);
            return r;
        }
        case AST_ARRAY_LITERAL: {
            int args[64]; for (size_t i = 0; i < node->as.array_literal.element_count; i++) args[i] = lower_expr(node->as.array_literal.elements[i]);
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int idx = jir_emit(current_jir_func, JIR_OP_ARRAY_INIT, r, -1, -1, jir_arena);
            if (node->as.array_literal.element_count > 0) {
                current_jir_func->insts[idx].call_args = malloc(sizeof(int) * node->as.array_literal.element_count);
                for(size_t i=0; i<node->as.array_literal.element_count; i++) current_jir_func->insts[idx].call_args[i] = args[i] >= 0 ? args[i] : 0;
                current_jir_func->insts[idx].call_arg_count = node->as.array_literal.element_count;
            }
            return r;
        }
        case AST_TUPLE_LITERAL: {
            int args[64];
            for (size_t i = 0; i < node->as.tuple_literal.count; i++) args[i] = lower_expr(node->as.tuple_literal.elements[i]);
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int idx = jir_emit(current_jir_func, JIR_OP_ARRAY_INIT, r, -1, -1, jir_arena);
            if (node->as.tuple_literal.count > 0) {
                current_jir_func->insts[idx].call_args = malloc(sizeof(int) * node->as.tuple_literal.count);
                for (size_t i = 0; i < node->as.tuple_literal.count; i++) current_jir_func->insts[idx].call_args[i] = args[i];
                current_jir_func->insts[idx].call_arg_count = node->as.tuple_literal.count;
            }
            return r;
        }
        case AST_STRUCT_INIT_EXPR: {
            int args[64];
            for (size_t i = 0; i < node->as.struct_init.field_count; i++) {
                args[i] = lower_expr(node->as.struct_init.field_values[i]);
            }
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int idx = jir_emit(current_jir_func, JIR_OP_STRUCT_INIT, r, -1, -1, jir_arena);
            current_jir_func->insts[idx].payload.name = node->as.struct_init.type->as.base_type;
            if (node->as.struct_init.field_count > 0) {
                current_jir_func->insts[idx].call_args = malloc(sizeof(int) * node->as.struct_init.field_count);
                current_jir_func->insts[idx].field_names = malloc(sizeof(Token) * node->as.struct_init.field_count);
                for (size_t i = 0; i < node->as.struct_init.field_count; i++) {
                    current_jir_func->insts[idx].call_args[i] = args[i];
                    current_jir_func->insts[idx].field_names[i] = node->as.struct_init.field_names[i];
                }
                current_jir_func->insts[idx].call_arg_count = node->as.struct_init.field_count;
                current_jir_func->insts[idx].field_name_count = node->as.struct_init.field_count;
            }
            return r;
        }
        case AST_INDEX_EXPR: {
            int r_obj = lower_expr(node->as.index_expr.object); int r_idx = lower_expr(node->as.index_expr.index);
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            jir_emit(current_jir_func, JIR_OP_MEMBER_ACCESS, r, r_obj >= 0 ? r_obj : 0, r_idx >= 0 ? r_idx : 0, jir_arena);
            return r;
        }
        case AST_NEW_EXPR: {
            int r_val = lower_expr(node->as.new_expr.value);
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            jir_emit(current_jir_func, JIR_OP_MALLOC, r, -1, -1, jir_arena);
            jir_emit(current_jir_func, JIR_OP_STORE_PTR, r, r_val >= 0 ? r_val : 0, -1, jir_arena);
            return r;
        }
        case AST_MEMBER_ACCESS: {
            if (node->as.member_access.object &&
                node->as.member_access.object->symbol &&
                node->as.member_access.object->symbol->kind == SYM_NAMESPACE) {
                int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
                int idx = jir_emit(current_jir_func, JIR_OP_LOAD_SYM, r, -1, -1, jir_arena);
                current_jir_func->insts[idx].payload.name = node->as.member_access.member;
                return r;
            }
            if (node->as.member_access.object &&
                node->as.member_access.object->type == AST_UNARY_EXPR &&
                node->as.member_access.object->as.unary.op == TOKEN_DOLLAR &&
                node->as.member_access.member.length == 3 &&
                strncmp(node->as.member_access.member.start, "tag", 3) == 0) {
                int r_obj = lower_expr(node->as.member_access.object->as.unary.right);
                int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
                jir_emit(current_jir_func, JIR_OP_GET_TAG, r, r_obj, -1, jir_arena);
                return r;
            }
            int r_obj = lower_expr(node->as.member_access.object);
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int idx = jir_emit(current_jir_func, JIR_OP_MEMBER_ACCESS, r, r_obj >= 0 ? r_obj : 0, -1, jir_arena);
            current_jir_func->insts[idx].payload.name = node->as.member_access.member;
            return r;
        }
        case AST_UNARY_EXPR: {
            if (node->as.unary.op == TOKEN_STAR) {
                int r_right = lower_expr(node->as.unary.right);
                int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
                jir_emit(current_jir_func, JIR_OP_DEREF, r, r_right, -1, jir_arena);
                return r;
            }
            if (node->as.unary.op == TOKEN_DOLLAR) {
                return lower_expr(node->as.unary.right);
            }
            return lower_expr(node->as.unary.right);
        }
        case AST_ENUM_ACCESS: {
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int idx = jir_emit(current_jir_func, JIR_OP_LOAD_SYM, r, -1, -1, jir_arena);
            current_jir_func->insts[idx].payload.name = node->as.enum_access.member_name;
            return r;
        }
        default: return -1;
    }
}

JirModule* lower_to_jir(ASTNode* root, Arena* arena) {
    jir_arena = arena;
    current_ast_root = root;
    JirModule* mod = jir_module_new(arena);
    current_jir_mod = mod;
    for (size_t i = 0; i < root->as.block.count; i++) {
        ASTNode* stmt = root->as.block.statements[i];
        if (stmt->type == AST_FUNC_DECL) {
            JirFunction* f = jir_function_new(mod, stmt->as.func_decl.name);
            f->return_type = stmt->as.func_decl.return_type;
            current_jir_func = f;
            for (size_t j = 0; j < stmt->as.func_decl.param_count; j++) {
                ASTNode* p = stmt->as.func_decl.params[j];
                jir_alloc_local(f, p->as.var_decl.name, p->as.var_decl.type, true, arena);
            }
            lower_stmt(stmt->as.func_decl.body);
        }
    }
    Token main_name = {TOKEN_IDENTIFIER, "module_main", 11, 0};
    JirFunction* main_f = jir_function_new(mod, main_name);
    current_jir_func = main_f;
    for (size_t i = 0; i < root->as.block.count; i++) {
        ASTNode* stmt = root->as.block.statements[i];
        if (stmt->type != AST_FUNC_DECL && stmt->type != AST_STRUCT_DECL && stmt->type != AST_UNION_DECL && stmt->type != AST_IMPORT) {
            lower_stmt(stmt);
        }
    }
    return mod;
}
