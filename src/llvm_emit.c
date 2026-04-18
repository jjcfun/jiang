#include "llvm_emit.h"

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <stdlib.h>
#include <string.h>

static LLVMValueRef get_or_add_abort(LLVMModuleRef module, LLVMContextRef context) {
    LLVMValueRef fn = LLVMGetNamedFunction(module, "abort");
    if (!fn) {
        fn = LLVMAddFunction(module, "abort", LLVMFunctionType(LLVMVoidTypeInContext(context), 0, 0, 0));
    }
    return fn;
}

static LLVMValueRef get_or_add_printf(LLVMModuleRef module, LLVMContextRef context) {
    LLVMValueRef fn = LLVMGetNamedFunction(module, "printf");
    if (!fn) {
        LLVMTypeRef args[1];
        args[0] = LLVMPointerType(LLVMInt8TypeInContext(context), 0);
        fn = LLVMAddFunction(module, "printf", LLVMFunctionType(LLVMInt32TypeInContext(context), args, 1, 1));
    }
    return fn;
}

static LLVMValueRef get_or_add_trap(LLVMModuleRef module, LLVMContextRef context) {
    LLVMValueRef fn = LLVMGetNamedFunction(module, "llvm.trap");
    if (!fn) {
        fn = LLVMAddFunction(module, "llvm.trap", LLVMFunctionType(LLVMVoidTypeInContext(context), 0, 0, 0));
    }
    return fn;
}

static LLVMValueRef get_or_add_format_string(LLVMModuleRef module, LLVMContextRef context, const char* name, const char* value) {
    LLVMValueRef global = LLVMGetNamedGlobal(module, name);
    if (!global) {
        LLVMValueRef init = LLVMConstStringInContext(context, value, (unsigned)strlen(value), 1);
        LLVMTypeRef ty = LLVMTypeOf(init);
        global = LLVMAddGlobal(module, ty, name);
        LLVMSetInitializer(global, init);
        LLVMSetGlobalConstant(global, 1);
        LLVMSetLinkage(global, LLVMPrivateLinkage);
        LLVMSetUnnamedAddress(global, LLVMGlobalUnnamedAddr);
    }
    return global;
}

static LLVMValueRef gep_cstr(LLVMBuilderRef builder, LLVMValueRef global) {
    LLVMValueRef zero = LLVMConstInt(LLVMInt32Type(), 0, 0);
    LLVMValueRef idx[2] = { zero, zero };
    LLVMTypeRef ty = LLVMGlobalGetValueType(global);
    return LLVMBuildInBoundsGEP2(builder, ty, global, idx, 2, "fmt");
}

typedef struct BindingAlloca {
    const JirBinding* binding;
    LLVMValueRef alloca_value;
} BindingAlloca;

typedef struct LoopTarget {
    LLVMBasicBlockRef continue_block;
    LLVMBasicBlockRef break_block;
} LoopTarget;

typedef struct FunctionCodegen {
    const JirProgram* program;
    LLVMModuleRef module;
    LLVMContextRef context;
    LLVMBuilderRef builder;
    LLVMBuilderRef entry_builder;
    const JirFunction* function;
    LLVMValueRef llvm_function;
    BindingAlloca* allocas;
    int alloca_count;
    int alloca_capacity;
    LoopTarget* loops;
    int loop_count;
    int loop_capacity;
} FunctionCodegen;

static LLVMTypeRef llvm_type(LLVMContextRef context, const JirType* type);

static LLVMTypeRef llvm_tuple_type(LLVMContextRef context, const JirType* type) {
    LLVMTypeRef* items = 0;
    LLVMTypeRef out = 0;
    int i = 0;
    if (type->tuple_items.count > 0) {
        items = (LLVMTypeRef*)malloc((size_t)type->tuple_items.count * sizeof(LLVMTypeRef));
        for (i = 0; i < type->tuple_items.count; ++i) {
            items[i] = llvm_type(context, type->tuple_items.items[i]);
        }
    }
    out = LLVMStructTypeInContext(context, items, (unsigned)type->tuple_items.count, 0);
    free(items);
    return out;
}

static LLVMTypeRef llvm_union_payload_type(LLVMContextRef context, const JirType* type) {
    unsigned payload_slots = 0;
    if (type->union_decl && type->union_decl->payload_slots > 0) {
        payload_slots = (unsigned)type->union_decl->payload_slots;
    }
    return LLVMArrayType(LLVMInt64TypeInContext(context), payload_slots);
}

