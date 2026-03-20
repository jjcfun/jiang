#include "lower.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static JirReg lower_expression(ASTNode* expr, JirFunction* func, Arena* arena);
static void lower_statement(ASTNode* stmt, JirFunction* func, Arena* arena);

static TypeExpr* ensure_type(TypeExpr* type) {
    if (type) return type;
    static TypeExpr int_t = { .kind = TYPE_BASE };
    int_t.as.base_type = (Token){ .start = "Int", .length = 3 };
    return &int_t;
}

static JirReg lower_expression(ASTNode* expr, JirFunction* func, Arena* arena) {
    if (!expr || !func) return -1;
    TypeExpr* et = ensure_type(expr->evaluated_type);
    switch (expr->type) {
        case AST_LITERAL_NUMBER: {
            JirReg d = jir_alloc_temp(func, et, arena);
            bool is_f = (et->kind == TYPE_BASE && et->as.base_type.length == 5 && strncmp(et->as.base_type.start, "Float", 5) == 0);
            int idx = jir_emit(func, is_f ? JIR_OP_CONST_FLOAT : JIR_OP_CONST_INT, d, -1, -1, arena);
            if (is_f) func->insts[idx].payload.float_val = expr->as.number.value;
            else func->insts[idx].payload.int_val = (int64_t)expr->as.number.value;
            return d;
        }
        case AST_LITERAL_STRING: {
            Token t = {TOKEN_IDENTIFIER, "String", 6, 0}; 
            JirReg d = jir_alloc_temp(func, type_new_base(NULL, t), arena);
            int idx = jir_emit(func, JIR_OP_CONST_STR, d, -1, -1, arena);
            size_t l = expr->as.string.value.length; const char* s = expr->as.string.value.start;
            if (l >= 2 && s[0] == '"') { s++; l -= 2; }
            char* c = arena_alloc(arena, l + 1); memcpy(c, s, l); c[l] = '\0';
            func->insts[idx].payload.str_val = c; return d;
        }
        case AST_IDENTIFIER: {
            for (int i = (int)func->local_count - 1; i >= 0; i--) {
                if (strncmp(func->locals[i].name, expr->as.identifier.name.start, expr->as.identifier.name.length) == 0 &&
                    func->locals[i].name[expr->as.identifier.name.length] == '\0') return (JirReg)i;
            }
            JirReg d = jir_alloc_temp(func, et, arena);
            int idx = jir_emit(func, JIR_OP_LOAD_SYM, d, -1, -1, arena);
            func->insts[idx].payload.name = expr->as.identifier.name; return d;
        }
        case AST_BINARY_EXPR: {
            if (expr->as.binary.op == TOKEN_EQUAL) {
                JirReg v = lower_expression(expr->as.binary.right, func, arena);
                JirReg t = lower_expression(expr->as.binary.left, func, arena);
                if (t >= 0 && v >= 0) jir_emit(func, JIR_OP_STORE, t, v, -1, arena);
                return t;
            }
            if (expr->as.binary.op == TOKEN_EQUAL_EQUAL && expr->as.binary.right && expr->as.binary.right->type == AST_FUNC_CALL) {
                ASTNode* call = expr->as.binary.right;
                if (call->as.func_call.callee->type == AST_MEMBER_ACCESS) {
                    JirReg u = lower_expression(expr->as.binary.left, func, arena);
                    JirReg im = jir_alloc_temp(func, et, arena); JirReg tg = jir_alloc_temp(func, NULL, arena);
                    if (u >= 0) {
                        jir_emit(func, JIR_OP_GET_TAG, tg, u, -1, arena);
                        int idx = jir_emit(func, JIR_OP_CMP_EQ, im, tg, -1, arena);
                        func->insts[idx].payload.name = call->as.func_call.callee->as.member_access.member;
                        for (size_t i = 0; i < call->as.func_call.arg_count; i++) {
                            ASTNode* arg = call->as.func_call.args[i];
                            if (arg->type == AST_PATTERN) {
                                JirReg v = lower_expression(arg, func, arena);
                                int eidx = jir_emit(func, JIR_OP_EXTRACT_FIELD, v, u, -1, arena);
                                func->insts[eidx].payload.name = call->as.func_call.callee->as.member_access.member;
                                func->insts[eidx].src2 = (int32_t)i;
                            }
                        }
                    }
                    return im;
                }
            }
            JirReg l = lower_expression(expr->as.binary.left, func, arena);
            JirReg r = lower_expression(expr->as.binary.right, func, arena);
            JirReg d = jir_alloc_temp(func, et, arena);
            if (l >= 0 && r >= 0) {
                JirOpCode op;
                switch (expr->as.binary.op) {
                    case TOKEN_PLUS: op = JIR_OP_ADD; break;
                    case TOKEN_MINUS: op = JIR_OP_SUB; break;
                    case TOKEN_STAR: op = JIR_OP_MUL; break;
                    case TOKEN_SLASH: op = JIR_OP_DIV; break;
                    case TOKEN_EQUAL_EQUAL: op = JIR_OP_CMP_EQ; break;
                    case TOKEN_BANG_EQUAL: op = JIR_OP_CMP_NEQ; break;
                    case TOKEN_LESS: op = JIR_OP_CMP_LT; break;
                    case TOKEN_GREATER: op = JIR_OP_CMP_GT; break;
                    default: op = JIR_OP_ADD; break;
                }
                jir_emit(func, op, d, l, r, arena);
            }
            return d;
        }
        case AST_MEMBER_ACCESS: {
            JirReg o = lower_expression(expr->as.member_access.object, func, arena);
            JirReg d = jir_alloc_temp(func, et, arena);
            if (o >= 0) {
                int idx = jir_emit(func, JIR_OP_MEMBER_ACCESS, d, o, -1, arena);
                func->insts[idx].payload.name = expr->as.member_access.member;
            }
            return d;
        }
        case AST_FUNC_CALL: {
            size_t c = expr->as.func_call.arg_count; 
            JirReg* args = (c > 0) ? arena_alloc(arena, sizeof(JirReg) * c) : NULL;
            for (size_t i = 0; i < c; i++) args[i] = lower_expression(expr->as.func_call.args[i], func, arena);
            JirReg d = jir_alloc_temp(func, et, arena); Token n = {0};
            if (expr->as.func_call.callee->type == AST_IDENTIFIER) n = expr->as.func_call.callee->as.identifier.name;
            else if (expr->as.func_call.callee->type == AST_MEMBER_ACCESS) n = expr->as.func_call.callee->as.member_access.member;
            return jir_emit_call(func, d, n, args, c, arena);
        }
        case AST_UNARY_EXPR: {
            JirReg r = lower_expression(expr->as.unary.right, func, arena);
            JirReg d = jir_alloc_temp(func, et, arena);
            if (r >= 0) {
                if (expr->as.unary.op == TOKEN_DOLLAR) jir_emit(func, JIR_OP_STORE, d, r, -1, arena);
                else jir_emit(func, (expr->as.unary.op == TOKEN_BANG ? JIR_OP_NOT : JIR_OP_NEG), d, r, -1, arena);
            }
            return d;
        }
        case AST_PATTERN: return jir_alloc_local(func, expr->as.pattern.name, et, false, arena);
        default: return -1;
    }
}

