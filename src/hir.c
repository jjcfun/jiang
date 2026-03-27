#include "hir.h"
#include <string.h>

static HIRNode* hir_new_node(Arena* arena, HIRKind kind, int line) {
    HIRNode* node = arena_alloc(arena, sizeof(HIRNode));
    memset(node, 0, sizeof(HIRNode));
    node->kind = kind;
    node->line = line;
    return node;
}

static void hir_add_child(HIRNode* parent, HIRNode* child) {
    if (!parent || !child) return;
    if (!parent->first_child) {
        parent->first_child = child;
        parent->last_child = child;
        return;
    }
    parent->last_child->next_sibling = child;
    parent->last_child = child;
}

static HIRKind hir_kind_from_ast(NodeType type) {
    switch (type) {
        case AST_PROGRAM: return HIR_PROGRAM;
        case AST_BLOCK: return HIR_BLOCK;
        case AST_LITERAL_NUMBER: return HIR_LITERAL_NUMBER;
        case AST_LITERAL_STRING: return HIR_LITERAL_STRING;
        case AST_TUPLE_LITERAL: return HIR_TUPLE_LITERAL;
        case AST_IDENTIFIER: return HIR_IDENTIFIER;
        case AST_BINARY_EXPR: return HIR_BINARY_EXPR;
        case AST_UNARY_EXPR: return HIR_UNARY_EXPR;
        case AST_VAR_DECL: return HIR_VAR_DECL;
        case AST_FUNC_CALL: return HIR_FUNC_CALL;
        case AST_MEMBER_ACCESS: return HIR_MEMBER_ACCESS;
        case AST_IF_STMT: return HIR_IF_STMT;
        case AST_SWITCH_STMT: return HIR_SWITCH_STMT;
        case AST_SWITCH_CASE: return HIR_SWITCH_CASE;
        case AST_WHILE_STMT: return HIR_WHILE_STMT;
        case AST_FOR_STMT: return HIR_FOR_STMT;
        case AST_RANGE_EXPR: return HIR_RANGE_EXPR;
        case AST_FUNC_DECL: return HIR_FUNC_DECL;
        case AST_RETURN_STMT: return HIR_RETURN_STMT;
        case AST_STRUCT_DECL: return HIR_STRUCT_DECL;
        case AST_INIT_DECL: return HIR_INIT_DECL;
        case AST_STRUCT_INIT_EXPR: return HIR_STRUCT_INIT_EXPR;
        case AST_NEW_EXPR: return HIR_NEW_EXPR;
        case AST_INDEX_EXPR: return HIR_INDEX_EXPR;
        case AST_ARRAY_LITERAL: return HIR_ARRAY_LITERAL;
        case AST_ENUM_DECL: return HIR_ENUM_DECL;
        case AST_ENUM_ACCESS: return HIR_ENUM_ACCESS;
        case AST_BREAK_STMT: return HIR_BREAK_STMT;
        case AST_CONTINUE_STMT: return HIR_CONTINUE_STMT;
        case AST_IMPORT: return HIR_IMPORT;
        case AST_UNION_DECL: return HIR_UNION_DECL;
        case AST_BINDING_LIST: return HIR_BINDING_LIST;
        case AST_BINDING_ASSIGN: return HIR_BINDING_ASSIGN;
        case AST_PATTERN: return HIR_PATTERN;
    }
    return HIR_PROGRAM;
}

static HIRKind hir_kind_from_type(TypeKind kind) {
    switch (kind) {
        case TYPE_BASE: return HIR_TYPE_BASE;
        case TYPE_POINTER: return HIR_TYPE_POINTER;
        case TYPE_ARRAY: return HIR_TYPE_ARRAY;
        case TYPE_SLICE: return HIR_TYPE_SLICE;
        case TYPE_NULLABLE: return HIR_TYPE_NULLABLE;
        case TYPE_MUTABLE: return HIR_TYPE_MUTABLE;
        case TYPE_TUPLE: return HIR_TYPE_TUPLE;
    }
    return HIR_TYPE_BASE;
}

static HIRNode* hir_build_type(TypeExpr* type, Arena* arena);
static HIRNode* hir_build_node(ASTNode* node, Arena* arena);

static void hir_attach_declared_type(HIRNode* parent, TypeExpr* type, Arena* arena) {
    if (!type) return;
    hir_add_child(parent, hir_build_type(type, arena));
}