static LLVMTypeRef llvm_union_type(LLVMContextRef context, const JirType* type) {
    LLVMTypeRef fields[2];
    fields[0] = LLVMInt64TypeInContext(context);
    fields[1] = llvm_union_payload_type(context, type);
    return LLVMStructTypeInContext(context, fields, 2, 0);
}

static LLVMTypeRef llvm_array_type(LLVMContextRef context, const JirType* type) {
    return LLVMArrayType(llvm_type(context, type->array_item), (unsigned)type->array_length);
}

static LLVMTypeRef llvm_type(LLVMContextRef context, const JirType* type) {
    switch (type->kind) {
        case HIR_TYPE_BOOL:
            return LLVMInt1TypeInContext(context);
        case HIR_TYPE_VOID:
            return LLVMVoidTypeInContext(context);
        case HIR_TYPE_TUPLE:
            return llvm_tuple_type(context, type);
        case HIR_TYPE_ARRAY:
            return llvm_array_type(context, type);
        case HIR_TYPE_UNION:
            return llvm_union_type(context, type);
        default:
            return LLVMInt64TypeInContext(context);
    }
}

static LLVMTypeRef llvm_function_type(LLVMContextRef context, const JirFunction* function) {
    LLVMTypeRef* params = 0;
    LLVMTypeRef fn_type;
    int i = 0;

    if (function->params.count > 0) {
        params = (LLVMTypeRef*)malloc((size_t)function->params.count * sizeof(LLVMTypeRef));
        for (i = 0; i < function->params.count; ++i) {
            params[i] = llvm_type(context, function->params.items[i]->type);
        }
    }

    fn_type = LLVMFunctionType(llvm_type(context, function->return_type), params, (unsigned)function->params.count, 0);
    free(params);
    return fn_type;
}

static LLVMValueRef llvm_const_expr(LLVMContextRef context, const JirExpr* expr) {
    LLVMValueRef* items = 0;
    LLVMValueRef value = 0;
    int i = 0;
    if (expr->kind == HIR_EXPR_BOOL) {
        return LLVMConstInt(llvm_type(context, expr->type), (unsigned long long)expr->as.bool_value, 0);
    }
    if (expr->kind == HIR_EXPR_INT) {
        return LLVMConstInt(llvm_type(context, expr->type), (unsigned long long)expr->as.int_value, 1);
    }
    if (expr->kind == HIR_EXPR_TUPLE) {
        if (expr->as.tuple.items.count > 0) {
            items = (LLVMValueRef*)malloc((size_t)expr->as.tuple.items.count * sizeof(LLVMValueRef));
            for (i = 0; i < expr->as.tuple.items.count; ++i) {
                items[i] = llvm_const_expr(context, expr->as.tuple.items.items[i]);
            }
        }
        value = LLVMConstStructInContext(context, items, (unsigned)expr->as.tuple.items.count, 0);
        free(items);
        return value;
    }
    if (expr->kind == HIR_EXPR_ARRAY) {
        if (expr->as.array.items.count > 0) {
            items = (LLVMValueRef*)malloc((size_t)expr->as.array.items.count * sizeof(LLVMValueRef));
            for (i = 0; i < expr->as.array.items.count; ++i) {
                items[i] = llvm_const_expr(context, expr->as.array.items.items[i]);
            }
        }
        value = LLVMConstArray(llvm_type(context, expr->type->array_item), items, (unsigned)expr->as.array.items.count);
        free(items);
        return value;
    }
    if (expr->kind == HIR_EXPR_VARIANT) {
        LLVMTypeRef payload_type = llvm_union_payload_type(context, expr->type);
        LLVMValueRef* payload_items = 0;
        LLVMValueRef payload_array = 0;
        LLVMValueRef fields[2];
        unsigned payload_len = (unsigned)expr->type->union_decl->payload_slots;
        if (payload_len > 0) {
            payload_items = (LLVMValueRef*)malloc((size_t)payload_len * sizeof(LLVMValueRef));
            for (i = 0; i < (int)payload_len; ++i) {
                payload_items[i] = LLVMConstInt(LLVMInt64TypeInContext(context), 0, 0);
            }
        }
        if (expr->as.variant.payload) {
            if (expr->as.variant.payload->type->kind == HIR_TYPE_TUPLE) {
                for (i = 0; i < expr->as.variant.payload->as.tuple.items.count; ++i) {
                    LLVMValueRef item = llvm_const_expr(context, expr->as.variant.payload->as.tuple.items.items[i]);
                    if (expr->as.variant.payload->as.tuple.items.items[i]->type->kind == HIR_TYPE_BOOL) {
                        item = LLVMConstInt(LLVMInt64TypeInContext(context), LLVMConstIntGetZExtValue(item), 0);
                    }
                    payload_items[i] = item;
                }
            } else {
                LLVMValueRef item = llvm_const_expr(context, expr->as.variant.payload);
                if (expr->as.variant.payload->type->kind == HIR_TYPE_BOOL) {
                    item = LLVMConstInt(LLVMInt64TypeInContext(context), LLVMConstIntGetZExtValue(item), 0);
                }
                payload_items[0] = item;
            }
        }
        payload_array = payload_len == 0 ? LLVMConstNull(payload_type) : LLVMConstArray(LLVMInt64TypeInContext(context), payload_items, payload_len);
        free(payload_items);
        fields[0] = LLVMConstInt(LLVMInt64TypeInContext(context), (unsigned long long)expr->as.variant.variant->tag_value, 0);
        fields[1] = payload_array;
        return LLVMConstStructInContext(context, fields, 2, 0);
    }
    return LLVMConstNull(llvm_type(context, expr->type));
}

