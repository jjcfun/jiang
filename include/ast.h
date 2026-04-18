#ifndef JIANG_AST_H
#define JIANG_AST_H

#include <stdint.h>

typedef struct AstType AstType;
typedef struct AstExpr AstExpr;
typedef struct AstStmt AstStmt;
typedef struct AstBindingPattern AstBindingPattern;
typedef struct AstSwitchCase AstSwitchCase;
typedef struct AstBlock AstBlock;
typedef struct AstStructField AstStructField;
typedef struct AstStructDecl AstStructDecl;

typedef struct AstTypeList {
    AstType* items;
    int count;
    int capacity;
} AstTypeList;

typedef enum AstTypeKind {
    AST_TYPE_INT = 0,
    AST_TYPE_UINT8,
    AST_TYPE_BOOL,
    AST_TYPE_VOID,
    AST_TYPE_INFER,
    AST_TYPE_NAMED,
    AST_TYPE_TUPLE,
    AST_TYPE_ARRAY,
} AstTypeKind;

struct AstType {
    AstTypeKind kind;
    int mutable_flag;
    AstTypeList tuple_items;
    char* named_name;
    AstType* array_item;
    int array_length;
};

typedef struct AstExprList {
    AstExpr** items;
    int count;
    int capacity;
} AstExprList;

typedef struct AstStructFieldInit {
    char* name;
    AstExpr* value;
    int line;
} AstStructFieldInit;

typedef struct AstStructFieldInitList {
    AstStructFieldInit* items;
    int count;
    int capacity;
} AstStructFieldInitList;

typedef enum AstExprKind {
    AST_EXPR_INT = 0,
    AST_EXPR_BOOL,
    AST_EXPR_STRING,
    AST_EXPR_NAME,
    AST_EXPR_BINARY,
    AST_EXPR_TERNARY,
    AST_EXPR_CALL,
    AST_EXPR_VARIANT,
    AST_EXPR_FIELD,
    AST_EXPR_STRUCT,
    AST_EXPR_TUPLE,
    AST_EXPR_ARRAY,
    AST_EXPR_INDEX,
} AstExprKind;

typedef enum AstBinaryOp {
    AST_BIN_ADD = 0,
    AST_BIN_SUB,
    AST_BIN_MUL,
    AST_BIN_DIV,
    AST_BIN_EQ,
    AST_BIN_NE,
    AST_BIN_LT,
    AST_BIN_LE,
    AST_BIN_GT,
    AST_BIN_GE,
} AstBinaryOp;

typedef struct AstEnumMember {
    char* name;
    int has_value;
    int64_t value;
    int line;
} AstEnumMember;

typedef struct AstEnumMemberList {
    AstEnumMember* items;
    int count;
    int capacity;
} AstEnumMemberList;

struct AstStructField {
    AstType type;
    char* name;
    AstExpr* default_value;
    int line;
};

typedef struct AstStructFieldList {
    AstStructField* items;
    int count;
    int capacity;
} AstStructFieldList;

typedef struct AstBindingPatternList {
    AstBindingPattern** items;
    int count;
    int capacity;
} AstBindingPatternList;

struct AstExpr {
    AstExprKind kind;
    int line;
    union {
        int64_t int_value;
        int bool_value;
        struct {
            char* text;
            int length;
        } string_lit;
        char* name;
        struct {
            AstBinaryOp op;
            AstExpr* left;
            AstExpr* right;
        } binary;
        struct {
            AstExpr* cond;
            AstExpr* then_expr;
            AstExpr* else_expr;
        } ternary;
        struct {
            char* callee;
            AstExprList args;
        } call;
        struct {
            char* union_name;
            char* variant_name;
            AstExpr* payload;
            AstBindingPatternList bindings;
            int pattern_flag;
        } variant;
        struct {
            AstExpr* base;
            char* name;
        } field;
        struct {
            char* type_name;
            AstStructFieldInitList fields;
        } struct_lit;
        struct {
            AstExprList items;
        } tuple;
        struct {
            AstExprList items;
        } array;
        struct {
            AstExpr* base;
            AstExpr* index;
        } index;
    } as;
};

typedef enum AstBindingPatternKind {
    AST_BINDING_NAME = 0,
    AST_BINDING_TUPLE,
} AstBindingPatternKind;

