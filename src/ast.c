#include "ast.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>

ASTNode* ast_new_node(Arena* arena, NodeType type, int line) {
    ASTNode* node = (ASTNode*)arena_alloc(arena, sizeof(ASTNode));
    if (node) {
        node->type = type;
        node->line = line;
        node->evaluated_type = NULL;
        memset(&node->as, 0, sizeof(node->as)); // Initialize union
    }
    return node;
}

TypeExpr* type_new_base(Arena* arena, Token token) {
    TypeExpr* type = (TypeExpr*)arena_alloc(arena, sizeof(TypeExpr));
    type->kind = TYPE_BASE;
    type->as.base_type = token;
    return type;
}

TypeExpr* type_new_modifier(Arena* arena, TypeKind kind, TypeExpr* element) {
    TypeExpr* type = (TypeExpr*)arena_alloc(arena, sizeof(TypeExpr));
    type->kind = kind;
    if (kind == TYPE_POINTER) type->as.pointer.element = element;
    else if (kind == TYPE_ARRAY) type->as.array.element = element;
    else if (kind == TYPE_SLICE) type->as.slice.element = element;
    else if (kind == TYPE_NULLABLE) type->as.nullable.element = element;
    else if (kind == TYPE_MUTABLE) type->as.mutable.element = element;
    return type;
}

TypeExpr* type_new_tuple(Arena* arena, TypeExpr** elements, Token* names, size_t count) {
    TypeExpr* type = (TypeExpr*)arena_alloc(arena, sizeof(TypeExpr));
    type->kind = TYPE_TUPLE;
    type->as.tuple.elements = elements;
    type->as.tuple.names = names;
    type->as.tuple.count = count;
    return type;
}

void type_print(TypeExpr* type) {
    if (!type) return;
    switch (type->kind) {
        case TYPE_BASE:
            printf("%.*s", (int)type->as.base_type.length, type->as.base_type.start);
            break;
        case TYPE_POINTER:
            type_print(type->as.pointer.element);
            printf("*");
            break;
        case TYPE_ARRAY:
            type_print(type->as.array.element);
            printf("[");
            if (type->as.array.length) {
                // If it's a literal number, print it, otherwise just ...
                if (type->as.array.length->type == AST_LITERAL_NUMBER) {
                    printf("%.0f", type->as.array.length->as.number.value);
                } else {
                    printf("...");
                }
            }
            printf("]");
            break;
        case TYPE_SLICE:
            type_print(type->as.slice.element);
            printf("[]");
            break;
        case TYPE_NULLABLE:
            type_print(type->as.nullable.element);
            printf("?");
            break;
        case TYPE_MUTABLE:
            type_print(type->as.mutable.element);
            printf("!");
            break;
        case TYPE_TUPLE:
            printf("(");
            for (size_t i = 0; i < type->as.tuple.count; i++) {
                type_print(type->as.tuple.elements[i]);
                if (type->as.tuple.names && type->as.tuple.names[i].length > 0) {
                    printf(" %.*s", (int)type->as.tuple.names[i].length, type->as.tuple.names[i].start);
                }
                if (i < type->as.tuple.count - 1) printf(", ");
            }
            printf(")");
            break;
    }
}

static void print_indent(int depth) {
    for (int i = 0; i < depth; i++) {
        printf("  ");
    }
}