static LLVMValueRef extend_union_payload_value(LLVMBuilderRef builder, LLVMContextRef context, LLVMValueRef value, const JirType* type) {
    if (type->kind == HIR_TYPE_BOOL) {
        return LLVMBuildZExt(builder, value, LLVMInt64TypeInContext(context), "union.zext");
    }
    return value;
}

static LLVMValueRef narrow_union_payload_value(LLVMBuilderRef builder, LLVMContextRef context, LLVMValueRef value, const JirType* type) {
    if (type->kind == HIR_TYPE_BOOL) {
        return LLVMBuildTrunc(builder, value, LLVMInt1TypeInContext(context), "union.trunc");
    }
    return value;
}

static int function_index(const JirProgram* program, const JirFunction* function) {
    int i = 0;
    for (i = 0; i < program->functions.count; ++i) {
        if (&program->functions.items[i] == function) {
            return i;
        }
    }
    return -1;
}

static LLVMValueRef llvm_function_for(const JirProgram* program, LLVMModuleRef module, const JirFunction* function) {
    int index = function_index(program, function);
    if (index < 0) {
        return 0;
    }
    return LLVMGetNamedFunction(module, program->functions.items[index].name);
}

static LLVMValueRef llvm_global_for(LLVMModuleRef module, const JirBinding* binding) {
    return LLVMGetNamedGlobal(module, binding->name);
}

static void add_alloca(FunctionCodegen* cg, const JirBinding* binding, LLVMValueRef alloca_value) {
    if (cg->alloca_count == cg->alloca_capacity) {
        int next_capacity = cg->alloca_capacity == 0 ? 8 : cg->alloca_capacity * 2;
        cg->allocas = (BindingAlloca*)realloc(cg->allocas, (size_t)next_capacity * sizeof(BindingAlloca));
        cg->alloca_capacity = next_capacity;
    }
    cg->allocas[cg->alloca_count].binding = binding;
    cg->allocas[cg->alloca_count].alloca_value = alloca_value;
    cg->alloca_count += 1;
}

static LLVMValueRef find_alloca(FunctionCodegen* cg, const JirBinding* binding) {
    int i = 0;
    for (i = 0; i < cg->alloca_count; ++i) {
        if (cg->allocas[i].binding == binding) {
            return cg->allocas[i].alloca_value;
        }
    }
    return 0;
}

static void push_loop(FunctionCodegen* cg, LLVMBasicBlockRef continue_block, LLVMBasicBlockRef break_block) {
    if (cg->loop_count == cg->loop_capacity) {
        int next_capacity = cg->loop_capacity == 0 ? 4 : cg->loop_capacity * 2;
        cg->loops = (LoopTarget*)realloc(cg->loops, (size_t)next_capacity * sizeof(LoopTarget));
        cg->loop_capacity = next_capacity;
    }
    cg->loops[cg->loop_count].continue_block = continue_block;
    cg->loops[cg->loop_count].break_block = break_block;
    cg->loop_count += 1;
}

static void pop_loop(FunctionCodegen* cg) {
    cg->loop_count -= 1;
}

