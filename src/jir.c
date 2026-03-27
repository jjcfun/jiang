#include "jir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

JirModule* jir_module_new(Arena* arena) {
    JirModule* mod = arena_alloc(arena, sizeof(JirModule));
    mod->functions = NULL;
    mod->function_count = 0;
    mod->function_capacity = 0;
    return mod;
}

JirFunction* jir_function_new(JirModule* mod, Token name) {
    JirFunction* func = malloc(sizeof(JirFunction)); // Use malloc for dynamic growth or manage in arena
    func->name = name;
    func->return_type = NULL;
    func->local_count = 0;
    func->local_capacity = 16;
    func->locals = malloc(sizeof(JirLocal) * func->local_capacity);
    func->inst_count = 0;
    func->inst_capacity = 32;
    func->insts = malloc(sizeof(JirInst) * func->inst_capacity);
    func->next_label_id = 0;

    if (mod->function_count >= mod->function_capacity) {
        mod->function_capacity = mod->function_capacity == 0 ? 8 : mod->function_capacity * 2;
        mod->functions = realloc(mod->functions, sizeof(JirFunction*) * mod->function_capacity);
    }
    mod->functions[mod->function_count++] = func;
    return func;
}

JirReg jir_alloc_local(JirFunction* func, Token name, TypeExpr* type, bool is_param, Arena* arena) {
    if (func->local_count >= func->local_capacity) {
        func->local_capacity *= 2;
        func->locals = realloc(func->locals, sizeof(JirLocal) * func->local_capacity);
    }
    JirReg reg = (JirReg)func->local_count;
    JirLocal* local = &func->locals[reg];
    snprintf(local->name, sizeof(local->name), "%.*s", (int)name.length, name.start);
    local->type = type; local->is_param = is_param; local->is_temp = false;
    func->local_count++; return reg;
}

JirReg jir_alloc_temp(JirFunction* func, TypeExpr* type, Arena* arena) {
    if (func->local_count >= func->local_capacity) {
        func->local_capacity *= 2;
        func->locals = realloc(func->locals, sizeof(JirLocal) * func->local_capacity);
    }
    JirReg reg = (JirReg)func->local_count;
    JirLocal* local = &func->locals[reg];
    snprintf(local->name, sizeof(local->name), "t%d", reg);
    local->type = type; local->is_param = false; local->is_temp = true;
    func->local_count++; return reg;
}

int jir_emit(JirFunction* func, JirOp op, JirReg dest, JirReg src1, JirReg src2, Arena* arena) {
    if (func->inst_count >= func->inst_capacity) {
        func->inst_capacity *= 2;
        func->insts = realloc(func->insts, sizeof(JirInst) * func->inst_capacity);
    }
    int idx = (int)func->inst_count++;
    memset(&func->insts[idx], 0, sizeof(JirInst));
    func->insts[idx].op = op;
    func->insts[idx].dest = dest;
    func->insts[idx].src1 = src1;
    func->insts[idx].src2 = src2;
    return idx;
}

JirReg jir_emit_call(JirFunction* func, JirReg dest, Token name, JirReg* args, size_t count, Arena* arena) {
    int idx = jir_emit(func, JIR_OP_CALL, dest, -1, -1, arena);
    func->insts[idx].payload.name = name;
    if (count > 0) {
        func->insts[idx].call_args = malloc(sizeof(JirReg) * count);
        memcpy(func->insts[idx].call_args, args, sizeof(JirReg) * count);
        func->insts[idx].call_arg_count = count;
    }
    return dest;
}

void jir_emit_label(JirFunction* func, uint32_t label_id, Arena* arena) {
    int idx = jir_emit(func, JIR_OP_LABEL, -1, -1, -1, arena);
    func->insts[idx].payload.label_id = label_id;
}

uint32_t jir_reserve_label(JirFunction* func) {
    return func->next_label_id++;
}

void jir_print_module(JirModule* mod) {
    for (size_t i = 0; i < mod->function_count; i++) {
        JirFunction* f = mod->functions[i];
        printf("\nFunction %.*s:\n", (int)f->name.length, f->name.start);
        printf("  Locals:\n");
        for (size_t j = 0; j < f->local_count; j++) {
            printf("    %%%zu: %s %s\n", j, f->locals[j].name, f->locals[j].is_param ? "(param)" : (f->locals[j].is_temp ? "(temp)" : ""));
        }
        printf("  Instructions:\n");
        for (size_t j = 0; j < f->inst_count; j++) {
            JirInst* inst = &f->insts[j];
            switch (inst->op) {
                case JIR_OP_LABEL: printf("    L%u:\n", inst->payload.label_id); break;
                case JIR_OP_CONST_INT: printf("    %%%d = CONST_INT %lld\n", inst->dest, (long long)inst->payload.int_val); break;
                case JIR_OP_CALL: printf("    %%%d = CALL '%.*s'\n", inst->dest, (int)inst->payload.name.length, inst->payload.name.start); break;
                default: printf("    OP %d\n", inst->op); break;
            }
        }
    }
}