static HIRNode* hir_build_type(TypeExpr* type, Arena* arena) {
    if (!type) return NULL;
    HIRNode* node = hir_new_node(arena, hir_kind_from_type(type->kind), 0);
    node->type_expr = type;
    node->evaluated_type = type;
    switch (type->kind) {
        case TYPE_POINTER:
            hir_add_child(node, hir_build_type(type->as.pointer.element, arena));
            break;
        case TYPE_ARRAY:
            hir_add_child(node, hir_build_type(type->as.array.element, arena));
            if (type->as.array.length) hir_add_child(node, hir_build_node(type->as.array.length, arena));
            break;
        case TYPE_SLICE:
            hir_add_child(node, hir_build_type(type->as.slice.element, arena));
            break;
        case TYPE_NULLABLE:
            hir_add_child(node, hir_build_type(type->as.nullable.element, arena));
            break;
        case TYPE_MUTABLE:
            hir_add_child(node, hir_build_type(type->as.mutable.element, arena));
            break;
        case TYPE_TUPLE:
            for (size_t i = 0; i < type->as.tuple.count; i++) {
                hir_add_child(node, hir_build_type(type->as.tuple.elements[i], arena));
            }
            break;
        case TYPE_BASE:
            break;
    }
    return node;
}

static HIRNode* hir_build_node(ASTNode* node, Arena* arena) {
    if (!node) return NULL;

    HIRNode* hir = hir_new_node(arena, hir_kind_from_ast(node->type), node->line);
    hir->ast = node;
    hir->is_public = node->is_public;
    hir->evaluated_type = node->evaluated_type;
    hir->symbol = node->symbol;

    switch (node->type) {
        case AST_PROGRAM:
        case AST_BLOCK:
            for (size_t i = 0; i < node->as.block.count; i++) {
                hir_add_child(hir, hir_build_node(node->as.block.statements[i], arena));
            }
            break;
        case AST_BINDING_LIST:
            for (size_t i = 0; i < node->as.binding_list.count; i++) {
                hir_add_child(hir, hir_build_node(node->as.binding_list.items[i], arena));
            }
            break;
        case AST_VAR_DECL:
            hir_attach_declared_type(hir, node->as.var_decl.type, arena);
            hir_add_child(hir, hir_build_node(node->as.var_decl.initializer, arena));
            break;
        case AST_FUNC_DECL:
            hir_attach_declared_type(hir, node->as.func_decl.return_type, arena);
            for (size_t i = 0; i < node->as.func_decl.param_count; i++) {
                hir_add_child(hir, hir_build_node(node->as.func_decl.params[i], arena));
            }
            hir_add_child(hir, hir_build_node(node->as.func_decl.body, arena));
            break;
        case AST_INIT_DECL:
            for (size_t i = 0; i < node->as.init_decl.param_count; i++) {
                hir_add_child(hir, hir_build_node(node->as.init_decl.params[i], arena));
            }
            hir_add_child(hir, hir_build_node(node->as.init_decl.body, arena));
            break;
        case AST_PATTERN:
            hir_attach_declared_type(hir, node->as.pattern.type, arena);
            break;
        case AST_IF_STMT:
            hir_add_child(hir, hir_build_node(node->as.if_stmt.condition, arena));
            hir_add_child(hir, hir_build_node(node->as.if_stmt.then_branch, arena));
            hir_add_child(hir, hir_build_node(node->as.if_stmt.else_branch, arena));
            break;
        case AST_SWITCH_STMT:
            hir_add_child(hir, hir_build_node(node->as.switch_stmt.expression, arena));
            for (size_t i = 0; i < node->as.switch_stmt.case_count; i++) {
                hir_add_child(hir, hir_build_node(node->as.switch_stmt.cases[i], arena));
            }
            hir_add_child(hir, hir_build_node(node->as.switch_stmt.else_branch, arena));
            break;
        case AST_SWITCH_CASE:
            hir_add_child(hir, hir_build_node(node->as.switch_case.pattern, arena));
            hir_add_child(hir, hir_build_node(node->as.switch_case.body, arena));
            break;
        case AST_WHILE_STMT:
            hir_add_child(hir, hir_build_node(node->as.while_stmt.condition, arena));
            hir_add_child(hir, hir_build_node(node->as.while_stmt.body, arena));
            break;
        case AST_FOR_STMT:
            hir_add_child(hir, hir_build_node(node->as.for_stmt.pattern, arena));
            hir_add_child(hir, hir_build_node(node->as.for_stmt.iterable, arena));
            hir_add_child(hir, hir_build_node(node->as.for_stmt.body, arena));
            break;
        case AST_RANGE_EXPR:
            hir_add_child(hir, hir_build_node(node->as.range.start, arena));
            hir_add_child(hir, hir_build_node(node->as.range.end, arena));
            break;
        case AST_RETURN_STMT:
            hir_add_child(hir, hir_build_node(node->as.return_stmt.expression, arena));
            break;
        case AST_STRUCT_DECL:
            for (size_t i = 0; i < node->as.struct_decl.member_count; i++) {
                hir_add_child(hir, hir_build_node(node->as.struct_decl.members[i], arena));
            }
            break;
        case AST_STRUCT_INIT_EXPR:
            hir_attach_declared_type(hir, node->as.struct_init.type, arena);
            for (size_t i = 0; i < node->as.struct_init.field_count; i++) {
                hir_add_child(hir, hir_build_node(node->as.struct_init.field_values[i], arena));
            }
            break;
        case AST_NEW_EXPR:
            hir_add_child(hir, hir_build_node(node->as.new_expr.value, arena));
            break;
        case AST_INDEX_EXPR:
            hir_add_child(hir, hir_build_node(node->as.index_expr.object, arena));
            hir_add_child(hir, hir_build_node(node->as.index_expr.index, arena));
            break;
        case AST_ARRAY_LITERAL:
            hir_attach_declared_type(hir, node->as.array_literal.type, arena);
            for (size_t i = 0; i < node->as.array_literal.element_count; i++) {
                hir_add_child(hir, hir_build_node(node->as.array_literal.elements[i], arena));
            }
            break;
        case AST_ENUM_DECL:
            hir_attach_declared_type(hir, node->as.enum_decl.base_type, arena);
            for (size_t i = 0; i < node->as.enum_decl.member_count; i++) {
                hir_add_child(hir, hir_build_node(node->as.enum_decl.member_values[i], arena));
            }
            break;
        case AST_UNION_DECL:
            for (size_t i = 0; i < node->as.union_decl.member_count; i++) {
                hir_add_child(hir, hir_build_node(node->as.union_decl.members[i], arena));
            }
            break;
        case AST_BINDING_ASSIGN:
            hir_add_child(hir, hir_build_node(node->as.binding_assign.bindings, arena));
            hir_add_child(hir, hir_build_node(node->as.binding_assign.value, arena));
            break;
        case AST_BINARY_EXPR:
            hir_add_child(hir, hir_build_node(node->as.binary.left, arena));
            hir_add_child(hir, hir_build_node(node->as.binary.right, arena));
            break;
        case AST_UNARY_EXPR:
            hir_add_child(hir, hir_build_node(node->as.unary.right, arena));
            break;
        case AST_FUNC_CALL:
            hir_add_child(hir, hir_build_node(node->as.func_call.callee, arena));
            for (size_t i = 0; i < node->as.func_call.arg_count; i++) {
                hir_add_child(hir, hir_build_node(node->as.func_call.args[i], arena));
            }
            break;
        case AST_MEMBER_ACCESS:
            hir_add_child(hir, hir_build_node(node->as.member_access.object, arena));
            break;
        case AST_LITERAL_NUMBER:
        case AST_LITERAL_STRING:
        case AST_TUPLE_LITERAL:
            if (node->type == AST_TUPLE_LITERAL) {
                for (size_t i = 0; i < node->as.tuple_literal.count; i++) {
                    hir_add_child(hir, hir_build_node(node->as.tuple_literal.elements[i], arena));
                }
            }
            break;
        case AST_IDENTIFIER:
        case AST_ENUM_ACCESS:
        case AST_BREAK_STMT:
        case AST_CONTINUE_STMT:
        case AST_IMPORT:
            break;
    }

    return hir;
}