static LoopTarget* current_loop(FunctionCodegen* cg) {
    if (cg->loop_count <= 0) {
        return 0;
    }
    return &cg->loops[cg->loop_count - 1];
}

static LLVMValueRef emit_expr(FunctionCodegen* cg, const JirExpr* expr);

static void create_binding_allocas(FunctionCodegen* cg) {
    int i = 0;
    for (i = 0; i < cg->function->params.count; ++i) {
        const JirBinding* binding = cg->function->params.items[i];
        LLVMValueRef slot = LLVMBuildAlloca(cg->entry_builder, llvm_type(cg->context, binding->type), binding->name);
        LLVMBuildStore(cg->entry_builder, LLVMGetParam(cg->llvm_function, (unsigned)i), slot);
        add_alloca(cg, binding, slot);
    }
    for (i = 0; i < cg->function->locals.count; ++i) {
        const JirBinding* binding = cg->function->locals.items[i];
        LLVMValueRef slot = LLVMBuildAlloca(cg->entry_builder, llvm_type(cg->context, binding->type), binding->name);
        add_alloca(cg, binding, slot);
    }
}

static LLVMValueRef emit_builtin_panic(FunctionCodegen* cg) {
    LLVMValueRef trap_fn = get_or_add_trap(cg->module, cg->context);
    LLVMValueRef abort_fn = get_or_add_abort(cg->module, cg->context);
    LLVMBuildCall2(cg->builder, LLVMGlobalGetValueType(trap_fn), trap_fn, 0, 0, "");
    LLVMBuildCall2(cg->builder, LLVMGlobalGetValueType(abort_fn), abort_fn, 0, 0, "");
    LLVMBuildUnreachable(cg->builder);
    return LLVMConstInt(LLVMInt64TypeInContext(cg->context), 0, 0);
}

static LLVMValueRef emit_builtin_assert(FunctionCodegen* cg, const JirExpr* expr) {
    LLVMValueRef cond = emit_expr(cg, expr->as.call.args.items[0]);
    LLVMBasicBlockRef ok_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "assert.ok");
    LLVMBasicBlockRef fail_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "assert.fail");
    LLVMBasicBlockRef cont_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "assert.cont");
    LLVMBuildCondBr(cg->builder, cond, ok_block, fail_block);
    LLVMPositionBuilderAtEnd(cg->builder, fail_block);
    emit_builtin_panic(cg);
    LLVMPositionBuilderAtEnd(cg->builder, ok_block);
    LLVMBuildBr(cg->builder, cont_block);
    LLVMPositionBuilderAtEnd(cg->builder, cont_block);
    return LLVMConstInt(LLVMInt64TypeInContext(cg->context), 0, 0);
}

static LLVMValueRef emit_builtin_print(FunctionCodegen* cg, const JirExpr* expr) {
    LLVMValueRef printf_fn = get_or_add_printf(cg->module, cg->context);
    LLVMValueRef arg = emit_expr(cg, expr->as.call.args.items[0]);
    LLVMValueRef args[2];
    if (expr->as.call.args.items[0]->type->kind == HIR_TYPE_BOOL) {
        LLVMValueRef true_str = gep_cstr(cg->builder, get_or_add_format_string(cg->module, cg->context, ".str.true", "true\n"));
        LLVMValueRef false_str = gep_cstr(cg->builder, get_or_add_format_string(cg->module, cg->context, ".str.false", "false\n"));
        args[0] = LLVMBuildSelect(cg->builder, arg, true_str, false_str, "print.bool");
        return LLVMBuildCall2(cg->builder, LLVMGlobalGetValueType(printf_fn), printf_fn, args, 1, "printtmp");
    }
    args[0] = gep_cstr(cg->builder, get_or_add_format_string(cg->module, cg->context, ".str.int", "%lld\n"));
    args[1] = arg;
    return LLVMBuildCall2(cg->builder, LLVMGlobalGetValueType(printf_fn), printf_fn, args, 2, "printtmp");
}

