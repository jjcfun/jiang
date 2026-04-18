#include "lower.h"

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
} LowerContext;

static void binding_list_push(HirBindingList* list, HirBinding* binding) {
    if (list->count == list->capacity) {
        int next_capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        list->items = (HirBinding**)realloc(list->items, (size_t)next_capacity * sizeof(HirBinding*));
        list->capacity = next_capacity;
    }
    list->items[list->count++] = binding;
}

static void stmt_list_push(HirStmtList* list, HirStmt* stmt) {
    if (list->count == list->capacity) {
        int next_capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        list->items = (HirStmt**)realloc(list->items, (size_t)next_capacity * sizeof(HirStmt*));
        list->capacity = next_capacity;
    }
    list->items[list->count++] = stmt;
}

static void expr_list_push(HirExprList* list, HirExpr* expr) {
    if (list->count == list->capacity) {
        int next_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        list->items = (HirExpr**)realloc(list->items, (size_t)next_capacity * sizeof(HirExpr*));
        list->capacity = next_capacity;
    }
    list->items[list->count++] = expr;
}

static void function_list_push(HirFunctionList* list, HirFunction fn) {
    if (list->count == list->capacity) {
        int next_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        list->items = (HirFunction*)realloc(list->items, (size_t)next_capacity * sizeof(HirFunction));
        list->capacity = next_capacity;
    }
    list->items[list->count++] = fn;
}

static void global_list_push(HirGlobalList* list, HirGlobal global) {
    if (list->count == list->capacity) {
        int next_capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        list->items = (HirGlobal*)realloc(list->items, (size_t)next_capacity * sizeof(HirGlobal));
        list->capacity = next_capacity;
    }
    list->items[list->count++] = global;
}

static HirTypeKind lower_type(AstTypeKind type) {
    return type == AST_TYPE_BOOL ? HIR_TYPE_BOOL : HIR_TYPE_INT;
}

static HirBinding* new_binding(HirTypeKind type, const char* name, HirBindingKind kind, int line) {
    HirBinding* binding = (HirBinding*)calloc(1, sizeof(HirBinding));
    binding->type = type;
    binding->name = (char*)name;
    binding->kind = kind;
    binding->line = line;
    return binding;
}

static HirExpr* new_expr(HirExprKind kind, HirTypeKind type, int line) {
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

static int fail(LowerContext* ctx, const char* error) {
    ctx->error = error;
    return 0;
}

static HirFunction* find_function(HirProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        if (strcmp(program->functions.items[i].name, name) == 0) {
            return &program->functions.items[i];
        }
    }
    return 0;
}

static HirGlobal* find_global(HirProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->globals.count; ++i) {
        if (strcmp(program->globals.items[i].binding->name, name) == 0) {
            return &program->globals.items[i];
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
                return scope->bindings.items[i];
            }
        }
        scope = scope->parent;
    }
    if (find_global(ctx->program, name)) {
        return find_global(ctx->program, name)->binding;
    }
    return 0;
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
        out->type = HIR_TYPE_INT;
        return 1;
    }
    if (expr->as.call.args.count != 1) return fail(ctx, builtin == HIR_BUILTIN_ASSERT ? "assert expects exactly one argument" : "print expects exactly one argument");
    arg = lower_expr(ctx, expr->as.call.args.items[0]);
    if (!arg) return 0;
    if (builtin == HIR_BUILTIN_ASSERT) { if (arg->type != HIR_TYPE_BOOL) return fail(ctx, "assert requires a Bool argument"); }
    else if (arg->type != HIR_TYPE_INT && arg->type != HIR_TYPE_BOOL) return fail(ctx, "print requires an Int or Bool argument");
    expr_list_push(&out->as.call.args, arg);
    out->type = HIR_TYPE_INT;
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
        if (arg->type != callee->params.items[i]->type) {
            return fail(ctx, "call argument type mismatch");
        }
        expr_list_push(out_args, arg);
    }
    return 1;
}