HIRModule* hir_build_module(ASTNode* root, Arena* arena) {
    if (!root) return NULL;
    HIRModule* module = arena_alloc(arena, sizeof(HIRModule));
    module->ast_root = root;
    module->root = hir_build_node(root, arena);
    return module;
}

const char* hir_kind_name(HIRKind kind) {
    switch (kind) {
        case HIR_PROGRAM: return "program";
        case HIR_BLOCK: return "block";
        case HIR_LITERAL_NUMBER: return "number";
        case HIR_LITERAL_STRING: return "string";
        case HIR_TUPLE_LITERAL: return "tuple_literal";
        case HIR_IDENTIFIER: return "identifier";
        case HIR_BINARY_EXPR: return "binary_expr";
        case HIR_UNARY_EXPR: return "unary_expr";
        case HIR_VAR_DECL: return "var_decl";
        case HIR_FUNC_CALL: return "func_call";
        case HIR_MEMBER_ACCESS: return "member_access";
        case HIR_IF_STMT: return "if_stmt";
        case HIR_SWITCH_STMT: return "switch_stmt";
        case HIR_SWITCH_CASE: return "switch_case";
        case HIR_WHILE_STMT: return "while_stmt";
        case HIR_FOR_STMT: return "for_stmt";
        case HIR_RANGE_EXPR: return "range_expr";
        case HIR_FUNC_DECL: return "func_decl";
        case HIR_RETURN_STMT: return "return_stmt";
        case HIR_STRUCT_DECL: return "struct_decl";
        case HIR_INIT_DECL: return "init_decl";
        case HIR_STRUCT_INIT_EXPR: return "struct_init_expr";
        case HIR_NEW_EXPR: return "new_expr";
        case HIR_INDEX_EXPR: return "index_expr";
        case HIR_ARRAY_LITERAL: return "array_literal";
        case HIR_ENUM_DECL: return "enum_decl";
        case HIR_ENUM_ACCESS: return "enum_access";
        case HIR_BREAK_STMT: return "break_stmt";
        case HIR_CONTINUE_STMT: return "continue_stmt";
        case HIR_IMPORT: return "import";
        case HIR_UNION_DECL: return "union_decl";
        case HIR_BINDING_LIST: return "binding_list";
        case HIR_BINDING_ASSIGN: return "binding_assign";
        case HIR_PATTERN: return "pattern";
        case HIR_TYPE_BASE: return "type_base";
        case HIR_TYPE_POINTER: return "type_pointer";
        case HIR_TYPE_ARRAY: return "type_array";
        case HIR_TYPE_SLICE: return "type_slice";
        case HIR_TYPE_NULLABLE: return "type_nullable";
        case HIR_TYPE_MUTABLE: return "type_mutable";
        case HIR_TYPE_TUPLE: return "type_tuple";
    }
    return "unknown";
}