static LLVMValueRef emit_expr(FunctionCodegen* cg, const JirExpr* expr) {
    int i = 0;
    switch (expr->kind) {
        case HIR_EXPR_INT:
        case HIR_EXPR_BOOL:
            return llvm_const_expr(cg->context, expr);
        case HIR_EXPR_BINDING:
            if (expr->as.binding->kind == HIR_BINDING_GLOBAL) {
                LLVMValueRef global = llvm_global_for(cg->module, expr->as.binding);
                return LLVMBuildLoad2(cg->builder, llvm_type(cg->context, expr->type), global, expr->as.binding->name);
            }
            return LLVMBuildLoad2(cg->builder, llvm_type(cg->context, expr->type), find_alloca(cg, expr->as.binding), expr->as.binding->name);
        case HIR_EXPR_CALL: {
            if (expr->as.call.builtin == HIR_BUILTIN_ASSERT) {
                return emit_builtin_assert(cg, expr);
            }
            if (expr->as.call.builtin == HIR_BUILTIN_PRINT) {
                return emit_builtin_print(cg, expr);
            }
            if (expr->as.call.builtin == HIR_BUILTIN_PANIC) {
                return emit_builtin_panic(cg);
            }
            LLVMValueRef callee = llvm_function_for(cg->program, cg->module, expr->as.call.callee);
            LLVMTypeRef callee_type = llvm_function_type(cg->context, expr->as.call.callee);
            LLVMValueRef* args = 0;
            LLVMValueRef result = 0;
            if (expr->as.call.args.count > 0) {
                args = (LLVMValueRef*)malloc((size_t)expr->as.call.args.count * sizeof(LLVMValueRef));
                for (i = 0; i < expr->as.call.args.count; ++i) {
                    args[i] = emit_expr(cg, expr->as.call.args.items[i]);
                }
            }
            result = LLVMBuildCall2(cg->builder, callee_type, callee, args, (unsigned)expr->as.call.args.count, expr->type->kind == HIR_TYPE_VOID ? "" : "calltmp");
            free(args);
            return result;
        }
        case HIR_EXPR_BINARY: {
            LLVMValueRef left = emit_expr(cg, expr->as.binary.left);
            LLVMValueRef right = emit_expr(cg, expr->as.binary.right);
            switch (expr->as.binary.op) {
                case HIR_BIN_ADD: return LLVMBuildAdd(cg->builder, left, right, "addtmp");
                case HIR_BIN_SUB: return LLVMBuildSub(cg->builder, left, right, "subtmp");
                case HIR_BIN_MUL: return LLVMBuildMul(cg->builder, left, right, "multmp");
                case HIR_BIN_DIV: return LLVMBuildSDiv(cg->builder, left, right, "divtmp");
                case HIR_BIN_EQ: return LLVMBuildICmp(cg->builder, LLVMIntEQ, left, right, "eqtmp");
                case HIR_BIN_NE: return LLVMBuildICmp(cg->builder, LLVMIntNE, left, right, "netmp");
                case HIR_BIN_LT: return LLVMBuildICmp(cg->builder, LLVMIntSLT, left, right, "lttmp");
                case HIR_BIN_LE: return LLVMBuildICmp(cg->builder, LLVMIntSLE, left, right, "letmp");
                case HIR_BIN_GT: return LLVMBuildICmp(cg->builder, LLVMIntSGT, left, right, "gttmp");
                case HIR_BIN_GE: return LLVMBuildICmp(cg->builder, LLVMIntSGE, left, right, "getmp");
            }
        }
        case HIR_EXPR_TUPLE: {
            LLVMValueRef value = LLVMGetUndef(llvm_type(cg->context, expr->type));
            for (i = 0; i < expr->as.tuple.items.count; ++i) {
                value = LLVMBuildInsertValue(cg->builder, value, emit_expr(cg, expr->as.tuple.items.items[i]), (unsigned)i, "tuple.ins");
            }
            return value;
        }
        case HIR_EXPR_VARIANT: {
            LLVMValueRef union_value = LLVMGetUndef(llvm_type(cg->context, expr->type));
            LLVMValueRef payload_array = LLVMConstNull(llvm_union_payload_type(cg->context, expr->type));
            union_value = LLVMBuildInsertValue(
                cg->builder,
                union_value,
                LLVMConstInt(LLVMInt64TypeInContext(cg->context), (unsigned long long)expr->as.variant.variant->tag_value, 0),
                0,
                "union.tag");
            if (expr->as.variant.payload) {
                if (expr->as.variant.payload->type->kind == HIR_TYPE_TUPLE) {
                    for (i = 0; i < expr->as.variant.payload->as.tuple.items.count; ++i) {
                        LLVMValueRef item = emit_expr(cg, expr->as.variant.payload->as.tuple.items.items[i]);
                        item = extend_union_payload_value(cg->builder, cg->context, item, expr->as.variant.payload->as.tuple.items.items[i]->type);
                        payload_array = LLVMBuildInsertValue(cg->builder, payload_array, item, (unsigned)i, "union.payload.ins");
                    }
                } else {
                    LLVMValueRef item = emit_expr(cg, expr->as.variant.payload);
                    item = extend_union_payload_value(cg->builder, cg->context, item, expr->as.variant.payload->type);
                    payload_array = LLVMBuildInsertValue(cg->builder, payload_array, item, 0, "union.payload.ins");
                }
            }
            return LLVMBuildInsertValue(cg->builder, union_value, payload_array, 1, "union.value");
        }
        case HIR_EXPR_UNION_TAG:
            return LLVMBuildExtractValue(cg->builder, emit_expr(cg, expr->as.union_tag.value), 0, "union.tag");
        case HIR_EXPR_UNION_FIELD: {
            LLVMValueRef payload_array = LLVMBuildExtractValue(cg->builder, emit_expr(cg, expr->as.union_field.value), 1, "union.payload");
            LLVMValueRef item = LLVMBuildExtractValue(cg->builder, payload_array, (unsigned)expr->as.union_field.field_index, "union.field");
            return narrow_union_payload_value(cg->builder, cg->context, item, expr->type);
        }
        case HIR_EXPR_ARRAY: {
            LLVMValueRef value = LLVMGetUndef(llvm_type(cg->context, expr->type));
            for (i = 0; i < expr->as.array.items.count; ++i) {
                value = LLVMBuildInsertValue(cg->builder, value, emit_expr(cg, expr->as.array.items.items[i]), (unsigned)i, "array.ins");
            }
            return value;
        }
        case HIR_EXPR_INDEX:
            if (expr->as.index.base->type->kind == HIR_TYPE_TUPLE) {
                return LLVMBuildExtractValue(cg->builder, emit_expr(cg, expr->as.index.base), (unsigned)expr->as.index.index->as.int_value, "tuple.idx");
            }
            if (expr->as.index.base->type->kind == HIR_TYPE_ARRAY && expr->as.index.base->kind == HIR_EXPR_BINDING) {
                LLVMValueRef indices[2];
                LLVMValueRef base_ptr = 0;
                indices[0] = LLVMConstInt(LLVMInt32TypeInContext(cg->context), 0, 0);
                indices[1] = emit_expr(cg, expr->as.index.index);
                if (expr->as.index.base->as.binding->kind == HIR_BINDING_GLOBAL) {
                    base_ptr = llvm_global_for(cg->module, expr->as.index.base->as.binding);
                } else {
                    base_ptr = find_alloca(cg, expr->as.index.base->as.binding);
                }
                return LLVMBuildLoad2(
                    cg->builder,
                    llvm_type(cg->context, expr->type),
                    LLVMBuildInBoundsGEP2(cg->builder, llvm_type(cg->context, expr->as.index.base->type), base_ptr, indices, 2, "array.ptr"),
                    "array.idx");
            }
            return 0;
    }
    return 0;
}

