#ifndef JIANG_CODEGEN_INFO_H
#define JIANG_CODEGEN_INFO_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "ast.h"

typedef struct {
    char header_path[256];
} CodegenImportInfo;

typedef struct {
    Token name;
    TypeExpr* type;
    bool is_public;
} CodegenFieldInfo;

typedef struct {
    Token name;
    TypeExpr* type;
} CodegenParamInfo;

typedef struct {
    Token name;
    Token generated_name;
    TypeExpr* return_type;
    CodegenParamInfo* params;
    size_t param_count;
    bool is_public;
    bool is_static_method;
    bool is_method;
    bool is_init;
} CodegenFunctionInfo;

typedef struct {
    Token name;
    int64_t value;
} CodegenEnumMemberInfo;

typedef struct {
    Token name;
    TypeExpr* base_type;
    CodegenEnumMemberInfo* members;
    size_t member_count;
} CodegenEnumInfo;

typedef struct {
    Token name;
    CodegenFieldInfo* fields;
    size_t field_count;
    CodegenFunctionInfo* methods;
    size_t method_count;
    CodegenParamInfo* init_params;
    size_t init_param_count;
    bool has_init;
} CodegenStructInfo;

typedef struct {
    Token name;
    TypeExpr* type;
} CodegenUnionVariantInfo;

typedef struct {
    Token name;
    Token tag_enum;
    CodegenUnionVariantInfo* variants;
    size_t variant_count;
} CodegenUnionInfo;

typedef enum {
    CG_GLOBAL_INIT_NONE,
    CG_GLOBAL_INIT_INT,
    CG_GLOBAL_INIT_FLOAT,
    CG_GLOBAL_INIT_STRING,
    CG_GLOBAL_INIT_ENUM,
} CodegenGlobalInitKind;

typedef struct {
    Token name;
    TypeExpr* type;
    bool is_public;
    CodegenGlobalInitKind init_kind;
    int64_t int_value;
    double float_value;
    Token string_value;
    Token enum_name;
    Token enum_member;
} CodegenGlobalInfo;

typedef struct {
    CodegenImportInfo* imports;
    size_t import_count;
    CodegenEnumInfo* enums;
    size_t enum_count;
    CodegenStructInfo* structs;
    size_t struct_count;
    CodegenUnionInfo* unions;
    size_t union_count;
    CodegenFunctionInfo* functions;
    size_t function_count;
    CodegenGlobalInfo* globals;
    size_t global_count;
} CodegenModuleInfo;

#endif
