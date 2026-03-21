#ifndef JIANG_AST_H
#define JIANG_AST_H

#include "lexer.h"
#include "arena.h"
#include <stdbool.h>

typedef struct ASTNode ASTNode;

typedef enum {
    TYPE_BASE,
    TYPE_POINTER,
    TYPE_ARRAY,
    TYPE_SLICE,
    TYPE_NULLABLE,
    TYPE_MUTABLE,
    TYPE_TUPLE
} TypeKind;

typedef struct TypeExpr TypeExpr;

struct TypeExpr {
    TypeKind kind;
    union {
        Token base_type; // For TYPE_BASE
        struct {
            TypeExpr* element;
        } pointer; // For TYPE_POINTER
        struct {
            TypeExpr* element;
            ASTNode* length; // For TYPE_ARRAY
        } array;
        struct {
            TypeExpr* element;
        } slice; // For TYPE_SLICE
        struct {
            TypeExpr* element;
        } nullable; // For TYPE_NULLABLE
        struct {
            TypeExpr* element;
        } mutable; // For TYPE_MUTABLE
        struct {
            struct TypeExpr** elements;
            Token* names; // Optional, can be NULL
            size_t count;
        } tuple; // For TYPE_TUPLE
    } as;
};

typedef enum {
    AST_PROGRAM,
    AST_BLOCK,
    AST_LITERAL_NUMBER,
    AST_LITERAL_STRING,
    AST_TUPLE_LITERAL,
    AST_IDENTIFIER,
    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_VAR_DECL,
    AST_FUNC_CALL,
    AST_MEMBER_ACCESS,
    AST_IF_STMT,
    AST_WHILE_STMT,
    AST_FOR_STMT,
    AST_RANGE_EXPR,
    AST_FUNC_DECL,
    AST_RETURN_STMT,
    AST_STRUCT_DECL,
    AST_INIT_DECL,
    AST_STRUCT_INIT_EXPR,
    AST_NEW_EXPR,
    AST_INDEX_EXPR,
    AST_ARRAY_LITERAL,
    AST_ENUM_DECL,
    AST_ENUM_ACCESS,
    AST_BREAK_STMT,
    AST_CONTINUE_STMT,
    AST_IMPORT,
    AST_UNION_DECL,
    AST_BINDING_LIST,
    AST_BINDING_ASSIGN,
    AST_PATTERN
    } NodeType;

    typedef enum {
        SYM_VAR,
        SYM_FUNC,
        SYM_STRUCT,
        SYM_ENUM,
        SYM_VARIANT,
        SYM_PARAM,
        SYM_BUILTIN,
        SYM_NAMESPACE,
    } SymbolKind;

    typedef struct Symbol {
        char        name[128];
        SymbolKind  kind;
        TypeExpr*   type;
        void*       module_ptr;
        void*       module_owner;
        int         defined_line;
        bool        is_public;
        struct Symbol* next;
    } Symbol;

    typedef struct ASTNode ASTNode;
    struct ASTNode {
    NodeType type;
    int line; // Line number for error reporting
    bool is_public; // Visibility flag
    TypeExpr* evaluated_type; // Type information determined during semantic analysis
    Symbol* symbol;    // <--- Explicit binding to resolved symbol

    // Union to hold varying node data depending on type
    union {
        // AST_PATTERN
        struct {
            TypeExpr* type; // NULL for inferred '_'
            Token name;
            Token field_name; // Field name being deconstructed (if any)
            int field_index;  // Field index being deconstructed
        } pattern;

        // AST_PROGRAM or AST_BLOCK
        struct {
            ASTNode** statements;
            size_t count;
            size_t capacity;
        } block;

        // AST_BINDING_LIST
        struct {
            ASTNode** items;
            size_t count;
        } binding_list;

        // AST_IMPORT
        struct {
            bool is_public;
            Token alias; // Optional
            Token path;  // String literal
            char resolved_path[256]; // <--- Added: store absolute path
        } import_decl;

        // AST_LITERAL_NUMBER
        struct {
            double value;
        } number;

        // AST_LITERAL_STRING
        struct {
            Token value;
        } string;

        // AST_TUPLE_LITERAL
        struct {
            ASTNode** elements;
            size_t count;
        } tuple_literal;

        // AST_IDENTIFIER
        struct {
            Token name;
        } identifier;

        // AST_BINARY_EXPR
        struct {
            ASTNode* left;
            ASTNode* right;
            TokenType op;
        } binary;

        // AST_UNARY_EXPR
        struct {
            ASTNode* right;
            TokenType op;
        } unary;

        // AST_VAR_DECL
        struct {
            TypeExpr* type;
            Token name;
            ASTNode* initializer;
        } var_decl;

        // AST_FUNC_CALL
        struct {
            ASTNode* callee;
            ASTNode** args;
            size_t arg_count;
        } func_call;

        // AST_MEMBER_ACCESS
        struct {
            ASTNode* object;
            Token member;
        } member_access;

        // AST_NEW_EXPR
        struct {
            ASTNode* value;
        } new_expr;

        // AST_IF_STMT
        struct {
            ASTNode* condition;
            ASTNode* then_branch;
            ASTNode* else_branch;
        } if_stmt;

        // AST_WHILE_STMT
        struct {
            ASTNode* condition;
            ASTNode* body;
        } while_stmt;

        // AST_FOR_STMT
        struct {
            ASTNode* pattern;
            ASTNode* iterable;
            ASTNode* body;
        } for_stmt;

        // AST_RANGE_EXPR
        struct {
            ASTNode* start;
            ASTNode* end;
        } range;

        // AST_FUNC_DECL
        struct {
            TypeExpr* return_type;
            Token name;
            ASTNode** params;
            size_t param_count;
            ASTNode* body;
        } func_decl;

        // AST_RETURN_STMT
        struct {
            ASTNode* expression;
        } return_stmt;

        // AST_STRUCT_DECL
        struct {
            Token name;
            ASTNode** members;
            size_t member_count;
        } struct_decl;

        // AST_INIT_DECL (Constructor)
        struct {
            ASTNode** params;
            size_t param_count;
            ASTNode* body;
        } init_decl;

        // AST_STRUCT_INIT_EXPR
        struct {
            TypeExpr* type; // Optional
            Token* field_names;
            ASTNode** field_values;
            size_t field_count;
        } struct_init;

        // AST_INDEX_EXPR
        struct {
            ASTNode* object;
            ASTNode* index;
        } index_expr;

        // AST_ARRAY_LITERAL
        struct {
            TypeExpr* type; // Optional
            ASTNode** elements;
            size_t element_count;
        } array_literal;

        // AST_ENUM_DECL
        struct {
            Token name;
            TypeExpr* base_type;
            Token* member_names;
            ASTNode** member_values;
            size_t member_count;
        } enum_decl;

        // AST_ENUM_ACCESS
        struct {
            Token enum_name; // (Token){0} if inferred
            Token member_name;
        } enum_access;

        // AST_UNION_DECL
        struct {
            Token name;
            Token tag_enum; // Optional
            ASTNode** members;
            size_t member_count;
        } union_decl;

        // AST_BINDING_ASSIGN
        struct {
            ASTNode* bindings;
            ASTNode* value;
        } binding_assign;
    } as;
};

// Create a new TypeExpr
TypeExpr* type_new_base(Arena* arena, Token token);
TypeExpr* type_new_modifier(Arena* arena, TypeKind kind, TypeExpr* element);
TypeExpr* type_new_tuple(Arena* arena, TypeExpr** elements, Token* names, size_t count);

// Utility to print TypeExpr
void type_print(TypeExpr* type);

// Create a new AST node allocated in the given arena
ASTNode* ast_new_node(Arena* arena, NodeType type, int line);

// Debug helper to print the AST tree structure
void ast_print(ASTNode* node, int depth);

#endif // JIANG_AST_H
