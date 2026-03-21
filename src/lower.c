#include "lower.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static JirModule* current_jir_mod = NULL;
static JirFunction* current_jir_func = NULL;
static Arena* jir_arena = NULL;

static int lower_expr(ASTNode* node);

static void lower_stmt(ASTNode* node) {
    if (!node) return;
    switch (node->type) {
        case AST_VAR_DECL: {
            int r_val = lower_expr(node->as.var_decl.initializer);
            TypeExpr* t = node->as.var_decl.type ? node->as.var_decl.type : (node->as.var_decl.initializer ? node->as.var_decl.initializer->evaluated_type : NULL);
            int r_var = jir_alloc_local(current_jir_func, node->as.var_decl.name, t, false, jir_arena);
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
            jir_emit_label(current_jir_func, l_start, jir_arena);
            int r_cond = lower_expr(node->as.while_stmt.condition);
            int idx = jir_emit(current_jir_func, JIR_OP_JUMP_IF_FALSE, -1, r_cond >= 0 ? r_cond : 0, -1, jir_arena);
            current_jir_func->insts[idx].payload.label_id = l_end;
            lower_stmt(node->as.while_stmt.body);
            int idx2 = jir_emit(current_jir_func, JIR_OP_JUMP, -1, -1, -1, jir_arena);
            current_jir_func->insts[idx2].payload.label_id = l_start;
            jir_emit_label(current_jir_func, l_end, jir_arena);
            break;
        }
        case AST_RETURN_STMT: {
            int r = lower_expr(node->as.return_stmt.expression);
            jir_emit(current_jir_func, JIR_OP_RETURN, -1, r >= 0 ? r : -1, -1, jir_arena);
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
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int idx = jir_emit(current_jir_func, JIR_OP_LOAD_SYM, r, -1, -1, jir_arena);
            current_jir_func->insts[idx].payload.name = node->as.identifier.name;
            return r;
        }
        case AST_BINARY_EXPR: {
            int r1 = lower_expr(node->as.binary.left); int r2 = lower_expr(node->as.binary.right);
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            JirOp op;
            switch (node->as.binary.op) {
                case TOKEN_PLUS: op = JIR_OP_ADD; break;
                case TOKEN_MINUS: op = JIR_OP_SUB; break;
                case TOKEN_STAR: op = JIR_OP_MUL; break;
                case TOKEN_SLASH: op = JIR_OP_DIV; break;
                case TOKEN_EQUAL_EQUAL: op = JIR_OP_CMP_EQ; break;
                default: op = JIR_OP_ADD; break;
            }
            jir_emit(current_jir_func, op, r, r1 >= 0 ? r1 : 0, r2 >= 0 ? r2 : 0, jir_arena);
            return r;
        }
        case AST_FUNC_CALL: {
            int args[32]; for (size_t i = 0; i < node->as.func_call.arg_count; i++) args[i] = lower_expr(node->as.func_call.args[i]);
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            Token callee_name = {TOKEN_IDENTIFIER, "anonymous", 9, 0};
            if (node->as.func_call.callee->type == AST_IDENTIFIER) callee_name = node->as.func_call.callee->as.identifier.name;
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
            int r_obj = lower_expr(node->as.member_access.object);
            int r = jir_alloc_temp(current_jir_func, node->evaluated_type, jir_arena);
            int idx = jir_emit(current_jir_func, JIR_OP_MEMBER_ACCESS, r, r_obj >= 0 ? r_obj : 0, -1, jir_arena);
            current_jir_func->insts[idx].payload.name = node->as.member_access.member;
            return r;
        }
        default: return -1;
    }
}

JirModule* lower_to_jir(ASTNode* root, Arena* arena) {
    jir_arena = arena;
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