static const char* symbol_kind_name(SymbolKind kind) {
    switch (kind) {
        case SYM_VAR: return "var";
        case SYM_FUNC: return "func";
        case SYM_STRUCT: return "struct";
        case SYM_ENUM: return "enum";
        case SYM_VARIANT: return "variant";
        case SYM_PARAM: return "param";
        case SYM_BUILTIN: return "builtin";
        case SYM_NAMESPACE: return "namespace";
    }
    return "unknown";
}

static void hir_dump_type(FILE* out, TypeExpr* type) {
    if (!type) {
        fputs("<none>", out);
        return;
    }
    switch (type->kind) {
        case TYPE_BASE:
            fprintf(out, "%.*s", (int)type->as.base_type.length, type->as.base_type.start);
            break;
        case TYPE_POINTER:
            hir_dump_type(out, type->as.pointer.element);
            fputc('*', out);
            break;
        case TYPE_ARRAY:
            hir_dump_type(out, type->as.array.element);
            fputs("[...]", out);
            break;
        case TYPE_SLICE:
            hir_dump_type(out, type->as.slice.element);
            fputs("[]", out);
            break;
        case TYPE_NULLABLE:
            hir_dump_type(out, type->as.nullable.element);
            fputc('?', out);
            break;
        case TYPE_MUTABLE:
            hir_dump_type(out, type->as.mutable.element);
            fputc('!', out);
            break;
        case TYPE_TUPLE:
            fputc('(', out);
            for (size_t i = 0; i < type->as.tuple.count; i++) {
                if (i > 0) fputs(", ", out);
                hir_dump_type(out, type->as.tuple.elements[i]);
                if (type->as.tuple.names && type->as.tuple.names[i].length > 0) {
                    fprintf(out, " %.*s", (int)type->as.tuple.names[i].length, type->as.tuple.names[i].start);
                }
            }
            fputc(')', out);
            break;
    }
}

static void hir_dump_indent(FILE* out, int depth) {
    for (int i = 0; i < depth; i++) fputs("  ", out);
}