void ast_print(ASTNode* node, int depth) {
    if (!node) return;

    print_indent(depth);
    switch (node->type) {
        case AST_PATTERN:
            printf("Pattern (Type: ");
            if (node->as.pattern.type) type_print(node->as.pattern.type);
            else printf("_");
            printf(", Name: '%.*s')\n", (int)node->as.pattern.name.length, node->as.pattern.name.start);
            break;
        case AST_BINDING_LIST:
            printf("BindingList (\n");
            for (size_t i = 0; i < node->as.binding_list.count; i++) {
                ast_print(node->as.binding_list.items[i], depth + 1);
            }
            print_indent(depth);
            printf(")\n");
            break;
        case AST_PROGRAM:
            printf("Program [\n");
            for (size_t i = 0; i < node->as.block.count; i++) {
                ast_print(node->as.block.statements[i], depth + 1);
            }
            print_indent(depth);
            printf("]\n");
            break;
        case AST_BLOCK:
            printf("Block {\n");
            for (size_t i = 0; i < node->as.block.count; i++) {
                ast_print(node->as.block.statements[i], depth + 1);
            }
            print_indent(depth);
            printf("}\n");
            break;
        case AST_LITERAL_NUMBER:
            printf("Number(%.2f)\n", node->as.number.value);
            break;
        case AST_LITERAL_STRING:
            printf("String(\"%.*s\")\n", (int)node->as.string.value.length, node->as.string.value.start);
            break;
        case AST_TUPLE_LITERAL:
            printf("TupleLiteral (\n");
            for (size_t i = 0; i < node->as.tuple_literal.count; i++) {
                ast_print(node->as.tuple_literal.elements[i], depth + 1);
            }
            print_indent(depth);
            printf(")\n");
            break;
        case AST_IDENTIFIER:
            printf("Identifier(%.*s)\n", (int)node->as.identifier.name.length, node->as.identifier.name.start);
            break;
        case AST_BINARY_EXPR:
            printf("BinaryExpr (\n");
            print_indent(depth + 1);
            printf("Operator: %d\n", node->as.binary.op); // Simplify for now
            ast_print(node->as.binary.left, depth + 1);
            ast_print(node->as.binary.right, depth + 1);
            print_indent(depth);
            printf(")\n");
            break;
        case AST_VAR_DECL:
            printf("VarDecl (Type: ");
            type_print(node->as.var_decl.type);
            printf(", Name: '%.*s') = \n", (int)node->as.var_decl.name.length, node->as.var_decl.name.start);
            ast_print(node->as.var_decl.initializer, depth + 1);
            break;
        case AST_BINDING_ASSIGN:
            printf("BindingAssign (\n");
            print_indent(depth + 1);
            printf("Bindings:\n");
            ast_print(node->as.binding_assign.bindings, depth + 2);
            print_indent(depth + 1);
            printf("Value:\n");
            ast_print(node->as.binding_assign.value, depth + 2);
            print_indent(depth);
            printf(")\n");
            break;
        case AST_FUNC_CALL:
            printf("FuncCall (\n");
            ast_print(node->as.func_call.callee, depth + 1);
            print_indent(depth + 1);
            printf("Args [\n");
            for (size_t i = 0; i < node->as.func_call.arg_count; i++) {
                ast_print(node->as.func_call.args[i], depth + 2);
            }
            print_indent(depth + 1);
            printf("]\n");
            print_indent(depth);
            printf(")\n");
            break;
        case AST_MEMBER_ACCESS:
            printf("MemberAccess (\n");
            ast_print(node->as.member_access.object, depth + 1);
            print_indent(depth + 1);
            printf("Member: %.*s\n", (int)node->as.member_access.member.length, node->as.member_access.member.start);
            print_indent(depth);
            printf(")\n");
            break;
        case AST_IF_STMT:
            printf("IfStmt (\n");
            print_indent(depth + 1);
            printf("Condition:\n");
            ast_print(node->as.if_stmt.condition, depth + 2);
            print_indent(depth + 1);
            printf("Then:\n");
            ast_print(node->as.if_stmt.then_branch, depth + 2);
            if (node->as.if_stmt.else_branch) {
                print_indent(depth + 1);
                printf("Else:\n");
                ast_print(node->as.if_stmt.else_branch, depth + 2);
            }
            print_indent(depth);
            printf(")\n");
            break;
        case AST_WHILE_STMT:
            printf("WhileStmt (\n");
            print_indent(depth + 1);
            printf("Condition:\n");
            ast_print(node->as.while_stmt.condition, depth + 2);
            print_indent(depth + 1);
            printf("Body:\n");
            ast_print(node->as.while_stmt.body, depth + 2);
            print_indent(depth);
            printf(")\n");
            break;
        case AST_FOR_STMT:
            printf("ForStmt (\n");
            print_indent(depth + 1);
            printf("Pattern:\n");
            ast_print(node->as.for_stmt.pattern, depth + 2);
            print_indent(depth + 1);
            printf("Iterable:\n");
            ast_print(node->as.for_stmt.iterable, depth + 2);
            print_indent(depth + 1);
            printf("Body:\n");
            ast_print(node->as.for_stmt.body, depth + 2);
            print_indent(depth);
            printf(")\n");
            break;
        case AST_RANGE_EXPR:
            printf("RangeExpr (\n");
            ast_print(node->as.range.start, depth + 1);
            print_indent(depth + 1);
            printf("..\n");
            ast_print(node->as.range.end, depth + 1);
            print_indent(depth);
            printf(")\n");
            break;
        case AST_FUNC_DECL:
            printf("FuncDecl (ReturnType: ");
            type_print(node->as.func_decl.return_type);
            printf(", Name: '%.*s')\n", (int)node->as.func_decl.name.length, node->as.func_decl.name.start);
            print_indent(depth + 1);
            printf("Params [\n");
            for (size_t i = 0; i < node->as.func_decl.param_count; i++) {
                ast_print(node->as.func_decl.params[i], depth + 2);
            }
            print_indent(depth + 1);
            printf("]\n");
            print_indent(depth + 1);
            printf("Body:\n");
            ast_print(node->as.func_decl.body, depth + 2);
            break;
        case AST_RETURN_STMT:
            printf("ReturnStmt (\n");
            if (node->as.return_stmt.expression) {
                ast_print(node->as.return_stmt.expression, depth + 1);
            }
            print_indent(depth);
            printf(")\n");
            break;
        case AST_STRUCT_DECL:
            printf("StructDecl (Name: '%.*s')\n", (int)node->as.struct_decl.name.length, node->as.struct_decl.name.start);
            print_indent(depth + 1);
            printf("Members [\n");
            for (size_t i = 0; i < node->as.struct_decl.member_count; i++) {
                ast_print(node->as.struct_decl.members[i], depth + 2);
            }
            print_indent(depth + 1);
            printf("]\n");
            break;
        case AST_INIT_DECL:
            printf("InitDecl (\n");
            print_indent(depth + 1);
            printf("Params [\n");
            for (size_t i = 0; i < node->as.init_decl.param_count; i++) {
                ast_print(node->as.init_decl.params[i], depth + 2);
            }
            print_indent(depth + 1);
            printf("]\n");
            print_indent(depth + 1);
            printf("Body:\n");
            ast_print(node->as.init_decl.body, depth + 2);
            print_indent(depth);
            printf(")\n");
            break;
        case AST_STRUCT_INIT_EXPR:
            printf("StructInitExpr (\n");
            if (node->as.struct_init.type) {
                print_indent(depth + 1);
                printf("Type: ");
                type_print(node->as.struct_init.type);
                printf("\n");
            }
            print_indent(depth + 1);
            printf("Fields [\n");
            for (size_t i = 0; i < node->as.struct_init.field_count; i++) {
                print_indent(depth + 2);
                printf("Field '%.*s':\n", (int)node->as.struct_init.field_names[i].length, node->as.struct_init.field_names[i].start);
                ast_print(node->as.struct_init.field_values[i], depth + 3);
            }
            print_indent(depth + 1);
            printf("]\n");
            print_indent(depth);
            printf(")\n");
            break;
        case AST_NEW_EXPR:
            printf("NewExpr (\n");
            ast_print(node->as.new_expr.value, depth + 1);
            print_indent(depth);
            printf(")\n");
            break;
        case AST_INDEX_EXPR:
            printf("IndexExpr (\n");
            print_indent(depth + 1);
            printf("Object:\n");
            ast_print(node->as.index_expr.object, depth + 2);
            print_indent(depth + 1);
            printf("Index:\n");
            ast_print(node->as.index_expr.index, depth + 2);
            print_indent(depth);
            printf(")\n");
            break;
        case AST_ARRAY_LITERAL:
            printf("ArrayLiteral [\n");
            for (size_t i = 0; i < node->as.array_literal.element_count; i++) {
                ast_print(node->as.array_literal.elements[i], depth + 1);
            }
            print_indent(depth);
            printf("]\n");
            break;
        case AST_ENUM_DECL:
            printf("EnumDecl (Name: '%.*s')\n", (int)node->as.enum_decl.name.length, node->as.enum_decl.name.start);
            if (node->as.enum_decl.base_type) {
                print_indent(depth + 1);
                printf("Base: ");
                type_print(node->as.enum_decl.base_type);
                printf("\n");
            }
            print_indent(depth + 1);
            printf("Members [\n");
            for (size_t i = 0; i < node->as.enum_decl.member_count; i++) {
                print_indent(depth + 2);
                printf("Member '%.*s'", (int)node->as.enum_decl.member_names[i].length, node->as.enum_decl.member_names[i].start);
                if (node->as.enum_decl.member_values[i]) {
                    printf(" = \n");
                    ast_print(node->as.enum_decl.member_values[i], depth + 3);
                } else {
                    printf("\n");
                }
            }
            print_indent(depth + 1);
            printf("]\n");
            break;
        case AST_ENUM_ACCESS:
            printf("EnumAccess (%.*s.%.*s)\n", 
                (int)node->as.enum_access.enum_name.length, node->as.enum_access.enum_name.start,
                (int)node->as.enum_access.member_name.length, node->as.enum_access.member_name.start);
            break;
        default:
            printf("Unknown node type %d\n", node->type);
            break;
    }
}
