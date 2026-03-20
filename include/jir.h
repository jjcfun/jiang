#ifndef JIANG_JIR_H
#define JIANG_JIR_H

#include "ast.h"
#include "arena.h"
#include <stdint.h>
#include <stdbool.h>

// JIR Register/Local index. -1 indicates invalid/none.
typedef int32_t JirReg;

// ── Opcodes ───────────────────────────────────────────────────────────────
typedef enum {
    // Memory / Constants
    JIR_OP_CONST_INT,     // dest = const int
    JIR_OP_CONST_FLOAT,   // dest = const float
    JIR_OP_CONST_STR,     // dest = const string
    JIR_OP_LOAD_SYM,      // dest = load global symbol/variable
    JIR_OP_STORE,         // *dest = src1

    // Arithmetic & Logic
    JIR_OP_ADD,           // dest = src1 + src2
    JIR_OP_SUB,           // dest = src1 - src2
    JIR_OP_MUL,           // dest = src1 * src2
    JIR_OP_DIV,           // dest = src1 / src2
    JIR_OP_NOT,           // dest = !src1
    JIR_OP_NEG,           // dest = -src1

    // Comparison
    JIR_OP_CMP_EQ,        // dest = src1 == src2
    JIR_OP_CMP_NEQ,       // dest = src1 != src2
    JIR_OP_CMP_LT,        // dest = src1 < src2
    JIR_OP_CMP_GT,        // dest = src1 > src2
    JIR_OP_CMP_LTE,       // dest = src1 <= src2
    JIR_OP_CMP_GTE,       // dest = src1 >= src2

    // Control Flow
    JIR_OP_JUMP,          // goto label (src1)
    JIR_OP_JUMP_IF_FALSE, // if !src1 goto label (src2)
    JIR_OP_LABEL,         // label marker
    JIR_OP_RETURN,        // return src1
    JIR_OP_CALL,          // dest = call src1(args...)

    // Structures & Unions
    JIR_OP_STRUCT_INIT,   // dest = init struct (complex, might need special handling)
    JIR_OP_MEMBER_ACCESS, // dest = src1.member
    JIR_OP_GET_TAG,       // dest = get union tag of src1
    JIR_OP_SET_TAG,       // src1.tag = src2
    JIR_OP_EXTRACT_FIELD, // dest = extract union variant field from src1
    JIR_OP_SET_FIELD,     // src1.as.variant.field = src2
    JIR_OP_ADDR_OF,       // dest = &src1
    JIR_OP_STORE_MEMBER,  // src1.member = src2
} JirOpCode;

// ── Instructions ──────────────────────────────────────────────────────────
typedef struct {
    JirOpCode op;
    JirReg dest;
    JirReg src1;
    JirReg src2;
    
    // For constants, labels, or member names that don't fit in registers
    union {
        int64_t int_val;
        double float_val;
        const char* str_val;
        Token name; // Useful for member access or symbol lookup
        uint32_t label_id;
    } payload;
    
    // For Call instructions, we might need a variable length array of arguments.
    // In a flat IR, arguments are often pushed onto a virtual stack or linked list,
    // but for simplicity we can store a pointer to an array of registers.
    JirReg* call_args;
    size_t call_arg_count;
} JirInst;

// ── Locals & Registers ────────────────────────────────────────────────────
typedef struct {
    char name[64];        // Empty if temporary
    TypeExpr* type;       // Evaluated type
    bool is_param;        // Is this a function parameter?
    bool is_temp;         // Is this a compiler-generated temp variable?
    // Could add offset or storage info for LLVM/C code gen
} JirLocal;

// ── Functions & Modules ───────────────────────────────────────────────────
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
    
    // Also need to store global variables, structs, unions...
    // For now, we can rely on semantic.c's symbol table for global lookup,
    // or explicitly copy global declarations into the JIR Module.
} JirModule;

// ── API ───────────────────────────────────────────────────────────────────

JirModule* jir_create_module(Arena* arena);
JirFunction* jir_create_function(JirModule* mod, Token name, TypeExpr* ret_type, Arena* arena);

// Allocate a new local or temp register
JirReg jir_alloc_local(JirFunction* func, Token name, TypeExpr* type, bool is_param, Arena* arena);
JirReg jir_alloc_temp(JirFunction* func, TypeExpr* type, Arena* arena);

// Emit an instruction. Returns the index of the emitted instruction.
int jir_emit(JirFunction* func, JirOpCode op, JirReg dest, JirReg src1, JirReg src2, Arena* arena);

// Emit a call instruction with arguments managed by arena
JirReg jir_emit_call(JirFunction* func, JirReg dest, Token name, JirReg* args, size_t count, Arena* arena);

// Debug: Print the IR
void jir_print_module(JirModule* mod);

#endif // JIANG_JIR_H