static void hir_dump_payload(FILE* out, HIRNode* node) {
    if (node->type_expr && !node->ast) {
        fputs(" value=", out);
        hir_dump_type(out, node->type_expr);
        return;
    }

    if (!node->ast) return;

    switch (node->ast->type) {
        case AST_IMPORT:
            if (node->ast->as.import_decl.alias.length > 0) {
                fprintf(out, " alias=%.*s", (int)node->ast->as.import_decl.alias.length,
                        node->ast->as.import_decl.alias.start);
            }
            fprintf(out, " path=%.*s", (int)node->ast->as.import_decl.path.length,
                    node->ast->as.import_decl.path.start);
            break;
        case AST_IDENTIFIER:
            fprintf(out, " name=%.*s", (int)node->ast->as.identifier.name.length,
                    node->ast->as.identifier.name.start);
            break;
        case AST_VAR_DECL:
            fprintf(out, " name=%.*s", (int)node->ast->as.var_decl.name.length,
                    node->ast->as.var_decl.name.start);
            break;
        case AST_FUNC_DECL:
            fprintf(out, " name=%.*s", (int)node->ast->as.func_decl.name.length,
                    node->ast->as.func_decl.name.start);
            break;
        case AST_STRUCT_DECL:
            fprintf(out, " name=%.*s", (int)node->ast->as.struct_decl.name.length,
                    node->ast->as.struct_decl.name.start);
            break;
        case AST_ENUM_DECL:
            fprintf(out, " name=%.*s", (int)node->ast->as.enum_decl.name.length,
                    node->ast->as.enum_decl.name.start);
            break;
        case AST_UNION_DECL:
            fprintf(out, " name=%.*s", (int)node->ast->as.union_decl.name.length,
                    node->ast->as.union_decl.name.start);
            break;
        case AST_MEMBER_ACCESS:
            fprintf(out, " member=%.*s", (int)node->ast->as.member_access.member.length,
                    node->ast->as.member_access.member.start);
            break;
        case AST_ENUM_ACCESS:
            if (node->ast->as.enum_access.enum_name.length > 0) {
                fprintf(out, " enum=%.*s", (int)node->ast->as.enum_access.enum_name.length,
                        node->ast->as.enum_access.enum_name.start);
            }
            fprintf(out, " member=%.*s", (int)node->ast->as.enum_access.member_name.length,
                    node->ast->as.enum_access.member_name.start);
            break;
        case AST_PATTERN:
            fprintf(out, " name=%.*s", (int)node->ast->as.pattern.name.length,
                    node->ast->as.pattern.name.start);
            break;
        case AST_LITERAL_NUMBER:
            fprintf(out, " value=%.0f", node->ast->as.number.value);
            break;
        case AST_LITERAL_STRING:
            fprintf(out, " value=%.*s", (int)node->ast->as.string.value.length,
                    node->ast->as.string.value.start);
            break;
        case AST_BINARY_EXPR:
            fprintf(out, " op=%d", node->ast->as.binary.op);
            break;
        case AST_UNARY_EXPR:
            fprintf(out, " op=%d", node->ast->as.unary.op);
            break;
        default:
            break;
    }
}

static void hir_dump_node(FILE* out, HIRNode* node, int depth) {
    if (!node) return;
    hir_dump_indent(out, depth);
    fprintf(out, "%s", hir_kind_name(node->kind));
    hir_dump_payload(out, node);
    fprintf(out, " line=%d", node->line);
    fputs(" type=", out);
    hir_dump_type(out, node->evaluated_type ? node->evaluated_type : node->type_expr);
    if (node->symbol) {
        fprintf(out, " symbol=%s:%s", node->symbol->name, symbol_kind_name(node->symbol->kind));
    }
    if (node->is_public) fputs(" public", out);
    fputc('\n', out);

    for (HIRNode* child = node->first_child; child; child = child->next_sibling) {
        hir_dump_node(out, child, depth + 1);
    }
}

void hir_dump(FILE* out, HIRModule* module) {
    if (!module || !module->root) return;
    hir_dump_node(out, module->root, 0);
}

HIRNode* hir_root(HIRModule* module) {
    return module ? module->root : NULL;
}

HIRNode* hir_first_child(HIRNode* node) {
    return node ? node->first_child : NULL;
}

HIRNode* hir_next_sibling(HIRNode* node) {
    return node ? node->next_sibling : NULL;
}

ASTNode* hir_source_ast(HIRNode* node) {
    return node ? node->ast : NULL;
}