static void lower_statement(ASTNode* stmt, JirFunction* func, Arena* arena) {
    if (!stmt || !func) return;
    switch (stmt->type) {
        case AST_VAR_DECL: {
            JirReg r = jir_alloc_local(func, stmt->as.var_decl.name, stmt->as.var_decl.type, false, arena);
            if (stmt->as.var_decl.initializer) {
                JirReg v = lower_expression(stmt->as.var_decl.initializer, func, arena);
                if (v >= 0) jir_emit(func, JIR_OP_STORE, r, v, -1, arena);
            }
            break;
        }
        case AST_BLOCK: for (size_t i = 0; i < stmt->as.block.count; i++) lower_statement(stmt->as.block.statements[i], func, arena); break;
        case AST_IF_STMT: {
            uint32_t el = func->next_label_id++, en = func->next_label_id++;
            JirReg c = lower_expression(stmt->as.if_stmt.condition, func, arena);
            if (c >= 0) {
                int idx = jir_emit(func, JIR_OP_JUMP_IF_FALSE, -1, c, -1, arena);
                func->insts[idx].payload.label_id = el;
            }
            lower_statement(stmt->as.if_stmt.then_branch, func, arena);
            int jidx = jir_emit(func, JIR_OP_JUMP, -1, -1, -1, arena); func->insts[jidx].payload.label_id = en;
            int lidx = jir_emit(func, JIR_OP_LABEL, -1, -1, -1, arena); func->insts[lidx].payload.label_id = el;
            if (stmt->as.if_stmt.else_branch) lower_statement(stmt->as.if_stmt.else_branch, func, arena);
            int endidx = jir_emit(func, JIR_OP_LABEL, -1, -1, -1, arena); func->insts[endidx].payload.label_id = en;
            break;
        }
        case AST_WHILE_STMT: {
            uint32_t sl = func->next_label_id++, en = func->next_label_id++;
            int sidx = jir_emit(func, JIR_OP_LABEL, -1, -1, -1, arena); func->insts[sidx].payload.label_id = sl;
            JirReg c = lower_expression(stmt->as.while_stmt.condition, func, arena);
            if (c >= 0) {
                int idx = jir_emit(func, JIR_OP_JUMP_IF_FALSE, -1, c, -1, arena);
                func->insts[idx].payload.label_id = en;
            }
            lower_statement(stmt->as.while_stmt.body, func, arena);
            int jidx = jir_emit(func, JIR_OP_JUMP, -1, -1, -1, arena); func->insts[jidx].payload.label_id = sl;
            int endidx = jir_emit(func, JIR_OP_LABEL, -1, -1, -1, arena); func->insts[endidx].payload.label_id = en;
            break;
        }
        case AST_RETURN_STMT: { JirReg v = lower_expression(stmt->as.return_stmt.expression, func, arena); if (v >= 0) jir_emit(func, JIR_OP_RETURN, -1, v, -1, arena); break; }
        case AST_FUNC_CALL: case AST_BINARY_EXPR: lower_expression(stmt, func, arena); break;
        default: break;
    }
}

JirModule* lower_to_jir(ASTNode* root, Arena* arena) {
    JirModule* mod = jir_create_module(arena);
    JirFunction* mf = jir_create_function(mod, (Token){TOKEN_IDENTIFIER, "main", 4, 0}, NULL, arena);
    if (root->type == AST_PROGRAM || root->type == AST_BLOCK) {
        for (size_t i = 0; i < root->as.block.count; i++) {
            ASTNode* n = root->as.block.statements[i];
            if (n->type == AST_FUNC_DECL) {
                JirFunction* f = jir_create_function(mod, n->as.func_decl.name, n->as.func_decl.return_type, arena);
                for (size_t j = 0; j < n->as.func_decl.param_count; j++) {
                    ASTNode* p = n->as.func_decl.params[j];
                    jir_alloc_local(f, p->as.var_decl.name, p->as.var_decl.type, true, arena);
                }
                lower_statement(n->as.func_decl.body, f, arena);
            } else lower_statement(n, mf, arena);
        }
    }
    return mod;
}
