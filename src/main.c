#include "llvm_emit.h"
#include "lower.h"
#include "parser.h"
#include "vec.h"

#include <llvm-c/Core.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define function_list_push(list, fn) VEC_PUSH((list), (fn))
#define global_list_push(list, global) VEC_PUSH((list), (global))
#define struct_list_push(list, struct_decl) VEC_PUSH((list), (struct_decl))
#define enum_list_push(list, enum_decl) VEC_PUSH((list), (enum_decl))
#define union_list_push(list, union_decl) VEC_PUSH((list), (union_decl))
#define param_list_push(list, param) VEC_PUSH((list), (param))
#define stmt_list_push(list, stmt) VEC_PUSH((list), (stmt))
#define expr_list_push(list, expr) VEC_PUSH((list), (expr))
#define type_list_push(list, type) VEC_PUSH((list), (type))
#define struct_field_list_push(list, field) VEC_PUSH((list), (field))
#define struct_field_init_list_push(list, field) VEC_PUSH((list), (field))
#define enum_member_list_push(list, member) VEC_PUSH((list), (member))
#define union_variant_list_push(list, variant) VEC_PUSH((list), (variant))
#define binding_pattern_list_push(list, pattern) VEC_PUSH((list), (pattern))
#define switch_case_list_push(list, switch_case) VEC_PUSH((list), (switch_case))
#define name_list_push(list, name) VEC_PUSH((list), (name))