struct AstBindingPattern {
    AstBindingPatternKind kind;
    int line;
    AstType type;
    char* name;
    AstBindingPatternList items;
};

typedef struct AstStmtList {
    AstStmt** items;
    int count;
    int capacity;
} AstStmtList;

struct AstBlock {
    AstStmtList stmts;
};

struct AstSwitchCase {
    AstExpr* pattern;
    AstBlock body;
    int is_else;
};

typedef struct AstSwitchCaseList {
    AstSwitchCase* items;
    int count;
    int capacity;
} AstSwitchCaseList;

typedef struct AstParam {
    AstType type;
    char* name;
    int line;
} AstParam;

typedef struct AstParamList {
    AstParam* items;
    int count;
    int capacity;
} AstParamList;

typedef enum AstStmtKind {
    AST_STMT_RETURN = 0,
    AST_STMT_VAR_DECL,
    AST_STMT_ASSIGN,
    AST_STMT_IF,
    AST_STMT_SWITCH,
    AST_STMT_WHILE,
    AST_STMT_FOR_RANGE,
    AST_STMT_FOR_EACH,
    AST_STMT_BREAK,
    AST_STMT_CONTINUE,
    AST_STMT_EXPR,
    AST_STMT_DESTRUCTURE,
} AstStmtKind;

struct AstStmt {
    AstStmtKind kind;
    int line;
    union {
        struct {
            AstExpr* expr;
        } ret;
        struct {
            AstType type;
            char* name;
            AstExpr* init;
        } var_decl;
        struct {
            AstExpr* target;
            AstExpr* value;
        } assign;
        struct {
            AstExpr* cond;
            AstBlock then_block;
            AstBlock else_block;
            int has_else;
        } if_stmt;
        struct {
            AstExpr* value;
            AstSwitchCaseList cases;
        } switch_stmt;
        struct {
            AstExpr* cond;
            AstBlock body;
        } while_stmt;
        struct {
            AstType type;
            char* name;
            AstExpr* start;
            AstExpr* end;
            AstBlock body;
        } for_range;
        struct {
            AstBindingPattern* pattern;
            AstExpr* iterable;
            AstBlock body;
            int indexed_flag;
        } for_each;
        struct {
            AstExpr* expr;
        } expr_stmt;
        struct {
            AstParamList bindings;
            AstExpr* init;
        } destructure;
    } as;
};

typedef struct AstFunction {
    AstType return_type;
    char* name;
    AstParamList params;
    AstBlock body;
    int struct_init_flag;
    int method_flag;
    int static_method_flag;
    char* owner_type_name;
    int line;
} AstFunction;

struct AstEnumDecl {
    char* name;
    AstEnumMemberList members;
    int line;
};

struct AstStructDecl {
    char* name;
    AstStructFieldList fields;
    AstParamList init_params;
    AstBlock init_body;
    int has_init;
    int init_line;
    int line;
};

typedef struct AstStructList {
    AstStructDecl* items;
    int count;
    int capacity;
} AstStructList;
typedef struct AstEnumDecl AstEnumDecl;

typedef struct AstEnumList {
    AstEnumDecl* items;
    int count;
    int capacity;
} AstEnumList;

typedef struct AstUnionVariant {
    AstType type;
    char* name;
    int line;
} AstUnionVariant;

typedef struct AstUnionVariantList {
    AstUnionVariant* items;
    int count;
    int capacity;
} AstUnionVariantList;

struct AstUnionDecl {
    char* tag_name;
    char* name;
    AstUnionVariantList variants;
    int line;
};
typedef struct AstUnionDecl AstUnionDecl;

typedef struct AstUnionList {
    AstUnionDecl* items;
    int count;
    int capacity;
} AstUnionList;

typedef struct AstFunctionList {
    AstFunction* items;
    int count;
    int capacity;
} AstFunctionList;

typedef struct AstGlobal {
    AstType type;
    char* name;
    AstExpr* init;
    int line;
} AstGlobal;

typedef struct AstGlobalList {
    AstGlobal* items;
    int count;
    int capacity;
} AstGlobalList;

typedef struct AstProgram {
    AstStructList structs;
    AstEnumList enums;
    AstUnionList unions;
    AstGlobalList globals;
    AstFunctionList functions;
} AstProgram;

#endif