static HirExpr* lower_expr(LowerContext* ctx, const AstExpr* expr) {
    HirExpr* out = 0;
    switch (expr->kind) {
        case AST_EXPR_INT:
            out = new_expr(HIR_EXPR_INT, HIR_TYPE_INT, expr->line);
            out->as.int_value = expr->as.int_value;
            return out;
        case AST_EXPR_BOOL:
            out = new_expr(HIR_EXPR_BOOL, HIR_TYPE_BOOL, expr->line);
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
        case AST_EXPR_CALL: {
            HirBuiltinKind builtin = builtin_kind(expr->as.call.callee);
            if (builtin != HIR_BUILTIN_NONE) {
                out = new_expr(HIR_EXPR_CALL, HIR_TYPE_INT, expr->line);
                if (!lower_builtin_call(ctx, expr, out, builtin)) return 0;
                return out;
            }
            HirFunction* callee = find_function(ctx->program, expr->as.call.callee);
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
            out = new_expr(HIR_EXPR_BINARY, HIR_TYPE_INT, expr->line);
            out->as.binary.left = left;
            out->as.binary.right = right;
            switch (expr->as.binary.op) {
                case AST_BIN_ADD: out->as.binary.op = HIR_BIN_ADD; break;
                case AST_BIN_SUB: out->as.binary.op = HIR_BIN_SUB; break;
                case AST_BIN_MUL: out->as.binary.op = HIR_BIN_MUL; break;
                case AST_BIN_DIV: out->as.binary.op = HIR_BIN_DIV; break;
                case AST_BIN_EQ: out->as.binary.op = HIR_BIN_EQ; out->type = HIR_TYPE_BOOL; break;
                case AST_BIN_NE: out->as.binary.op = HIR_BIN_NE; out->type = HIR_TYPE_BOOL; break;
                case AST_BIN_LT: out->as.binary.op = HIR_BIN_LT; out->type = HIR_TYPE_BOOL; break;
                case AST_BIN_LE: out->as.binary.op = HIR_BIN_LE; out->type = HIR_TYPE_BOOL; break;
                case AST_BIN_GT: out->as.binary.op = HIR_BIN_GT; out->type = HIR_TYPE_BOOL; break;
                case AST_BIN_GE: out->as.binary.op = HIR_BIN_GE; out->type = HIR_TYPE_BOOL; break;
            }
            if (expr->as.binary.op == AST_BIN_ADD || expr->as.binary.op == AST_BIN_SUB || expr->as.binary.op == AST_BIN_MUL || expr->as.binary.op == AST_BIN_DIV) {
                if (left->type != HIR_TYPE_INT || right->type != HIR_TYPE_INT) {
                    fail(ctx, "arithmetic requires Int operands");
                    return 0;
                }
            } else {
                if (left->type != right->type) {
                    fail(ctx, "comparison requires matching operand types");
                    return 0;
                }
                if (left->type != HIR_TYPE_INT && left->type != HIR_TYPE_BOOL) {
                    fail(ctx, "comparison operand type unsupported");
                    return 0;
                }
            }
            return out;
        }
    }
    fail(ctx, "unsupported expression kind");
    return 0;
}

static int lower_block(LowerContext* ctx, const AstBlock* ast_block, HirBlock* out_block);