static char* dup_text(const char* text) {
    size_t n = strlen(text);
    char* out = (char*)malloc(n + 1);
    if (!out) {
        return 0;
    }
    memcpy(out, text, n + 1);
    return out;
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

static char* read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    char* buffer = 0;
    long length = 0;
    size_t read_bytes = 0;

    if (!file) {
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    length = ftell(file);
    if (length < 0) {
        fclose(file);
        return 0;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    buffer = (char*)malloc((size_t)length + 1);
    if (!buffer) {
        fclose(file);
        return 0;
    }

    read_bytes = fread(buffer, 1, (size_t)length, file);
    fclose(file);
    if (read_bytes != (size_t)length) {
        free(buffer);
        return 0;
    }
    buffer[length] = '\0';
    return buffer;
}

static void usage(const char* argv0) {
    fprintf(stderr, "usage: %s --emit-llvm <file>\n", argv0);
}

static int program_has_public_type(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->structs.count; ++i) {
        if (program->structs.items[i].public_flag && strcmp(program->structs.items[i].name, name) == 0) {
            return 1;
        }
    }
    for (i = 0; i < program->enums.count; ++i) {
        if (program->enums.items[i].public_flag && strcmp(program->enums.items[i].name, name) == 0) {
            return 1;
        }
    }
    for (i = 0; i < program->unions.count; ++i) {
        if (program->unions.items[i].public_flag && strcmp(program->unions.items[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int program_has_public_value(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        if (program->functions.items[i].public_flag && strcmp(program->functions.items[i].name, name) == 0) {
            return 1;
        }
    }
    for (i = 0; i < program->globals.count; ++i) {
        if (program->globals.items[i].public_flag && strcmp(program->globals.items[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int program_has_any_type(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->structs.count; ++i) {
        if (strcmp(program->structs.items[i].name, name) == 0) return 1;
    }
    for (i = 0; i < program->enums.count; ++i) {
        if (strcmp(program->enums.items[i].name, name) == 0) return 1;
    }
    for (i = 0; i < program->unions.count; ++i) {
        if (strcmp(program->unions.items[i].name, name) == 0) return 1;
    }
    return 0;
}

static int program_has_any_value(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        if (strcmp(program->functions.items[i].name, name) == 0) return 1;
    }
    for (i = 0; i < program->globals.count; ++i) {
        if (strcmp(program->globals.items[i].name, name) == 0) return 1;
    }
    return 0;
}

static char* remap_exported_name(const AstProgram* program, const char* prefix, const char* name) {
    if (!name) {
        return 0;
    }
    if (prefix && (program_has_public_type(program, name) || program_has_public_value(program, name))) {
        return dup_join3(prefix, ".", name);
    }
    return dup_text(name);
}

static AstType clone_type(const AstProgram* source, const char* prefix, const AstType* type);
static AstExpr* clone_expr(const AstProgram* source, const char* prefix, const AstExpr* expr);
static AstBindingPattern* clone_binding_pattern(const AstProgram* source, const char* prefix, const AstBindingPattern* pattern);
static int clone_block(const AstProgram* source, const char* prefix, AstBlock* out, const AstBlock* block);

static AstType clone_type(const AstProgram* source, const char* prefix, const AstType* type) {
    AstType out;
    int i = 0;
    memset(&out, 0, sizeof(out));
    out.kind = type->kind;
    out.mutable_flag = type->mutable_flag;
    out.array_length = type->array_length;
    if (type->named_name) {
        out.named_name = remap_exported_name(source, prefix, type->named_name);
    }
    for (i = 0; i < type->type_args.count; ++i) {
        type_list_push(&out.type_args, clone_type(source, prefix, &type->type_args.items[i]));
    }
    if (type->array_item) {
        out.array_item = (AstType*)malloc(sizeof(AstType));
        *out.array_item = clone_type(source, prefix, type->array_item);
    }
    for (i = 0; i < type->tuple_items.count; ++i) {
        AstType item = clone_type(source, prefix, &type->tuple_items.items[i]);
        type_list_push(&out.tuple_items, item);
    }
    return out;
}

static AstBindingPattern* clone_binding_pattern(const AstProgram* source, const char* prefix, const AstBindingPattern* pattern) {
    AstBindingPattern* out = (AstBindingPattern*)calloc(1, sizeof(AstBindingPattern));
    int i = 0;
    out->kind = pattern->kind;
    out->line = pattern->line;
    out->type = clone_type(source, prefix, &pattern->type);
    if (pattern->name) {
        out->name = dup_text(pattern->name);
    }
    for (i = 0; i < pattern->items.count; ++i) {
        binding_pattern_list_push(&out->items, clone_binding_pattern(source, prefix, pattern->items.items[i]));
    }
    return out;
}

static AstExpr* clone_expr(const AstProgram* source, const char* prefix, const AstExpr* expr) {
    AstExpr* out = 0;
    int i = 0;
    if (!expr) {
        return 0;
    }
    out = (AstExpr*)calloc(1, sizeof(AstExpr));
    out->kind = expr->kind;
    out->line = expr->line;
    switch (expr->kind) {
        case AST_EXPR_INT:
            out->as.int_value = expr->as.int_value;
            break;
        case AST_EXPR_BOOL:
            out->as.bool_value = expr->as.bool_value;
            break;
        case AST_EXPR_NULL:
            break;
        case AST_EXPR_SIZE_OF:
            out->as.size_of_type = clone_type(source, prefix, &expr->as.size_of_type);
            break;
        case AST_EXPR_STRING:
            out->as.string_lit.text = dup_text(expr->as.string_lit.text);
            out->as.string_lit.length = expr->as.string_lit.length;
            break;
        case AST_EXPR_NAME:
            out->as.name = remap_exported_name(source, prefix, expr->as.name);
            break;
        case AST_EXPR_ADDR:
        case AST_EXPR_DEREF:
        case AST_EXPR_NEW:
        case AST_EXPR_FREE:
            out->as.unary.value = clone_expr(source, prefix, expr->as.unary.value);
            break;
        case AST_EXPR_BINARY:
            out->as.binary.op = expr->as.binary.op;
            out->as.binary.left = clone_expr(source, prefix, expr->as.binary.left);
            out->as.binary.right = clone_expr(source, prefix, expr->as.binary.right);
            break;
        case AST_EXPR_COALESCE:
            out->as.coalesce.left = clone_expr(source, prefix, expr->as.coalesce.left);
            out->as.coalesce.right = clone_expr(source, prefix, expr->as.coalesce.right);
            break;
        case AST_EXPR_TERNARY:
            out->as.ternary.cond = clone_expr(source, prefix, expr->as.ternary.cond);
            out->as.ternary.then_expr = clone_expr(source, prefix, expr->as.ternary.then_expr);
            out->as.ternary.else_expr = clone_expr(source, prefix, expr->as.ternary.else_expr);
            break;
        case AST_EXPR_CALL:
            out->as.call.callee = remap_exported_name(source, prefix, expr->as.call.callee);
            for (i = 0; i < expr->as.call.type_args.count; ++i) {
                type_list_push(&out->as.call.type_args, clone_type(source, prefix, &expr->as.call.type_args.items[i]));
            }
            for (i = 0; i < expr->as.call.args.count; ++i) {
                expr_list_push(&out->as.call.args, clone_expr(source, prefix, expr->as.call.args.items[i]));
            }
            break;
        case AST_EXPR_VARIANT:
            if (expr->as.variant.union_name) {
                out->as.variant.union_name = remap_exported_name(source, prefix, expr->as.variant.union_name);
            }
            out->as.variant.variant_name = dup_text(expr->as.variant.variant_name);
            out->as.variant.pattern_flag = expr->as.variant.pattern_flag;
            out->as.variant.payload = clone_expr(source, prefix, expr->as.variant.payload);
            for (i = 0; i < expr->as.variant.bindings.count; ++i) {
                binding_pattern_list_push(&out->as.variant.bindings, clone_binding_pattern(source, prefix, expr->as.variant.bindings.items[i]));
            }
            break;
        case AST_EXPR_FIELD:
        case AST_EXPR_OPTIONAL_FIELD:
            out->as.field.base = clone_expr(source, prefix, expr->as.field.base);
            out->as.field.name = dup_text(expr->as.field.name);
            break;
        case AST_EXPR_STRUCT:
            out->as.struct_lit.type_name = remap_exported_name(source, prefix, expr->as.struct_lit.type_name);
            for (i = 0; i < expr->as.struct_lit.type_args.count; ++i) {
                type_list_push(&out->as.struct_lit.type_args, clone_type(source, prefix, &expr->as.struct_lit.type_args.items[i]));
            }
            for (i = 0; i < expr->as.struct_lit.fields.count; ++i) {
                AstStructFieldInit init;
                memset(&init, 0, sizeof(init));
                init.name = dup_text(expr->as.struct_lit.fields.items[i].name);
                init.value = clone_expr(source, prefix, expr->as.struct_lit.fields.items[i].value);
                init.line = expr->as.struct_lit.fields.items[i].line;
                struct_field_init_list_push(&out->as.struct_lit.fields, init);
            }
            break;
        case AST_EXPR_TUPLE:
            for (i = 0; i < expr->as.tuple.items.count; ++i) {
                expr_list_push(&out->as.tuple.items, clone_expr(source, prefix, expr->as.tuple.items.items[i]));
            }
            break;
        case AST_EXPR_ARRAY:
            for (i = 0; i < expr->as.array.items.count; ++i) {
                expr_list_push(&out->as.array.items, clone_expr(source, prefix, expr->as.array.items.items[i]));
            }
            break;
        case AST_EXPR_INDEX:
        case AST_EXPR_OPTIONAL_INDEX:
            out->as.index.base = clone_expr(source, prefix, expr->as.index.base);
            out->as.index.index = clone_expr(source, prefix, expr->as.index.index);
            break;
        case AST_EXPR_SLICE_LENGTH:
            out->as.slice_length.base = clone_expr(source, prefix, expr->as.slice_length.base);
            break;
    }
    return out;
}

static AstStmt* clone_stmt(const AstProgram* source, const char* prefix, const AstStmt* stmt) {
    AstStmt* out = (AstStmt*)calloc(1, sizeof(AstStmt));
    int i = 0;
    out->kind = stmt->kind;
    out->line = stmt->line;
    switch (stmt->kind) {
        case AST_STMT_RETURN:
            out->as.ret.expr = clone_expr(source, prefix, stmt->as.ret.expr);
            break;
        case AST_STMT_VAR_DECL:
            out->as.var_decl.type = clone_type(source, prefix, &stmt->as.var_decl.type);
            out->as.var_decl.name = dup_text(stmt->as.var_decl.name);
            out->as.var_decl.init = clone_expr(source, prefix, stmt->as.var_decl.init);
            break;
        case AST_STMT_ASSIGN:
            out->as.assign.target = clone_expr(source, prefix, stmt->as.assign.target);
            out->as.assign.value = clone_expr(source, prefix, stmt->as.assign.value);
            break;
        case AST_STMT_IF:
            out->as.if_stmt.cond = clone_expr(source, prefix, stmt->as.if_stmt.cond);
            out->as.if_stmt.has_else = stmt->as.if_stmt.has_else;
            clone_block(source, prefix, &out->as.if_stmt.then_block, &stmt->as.if_stmt.then_block);
            clone_block(source, prefix, &out->as.if_stmt.else_block, &stmt->as.if_stmt.else_block);
            break;
        case AST_STMT_SWITCH:
            out->as.switch_stmt.value = clone_expr(source, prefix, stmt->as.switch_stmt.value);
            for (i = 0; i < stmt->as.switch_stmt.cases.count; ++i) {
                AstSwitchCase item;
                memset(&item, 0, sizeof(item));
                item.pattern = clone_expr(source, prefix, stmt->as.switch_stmt.cases.items[i].pattern);
                item.is_else = stmt->as.switch_stmt.cases.items[i].is_else;
                clone_block(source, prefix, &item.body, &stmt->as.switch_stmt.cases.items[i].body);
                switch_case_list_push(&out->as.switch_stmt.cases, item);
            }
            break;
        case AST_STMT_WHILE:
            out->as.while_stmt.cond = clone_expr(source, prefix, stmt->as.while_stmt.cond);
            clone_block(source, prefix, &out->as.while_stmt.body, &stmt->as.while_stmt.body);
            break;
        case AST_STMT_FOR_RANGE:
            out->as.for_range.type = clone_type(source, prefix, &stmt->as.for_range.type);
            out->as.for_range.name = dup_text(stmt->as.for_range.name);
            out->as.for_range.start = clone_expr(source, prefix, stmt->as.for_range.start);
            out->as.for_range.end = clone_expr(source, prefix, stmt->as.for_range.end);
            clone_block(source, prefix, &out->as.for_range.body, &stmt->as.for_range.body);
            break;
        case AST_STMT_FOR_EACH:
            out->as.for_each.pattern = clone_binding_pattern(source, prefix, stmt->as.for_each.pattern);
            out->as.for_each.iterable = clone_expr(source, prefix, stmt->as.for_each.iterable);
            out->as.for_each.indexed_flag = stmt->as.for_each.indexed_flag;
            clone_block(source, prefix, &out->as.for_each.body, &stmt->as.for_each.body);
            break;
        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            break;
        case AST_STMT_EXPR:
            out->as.expr_stmt.expr = clone_expr(source, prefix, stmt->as.expr_stmt.expr);
            break;
        case AST_STMT_DESTRUCTURE:
            for (i = 0; i < stmt->as.destructure.bindings.count; ++i) {
                AstParam binding;
                memset(&binding, 0, sizeof(binding));
                binding.type = clone_type(source, prefix, &stmt->as.destructure.bindings.items[i].type);
                binding.name = dup_text(stmt->as.destructure.bindings.items[i].name);
                binding.line = stmt->as.destructure.bindings.items[i].line;
                param_list_push(&out->as.destructure.bindings, binding);
            }
            out->as.destructure.init = clone_expr(source, prefix, stmt->as.destructure.init);
            break;
    }
    return out;
}

static int clone_block(const AstProgram* source, const char* prefix, AstBlock* out, const AstBlock* block) {
    int i = 0;
    memset(out, 0, sizeof(*out));
    for (i = 0; i < block->stmts.count; ++i) {
        stmt_list_push(&out->stmts, clone_stmt(source, prefix, block->stmts.items[i]));
    }
    return 1;
}

static AstFunction clone_function(const AstProgram* source, const char* prefix, const AstFunction* fn, int public_flag) {
    AstFunction out;
    int i = 0;
    memset(&out, 0, sizeof(out));
    out.return_type = clone_type(source, prefix, &fn->return_type);
    out.name = remap_exported_name(source, prefix, fn->name);
    for (i = 0; i < fn->type_params.count; ++i) {
        name_list_push(&out.type_params, dup_text(fn->type_params.items[i]));
    }
    out.public_flag = public_flag;
    out.struct_init_flag = fn->struct_init_flag;
    out.method_flag = fn->method_flag;
    out.static_method_flag = fn->static_method_flag;
    out.line = fn->line;
    if (fn->owner_type_name) {
        out.owner_type_name = remap_exported_name(source, prefix, fn->owner_type_name);
    }
    for (i = 0; i < fn->params.count; ++i) {
        AstParam param;
        memset(&param, 0, sizeof(param));
        param.type = clone_type(source, prefix, &fn->params.items[i].type);
        param.name = dup_text(fn->params.items[i].name);
        param.line = fn->params.items[i].line;
        param_list_push(&out.params, param);
    }
    clone_block(source, prefix, &out.body, &fn->body);
    return out;
}

static AstFunction clone_function_as(const AstProgram* source, const AstFunction* fn, const char* new_name, int public_flag) {
    AstFunction out = clone_function(source, 0, fn, public_flag);
    out.name = dup_text(new_name);
    return out;
}

static AstGlobal clone_global(const AstProgram* source, const char* prefix, const AstGlobal* global, int public_flag) {
    AstGlobal out;
    memset(&out, 0, sizeof(out));
    out.type = clone_type(source, prefix, &global->type);
    out.name = remap_exported_name(source, prefix, global->name);
    out.init = clone_expr(source, prefix, global->init);
    out.public_flag = public_flag;
    out.line = global->line;
    return out;
}

static AstGlobal clone_global_as(const AstProgram* source, const AstGlobal* global, const char* new_name, int public_flag) {
    AstGlobal out = clone_global(source, 0, global, public_flag);
    out.name = dup_text(new_name);
    return out;
}

static AstStructDecl clone_struct_decl(const AstProgram* source, const char* prefix, const AstStructDecl* decl, int public_flag) {
    AstStructDecl out;
    int i = 0;
    memset(&out, 0, sizeof(out));
    out.name = remap_exported_name(source, prefix, decl->name);
    for (i = 0; i < decl->type_params.count; ++i) {
        name_list_push(&out.type_params, dup_text(decl->type_params.items[i]));
    }
    out.public_flag = public_flag;
    out.has_init = decl->has_init;
    out.init_line = decl->init_line;
    out.line = decl->line;
    for (i = 0; i < decl->fields.count; ++i) {
        AstStructField field;
        memset(&field, 0, sizeof(field));
        field.type = clone_type(source, prefix, &decl->fields.items[i].type);
        field.name = dup_text(decl->fields.items[i].name);
        field.default_value = clone_expr(source, prefix, decl->fields.items[i].default_value);
        field.line = decl->fields.items[i].line;
        struct_field_list_push(&out.fields, field);
    }
    for (i = 0; i < decl->init_params.count; ++i) {
        AstParam param;
        memset(&param, 0, sizeof(param));
        param.type = clone_type(source, prefix, &decl->init_params.items[i].type);
        param.name = dup_text(decl->init_params.items[i].name);
        param.line = decl->init_params.items[i].line;
        param_list_push(&out.init_params, param);
    }
    clone_block(source, prefix, &out.init_body, &decl->init_body);
    return out;
}

static AstStructDecl clone_struct_decl_as(const AstProgram* source, const AstStructDecl* decl, const char* new_name, int public_flag) {
    AstStructDecl out = clone_struct_decl(source, 0, decl, public_flag);
    out.name = dup_text(new_name);
    return out;
}

static AstEnumDecl clone_enum_decl(const AstProgram* source, const char* prefix, const AstEnumDecl* decl, int public_flag) {
    AstEnumDecl out;
    int i = 0;
    (void)source;
    memset(&out, 0, sizeof(out));
    out.name = prefix ? dup_join3(prefix, ".", decl->name) : dup_text(decl->name);
    out.public_flag = public_flag;
    out.line = decl->line;
    for (i = 0; i < decl->members.count; ++i) {
        AstEnumMember member = decl->members.items[i];
        member.name = dup_text(decl->members.items[i].name);
        enum_member_list_push(&out.members, member);
    }
    return out;
}

static AstEnumDecl clone_enum_decl_as(const AstProgram* source, const AstEnumDecl* decl, const char* new_name, int public_flag) {
    AstEnumDecl out = clone_enum_decl(source, 0, decl, public_flag);
    out.name = dup_text(new_name);
    return out;
}

static AstUnionDecl clone_union_decl(const AstProgram* source, const char* prefix, const AstUnionDecl* decl, int public_flag) {
    AstUnionDecl out;
    int i = 0;
    memset(&out, 0, sizeof(out));
    out.tag_name = decl->tag_name ? remap_exported_name(source, prefix, decl->tag_name) : 0;
    out.name = remap_exported_name(source, prefix, decl->name);
    out.public_flag = public_flag;
    out.line = decl->line;
    for (i = 0; i < decl->variants.count; ++i) {
        AstUnionVariant variant;
        memset(&variant, 0, sizeof(variant));
        variant.type = clone_type(source, prefix, &decl->variants.items[i].type);
        variant.name = dup_text(decl->variants.items[i].name);
        variant.line = decl->variants.items[i].line;
        union_variant_list_push(&out.variants, variant);
    }
    return out;
}

static AstUnionDecl clone_union_decl_as(const AstProgram* source, const AstUnionDecl* decl, const char* new_name, int public_flag) {
    AstUnionDecl out = clone_union_decl(source, 0, decl, public_flag);
    out.name = dup_text(new_name);
    return out;
}

static const AstFunction* find_ast_function(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        if (strcmp(program->functions.items[i].name, name) == 0) return &program->functions.items[i];
    }
    return 0;
}

static const AstGlobal* find_ast_global(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->globals.count; ++i) {
        if (strcmp(program->globals.items[i].name, name) == 0) return &program->globals.items[i];
    }
    return 0;
}

static const AstStructDecl* find_ast_struct(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->structs.count; ++i) {
        if (strcmp(program->structs.items[i].name, name) == 0) return &program->structs.items[i];
    }
    return 0;
}

static const AstEnumDecl* find_ast_enum(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->enums.count; ++i) {
        if (strcmp(program->enums.items[i].name, name) == 0) return &program->enums.items[i];
    }
    return 0;
}

static const AstUnionDecl* find_ast_union(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->unions.count; ++i) {
        if (strcmp(program->unions.items[i].name, name) == 0) return &program->unions.items[i];
    }
    return 0;
}

static int validate_module_decls(const AstProgram* own_program, const char** error) {
    int i = 0;
    int j = 0;
    for (i = 0; i < own_program->imports.count; ++i) {
        const char* alias = own_program->imports.items[i].alias_name;
        if (!alias) {
            continue;
        }
        for (j = i + 1; j < own_program->imports.count; ++j) {
            if (own_program->imports.items[j].alias_name &&
                strcmp(alias, own_program->imports.items[j].alias_name) == 0) {
                *error = "duplicate import alias";
                return 0;
            }
        }
        if (program_has_any_type(own_program, alias) || program_has_any_value(own_program, alias)) {
            *error = "import alias conflicts with top-level declaration";
            return 0;
        }
        for (j = 0; j < own_program->aliases.count; ++j) {
            if (strcmp(alias, own_program->aliases.items[j].name) == 0) {
                *error = "import alias conflicts with alias";
                return 0;
            }
        }
    }
    return 1;
}

static int apply_aliases(AstProgram* dest, const AstProgram* own_program, const char** error) {
    int i = 0;
    for (i = 0; i < own_program->aliases.count; ++i) {
        const AstAliasDecl* alias = &own_program->aliases.items[i];
        const AstFunction* fn = find_ast_function(dest, alias->target_name);
        const AstGlobal* global = find_ast_global(dest, alias->target_name);
        const AstStructDecl* struct_decl = find_ast_struct(dest, alias->target_name);
        const AstEnumDecl* enum_decl = find_ast_enum(dest, alias->target_name);
        const AstUnionDecl* union_decl = find_ast_union(dest, alias->target_name);
        if (fn) {
            function_list_push(&dest->functions, clone_function_as(dest, fn, alias->name, alias->public_flag));
            continue;
        }
        if (global) {
            global_list_push(&dest->globals, clone_global_as(dest, global, alias->name, alias->public_flag));
            continue;
        }
        if (struct_decl) {
            struct_list_push(&dest->structs, clone_struct_decl_as(dest, struct_decl, alias->name, alias->public_flag));
            continue;
        }
        if (enum_decl) {
            enum_list_push(&dest->enums, clone_enum_decl_as(dest, enum_decl, alias->name, alias->public_flag));
            continue;
        }
        if (union_decl) {
            union_list_push(&dest->unions, clone_union_decl_as(dest, union_decl, alias->name, alias->public_flag));
            continue;
        }
        *error = "unknown alias target";
        return 0;
    }
    return 1;
}

static void merge_public_import(AstProgram* dest, const AstProgram* imported, const char* prefix) {
    int i = 0;
    for (i = 0; i < imported->structs.count; ++i) {
        if (imported->structs.items[i].public_flag) {
            struct_list_push(&dest->structs, clone_struct_decl(imported, prefix, &imported->structs.items[i], 0));
        }
    }
    for (i = 0; i < imported->enums.count; ++i) {
        if (imported->enums.items[i].public_flag) {
            enum_list_push(&dest->enums, clone_enum_decl(imported, prefix, &imported->enums.items[i], 0));
        }
    }
    for (i = 0; i < imported->unions.count; ++i) {
        if (imported->unions.items[i].public_flag) {
            union_list_push(&dest->unions, clone_union_decl(imported, prefix, &imported->unions.items[i], 0));
        }
    }
    for (i = 0; i < imported->globals.count; ++i) {
        if (imported->globals.items[i].public_flag) {
            global_list_push(&dest->globals, clone_global(imported, prefix, &imported->globals.items[i], 0));
        }
    }
    for (i = 0; i < imported->functions.count; ++i) {
        if (imported->functions.items[i].public_flag) {
            function_list_push(&dest->functions, clone_function(imported, prefix, &imported->functions.items[i], 0));
        }
    }
}

static void append_own_decls(AstProgram* dest, const AstProgram* own) {
    int i = 0;
    for (i = 0; i < own->structs.count; ++i) {
        struct_list_push(&dest->structs, clone_struct_decl(own, 0, &own->structs.items[i], own->structs.items[i].public_flag));
    }
    for (i = 0; i < own->enums.count; ++i) {
        enum_list_push(&dest->enums, clone_enum_decl(own, 0, &own->enums.items[i], own->enums.items[i].public_flag));
    }
    for (i = 0; i < own->unions.count; ++i) {
        union_list_push(&dest->unions, clone_union_decl(own, 0, &own->unions.items[i], own->unions.items[i].public_flag));
    }
    for (i = 0; i < own->globals.count; ++i) {
        global_list_push(&dest->globals, clone_global(own, 0, &own->globals.items[i], own->globals.items[i].public_flag));
    }
    for (i = 0; i < own->functions.count; ++i) {
        function_list_push(&dest->functions, clone_function(own, 0, &own->functions.items[i], own->functions.items[i].public_flag));
    }
}

static char* dirname_dup(const char* path) {
    const char* slash = strrchr(path, '/');
    char* out = 0;
    size_t len = 0;
    if (!slash) {
        return dup_text(".");
    }
    len = (size_t)(slash - path);
    out = (char*)malloc(len + 1);
    if (!out) {
        return 0;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

static char* resolve_import_path(const char* from_path, const char* import_path) {
    char* dir = dirname_dup(from_path);
    char* full = 0;
    if (!dir) {
        return 0;
    }
    if (import_path[0] == '/') {
        free(dir);
        return dup_text(import_path);
    }
    full = dup_join3(dir, "/", import_path);
    free(dir);
    return full;
}

static int path_in_stack(const char* path, const char** stack, int depth) {
    int i = 0;
    for (i = 0; i < depth; ++i) {
        if (strcmp(path, stack[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int load_effective_program(const char* path, const char** stack, int depth, AstProgram* out_program, const char** error) {
    Parser parser;
    AstProgram own_program;
    char* source = 0;
    int i = 0;
    memset(out_program, 0, sizeof(*out_program));
    if (path_in_stack(path, stack, depth)) {
        *error = "import cycle";
        return 0;
    }
    source = read_file(path);
    if (!source) {
        *error = "failed to read import";
        return 0;
    }
    parser_init(&parser, source);
    if (!parser_parse_program(&parser, &own_program)) {
        static char parse_error[256];
        snprintf(parse_error, sizeof(parse_error), "%s at line %d", parser.error, parser.error_line);
        *error = parse_error;
        free(source);
        return 0;
    }
    if (!validate_module_decls(&own_program, error)) {
        free(source);
        return 0;
    }
    stack[depth] = path;
    for (i = 0; i < own_program.imports.count; ++i) {
        AstProgram imported;
        char* import_path = resolve_import_path(path, own_program.imports.items[i].path);
        if (!import_path) {
            *error = "failed to resolve import";
            free(source);
            return 0;
        }
        if (!load_effective_program(import_path, stack, depth + 1, &imported, error)) {
            free(import_path);
            free(source);
            return 0;
        }
        merge_public_import(out_program, &imported, own_program.imports.items[i].alias_name);
        free(import_path);
    }
    if (!apply_aliases(out_program, &own_program, error)) {
        free(source);
        return 0;
    }
    append_own_decls(out_program, &own_program);
    free(source);
    return 1;
}

typedef struct MonoContext {
    const AstProgram* source;
    AstProgram* out;
    const char* error;
} MonoContext;

typedef struct TypeSubst {
    const char* name;
    AstType type;
} TypeSubst;

typedef struct TypeSubstList {
    TypeSubst* items;
    int count;
    int capacity;
} TypeSubstList;

#define subst_list_push(list, item) VEC_PUSH((list), (item))

static const AstStructDecl* find_generic_struct_template(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->structs.count; ++i) {
        if (program->structs.items[i].type_params.count > 0 &&
            strcmp(program->structs.items[i].name, name) == 0) {
            return &program->structs.items[i];
        }
    }
    return 0;
}

static const AstFunction* find_generic_function_template(const AstProgram* program, const char* name) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        if (program->functions.items[i].type_params.count > 0 &&
            strcmp(program->functions.items[i].name, name) == 0) {
            return &program->functions.items[i];
        }
    }
    return 0;
}

static int is_generic_owner_method(const AstProgram* program, const AstFunction* fn) {
    return fn->method_flag &&
           fn->owner_type_name &&
           find_generic_struct_template(program, fn->owner_type_name) != 0;
}

static const AstFunction* find_method_template(const AstProgram* program, const char* owner_name, const char* method_name) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        if (program->functions.items[i].method_flag &&
            program->functions.items[i].owner_type_name &&
            strcmp(program->functions.items[i].owner_type_name, owner_name) == 0 &&
            strcmp(program->functions.items[i].name, method_name) == 0) {
            return &program->functions.items[i];
        }
    }
    return 0;
}

static const AstType* lookup_subst(const TypeSubstList* subst, const char* name) {
    int i = 0;
    for (i = 0; i < subst->count; ++i) {
        if (strcmp(subst->items[i].name, name) == 0) {
            return &subst->items[i].type;
        }
    }
    return 0;
}

static char* mangle_type_name(const AstType* type);

static AstType ast_type_copy(const AstType* type) {
    AstType out;
    int i = 0;
    memset(&out, 0, sizeof(out));
    out.kind = type->kind;
    out.mutable_flag = type->mutable_flag;
    if (type->named_name) {
        out.named_name = dup_text(type->named_name);
    }
    for (i = 0; i < type->type_args.count; ++i) {
        type_list_push(&out.type_args, ast_type_copy(&type->type_args.items[i]));
    }
    if (type->array_item) {
        out.array_item = (AstType*)malloc(sizeof(AstType));
        *out.array_item = ast_type_copy(type->array_item);
    }
    for (i = 0; i < type->tuple_items.count; ++i) {
        type_list_push(&out.tuple_items, ast_type_copy(&type->tuple_items.items[i]));
    }
    out.array_length = type->array_length;
    return out;
}

static char* mangle_type_name(const AstType* type) {
    char buffer[64];
    char* out = 0;
    int i = 0;
    switch (type->kind) {
        case AST_TYPE_INT:
            return dup_text("Int");
        case AST_TYPE_UINT8:
            return dup_text("UInt8");
        case AST_TYPE_BOOL:
            return dup_text("Bool");
        case AST_TYPE_VOID:
            return dup_text("Void");
        case AST_TYPE_INFER:
            return dup_text("_");
        case AST_TYPE_POINTER: {
            char* item = mangle_type_name(type->array_item);
            out = dup_join3(item, "_ptr", "");
            free(item);
            return out;
        }
        case AST_TYPE_SLICE: {
            char* item = mangle_type_name(type->array_item);
            out = dup_join3(item, "_slice", "");
            free(item);
            return out;
        }
        case AST_TYPE_OPTIONAL: {
            char* item = mangle_type_name(type->array_item);
            out = dup_join3(item, "_opt", "");
            free(item);
            return out;
        }
        case AST_TYPE_ARRAY: {
            char* item = mangle_type_name(type->array_item);
            snprintf(buffer, sizeof(buffer), "_arr%d", type->array_length);
            out = dup_join3(item, buffer, "");
            free(item);
            return out;
        }
        case AST_TYPE_TUPLE: {
            out = dup_text("Tuple");
            for (i = 0; i < type->tuple_items.count; ++i) {
                char* item = mangle_type_name(&type->tuple_items.items[i]);
                char* next = dup_join3(out, "__", item);
                free(out);
                free(item);
                out = next;
            }
            return out;
        }
        case AST_TYPE_NAMED:
            out = dup_text(type->named_name);
            for (i = 0; i < type->type_args.count; ++i) {
                char* item = mangle_type_name(&type->type_args.items[i]);
                char* next = dup_join3(out, "__", item);
                free(out);
                free(item);
                out = next;
            }
            return out;
    }
    return dup_text("Type");
}

static char* make_instantiated_name(const char* base_name, const AstTypeList* type_args) {
    char* out = dup_text(base_name);
    int i = 0;
    for (i = 0; i < type_args->count; ++i) {
        char* item = mangle_type_name(&type_args->items[i]);
        char* next = dup_join3(out, "__", item);
        free(out);
        free(item);
        out = next;
    }
    return out;
}

static AstType substitute_type(const AstType* type, const TypeSubstList* subst) {
    AstType out;
    const AstType* replaced = 0;
    int i = 0;
    if (type->kind == AST_TYPE_NAMED && type->type_args.count == 0) {
        replaced = lookup_subst(subst, type->named_name);
        if (replaced) {
            return ast_type_copy(replaced);
        }
    }
    memset(&out, 0, sizeof(out));
    out.kind = type->kind;
    out.mutable_flag = type->mutable_flag;
    if (type->named_name) {
        out.named_name = dup_text(type->named_name);
    }
    for (i = 0; i < type->type_args.count; ++i) {
        type_list_push(&out.type_args, substitute_type(&type->type_args.items[i], subst));
    }
    if (type->array_item) {
        out.array_item = (AstType*)malloc(sizeof(AstType));
        *out.array_item = substitute_type(type->array_item, subst);
    }
    for (i = 0; i < type->tuple_items.count; ++i) {
        type_list_push(&out.tuple_items, substitute_type(&type->tuple_items.items[i], subst));
    }
    out.array_length = type->array_length;
    return out;
}

static int ast_type_is_equal(const AstType* left, const AstType* right) {
    int i = 0;
    if (left->kind != right->kind || left->mutable_flag != right->mutable_flag) {
        return 0;
    }
    if ((left->named_name || right->named_name) &&
        (!left->named_name || !right->named_name || strcmp(left->named_name, right->named_name) != 0)) {
        return 0;
    }
    if (left->array_length != right->array_length ||
        left->type_args.count != right->type_args.count ||
        left->tuple_items.count != right->tuple_items.count) {
        return 0;
    }
    if ((left->array_item == 0) != (right->array_item == 0)) {
        return 0;
    }
    if (left->array_item && !ast_type_is_equal(left->array_item, right->array_item)) {
        return 0;
    }
    for (i = 0; i < left->type_args.count; ++i) {
        if (!ast_type_is_equal(&left->type_args.items[i], &right->type_args.items[i])) {
            return 0;
        }
    }
    for (i = 0; i < left->tuple_items.count; ++i) {
        if (!ast_type_is_equal(&left->tuple_items.items[i], &right->tuple_items.items[i])) {
            return 0;
        }
    }
    return 1;
}

static const AstFunction* find_ast_function_exact(const AstProgram* program, const char* name) {
    return find_ast_function(program, name);
}

static const AstStructDecl* find_ast_struct_exact(const AstProgram* program, const char* name) {
    return find_ast_struct(program, name);
}

typedef struct LocalTypeEntry {
    const char* name;
    AstType type;
} LocalTypeEntry;

typedef struct LocalTypeList {
    LocalTypeEntry* items;
    int count;
    int capacity;
} LocalTypeList;

#define local_type_list_push(list, item) VEC_PUSH((list), (item))

static const AstType* lookup_local_type(const LocalTypeList* locals, const char* name) {
    int i = 0;
    for (i = locals->count - 1; i >= 0; --i) {
        if (strcmp(locals->items[i].name, name) == 0) {
            return &locals->items[i].type;
        }
    }
    return 0;
}

static AstType infer_expr_type(const AstProgram* program, const LocalTypeList* locals, const AstExpr* expr) {
    AstType out;
    memset(&out, 0, sizeof(out));
    out.kind = AST_TYPE_VOID;
    switch (expr->kind) {
        case AST_EXPR_INT:
            out.kind = AST_TYPE_INT;
            return out;
        case AST_EXPR_BOOL:
            out.kind = AST_TYPE_BOOL;
            return out;
        case AST_EXPR_STRING:
            out.kind = AST_TYPE_ARRAY;
            out.array_item = (AstType*)malloc(sizeof(AstType));
            memset(out.array_item, 0, sizeof(AstType));
            out.array_item->kind = AST_TYPE_UINT8;
            out.array_length = expr->as.string_lit.length;
            return out;
        case AST_EXPR_NAME: {
            const AstType* local = lookup_local_type(locals, expr->as.name);
            if (local) {
                return ast_type_copy(local);
            }
            {
                const AstGlobal* global = find_ast_global(program, expr->as.name);
                if (global) {
                    return ast_type_copy(&global->type);
                }
            }
            return out;
        }
        case AST_EXPR_STRUCT:
            out.kind = AST_TYPE_NAMED;
            out.named_name = dup_text(expr->as.struct_lit.type_name);
            return out;
        case AST_EXPR_CALL: {
            const AstFunction* fn = find_ast_function_exact(program, expr->as.call.callee);
            if (fn) {
                return ast_type_copy(&fn->return_type);
            }
            return out;
        }
        case AST_EXPR_FIELD: {
            AstType base = infer_expr_type(program, locals, expr->as.field.base);
            const AstStructDecl* st = 0;
            int i = 0;
            if (base.kind == AST_TYPE_NAMED) {
                st = find_ast_struct_exact(program, base.named_name);
                if (st) {
                    for (i = 0; i < st->fields.count; ++i) {
                        if (strcmp(st->fields.items[i].name, expr->as.field.name) == 0) {
                            return ast_type_copy(&st->fields.items[i].type);
                        }
                    }
                }
            }
            return out;
        }
        default:
            return out;
    }
}

static int transform_type(MonoContext* mono, AstType* type);
static int transform_expr(MonoContext* mono, AstExpr* expr, LocalTypeList* locals);
static int instantiate_struct_template(MonoContext* mono, const AstStructDecl* templ, const AstTypeList* type_args, char** instantiated_name);
static int instantiate_function_template(MonoContext* mono, const AstFunction* templ, const AstTypeList* type_args, char** instantiated_name);

static int copy_subst_from_template(TypeSubstList* subst, const AstNameList* type_params, const AstTypeList* type_args) {
    int i = 0;
    if (type_params->count != type_args->count) {
        return 0;
    }
    for (i = 0; i < type_params->count; ++i) {
        TypeSubst item;
        item.name = type_params->items[i];
        item.type = ast_type_copy(&type_args->items[i]);
        subst_list_push(subst, item);
    }
    return 1;
}

static int transform_type(MonoContext* mono, AstType* type) {
    int i = 0;
    if (type->array_item && !transform_type(mono, type->array_item)) {
        return 0;
    }
    for (i = 0; i < type->tuple_items.count; ++i) {
        if (!transform_type(mono, &type->tuple_items.items[i])) {
            return 0;
        }
    }
    for (i = 0; i < type->type_args.count; ++i) {
        if (!transform_type(mono, &type->type_args.items[i])) {
            return 0;
        }
    }
    if (type->kind == AST_TYPE_NAMED && type->type_args.count > 0) {
        const AstStructDecl* templ = find_generic_struct_template(mono->source, type->named_name);
        char* instantiated_name = 0;
        if (!templ) {
            mono->error = "unknown generic type";
            return 0;
        }
        if (!instantiate_struct_template(mono, templ, &type->type_args, &instantiated_name)) {
            return 0;
        }
        free(type->named_name);
        type->named_name = instantiated_name;
        memset(&type->type_args, 0, sizeof(type->type_args));
    }
    return 1;
}

static AstExpr* clone_expr_subst(const AstExpr* expr, const TypeSubstList* subst);

static AstType clone_type_subst(const AstType* type, const TypeSubstList* subst) {
    AstType out = substitute_type(type, subst);
    int i = 0;
    if (out.array_item) {
        AstType tmp = clone_type_subst(out.array_item, subst);
        free(out.array_item);
        out.array_item = (AstType*)malloc(sizeof(AstType));
        *out.array_item = tmp;
    }
    for (i = 0; i < out.type_args.count; ++i) {
        out.type_args.items[i] = clone_type_subst(&out.type_args.items[i], subst);
    }
    for (i = 0; i < out.tuple_items.count; ++i) {
        out.tuple_items.items[i] = clone_type_subst(&out.tuple_items.items[i], subst);
    }
    return out;
}

static AstExpr* clone_expr_subst(const AstExpr* expr, const TypeSubstList* subst) {
    if (!expr) {
        return 0;
    }
    AstExpr* out = (AstExpr*)calloc(1, sizeof(AstExpr));
    int i = 0;
    out->kind = expr->kind;
    out->line = expr->line;
    switch (expr->kind) {
        case AST_EXPR_INT:
            out->as.int_value = expr->as.int_value;
            break;
        case AST_EXPR_BOOL:
            out->as.bool_value = expr->as.bool_value;
            break;
        case AST_EXPR_NULL:
            break;
        case AST_EXPR_SIZE_OF:
            out->as.size_of_type = clone_type_subst(&expr->as.size_of_type, subst);
            break;
        case AST_EXPR_STRING:
            out->as.string_lit.text = dup_text(expr->as.string_lit.text);
            out->as.string_lit.length = expr->as.string_lit.length;
            break;
        case AST_EXPR_NAME:
            out->as.name = dup_text(expr->as.name);
            break;
        case AST_EXPR_ADDR:
        case AST_EXPR_DEREF:
        case AST_EXPR_NEW:
        case AST_EXPR_FREE:
            out->as.unary.value = clone_expr_subst(expr->as.unary.value, subst);
            break;
        case AST_EXPR_BINARY:
            out->as.binary.op = expr->as.binary.op;
            out->as.binary.left = clone_expr_subst(expr->as.binary.left, subst);
            out->as.binary.right = clone_expr_subst(expr->as.binary.right, subst);
            break;
        case AST_EXPR_COALESCE:
            out->as.coalesce.left = clone_expr_subst(expr->as.coalesce.left, subst);
            out->as.coalesce.right = clone_expr_subst(expr->as.coalesce.right, subst);
            break;
        case AST_EXPR_TERNARY:
            out->as.ternary.cond = clone_expr_subst(expr->as.ternary.cond, subst);
            out->as.ternary.then_expr = clone_expr_subst(expr->as.ternary.then_expr, subst);
            out->as.ternary.else_expr = clone_expr_subst(expr->as.ternary.else_expr, subst);
            break;
        case AST_EXPR_CALL:
            out->as.call.callee = dup_text(expr->as.call.callee);
            for (i = 0; i < expr->as.call.type_args.count; ++i) {
                type_list_push(&out->as.call.type_args, clone_type_subst(&expr->as.call.type_args.items[i], subst));
            }
            for (i = 0; i < expr->as.call.args.count; ++i) {
                expr_list_push(&out->as.call.args, clone_expr_subst(expr->as.call.args.items[i], subst));
            }
            break;
        case AST_EXPR_VARIANT:
            out->as.variant.union_name = expr->as.variant.union_name ? dup_text(expr->as.variant.union_name) : 0;
            out->as.variant.variant_name = dup_text(expr->as.variant.variant_name);
            out->as.variant.pattern_flag = expr->as.variant.pattern_flag;
            out->as.variant.payload = clone_expr_subst(expr->as.variant.payload, subst);
            break;
        case AST_EXPR_FIELD:
        case AST_EXPR_OPTIONAL_FIELD:
            out->as.field.base = clone_expr_subst(expr->as.field.base, subst);
            out->as.field.name = dup_text(expr->as.field.name);
            break;
        case AST_EXPR_STRUCT:
            out->as.struct_lit.type_name = dup_text(expr->as.struct_lit.type_name);
            for (i = 0; i < expr->as.struct_lit.type_args.count; ++i) {
                type_list_push(&out->as.struct_lit.type_args, clone_type_subst(&expr->as.struct_lit.type_args.items[i], subst));
            }
            for (i = 0; i < expr->as.struct_lit.fields.count; ++i) {
                AstStructFieldInit init = expr->as.struct_lit.fields.items[i];
                init.name = dup_text(init.name);
                init.value = clone_expr_subst(init.value, subst);
                struct_field_init_list_push(&out->as.struct_lit.fields, init);
            }
            break;
        case AST_EXPR_TUPLE:
            for (i = 0; i < expr->as.tuple.items.count; ++i) {
                expr_list_push(&out->as.tuple.items, clone_expr_subst(expr->as.tuple.items.items[i], subst));
            }
            break;
        case AST_EXPR_ARRAY:
            for (i = 0; i < expr->as.array.items.count; ++i) {
                expr_list_push(&out->as.array.items, clone_expr_subst(expr->as.array.items.items[i], subst));
            }
            break;
        case AST_EXPR_INDEX:
        case AST_EXPR_OPTIONAL_INDEX:
            out->as.index.base = clone_expr_subst(expr->as.index.base, subst);
            out->as.index.index = clone_expr_subst(expr->as.index.index, subst);
            break;
        case AST_EXPR_SLICE_LENGTH:
            out->as.slice_length.base = clone_expr_subst(expr->as.slice_length.base, subst);
            break;
    }
    return out;
}

static int transform_block(MonoContext* mono, AstBlock* block, LocalTypeList* locals);

static int transform_stmt(MonoContext* mono, AstStmt* stmt, LocalTypeList* locals) {
    int i = 0;
    switch (stmt->kind) {
        case AST_STMT_RETURN:
            return !stmt->as.ret.expr || transform_expr(mono, stmt->as.ret.expr, locals);
        case AST_STMT_VAR_DECL: {
            LocalTypeEntry entry;
            if (!transform_type(mono, &stmt->as.var_decl.type) ||
                !transform_expr(mono, stmt->as.var_decl.init, locals)) {
                return 0;
            }
            entry.name = stmt->as.var_decl.name;
            entry.type = ast_type_copy(&stmt->as.var_decl.type);
            local_type_list_push(locals, entry);
            return 1;
        }
        case AST_STMT_ASSIGN:
            return transform_expr(mono, stmt->as.assign.target, locals) &&
                   transform_expr(mono, stmt->as.assign.value, locals);
        case AST_STMT_IF:
            return transform_expr(mono, stmt->as.if_stmt.cond, locals) &&
                   transform_block(mono, &stmt->as.if_stmt.then_block, locals) &&
                   (!stmt->as.if_stmt.has_else || transform_block(mono, &stmt->as.if_stmt.else_block, locals));
        case AST_STMT_SWITCH:
            if (!transform_expr(mono, stmt->as.switch_stmt.value, locals)) {
                return 0;
            }
            for (i = 0; i < stmt->as.switch_stmt.cases.count; ++i) {
                if (stmt->as.switch_stmt.cases.items[i].pattern &&
                    !transform_expr(mono, stmt->as.switch_stmt.cases.items[i].pattern, locals)) {
                    return 0;
                }
                if (!transform_block(mono, &stmt->as.switch_stmt.cases.items[i].body, locals)) {
                    return 0;
                }
            }
            return 1;
        case AST_STMT_WHILE:
            return transform_expr(mono, stmt->as.while_stmt.cond, locals) &&
                   transform_block(mono, &stmt->as.while_stmt.body, locals);
        case AST_STMT_FOR_RANGE:
            return transform_type(mono, &stmt->as.for_range.type) &&
                   transform_expr(mono, stmt->as.for_range.start, locals) &&
                   transform_expr(mono, stmt->as.for_range.end, locals) &&
                   transform_block(mono, &stmt->as.for_range.body, locals);
        case AST_STMT_FOR_EACH:
            return transform_expr(mono, stmt->as.for_each.iterable, locals) &&
                   transform_block(mono, &stmt->as.for_each.body, locals);
        case AST_STMT_EXPR:
            return transform_expr(mono, stmt->as.expr_stmt.expr, locals);
        case AST_STMT_DESTRUCTURE:
            for (i = 0; i < stmt->as.destructure.bindings.count; ++i) {
                if (!transform_type(mono, &stmt->as.destructure.bindings.items[i].type)) {
                    return 0;
                }
            }
            return transform_expr(mono, stmt->as.destructure.init, locals);
        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            return 1;
    }
    return 1;
}

static int transform_block(MonoContext* mono, AstBlock* block, LocalTypeList* locals) {
    int i = 0;
    for (i = 0; i < block->stmts.count; ++i) {
        if (!transform_stmt(mono, block->stmts.items[i], locals)) {
            return 0;
        }
    }
    return 1;
}

static int infer_type_args_from_call(const AstProgram* program, const AstFunction* templ, const AstExpr* call, const LocalTypeList* locals, AstTypeList* out_args) {
    int i = 0;
    for (i = 0; i < templ->type_params.count; ++i) {
        int j = 0;
        int found = 0;
        for (j = 0; j < templ->params.count; ++j) {
            if (templ->params.items[j].type.kind == AST_TYPE_NAMED &&
                templ->params.items[j].type.type_args.count == 0 &&
                strcmp(templ->params.items[j].type.named_name, templ->type_params.items[i]) == 0) {
                AstType actual = infer_expr_type(program, locals, call->as.call.args.items[j]);
                if (actual.kind == AST_TYPE_VOID) {
                    return 0;
                }
                type_list_push(out_args, actual);
                found = 1;
                break;
            }
        }
        if (!found) {
            return 0;
        }
    }
    return 1;
}

static int instantiate_function_template(MonoContext* mono, const AstFunction* templ, const AstTypeList* type_args, char** instantiated_name) {
    AstFunction fn;
    TypeSubstList subst;
    LocalTypeList locals;
    int i = 0;
    memset(&subst, 0, sizeof(subst));
    memset(&locals, 0, sizeof(locals));
    *instantiated_name = make_instantiated_name(templ->name, type_args);
    if (find_ast_function(mono->out, *instantiated_name)) {
        return 1;
    }
    if (!copy_subst_from_template(&subst, &templ->type_params, type_args)) {
        mono->error = "generic function type argument mismatch";
        return 0;
    }
    memset(&fn, 0, sizeof(fn));
    fn.return_type = clone_type_subst(&templ->return_type, &subst);
    fn.name = dup_text(*instantiated_name);
    fn.public_flag = templ->public_flag;
    fn.struct_init_flag = templ->struct_init_flag;
    fn.method_flag = templ->method_flag;
    fn.static_method_flag = templ->static_method_flag;
    fn.owner_type_name = templ->owner_type_name ? dup_text(templ->owner_type_name) : 0;
    fn.line = templ->line;
    for (i = 0; i < templ->params.count; ++i) {
        AstParam param = templ->params.items[i];
        LocalTypeEntry entry;
        param.type = clone_type_subst(&param.type, &subst);
        param.name = dup_text(param.name);
        param_list_push(&fn.params, param);
        entry.name = param.name;
        entry.type = ast_type_copy(&param.type);
        local_type_list_push(&locals, entry);
    }
    for (i = 0; i < templ->body.stmts.count; ++i) {
        stmt_list_push(&fn.body.stmts, clone_stmt(mono->source, 0, templ->body.stmts.items[i]));
    }
    if (!transform_type(mono, &fn.return_type) || !transform_block(mono, &fn.body, &locals)) {
        return 0;
    }
    function_list_push(&mono->out->functions, fn);
    return 1;
}

static int instantiate_struct_template(MonoContext* mono, const AstStructDecl* templ, const AstTypeList* type_args, char** instantiated_name) {
    AstStructDecl decl;
    TypeSubstList subst;
    int i = 0;
    memset(&subst, 0, sizeof(subst));
    *instantiated_name = make_instantiated_name(templ->name, type_args);
    if (find_ast_struct(mono->out, *instantiated_name)) {
        return 1;
    }
    if (!copy_subst_from_template(&subst, &templ->type_params, type_args)) {
        mono->error = "generic struct type argument mismatch";
        return 0;
    }
    memset(&decl, 0, sizeof(decl));
    decl.name = dup_text(*instantiated_name);
    decl.public_flag = templ->public_flag;
    decl.has_init = templ->has_init;
    decl.init_line = templ->init_line;
    decl.line = templ->line;
    for (i = 0; i < templ->fields.count; ++i) {
        AstStructField field = templ->fields.items[i];
        field.type = clone_type_subst(&field.type, &subst);
        field.name = dup_text(field.name);
        field.default_value = clone_expr_subst(field.default_value, &subst);
        struct_field_list_push(&decl.fields, field);
    }
    for (i = 0; i < templ->init_params.count; ++i) {
        AstParam param = templ->init_params.items[i];
        param.type = clone_type_subst(&param.type, &subst);
        param.name = dup_text(param.name);
        param_list_push(&decl.init_params, param);
    }
    for (i = 0; i < templ->init_body.stmts.count; ++i) {
        stmt_list_push(&decl.init_body.stmts, clone_stmt(mono->source, 0, templ->init_body.stmts.items[i]));
    }
    if (!transform_block(mono, &decl.init_body, &(LocalTypeList){0})) {
        return 0;
    }
    struct_list_push(&mono->out->structs, decl);
    for (i = 0; i < mono->source->functions.count; ++i) {
        const AstFunction* method = &mono->source->functions.items[i];
        if (method->method_flag && method->owner_type_name && strcmp(method->owner_type_name, templ->name) == 0) {
            AstTypeList empty_args;
            AstFunction cloned = clone_function(mono->source, 0, method, method->public_flag);
            free(cloned.owner_type_name);
            cloned.owner_type_name = dup_text(*instantiated_name);
            cloned.type_params.count = 0;
            for (int j = 0; j < cloned.params.count; ++j) {
                cloned.params.items[j].type = clone_type_subst(&cloned.params.items[j].type, &subst);
            }
            cloned.return_type = clone_type_subst(&cloned.return_type, &subst);
            memset(&empty_args, 0, sizeof(empty_args));
            if (!transform_block(mono, &cloned.body, &(LocalTypeList){0})) {
                return 0;
            }
            function_list_push(&mono->out->functions, cloned);
        }
    }
    return 1;
}

static int transform_expr(MonoContext* mono, AstExpr* expr, LocalTypeList* locals) {
    int i = 0;
    if (!expr) {
        return 1;
    }
    switch (expr->kind) {
        case AST_EXPR_SIZE_OF:
            return transform_type(mono, &expr->as.size_of_type);
        case AST_EXPR_CALL: {
            const AstFunction* templ = 0;
            AstTypeList type_args;
            memset(&type_args, 0, sizeof(type_args));
            for (i = 0; i < expr->as.call.args.count; ++i) {
                if (!transform_expr(mono, expr->as.call.args.items[i], locals)) {
                    return 0;
                }
            }
            templ = find_generic_function_template(mono->source, expr->as.call.callee);
            if (!templ) {
                for (i = 0; i < expr->as.call.type_args.count; ++i) {
                    if (!transform_type(mono, &expr->as.call.type_args.items[i])) {
                        return 0;
                    }
                }
                return 1;
            }
            if (expr->as.call.type_args.count > 0) {
                for (i = 0; i < expr->as.call.type_args.count; ++i) {
                    AstType arg = expr->as.call.type_args.items[i];
                    if (!transform_type(mono, &arg)) {
                        return 0;
                    }
                    type_list_push(&type_args, arg);
                }
            } else if (!infer_type_args_from_call(mono->out, templ, expr, locals, &type_args)) {
                mono->error = "failed to infer generic function type arguments";
                return 0;
            }
            {
                char* instantiated_name = 0;
                if (!instantiate_function_template(mono, templ, &type_args, &instantiated_name)) {
                    return 0;
                }
                free(expr->as.call.callee);
                expr->as.call.callee = instantiated_name;
                memset(&expr->as.call.type_args, 0, sizeof(expr->as.call.type_args));
            }
            return 1;
        }
        case AST_EXPR_STRUCT:
            for (i = 0; i < expr->as.struct_lit.type_args.count; ++i) {
                if (!transform_type(mono, &expr->as.struct_lit.type_args.items[i])) {
                    return 0;
                }
            }
            if (expr->as.struct_lit.type_args.count > 0) {
                const AstStructDecl* templ = find_generic_struct_template(mono->source, expr->as.struct_lit.type_name);
                char* instantiated_name = 0;
                if (!templ) {
                    mono->error = "unknown generic type";
                    return 0;
                }
                if (!instantiate_struct_template(mono, templ, &expr->as.struct_lit.type_args, &instantiated_name)) {
                    return 0;
                }
                free(expr->as.struct_lit.type_name);
                expr->as.struct_lit.type_name = instantiated_name;
                memset(&expr->as.struct_lit.type_args, 0, sizeof(expr->as.struct_lit.type_args));
            }
            for (i = 0; i < expr->as.struct_lit.fields.count; ++i) {
                if (!transform_expr(mono, expr->as.struct_lit.fields.items[i].value, locals)) {
                    return 0;
                }
            }
            return 1;
        case AST_EXPR_FIELD:
        case AST_EXPR_OPTIONAL_FIELD:
            return transform_expr(mono, expr->as.field.base, locals);
        case AST_EXPR_ADDR:
        case AST_EXPR_DEREF:
        case AST_EXPR_NEW:
        case AST_EXPR_FREE:
            return transform_expr(mono, expr->as.unary.value, locals);
        case AST_EXPR_BINARY:
            return transform_expr(mono, expr->as.binary.left, locals) &&
                   transform_expr(mono, expr->as.binary.right, locals);
        case AST_EXPR_COALESCE:
            return transform_expr(mono, expr->as.coalesce.left, locals) &&
                   transform_expr(mono, expr->as.coalesce.right, locals);
        case AST_EXPR_TERNARY:
            return transform_expr(mono, expr->as.ternary.cond, locals) &&
                   transform_expr(mono, expr->as.ternary.then_expr, locals) &&
                   transform_expr(mono, expr->as.ternary.else_expr, locals);
        case AST_EXPR_VARIANT:
            return !expr->as.variant.payload || transform_expr(mono, expr->as.variant.payload, locals);
        case AST_EXPR_TUPLE:
            for (i = 0; i < expr->as.tuple.items.count; ++i) {
                if (!transform_expr(mono, expr->as.tuple.items.items[i], locals)) {
                    return 0;
                }
            }
            return 1;
        case AST_EXPR_ARRAY:
            for (i = 0; i < expr->as.array.items.count; ++i) {
                if (!transform_expr(mono, expr->as.array.items.items[i], locals)) {
                    return 0;
                }
            }
            return 1;
        case AST_EXPR_INDEX:
        case AST_EXPR_OPTIONAL_INDEX:
            return transform_expr(mono, expr->as.index.base, locals) &&
                   transform_expr(mono, expr->as.index.index, locals);
        case AST_EXPR_SLICE_LENGTH:
            return transform_expr(mono, expr->as.slice_length.base, locals);
        case AST_EXPR_INT:
        case AST_EXPR_BOOL:
        case AST_EXPR_STRING:
        case AST_EXPR_NULL:
        case AST_EXPR_NAME:
            return 1;
    }
    return 1;
}

static int monomorphize_program(const AstProgram* source, AstProgram* out, const char** error) {
    MonoContext mono;
    int i = 0;
    memset(out, 0, sizeof(*out));
    mono.source = source;
    mono.out = out;
    mono.error = 0;
    for (i = 0; i < source->enums.count; ++i) {
        enum_list_push(&out->enums, clone_enum_decl(source, 0, &source->enums.items[i], source->enums.items[i].public_flag));
    }
    for (i = 0; i < source->unions.count; ++i) {
        union_list_push(&out->unions, clone_union_decl(source, 0, &source->unions.items[i], source->unions.items[i].public_flag));
    }
    for (i = 0; i < source->structs.count; ++i) {
        AstStructDecl decl;
        if (source->structs.items[i].type_params.count > 0) {
            continue;
        }
        decl = clone_struct_decl(source, 0, &source->structs.items[i], source->structs.items[i].public_flag);
        if (!transform_block(&mono, &decl.init_body, &(LocalTypeList){0})) {
            *error = mono.error;
            return 0;
        }
        struct_list_push(&out->structs, decl);
    }
    for (i = 0; i < source->globals.count; ++i) {
        AstGlobal global = clone_global(source, 0, &source->globals.items[i], source->globals.items[i].public_flag);
        if (!transform_type(&mono, &global.type) || !transform_expr(&mono, global.init, &(LocalTypeList){0})) {
            *error = mono.error;
            return 0;
        }
        global_list_push(&out->globals, global);
    }
    for (i = 0; i < source->functions.count; ++i) {
        AstFunction fn;
        LocalTypeList locals;
        int j = 0;
        if (source->functions.items[i].type_params.count > 0 || is_generic_owner_method(source, &source->functions.items[i])) {
            continue;
        }
        memset(&locals, 0, sizeof(locals));
        fn = clone_function(source, 0, &source->functions.items[i], source->functions.items[i].public_flag);
        if (!transform_type(&mono, &fn.return_type)) {
            *error = mono.error;
            return 0;
        }
        for (j = 0; j < fn.params.count; ++j) {
            LocalTypeEntry entry;
            if (!transform_type(&mono, &fn.params.items[j].type)) {
                *error = mono.error;
                return 0;
            }
            entry.name = fn.params.items[j].name;
            entry.type = ast_type_copy(&fn.params.items[j].type);
            local_type_list_push(&locals, entry);
        }
        if (!transform_block(&mono, &fn.body, &locals)) {
            *error = mono.error;
            return 0;
        }
        function_list_push(&out->functions, fn);
    }
    return 1;
}

int main(int argc, char** argv) {
    Parser parser;
    AstProgram ast;
    AstProgram mono_ast;
    HirProgram hir;
    JirProgram jir;
    char* source = 0;
    char* ir = 0;
    const char* error = 0;

    if (argc != 3 || strcmp(argv[1], "--emit-llvm") != 0) {
        usage(argv[0]);
        return 1;
    }

    {
        const char* stack[64] = {0};
        (void)parser;
        source = 0;
        if (!load_effective_program(argv[2], stack, 0, &ast, &error)) {
            fprintf(stderr, "error: %s\n", error ? error : "failed to load program");
            return 1;
        }
    }

    if (!monomorphize_program(&ast, &mono_ast, &error)) {
        fprintf(stderr, "error: %s\n", error ? error : "failed to monomorphize AST");
        return 1;
    }
    if (!lower_ast_to_hir(&mono_ast, &hir, &error)) {
        fprintf(stderr, "error: %s\n", error ? error : "failed to lower AST to HIR");
        return 1;
    }
    if (!lower_hir_to_jir(&hir, &jir, &error)) {
        fprintf(stderr, "error: %s\n", error ? error : "failed to lower HIR to JIR");
        return 1;
    }
    if (!emit_llvm_module(&jir, &ir, &error)) {
        fprintf(stderr, "error: %s\n", error ? error : "failed to emit LLVM IR");
        return 1;
    }

    fputs(ir, stdout);
    LLVMDisposeMessage(ir);
    return 0;
}
