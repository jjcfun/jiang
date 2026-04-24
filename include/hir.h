#ifndef JIANG_HIR_H
#define JIANG_HIR_H

#include "hashmap.h"

#include <stdint.h>

typedef struct HirType HirType;
typedef struct HirExpr HirExpr;
typedef struct HirStmt HirStmt;
typedef struct HirFunction HirFunction;
typedef struct HirBinding HirBinding;
typedef struct HirTryCatch HirTryCatch;
#ifndef JIANG_AST_EXPR_FWD_DECL
#define JIANG_AST_EXPR_FWD_DECL
typedef struct AstExpr AstExpr;
#endif
typedef struct HirEnumDecl HirEnumDecl;
typedef struct HirEnumMember HirEnumMember;
typedef struct HirUnionDecl HirUnionDecl;
typedef struct HirUnionVariant HirUnionVariant;
typedef struct HirStructDecl HirStructDecl;
typedef struct HirStructField HirStructField;
typedef struct HirErrorableEntry HirErrorableEntry;

typedef struct HirTypeList {
    HirType** items;
    int count;
    int capacity;
} HirTypeList;

typedef enum HirTypeKind {
    HIR_TYPE_INT = 0,
    HIR_TYPE_I8,
    HIR_TYPE_I16,
    HIR_TYPE_I32,
    HIR_TYPE_I64,
    HIR_TYPE_U8,
    HIR_TYPE_U16,
    HIR_TYPE_U32,
    HIR_TYPE_U64,
    HIR_TYPE_F16,
    HIR_TYPE_F32,
    HIR_TYPE_F64,
    HIR_TYPE_FLOAT,
    HIR_TYPE_DOUBLE,
    HIR_TYPE_CHARACTER,
    HIR_TYPE_UINT8,
    HIR_TYPE_STRING,
    HIR_TYPE_BOOL,
    HIR_TYPE_VOID,
    HIR_TYPE_SLICE,
    HIR_TYPE_REFERENCE,
    HIR_TYPE_POINTER,
    HIR_TYPE_MANY_POINTER,
    HIR_TYPE_ENUM,
    HIR_TYPE_STRUCT,
    HIR_TYPE_TUPLE,
    HIR_TYPE_ARRAY,
    HIR_TYPE_UNION,
    HIR_TYPE_OPTIONAL,
    HIR_TYPE_FUNCTION,
} HirTypeKind;

struct HirType {
    HirTypeKind kind;
    int mutable_flag;
    HirTypeList tuple_items;
    HirType* array_item;
    int array_length;
    HirType* return_type;
    HirEnumDecl* enum_decl;
    HirStructDecl* struct_decl;
    HirUnionDecl* union_decl;
};

typedef enum HirBindingKind {
    HIR_BINDING_PARAM = 0,
    HIR_BINDING_LOCAL,
    HIR_BINDING_GLOBAL,
} HirBindingKind;

struct HirBinding {
    HirType* type;
    char* label;
    char* name;
    AstExpr* default_value;
    int mutable_flag;
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
    HIR_EXPR_FLOAT,
    HIR_EXPR_CHAR,
    HIR_EXPR_BOOL,
    HIR_EXPR_CSTRING,
    HIR_EXPR_NULL,
    HIR_EXPR_OPTIONAL_SOME,
    HIR_EXPR_BINDING,
    HIR_EXPR_FUNCTION,
    HIR_EXPR_AS,
    HIR_EXPR_ADDR,
    HIR_EXPR_DEREF,
    HIR_EXPR_NEW,
    HIR_EXPR_FREE,
    HIR_EXPR_BINARY,
    HIR_EXPR_COALESCE,
    HIR_EXPR_CATCH_FALLBACK,
    HIR_EXPR_TERNARY,
    HIR_EXPR_CALL,
    HIR_EXPR_PROPAGATE,
    HIR_EXPR_ENUM_MEMBER,
    HIR_EXPR_VARIANT,
    HIR_EXPR_ENUM_VALUE,
    HIR_EXPR_UNION_TAG,
    HIR_EXPR_UNION_FIELD,
    HIR_EXPR_OPTIONAL_VALUE,
    HIR_EXPR_STRUCT,
    HIR_EXPR_STRUCT_FIELD,
    HIR_EXPR_TUPLE,
    HIR_EXPR_ARRAY,
    HIR_EXPR_INDEX,
    HIR_EXPR_SLICE,
    HIR_EXPR_SLICE_LENGTH,
} HirExprKind;

typedef enum HirBinaryOp {
    HIR_BIN_ADD = 0,
    HIR_BIN_SUB,
    HIR_BIN_MUL,
    HIR_BIN_MOD,
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
    HIR_BUILTIN_ALLOC,
    HIR_BUILTIN_ALLOC_ARRAY,
    HIR_BUILTIN_POINTER_OFFSET,
    HIR_BUILTIN_EQUAL,
    HIR_BUILTIN_HASH,
} HirBuiltinKind;

typedef struct HirExprList {
    HirExpr** items;
    int count;
    int capacity;
} HirExprList;

typedef struct HirStructFieldInit {
    HirStructField* field;
    HirExpr* value;
} HirStructFieldInit;

typedef struct HirStructFieldInitList {
    HirStructFieldInit* items;
    int count;
    int capacity;
} HirStructFieldInitList;

