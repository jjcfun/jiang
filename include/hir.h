#ifndef JIANG_HIR_H
#define JIANG_HIR_H

#include "hashmap.h"

#include <stdint.h>

typedef struct HirType HirType;
typedef struct HirExpr HirExpr;
typedef struct HirStmt HirStmt;
typedef struct HirFunction HirFunction;
typedef struct HirBinding HirBinding;
typedef struct HirUnionDecl HirUnionDecl;
typedef struct HirUnionVariant HirUnionVariant;

typedef struct HirTypeList {
    HirType** items;
    int count;
    int capacity;
} HirTypeList;

typedef enum HirTypeKind {
    HIR_TYPE_INT = 0,
    HIR_TYPE_BOOL,
    HIR_TYPE_VOID,
    HIR_TYPE_TUPLE,
    HIR_TYPE_ARRAY,
    HIR_TYPE_UNION,
} HirTypeKind;

struct HirType {
    HirTypeKind kind;
    HirTypeList tuple_items;
    HirType* array_item;
    int array_length;
    HirUnionDecl* union_decl;
};

typedef enum HirBindingKind {
    HIR_BINDING_PARAM = 0,
    HIR_BINDING_LOCAL,
    HIR_BINDING_GLOBAL,
} HirBindingKind;

struct HirBinding {
    HirType* type;
    char* name;
    HirBindingKind kind;
    int line;
};

typedef struct HirBindingList {
    HirBinding** items;
    int count;
    int capacity;
} HirBindingList;

typedef enum HirExprKind {
    HIR_EXPR_INT = 0,
    HIR_EXPR_BOOL,
    HIR_EXPR_BINDING,
    HIR_EXPR_BINARY,
    HIR_EXPR_CALL,
    HIR_EXPR_VARIANT,
    HIR_EXPR_UNION_TAG,
    HIR_EXPR_UNION_FIELD,
    HIR_EXPR_TUPLE,
    HIR_EXPR_ARRAY,
    HIR_EXPR_INDEX,
} HirExprKind;

typedef enum HirBinaryOp {
    HIR_BIN_ADD = 0,
    HIR_BIN_SUB,
    HIR_BIN_MUL,
    HIR_BIN_DIV,
    HIR_BIN_EQ,
    HIR_BIN_NE,
    HIR_BIN_LT,
    HIR_BIN_LE,
    HIR_BIN_GT,
    HIR_BIN_GE,
} HirBinaryOp;

typedef enum HirBuiltinKind {
    HIR_BUILTIN_NONE = 0,
    HIR_BUILTIN_ASSERT,
    HIR_BUILTIN_PRINT,
    HIR_BUILTIN_PANIC,
} HirBuiltinKind;

typedef struct HirExprList {
    HirExpr** items;
    int count;
    int capacity;
} HirExprList;

struct HirExpr {
    HirExprKind kind;
    HirType* type;
    int line;
    union {
        int64_t int_value;
        int bool_value;
        HirBinding* binding;
        struct {
            HirBinaryOp op;
            HirExpr* left;
            HirExpr* right;
        } binary;
        struct {
            HirFunction* callee;
            HirBuiltinKind builtin;
            HirExprList args;
        } call;
        struct {
            HirUnionVariant* variant;
            HirExpr* payload;
        } variant;
        struct {
            HirExpr* value;
        } union_tag;
        struct {
            HirExpr* value;
            HirUnionVariant* variant;
            int field_index;
        } union_field;
        struct {
            HirExprList items;
        } tuple;
        struct {
            HirExprList items;
        } array;
        struct {
            HirExpr* base;
            HirExpr* index;
        } index;
    } as;
};

typedef enum HirStmtKind {
    HIR_STMT_RETURN = 0,
    HIR_STMT_VAR_DECL,
    HIR_STMT_ASSIGN,
    HIR_STMT_IF,
    HIR_STMT_WHILE,
    HIR_STMT_FOR_RANGE,
    HIR_STMT_BREAK,
    HIR_STMT_CONTINUE,
    HIR_STMT_EXPR,
} HirStmtKind;

typedef struct HirStmtList {
    HirStmt** items;
    int count;
    int capacity;
} HirStmtList;

typedef struct HirBlock {
    HirStmtList stmts;
} HirBlock;

struct HirStmt {
    HirStmtKind kind;
    int line;
    union {
        struct {
            HirExpr* expr;
        } ret;
        struct {
            HirBinding* binding;
            HirExpr* init;
        } var_decl;
        struct {
            HirBinding* binding;
            HirExpr* value;
        } assign;
        struct {
            HirExpr* cond;
            HirBlock then_block;
            HirBlock else_block;
            int has_else;
        } if_stmt;
        struct {
            HirExpr* cond;
            HirBlock body;
        } while_stmt;
        struct {
            HirBinding* binding;
            HirExpr* start;
            HirExpr* end;
            HirBlock body;
        } for_range;
        struct {
            HirExpr* expr;
        } expr_stmt;
    } as;
};

struct HirFunction {
    HirType* return_type;
    char* name;
    HirBindingList params;
    HirBindingList locals;
    HirBlock body;
    int line;
};

typedef struct HirFunctionList {
    HirFunction* items;
    int count;
    int capacity;
} HirFunctionList;

struct HirUnionVariant {
    char* name;
    HirType* payload_type;
    int tag_value;
    int payload_slots;
};

typedef struct HirUnionVariantList {
    HirUnionVariant* items;
    int count;
    int capacity;
} HirUnionVariantList;

struct HirUnionDecl {
    char* name;
    char* tag_name;
    HirUnionVariantList variants;
    int payload_slots;
};

typedef struct HirUnionList {
    HirUnionDecl* items;
    int count;
    int capacity;
} HirUnionList;

typedef struct HirGlobal {
    HirBinding* binding;
    HirExpr* init;
    int line;
} HirGlobal;

typedef struct HirGlobalList {
    HirGlobal* items;
    int count;
    int capacity;
} HirGlobalList;

typedef struct HirProgram {
    HirUnionList unions;
    HirGlobalList globals;
    HirFunctionList functions;
    HirType int_type;
    HirType bool_type;
    HirType void_type;
    HirTypeList owned_types;
    HashMap global_map;
    HashMap function_map;
    HashMap type_name_map;
    HashMap union_name_map;
    HashMap variant_map;
} HirProgram;

#endif