static int block_terminated(LLVMBasicBlockRef block) {
    return LLVMGetBasicBlockTerminator(block) ? 1 : 0;
}

static void emit_block(FunctionCodegen* cg, const JirBlock* block);

static void emit_stmt(FunctionCodegen* cg, const JirStmt* stmt) {
    switch (stmt->kind) {
        case HIR_STMT_RETURN:
            if (!stmt->as.ret.expr || stmt->as.ret.expr->type->kind == HIR_TYPE_VOID) {
                LLVMBuildRetVoid(cg->builder);
            } else {
                LLVMBuildRet(cg->builder, emit_expr(cg, stmt->as.ret.expr));
            }
            return;
        case HIR_STMT_VAR_DECL:
            LLVMBuildStore(cg->builder, emit_expr(cg, stmt->as.var_decl.init), find_alloca(cg, stmt->as.var_decl.binding));
            return;
        case HIR_STMT_ASSIGN:
            if (stmt->as.assign.binding->kind == HIR_BINDING_GLOBAL) {
                LLVMBuildStore(cg->builder, emit_expr(cg, stmt->as.assign.value), llvm_global_for(cg->module, stmt->as.assign.binding));
            } else {
                LLVMBuildStore(cg->builder, emit_expr(cg, stmt->as.assign.value), find_alloca(cg, stmt->as.assign.binding));
            }
            return;
        case HIR_STMT_IF: {
            LLVMBasicBlockRef then_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "if.then");
            LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "if.end");
            LLVMBasicBlockRef else_block = 0;
            if (stmt->as.if_stmt.has_else) {
                else_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "if.else");
            }
            LLVMBuildCondBr(cg->builder, emit_expr(cg, stmt->as.if_stmt.cond), then_block, stmt->as.if_stmt.has_else ? else_block : merge_block);
            LLVMPositionBuilderAtEnd(cg->builder, then_block);
            emit_block(cg, &stmt->as.if_stmt.then_block);
            if (!block_terminated(LLVMGetInsertBlock(cg->builder))) {
                LLVMBuildBr(cg->builder, merge_block);
            }
            if (stmt->as.if_stmt.has_else) {
                LLVMPositionBuilderAtEnd(cg->builder, else_block);
                emit_block(cg, &stmt->as.if_stmt.else_block);
                if (!block_terminated(LLVMGetInsertBlock(cg->builder))) {
                    LLVMBuildBr(cg->builder, merge_block);
                }
            }
            LLVMPositionBuilderAtEnd(cg->builder, merge_block);
            return;
        }
        case HIR_STMT_WHILE: {
            LLVMBasicBlockRef cond_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "while.cond");
            LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "while.body");
            LLVMBasicBlockRef exit_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "while.end");
            LLVMBuildBr(cg->builder, cond_block);
            LLVMPositionBuilderAtEnd(cg->builder, cond_block);
            LLVMBuildCondBr(cg->builder, emit_expr(cg, stmt->as.while_stmt.cond), body_block, exit_block);
            LLVMPositionBuilderAtEnd(cg->builder, body_block);
            push_loop(cg, cond_block, exit_block);
            emit_block(cg, &stmt->as.while_stmt.body);
            pop_loop(cg);
            if (!block_terminated(LLVMGetInsertBlock(cg->builder))) {
                LLVMBuildBr(cg->builder, cond_block);
            }
            LLVMPositionBuilderAtEnd(cg->builder, exit_block);
            return;
        }
        case HIR_STMT_FOR_RANGE: {
            LLVMValueRef slot = find_alloca(cg, stmt->as.for_range.binding);
            LLVMBasicBlockRef cond_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "for.cond");
            LLVMBasicBlockRef body_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "for.body");
            LLVMBasicBlockRef step_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "for.step");
            LLVMBasicBlockRef exit_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "for.end");
            LLVMBuildStore(cg->builder, emit_expr(cg, stmt->as.for_range.start), slot);
            LLVMBuildBr(cg->builder, cond_block);
            LLVMPositionBuilderAtEnd(cg->builder, cond_block);
            LLVMBuildCondBr(cg->builder,
                LLVMBuildICmp(cg->builder, LLVMIntSLT,
                    LLVMBuildLoad2(cg->builder, llvm_type(cg->context, stmt->as.for_range.binding->type), slot, stmt->as.for_range.binding->name),
                    emit_expr(cg, stmt->as.for_range.end),
                    "forcmp"),
                body_block,
                exit_block);
            LLVMPositionBuilderAtEnd(cg->builder, body_block);
            push_loop(cg, step_block, exit_block);
            emit_block(cg, &stmt->as.for_range.body);
            pop_loop(cg);
            if (!block_terminated(LLVMGetInsertBlock(cg->builder))) {
                LLVMBuildBr(cg->builder, step_block);
            }
            LLVMPositionBuilderAtEnd(cg->builder, step_block);
            LLVMBuildStore(cg->builder,
                LLVMBuildAdd(cg->builder,
                    LLVMBuildLoad2(cg->builder, llvm_type(cg->context, stmt->as.for_range.binding->type), slot, stmt->as.for_range.binding->name),
                    LLVMConstInt(LLVMInt64TypeInContext(cg->context), 1, 0),
                    "forinc"),
                slot);
            LLVMBuildBr(cg->builder, cond_block);
            LLVMPositionBuilderAtEnd(cg->builder, exit_block);
            return;
        }
        case HIR_STMT_BREAK: {
            LoopTarget* loop = current_loop(cg);
            LLVMBuildBr(cg->builder, loop->break_block);
            return;
        }
        case HIR_STMT_CONTINUE: {
            LoopTarget* loop = current_loop(cg);
            LLVMBuildBr(cg->builder, loop->continue_block);
            return;
        }
        case HIR_STMT_EXPR:
            (void)emit_expr(cg, stmt->as.expr_stmt.expr);
            return;
    }
}

