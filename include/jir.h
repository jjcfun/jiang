#ifndef JIANG_JIR_H
#define JIANG_JIR_H

#include "hir.h"

typedef struct JirExpr JirExpr;
typedef struct JirFunction JirFunction;
typedef struct JirType JirType;
typedef struct JirBinding JirBinding;

typedef enum JirBindingKind {
    JIR_BINDING_PARAM = 0,
    JIR_BINDING_LOCAL,
    JIR_BINDING_GLOBAL,
} JirBindingKind;

typedef enum JirTypeKind {
    JIR_TYPE_INT = 0,
    JIR_TYPE_BOOL,
    JIR_TYPE_VOID,
    JIR_TYPE_ENUM,
    JIR_TYPE_STRUCT,
    JIR_TYPE_TUPLE,
    JIR_TYPE_ARRAY,
    JIR_TYPE_UNION,
} JirTypeKind;

typedef enum JirExprKind {
    JIR_EXPR_INT = 0,
    JIR_EXPR_BOOL,
    JIR_EXPR_BINDING,
    JIR_EXPR_BINARY,
    JIR_EXPR_TERNARY,
    JIR_EXPR_CALL,
    JIR_EXPR_ENUM_MEMBER,
    JIR_EXPR_VARIANT,
    JIR_EXPR_ENUM_VALUE,
    JIR_EXPR_UNION_TAG,
    JIR_EXPR_UNION_FIELD,
    JIR_EXPR_STRUCT,
    JIR_EXPR_STRUCT_FIELD,
    JIR_EXPR_TUPLE,
    JIR_EXPR_ARRAY,
    JIR_EXPR_INDEX,
} JirExprKind;

typedef enum JirBinaryOp {
    JIR_BIN_ADD = 0,
    JIR_BIN_SUB,
    JIR_BIN_MUL,
    JIR_BIN_DIV,
    JIR_BIN_EQ,
    JIR_BIN_NE,
    JIR_BIN_LT,
    JIR_BIN_LE,
    JIR_BIN_GT,
    JIR_BIN_GE,
} JirBinaryOp;

typedef enum JirBuiltinKind {
    JIR_BUILTIN_NONE = 0,
    JIR_BUILTIN_ASSERT,
    JIR_BUILTIN_PRINT,
    JIR_BUILTIN_PANIC,
} JirBuiltinKind;

typedef struct JirTypeList {
    JirType** items;
    int count;
    int capacity;
} JirTypeList;

typedef struct JirStructFieldDecl {
    char* name;
    JirType* type;
    int mutable_flag;
} JirStructFieldDecl;

typedef struct JirStructFieldDeclList {
    JirStructFieldDecl* items;
    int count;
    int capacity;
} JirStructFieldDeclList;

struct JirType {
    JirTypeKind kind;
    JirTypeList tuple_items;
    JirType* array_item;
    int array_length;
    JirStructFieldDeclList struct_fields;
    int union_payload_slots;
};

struct JirBinding {
    JirType* type;
    char* name;
    JirBindingKind kind;
    int line;
};

typedef struct JirBindingList {
    JirBinding** items;
    int count;
    int capacity;
} JirBindingList;

typedef struct JirExprList {
    JirExpr** items;
    int count;
    int capacity;
} JirExprList;

typedef struct JirStructFieldInit {
    int field_index;
    JirExpr* value;
} JirStructFieldInit;

typedef struct JirStructFieldInitList {
    JirStructFieldInit* items;
    int count;
    int capacity;
} JirStructFieldInitList;

struct JirExpr {
    JirExprKind kind;
    JirType* type;
    int line;
    union {
        int64_t int_value;
        int bool_value;
        JirBinding* binding;
        struct {
            JirBinaryOp op;
            JirExpr* left;
            JirExpr* right;
        } binary;
        struct {
            JirExpr* cond;
            JirExpr* then_expr;
            JirExpr* else_expr;
        } ternary;
        struct {
            JirFunction* callee;
            JirBuiltinKind builtin;
            JirExprList args;
        } call;
        struct {
            int64_t value;
        } enum_member;
        struct {
            int tag_value;
            JirExpr* payload;
        } variant;
        struct {
            JirExpr* value;
        } enum_value;
        struct {
            JirExpr* value;
        } union_tag;
        struct {
            JirExpr* value;
            int field_index;
        } union_field;
        struct {
            JirStructFieldInitList fields;
        } struct_lit;
        struct {
            JirExpr* base;
            int field_index;
        } struct_field;
        struct {
            JirExprList items;
        } tuple;
        struct {
            JirExprList items;
        } array;
        struct {
            JirExpr* base;
            JirExpr* index;
        } index;
    } as;
};

typedef struct JirGlobal {
    JirBinding* binding;
    JirExpr* init;
    int line;
} JirGlobal;

typedef struct JirGlobalList {
    JirGlobal* items;
    int count;
    int capacity;
} JirGlobalList;

struct JirFunction {
    JirType* return_type;
    char* name;
    char* method_name;
    JirBindingList params;
    JirBindingList locals;
    int struct_init_flag;
    int method_flag;
    int static_method_flag;
    char* owner_name;
    JirType* receiver_type;
    int line;
};

typedef struct JirFunctionList {
    JirFunction* items;
    int count;
    int capacity;
} JirFunctionList;

typedef enum JirInstKind {
    JIR_INST_PARAM = 0,
    JIR_INST_VAR_DECL,
    JIR_INST_ASSIGN,
    JIR_INST_EXPR,
} JirInstKind;

typedef enum JirTermKind {
    JIR_TERM_FALLTHROUGH = 0,
    JIR_TERM_RETURN,
    JIR_TERM_BRANCH,
    JIR_TERM_COND_BRANCH,
} JirTermKind;

typedef struct JirInst {
    JirInstKind kind;
    int line;
    JirBinding* binding;
    JirExpr* expr;
    JirExpr* target;
    JirExpr* value;
} JirInst;

typedef struct JirInstList {
    JirInst* items;
    int count;
    int capacity;
} JirInstList;

typedef struct JirBasicBlockRef {
    int index;
    char* name;
} JirBasicBlockRef;

typedef struct JirTerminator {
    JirTermKind kind;
    JirExpr* value;
    JirExpr* cond;
    JirBasicBlockRef then_target;
    JirBasicBlockRef else_target;
} JirTerminator;

typedef struct JirBasicBlock {
    char* name;
    JirInstList insts;
    JirTerminator term;
} JirBasicBlock;

typedef struct JirBasicBlockList {
    JirBasicBlock* items;
    int count;
    int capacity;
} JirBasicBlockList;

typedef struct JirLoweredFunction {
    char* name;
    JirFunction* function;
    JirBasicBlockList blocks;
} JirLoweredFunction;

typedef struct JirLoweredFunctionList {
    JirLoweredFunction* items;
    int count;
    int capacity;
} JirLoweredFunctionList;

typedef struct JirProgram {
    JirGlobalList globals;
    JirFunctionList functions;
    JirLoweredFunctionList lowered_functions;
} JirProgram;

#endif
