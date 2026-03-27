#ifndef JIANG_HIR_H
#define JIANG_HIR_H

#include <stdio.h>
#include <stdbool.h>
#include "ast.h"
#include "arena.h"

typedef enum {
    HIR_PROGRAM,
    HIR_BLOCK,
    HIR_LITERAL_NUMBER,
    HIR_LITERAL_STRING,
    HIR_TUPLE_LITERAL,
    HIR_IDENTIFIER,
    HIR_BINARY_EXPR,
    HIR_UNARY_EXPR,
    HIR_VAR_DECL,
    HIR_FUNC_CALL,
    HIR_MEMBER_ACCESS,
    HIR_IF_STMT,
    HIR_SWITCH_STMT,
    HIR_SWITCH_CASE,
    HIR_WHILE_STMT,
    HIR_FOR_STMT,
    HIR_RANGE_EXPR,
    HIR_FUNC_DECL,
    HIR_RETURN_STMT,
    HIR_STRUCT_DECL,
    HIR_INIT_DECL,
    HIR_STRUCT_INIT_EXPR,
    HIR_NEW_EXPR,
    HIR_INDEX_EXPR,
    HIR_ARRAY_LITERAL,
    HIR_ENUM_DECL,
    HIR_ENUM_ACCESS,
    HIR_BREAK_STMT,
    HIR_CONTINUE_STMT,
    HIR_IMPORT,
    HIR_UNION_DECL,
    HIR_BINDING_LIST,
    HIR_BINDING_ASSIGN,
    HIR_PATTERN,
    HIR_TYPE_BASE,
    HIR_TYPE_POINTER,
    HIR_TYPE_ARRAY,
    HIR_TYPE_SLICE,
    HIR_TYPE_NULLABLE,
    HIR_TYPE_MUTABLE,
    HIR_TYPE_TUPLE,
} HIRKind;

typedef struct HIRNode HIRNode;
typedef struct HIRModule HIRModule;

struct HIRNode {
    HIRKind kind;
    int line;
    bool is_public;
    TypeExpr* evaluated_type;
    Symbol* symbol;
    ASTNode* ast;
    TypeExpr* type_expr;
    HIRNode* first_child;
    HIRNode* last_child;
    HIRNode* next_sibling;
};

struct HIRModule {
    ASTNode* ast_root;
    HIRNode* root;
};

HIRModule* hir_build_module(ASTNode* root, Arena* arena);
void hir_dump(FILE* out, HIRModule* module);

const char* hir_kind_name(HIRKind kind);
HIRNode* hir_root(HIRModule* module);
HIRNode* hir_first_child(HIRNode* node);
HIRNode* hir_next_sibling(HIRNode* node);
ASTNode* hir_source_ast(HIRNode* node);

#endif
