#ifndef JIANG_JIR_H
#define JIANG_JIR_H

#include "ast.h"
#include "arena.h"
#include <stdint.h>
#include <stdbool.h>

typedef int JirReg;

typedef enum {
    JIR_OP_LABEL,
    JIR_OP_CONST_INT,
    JIR_OP_CONST_FLOAT,
    JIR_OP_CONST_STR,
    JIR_OP_LOAD_SYM,      // dest = sym_name (global or field)
    JIR_OP_STORE,         // dest = src1
    JIR_OP_STORE_PTR,     // *dest = src1
    JIR_OP_STORE_SYM,     // sym_name = src1
    JIR_OP_STORE_MEMBER,  // dest[src2] = src1 or dest.field = src1
    JIR_OP_ADD,
    JIR_OP_SUB,
    JIR_OP_MUL,
    JIR_OP_DIV,
    JIR_OP_CMP_EQ,
    JIR_OP_CMP_NEQ,
    JIR_OP_CMP_LT,
    JIR_OP_CMP_GT,
    JIR_OP_CMP_LTE,
    JIR_OP_CMP_GTE,
    JIR_OP_JUMP,
    JIR_OP_JUMP_IF_FALSE,
    JIR_OP_CALL,          // dest = call src1(args...)
    JIR_OP_RETURN,
    JIR_OP_ADDR_OF,
    JIR_OP_DEREF,
    JIR_OP_MALLOC,
    JIR_OP_FREE,
    JIR_OP_ARRAY_INIT,
    JIR_OP_STRUCT_INIT,
    JIR_OP_MEMBER_ACCESS,
    JIR_OP_MAKE_SLICE,
    JIR_OP_GET_TAG,
    JIR_OP_SET_TAG,
    JIR_OP_EXTRACT_FIELD,
    JIR_OP_SET_FIELD
} JirOp;

typedef struct {
    JirOp op;
    int dest;
    int src1;
    int src2;
    union {
        int64_t int_val;
        double float_val;
        const char* str_val;
        uint32_t label_id;
        Token name;
    } payload;
    int* call_args;
    size_t call_arg_count;
} JirInst;

typedef struct {
    char name[64];
    TypeExpr* type;
    bool is_param;
    bool is_temp;
} JirLocal;

typedef struct {
    Token name;
    TypeExpr* return_type;
    JirLocal* locals;
    size_t local_count;
    size_t local_capacity;
    JirInst* insts;
    size_t inst_count;
    size_t inst_capacity;
    uint32_t next_label_id;
} JirFunction;

typedef struct {
    JirFunction** functions;
    size_t function_count;
    size_t function_capacity;
} JirModule;

JirModule* jir_module_new(Arena* arena);
JirFunction* jir_function_new(JirModule* mod, Token name);
JirReg jir_alloc_local(JirFunction* func, Token name, TypeExpr* type, bool is_param, Arena* arena);
JirReg jir_alloc_temp(JirFunction* func, TypeExpr* type, Arena* arena);
int jir_emit(JirFunction* func, JirOp op, JirReg dest, JirReg src1, JirReg src2, Arena* arena);
JirReg jir_emit_call(JirFunction* func, JirReg dest, Token name, JirReg* args, size_t count, Arena* arena);
void jir_emit_label(JirFunction* func, uint32_t label_id, Arena* arena);
uint32_t jir_reserve_label(JirFunction* func);
void jir_print_module(JirModule* mod);

#endif