static void emit_block(FunctionCodegen* cg, const JirBlock* block) {
    int i = 0;
    for (i = 0; i < block->stmts.count; ++i) {
        emit_stmt(cg, block->stmts.items[i]);
        if (block_terminated(LLVMGetInsertBlock(cg->builder))) {
            return;
        }
    }
}

static int emit_globals(const JirProgram* program, LLVMModuleRef module, LLVMContextRef context) {
    int i = 0;
    for (i = 0; i < program->globals.count; ++i) {
        const JirGlobal* global = &program->globals.items[i];
        LLVMValueRef llvm_global = LLVMAddGlobal(module, llvm_type(context, global->binding->type), global->binding->name);
        LLVMSetInitializer(llvm_global, llvm_const_expr(context, global->init));
        LLVMSetLinkage(llvm_global, LLVMExternalLinkage);
    }
    return 1;
}

static int emit_function_decl(const JirFunction* function, LLVMModuleRef module, LLVMContextRef context) {
    LLVMTypeRef fn_type = llvm_function_type(context, function);
    LLVMAddFunction(module, function->name, fn_type);
    return 1;
}

static int emit_function_body(const JirProgram* program, LLVMModuleRef module, LLVMContextRef context, const JirFunction* function) {
    FunctionCodegen cg;
    LLVMBasicBlockRef entry;
    char* verify_error = 0;

    memset(&cg, 0, sizeof(cg));
    cg.program = program;
    cg.module = module;
    cg.context = context;
    cg.function = function;
    cg.llvm_function = LLVMGetNamedFunction(module, function->name);
    cg.builder = LLVMCreateBuilderInContext(context);
    cg.entry_builder = LLVMCreateBuilderInContext(context);

    entry = LLVMAppendBasicBlockInContext(context, cg.llvm_function, "entry");
    LLVMPositionBuilderAtEnd(cg.builder, entry);
    LLVMPositionBuilderAtEnd(cg.entry_builder, entry);

    create_binding_allocas(&cg);
    emit_block(&cg, &function->body);

    if (!block_terminated(LLVMGetInsertBlock(cg.builder))) {
        if (function->return_type->kind == HIR_TYPE_VOID) {
            LLVMBuildRetVoid(cg.builder);
        } else {
            LLVMBuildRet(cg.builder, LLVMConstNull(llvm_type(context, function->return_type)));
        }
    }

    if (LLVMVerifyFunction(cg.llvm_function, LLVMReturnStatusAction) != 0) {
        LLVMVerifyModule(module, LLVMReturnStatusAction, &verify_error);
        LLVMDisposeMessage(verify_error);
        LLVMDisposeBuilder(cg.entry_builder);
        LLVMDisposeBuilder(cg.builder);
        free(cg.allocas);
        free(cg.loops);
        return 0;
    }

    LLVMDisposeBuilder(cg.entry_builder);
    LLVMDisposeBuilder(cg.builder);
    free(cg.allocas);
    free(cg.loops);
    return 1;
}

int emit_llvm_module(const JirProgram* program, char** out_ir, const char** error) {
    LLVMContextRef context = LLVMContextCreate();
    LLVMModuleRef llvm_module = LLVMModuleCreateWithNameInContext("jiang", context);
    char* verify_error = 0;
    int i = 0;

    emit_globals(program, llvm_module, context);
    for (i = 0; i < program->functions.count; ++i) {
        emit_function_decl(&program->functions.items[i], llvm_module, context);
    }
    for (i = 0; i < program->functions.count; ++i) {
        if (!emit_function_body(program, llvm_module, context, &program->functions.items[i])) {
            *error = "failed to emit function body";
            LLVMDisposeModule(llvm_module);
            LLVMContextDispose(context);
            return 0;
        }
    }

    if (LLVMVerifyModule(llvm_module, LLVMReturnStatusAction, &verify_error) != 0) {
        *error = verify_error;
        LLVMDisposeModule(llvm_module);
        LLVMContextDispose(context);
        return 0;
    }

    *out_ir = LLVMPrintModuleToString(llvm_module);
    LLVMDisposeModule(llvm_module);
    LLVMContextDispose(context);
    return 1;
}
