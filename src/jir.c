#include "jir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

JirModule* jir_create_module(Arena* arena) {
    JirModule* mod = arena_alloc(arena, sizeof(JirModule));
    mod->functions = arena_alloc(arena, sizeof(JirFunction*) * 64);
    mod->function_count = 0;
    mod->function_capacity = 64;
    return mod;
}

JirFunction* jir_create_function(JirModule* mod, Token name, TypeExpr* ret_type, Arena* arena) {
    JirFunction* func = arena_alloc(arena, sizeof(JirFunction));
    func->name = name;
    func->return_type = ret_type;
    func->local_capacity = 64;
    func->locals = arena_alloc(arena, sizeof(JirLocal) * func->local_capacity);
    func->local_count = 0;
    func->inst_capacity = 128;
    func->insts = arena_alloc(arena, sizeof(JirInst) * func->inst_capacity);
    func->inst_count = 0;
    func->next_label_id = 0;
    if (mod->function_count >= mod->function_capacity) {
        size_t nc = mod->function_capacity * 2;
        JirFunction** nf = arena_alloc(arena, sizeof(JirFunction*) * nc);
        memcpy(nf, mod->functions, sizeof(JirFunction*) * mod->function_count);
        mod->functions = nf; mod->function_capacity = nc;
    }
    mod->functions[mod->function_count++] = func;
    return func;
}

JirReg jir_alloc_local(JirFunction* func, Token name, TypeExpr* type, bool is_param, Arena* arena) {
    if (func->local_count >= func->local_capacity) {
        size_t nc = func->local_capacity * 2;
        JirLocal* nl = arena_alloc(arena, sizeof(JirLocal) * nc);
        memcpy(nl, func->locals, sizeof(JirLocal) * func->local_count);
        func->locals = nl; func->local_capacity = nc;
    }
    JirReg reg = (JirReg)func->local_count;
    JirLocal* local = &func->locals[reg];
    if (name.length > 0) snprintf(local->name, sizeof(local->name), "%.*s", (int)name.length, name.start);
    else local->name[0] = '\0';
    local->type = type; local->is_param = is_param; local->is_temp = false;
    func->local_count++; return reg;
}

JirReg jir_alloc_temp(JirFunction* func, TypeExpr* type, Arena* arena) {
    if (func->local_count >= func->local_capacity) {
        size_t nc = func->local_capacity * 2;
        JirLocal* nl = arena_alloc(arena, sizeof(JirLocal) * nc);
        memcpy(nl, func->locals, sizeof(JirLocal) * func->local_count);
        func->locals = nl; func->local_capacity = nc;
    }
    JirReg reg = (JirReg)func->local_count;
    JirLocal* local = &func->locals[reg];
    snprintf(local->name, sizeof(local->name), "t%d", reg);
    local->type = type; local->is_param = false; local->is_temp = true;
    func->local_count++; return reg;
}

int jir_emit(JirFunction* func, JirOpCode op, JirReg dest, JirReg src1, JirReg src2, Arena* arena) {
    if (func->inst_count >= func->inst_capacity) {
        size_t nc = func->inst_capacity * 2;
        JirInst* ni = arena_alloc(arena, sizeof(JirInst) * nc);
        memcpy(ni, func->insts, sizeof(JirInst) * func->inst_count);
        func->insts = ni; func->inst_capacity = nc;
    }
    int idx = (int)func->inst_count;
    JirInst* inst = &func->insts[idx];
    inst->op = op; inst->dest = dest; inst->src1 = src1; inst->src2 = src2;
    inst->call_args = NULL; inst->call_arg_count = 0; memset(&inst->payload, 0, sizeof(inst->payload));
    func->inst_count++; return idx;
}

JirReg jir_emit_call(JirFunction* func, JirReg dest, Token name, JirReg* args, size_t count, Arena* arena) {
    int idx = jir_emit(func, JIR_OP_CALL, dest, -1, -1, arena);
    func->insts[idx].payload.name = name;
    func->insts[idx].call_arg_count = count;
    if (count > 0) {
        func->insts[idx].call_args = arena_alloc(arena, sizeof(JirReg) * count);
        memcpy(func->insts[idx].call_args, args, sizeof(JirReg) * count);
    }
    return dest;
}