static HirStmt* lower_stmt(LowerContext* ctx, const AstStmt* stmt) {
    HirStmt* out = new_stmt((HirStmtKind)stmt->kind, stmt->line);
    switch (stmt->kind) {
        case AST_STMT_RETURN:
            out->as.ret.expr = lower_expr(ctx, stmt->as.ret.expr);
            if (!out->as.ret.expr) {
                return 0;
            }
            if (out->as.ret.expr->type != ctx->current_function->return_type) {
                fail(ctx, "return type mismatch");
                return 0;
            }
            return out;
        case AST_STMT_VAR_DECL: {
            HirBinding* binding = 0;
            HirExpr* init = lower_expr(ctx, stmt->as.var_decl.init);
            HirTypeKind type = HIR_TYPE_INT;
            if (!init) {
                return 0;
            }
            if (stmt->as.var_decl.type.kind == AST_TYPE_INFER) {
                type = init->type;
            } else {
                type = lower_type(stmt->as.var_decl.type.kind);
                if (init->type != type) {
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
            HirBinding* binding = lookup_binding(ctx, stmt->as.assign.name);
            if (!binding) {
                fail(ctx, "assignment target not found");
                return 0;
            }
            out->as.assign.binding = binding;
            out->as.assign.value = lower_expr(ctx, stmt->as.assign.value);
            if (!out->as.assign.value) {
                return 0;
            }
            if (out->as.assign.value->type != binding->type) {
                fail(ctx, "assignment type mismatch");
                return 0;
            }
            return out;
        }
        case AST_STMT_IF: {
            Scope inner;
            out->as.if_stmt.cond = lower_expr(ctx, stmt->as.if_stmt.cond);
            if (!out->as.if_stmt.cond) {
                return 0;
            }
            if (out->as.if_stmt.cond->type != HIR_TYPE_BOOL) {
                fail(ctx, "if condition must be Bool");
                return 0;
            }
            push_scope(ctx, &inner);
            if (!lower_block(ctx, &stmt->as.if_stmt.then_block, &out->as.if_stmt.then_block)) {
                return 0;
            }
            pop_scope(ctx);
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
            if (out->as.while_stmt.cond->type != HIR_TYPE_BOOL) {
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
            HirTypeKind type = lower_type(stmt->as.for_range.type.kind);
            if (stmt->as.for_range.type.kind == AST_TYPE_INFER) {
                fail(ctx, "for-range variable type cannot be inferred");
                return 0;
            }
            if (!start) {
                return 0;
            }
            end = lower_expr(ctx, stmt->as.for_range.end);
            if (!end) {
                return 0;
            }
            if (type != HIR_TYPE_INT || start->type != HIR_TYPE_INT || end->type != HIR_TYPE_INT) {
                fail(ctx, "for-range currently requires Int bounds");
                return 0;
            }
            binding = new_binding(HIR_TYPE_INT, stmt->as.for_range.name, HIR_BINDING_LOCAL, stmt->line);
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
    }
    fail(ctx, "unsupported statement kind");
    return 0;
}

static int lower_block(LowerContext* ctx, const AstBlock* ast_block, HirBlock* out_block) {
    int i = 0;
    for (i = 0; i < ast_block->stmts.count; ++i) {
        HirStmt* stmt = lower_stmt(ctx, ast_block->stmts.items[i]);
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
        HirTypeKind type = HIR_TYPE_VOID;
        memset(&hir_global, 0, sizeof(hir_global));
        if (find_global(ctx->program, ast_global->name)) {
            return fail(ctx, "duplicate global");
        }
        if (ast_global->type.kind != AST_TYPE_INFER) {
            type = lower_type(ast_global->type.kind);
        }
        hir_global.binding = new_binding(type, ast_global->name, HIR_BINDING_GLOBAL, ast_global->line);
        hir_global.line = ast_global->line;
        global_list_push(&ctx->program->globals, hir_global);
    }
    return 1;
}

static int register_functions(LowerContext* ctx) {
    int i = 0;
    for (i = 0; i < ctx->ast->functions.count; ++i) {
        const AstFunction* ast_fn = &ctx->ast->functions.items[i];
        HirFunction hir_fn;
        int param_index = 0;
        memset(&hir_fn, 0, sizeof(hir_fn));
        if (find_function(ctx->program, ast_fn->name)) {
            return fail(ctx, "duplicate function");
        }
        hir_fn.return_type = lower_type(ast_fn->return_type.kind);
        hir_fn.name = ast_fn->name;
        hir_fn.line = ast_fn->line;
        for (param_index = 0; param_index < ast_fn->params.count; ++param_index) {
            HirBinding* param_binding = new_binding(lower_type(ast_fn->params.items[param_index].type.kind), ast_fn->params.items[param_index].name, HIR_BINDING_PARAM, ast_fn->params.items[param_index].line);
            binding_list_push(&hir_fn.params, param_binding);
        }
        function_list_push(&ctx->program->functions, hir_fn);
    }
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
        } else if (hir_global->init->type != hir_global->binding->type) {
            return fail(ctx, "global initializer type mismatch");
        }
    }
    return 1;
}

static int lower_functions(LowerContext* ctx) {
    int i = 0;
    for (i = 0; i < ctx->ast->functions.count; ++i) {
        Scope root_scope;
        int param_index = 0;
        HirFunction* hir_fn = &ctx->program->functions.items[i];
        ctx->current_function = hir_fn;
        push_scope(ctx, &root_scope);
        for (param_index = 0; param_index < hir_fn->params.count; ++param_index) {
            if (!bind_in_current_scope(ctx, hir_fn->params.items[param_index])) {
                return 0;
            }
        }
        if (!lower_block(ctx, &ctx->ast->functions.items[i].body, &hir_fn->body)) {
            return 0;
        }
        pop_scope(ctx);
        ctx->current_function = 0;
    }
    return 1;
}

int lower_ast_to_hir(const AstProgram* ast, HirProgram* hir, const char** error) {
    LowerContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    memset(hir, 0, sizeof(*hir));
    ctx.program = hir;
    ctx.ast = ast;
    if (!register_globals(&ctx) || !register_functions(&ctx) || !lower_globals(&ctx) || !lower_functions(&ctx)) {
        *error = ctx.error ? ctx.error : "failed to lower AST to HIR";
        return 0;
    }
    return 1;
}