struct HirExpr {
    HirExprKind kind;
    HirType* type;
    int line;
    union {
        int64_t int_value;
        double float_value;
        int64_t char_value;
        int bool_value;
        struct {
            char* text;
            int length;
        } cstring_lit;
        HirBinding* binding;
        HirFunction* function;
        struct {
            HirExpr* value;
        } unary;
        struct {
            HirBinaryOp op;
            HirExpr* left;
            HirExpr* right;
        } binary;
        struct {
            HirExpr* left;
            HirExpr* right;
        } coalesce;
        struct {
            HirExpr* left;
            HirExpr* fallback;
        } catch_fallback;
        struct {
            HirExpr* cond;
            HirExpr* then_expr;
            HirExpr* else_expr;
        } ternary;
        struct {
            HirFunction* callee;
            HirExpr* callee_value;
            HirBuiltinKind builtin;
            HirExprList args;
        } call;
        struct {
            HirExpr* value;
            HirType* result_type;
        } propagate;
        struct {
            HirEnumMember* member;
        } enum_member;
        struct {
            HirUnionVariant* variant;
            HirExpr* payload;
        } variant;
        struct {
            HirExpr* value;
        } enum_value;
        struct {
            HirExpr* value;
        } union_tag;
        struct {
            HirExpr* value;
            HirUnionVariant* variant;
            int field_index;
        } union_field;
        struct {
            HirExpr* value;
        } optional_value;
        struct {
            HirStructDecl* struct_decl;
            HirStructFieldInitList fields;
        } struct_lit;
        struct {
            HirExpr* base;
            HirStructField* field;
            int field_index;
        } struct_field;
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
        struct {
            HirExpr* base;
        } slice;
        struct {
            HirExpr* base;
        } slice_length;
    } as;
};

typedef enum HirStmtKind {
    HIR_STMT_RETURN = 0,
    HIR_STMT_THROW,
    HIR_STMT_VAR_DECL,
    HIR_STMT_ASSIGN,
    HIR_STMT_IF,
    HIR_STMT_TRY,
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

struct HirTryCatch {
    HirType* error_type;
    HirBinding* binding;
    HirBlock body;
};

typedef struct HirTryCatchList {
    HirTryCatch* items;
    int count;
    int capacity;
} HirTryCatchList;

struct HirStmt {
    HirStmtKind kind;
    int line;
    union {
        struct {
            HirExpr* expr;
        } ret;
        struct {
            HirExpr* expr;
        } throw_stmt;
        struct {
            HirBinding* binding;
            HirExpr* init;
        } var_decl;
        struct {
            HirBinding* binding;
            HirExpr* target;
            HirExpr* value;
        } assign;
        struct {
            HirExpr* cond;
            HirBlock then_block;
            HirBlock else_block;
            int has_else;
        } if_stmt;
        struct {
            HirBlock try_body;
            HirTryCatchList catches;
        } try_stmt;
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
    char* method_name;
    HirBindingList params;
    HirBindingList locals;
    HirBlock body;
    int struct_init_flag;
    int struct_deinit_flag;
    int extern_flag;
    int public_flag;
    int method_flag;
    int static_method_flag;
    int struct_init_index;
    HirStructDecl* owner_struct;
    HirType* receiver_type;
    int line;
};

typedef struct HirFunctionList {
    HirFunction* items;
    int count;
    int capacity;
} HirFunctionList;

struct HirEnumMember {
    char* name;
    int64_t value;
};

typedef struct HirEnumMemberList {
    HirEnumMember* items;
    int count;
    int capacity;
} HirEnumMemberList;

struct HirEnumDecl {
    char* name;
    HirEnumMemberList members;
};

typedef struct HirEnumList {
    HirEnumDecl* items;
    int count;
    int capacity;
} HirEnumList;

struct HirStructField {
    char* name;
    HirType* type;
    int mutable_flag;
};

typedef struct HirStructFieldList {
    HirStructField* items;
    int count;
    int capacity;
} HirStructFieldList;

struct HirStructDecl {
    char* name;
    HirStructFieldList fields;
    int record_flag;
    int has_init;
    int init_count;
    int from_string_literal;
    int has_deinit;
    char* deinit_name;
};

typedef struct HirStructList {
    HirStructDecl* items;
    int count;
    int capacity;
} HirStructList;

struct HirUnionVariant {
    char* name;
    HirType* payload_type;
    int tag_value;
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
    int64_t payload_size;
    int64_t payload_align;
    int errorable_flag;
};

typedef struct HirUnionList {
    HirUnionDecl* items;
    int count;
    int capacity;
} HirUnionList;

struct HirErrorableEntry {
    HirType* value_type;
    HirType* error_type;
    HirType* result_type;
    HirUnionDecl* union_decl;
};

typedef struct HirErrorableEntryList {
    HirErrorableEntry* items;
    int count;
    int capacity;
} HirErrorableEntryList;

typedef struct HirGlobal {
    HirBinding* binding;
    HirExpr* init;
    int extern_flag;
    int line;
} HirGlobal;

typedef struct HirGlobalList {
    HirGlobal* items;
    int count;
    int capacity;
} HirGlobalList;

typedef struct HirProgram {
    HirStructList structs;
    HirEnumList enums;
    HirUnionList unions;
    HirGlobalList globals;
    HirFunctionList functions;
    HirType int_type;
    HirType i8_type;
    HirType i16_type;
    HirType i32_type;
    HirType i64_type;
    HirType u8_type;
    HirType u16_type;
    HirType u32_type;
    HirType u64_type;
    HirType f16_type;
    HirType f32_type;
    HirType f64_type;
    HirType float_type;
    HirType double_type;
    HirType character_type;
    HirType uint8_type;
    HirType string_type;
    HirType bool_type;
    HirType void_type;
    HirTypeList owned_types;
    HirErrorableEntryList errorable_types;
    HashMap global_map;
    HashMap function_map;
    HashMap type_name_map;
    HashMap struct_name_map;
    HashMap enum_name_map;
    HashMap enum_member_map;
    HashMap union_name_map;
    HashMap variant_map;
} HirProgram;

#endif