static const char* op_to_str(JirOpCode op) {
    switch (op) {
        case JIR_OP_CONST_INT: return "CONST_INT";
        case JIR_OP_CONST_FLOAT: return "CONST_FLOAT";
        case JIR_OP_CONST_STR: return "CONST_STR";
        case JIR_OP_LOAD_SYM: return "LOAD_SYM";
        case JIR_OP_STORE: return "STORE";
        case JIR_OP_ADD: return "ADD";
        case JIR_OP_SUB: return "SUB";
        case JIR_OP_MUL: return "MUL";
        case JIR_OP_DIV: return "DIV";
        case JIR_OP_NOT: return "NOT";
        case JIR_OP_NEG: return "NEG";
        case JIR_OP_CMP_EQ: return "CMP_EQ";
        case JIR_OP_CMP_NEQ: return "CMP_NEQ";
        case JIR_OP_CMP_LT: return "CMP_LT";
        case JIR_OP_CMP_GT: return "CMP_GT";
        case JIR_OP_CMP_LTE: return "CMP_LTE";
        case JIR_OP_CMP_GTE: return "CMP_GTE";
        case JIR_OP_JUMP: return "JUMP";
        case JIR_OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
        case JIR_OP_LABEL: return "LABEL";
        case JIR_OP_RETURN: return "RETURN";
        case JIR_OP_CALL: return "CALL";
        case JIR_OP_STRUCT_INIT: return "STRUCT_INIT";
        case JIR_OP_MEMBER_ACCESS: return "MEMBER_ACCESS";
        case JIR_OP_GET_TAG: return "GET_TAG";
        case JIR_OP_EXTRACT_FIELD: return "EXTRACT_FIELD";
        default: return "UNKNOWN";
    }
}

void jir_print_module(JirModule* mod) {
    printf("\n=== JIR Module ===\n");
    for (size_t i = 0; i < mod->function_count; i++) {
        JirFunction* func = mod->functions[i];
        printf("\nFunction %.*s:\n", (int)func->name.length, func->name.start);
        printf("  Locals:\n");
        for (size_t j = 0; j < func->local_count; j++) {
            JirLocal* l = &func->locals[j];
            printf("    %%%zu: %s %s\n", j, l->name, l->is_param ? "(param)" : (l->is_temp ? "(temp)" : ""));
        }
        printf("  Instructions:\n");
        for (size_t j = 0; j < func->inst_count; j++) {
            JirInst* inst = &func->insts[j]; printf("    ");
            if (inst->op == JIR_OP_LABEL) { printf("L%u:\n", inst->payload.label_id); continue; }
            if (inst->dest >= 0) printf("%%%d = ", inst->dest);
            printf("%s ", op_to_str(inst->op));
            if (inst->src1 >= 0) printf("%%%d ", inst->src1);
            if (inst->src2 >= 0) printf("%%%d ", inst->src2);
            if (inst->op == JIR_OP_CONST_INT) printf("%lld", (long long)inst->payload.int_val);
            else if (inst->op == JIR_OP_CONST_FLOAT) printf("%f", inst->payload.float_val);
            else if (inst->op == JIR_OP_CONST_STR) printf("\"%s\"", inst->payload.str_val);
            else if (inst->op == JIR_OP_JUMP || inst->op == JIR_OP_JUMP_IF_FALSE) printf("-> L%u", inst->payload.label_id);
            else if (inst->op == JIR_OP_LOAD_SYM || inst->op == JIR_OP_MEMBER_ACCESS || inst->op == JIR_OP_EXTRACT_FIELD || inst->op == JIR_OP_CALL || inst->op == JIR_OP_CMP_EQ) {
                if (inst->payload.name.length > 0) printf("'%.*s'", (int)inst->payload.name.length, inst->payload.name.start);
            }
            if (inst->op == JIR_OP_CALL && inst->call_args) {
                printf("(");
                for (size_t k = 0; k < inst->call_arg_count; k++) { printf("%%%d", inst->call_args[k]); if (k < inst->call_arg_count - 1) printf(", "); }
                printf(")");
            }
            printf("\n");
        }
    }
    printf("==================\n\n");
}
