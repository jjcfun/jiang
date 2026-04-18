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
} FunctionCodegen;

typedef struct JirBlockMapEntry {
    const char* name;
    LLVMBasicBlockRef block;
} JirBlockMapEntry;

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

static LLVMTypeRef llvm_struct_type(LLVMContextRef context, const JirType* type) {
    LLVMTypeRef* fields = 0;
    LLVMTypeRef out = 0;
    int i = 0;
    if (type->struct_fields.count > 0) {
        fields = (LLVMTypeRef*)malloc((size_t)type->struct_fields.count * sizeof(LLVMTypeRef));
        for (i = 0; i < type->struct_fields.count; ++i) {
            fields[i] = llvm_type(context, type->struct_fields.items[i].type);
        }
    }
    out = LLVMStructTypeInContext(context, fields, (unsigned)type->struct_fields.count, 0);
    free(fields);
    return out;
}

static LLVMTypeRef llvm_union_payload_type(LLVMContextRef context, const JirType* type) {
    return LLVMArrayType(LLVMInt64TypeInContext(context), (unsigned)type->union_payload_slots);
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
        case JIR_TYPE_UINT8:
            return LLVMInt8TypeInContext(context);
        case JIR_TYPE_BOOL:
            return LLVMInt1TypeInContext(context);
        case JIR_TYPE_VOID:
            return LLVMVoidTypeInContext(context);
        case JIR_TYPE_ENUM:
            return LLVMInt64TypeInContext(context);
        case JIR_TYPE_STRUCT:
            return llvm_struct_type(context, type);
        case JIR_TYPE_TUPLE:
            return llvm_tuple_type(context, type);
        case JIR_TYPE_ARRAY:
            return llvm_array_type(context, type);
        case JIR_TYPE_UNION:
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
    if (expr->kind == JIR_EXPR_BOOL) {
        return LLVMConstInt(llvm_type(context, expr->type), (unsigned long long)expr->as.bool_value, 0);
    }
    if (expr->kind == JIR_EXPR_INT) {
        return LLVMConstInt(llvm_type(context, expr->type), (unsigned long long)expr->as.int_value, 1);
    }
    if (expr->kind == JIR_EXPR_ENUM_MEMBER) {
        return LLVMConstInt(llvm_type(context, expr->type), (unsigned long long)expr->as.enum_member.value, 1);
    }
    if (expr->kind == JIR_EXPR_TUPLE) {
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
    if (expr->kind == JIR_EXPR_STRUCT) {
        unsigned field_count = (unsigned)expr->type->struct_fields.count;
        LLVMValueRef* field_values = 0;
        if (field_count == 0) {
            return LLVMConstStructInContext(context, 0, 0, 0);
        }
        field_values = (LLVMValueRef*)malloc((size_t)field_count * sizeof(LLVMValueRef));
        for (i = 0; i < (int)field_count; ++i) {
            field_values[i] = LLVMConstNull(llvm_type(context, expr->type->struct_fields.items[i].type));
        }
        for (i = 0; i < expr->as.struct_lit.fields.count; ++i) {
            int field_index = expr->as.struct_lit.fields.items[i].field_index;
            field_values[field_index] = llvm_const_expr(context, expr->as.struct_lit.fields.items[i].value);
        }
        value = LLVMConstStructInContext(context, field_values, field_count, 0);
        free(field_values);
        return value;
    }
    if (expr->kind == JIR_EXPR_ARRAY) {
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
    if (expr->kind == JIR_EXPR_UNION_PACK) {
        LLVMTypeRef payload_type = llvm_union_payload_type(context, expr->type);
        LLVMValueRef* payload_items = 0;
        LLVMValueRef payload_array = 0;
        LLVMValueRef fields[2];
        unsigned payload_len = (unsigned)expr->type->union_payload_slots;
        if (payload_len > 0) {
            payload_items = (LLVMValueRef*)malloc((size_t)payload_len * sizeof(LLVMValueRef));
            for (i = 0; i < (int)payload_len; ++i) {
                payload_items[i] = LLVMConstInt(LLVMInt64TypeInContext(context), 0, 0);
            }
        }
        for (i = 0; i < expr->as.union_pack.payload_items.count; ++i) {
            LLVMValueRef item = llvm_const_expr(context, expr->as.union_pack.payload_items.items[i]);
            if (expr->as.union_pack.payload_items.items[i]->type->kind == JIR_TYPE_BOOL) {
                        item = LLVMConstInt(LLVMInt64TypeInContext(context), LLVMConstIntGetZExtValue(item), 0);
            }
            payload_items[i] = item;
        }
        payload_array = payload_len == 0 ? LLVMConstNull(payload_type) : LLVMConstArray(LLVMInt64TypeInContext(context), payload_items, payload_len);
        free(payload_items);
        fields[0] = LLVMConstInt(LLVMInt64TypeInContext(context), (unsigned long long)expr->as.union_pack.tag_value, 0);
        fields[1] = payload_array;
        return LLVMConstStructInContext(context, fields, 2, 0);
    }
    return LLVMConstNull(llvm_type(context, expr->type));
}

static LLVMValueRef extend_union_payload_value(LLVMBuilderRef builder, LLVMContextRef context, LLVMValueRef value, const JirType* type) {
    if (type->kind == JIR_TYPE_BOOL) {
        return LLVMBuildZExt(builder, value, LLVMInt64TypeInContext(context), "union.zext");
    }
    return value;
}

static LLVMValueRef narrow_union_payload_value(LLVMBuilderRef builder, LLVMContextRef context, LLVMValueRef value, const JirType* type) {
    if (type->kind == JIR_TYPE_BOOL) {
        return LLVMBuildTrunc(builder, value, LLVMInt1TypeInContext(context), "union.trunc");
    }
    return value;
}

static LLVMValueRef llvm_function_for(const JirProgram* program, LLVMModuleRef module, const JirFunction* function) {
    (void)program;
    return LLVMGetNamedFunction(module, function->name);
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

static LLVMValueRef emit_expr(FunctionCodegen* cg, const JirExpr* expr);
static LLVMValueRef emit_lvalue_ptr(FunctionCodegen* cg, const JirExpr* expr);

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
    if (expr->as.call.args.items[0]->type->kind == JIR_TYPE_BOOL) {
        LLVMValueRef true_str = gep_cstr(cg->builder, get_or_add_format_string(cg->module, cg->context, ".str.true", "true\n"));
        LLVMValueRef false_str = gep_cstr(cg->builder, get_or_add_format_string(cg->module, cg->context, ".str.false", "false\n"));
        args[0] = LLVMBuildSelect(cg->builder, arg, true_str, false_str, "print.bool");
        return LLVMBuildCall2(cg->builder, LLVMGlobalGetValueType(printf_fn), printf_fn, args, 1, "printtmp");
    }
    args[0] = gep_cstr(cg->builder, get_or_add_format_string(cg->module, cg->context, ".str.int", "%lld\n"));
    args[1] = arg;
    return LLVMBuildCall2(cg->builder, LLVMGlobalGetValueType(printf_fn), printf_fn, args, 2, "printtmp");
}

static LLVMValueRef emit_plain_call(FunctionCodegen* cg, const JirFunction* callee_fn, const JirExprList* args_list, const JirType* result_type) {
    LLVMValueRef callee = llvm_function_for(cg->program, cg->module, callee_fn);
    LLVMTypeRef callee_type = llvm_function_type(cg->context, callee_fn);
    LLVMValueRef* args = 0;
    LLVMValueRef result = 0;
    int i = 0;
    if (args_list->count > 0) {
        args = (LLVMValueRef*)malloc((size_t)args_list->count * sizeof(LLVMValueRef));
        for (i = 0; i < args_list->count; ++i) {
            args[i] = emit_expr(cg, args_list->items[i]);
        }
    }
    result = LLVMBuildCall2(cg->builder, callee_type, callee, args, (unsigned)args_list->count, result_type->kind == JIR_TYPE_VOID ? "" : "calltmp");
    free(args);
    return result;
}

static LLVMValueRef emit_lvalue_ptr(FunctionCodegen* cg, const JirExpr* expr) {
    switch (expr->kind) {
        case JIR_EXPR_BINDING:
            if (expr->as.binding->kind == JIR_BINDING_GLOBAL) {
                return llvm_global_for(cg->module, expr->as.binding);
            }
            return find_alloca(cg, expr->as.binding);
        case JIR_EXPR_INDEX:
            if (expr->as.index.base->type->kind == JIR_TYPE_ARRAY) {
                LLVMValueRef base_ptr = emit_lvalue_ptr(cg, expr->as.index.base);
                LLVMValueRef indices[2];
                if (!base_ptr) {
                    return 0;
                }
                indices[0] = LLVMConstInt(LLVMInt32TypeInContext(cg->context), 0, 0);
                indices[1] = emit_expr(cg, expr->as.index.index);
                return LLVMBuildInBoundsGEP2(
                    cg->builder,
                    llvm_type(cg->context, expr->as.index.base->type),
                    base_ptr,
                    indices,
                    2,
                    "array.ptr");
            }
            return 0;
        case JIR_EXPR_STRUCT_FIELD: {
            LLVMValueRef base_ptr = emit_lvalue_ptr(cg, expr->as.struct_field.base);
            LLVMValueRef indices[2];
            if (!base_ptr) {
                return 0;
            }
            indices[0] = LLVMConstInt(LLVMInt32TypeInContext(cg->context), 0, 0);
            indices[1] = LLVMConstInt(LLVMInt32TypeInContext(cg->context), (unsigned long long)expr->as.struct_field.field_index, 0);
            return LLVMBuildInBoundsGEP2(
                cg->builder,
                llvm_type(cg->context, expr->as.struct_field.base->type),
                base_ptr,
                indices,
                2,
                "struct.ptr");
        }
        default:
            return 0;
    }
}

static LLVMValueRef emit_expr(FunctionCodegen* cg, const JirExpr* expr) {
    int i = 0;
    if (!expr) {
        return 0;
    }
    switch (expr->kind) {
        case JIR_EXPR_INT:
        case JIR_EXPR_BOOL:
            return llvm_const_expr(cg->context, expr);
        case JIR_EXPR_BINDING:
            if (expr->as.binding->kind == JIR_BINDING_GLOBAL) {
                LLVMValueRef global = llvm_global_for(cg->module, expr->as.binding);
                return LLVMBuildLoad2(cg->builder, llvm_type(cg->context, expr->type), global, expr->as.binding->name);
            }
            return LLVMBuildLoad2(cg->builder, llvm_type(cg->context, expr->type), find_alloca(cg, expr->as.binding), expr->as.binding->name);
        case JIR_EXPR_CALL: {
            if (expr->as.call.builtin == JIR_BUILTIN_ASSERT) {
                return emit_builtin_assert(cg, expr);
            }
            if (expr->as.call.builtin == JIR_BUILTIN_PRINT) {
                return emit_builtin_print(cg, expr);
            }
            if (expr->as.call.builtin == JIR_BUILTIN_PANIC) {
                return emit_builtin_panic(cg);
            }
            return emit_plain_call(cg, expr->as.call.callee, &expr->as.call.args, expr->type);
        }
        case JIR_EXPR_STRUCT_INIT:
            return emit_plain_call(cg, expr->as.struct_init.init, &expr->as.struct_init.args, expr->type);
        case JIR_EXPR_BINARY: {
            LLVMValueRef left = emit_expr(cg, expr->as.binary.left);
            LLVMValueRef right = emit_expr(cg, expr->as.binary.right);
            switch (expr->as.binary.op) {
                case JIR_BIN_ADD: return LLVMBuildAdd(cg->builder, left, right, "addtmp");
                case JIR_BIN_SUB: return LLVMBuildSub(cg->builder, left, right, "subtmp");
                case JIR_BIN_MUL: return LLVMBuildMul(cg->builder, left, right, "multmp");
                case JIR_BIN_DIV: return LLVMBuildSDiv(cg->builder, left, right, "divtmp");
                case JIR_BIN_EQ: return LLVMBuildICmp(cg->builder, LLVMIntEQ, left, right, "eqtmp");
                case JIR_BIN_NE: return LLVMBuildICmp(cg->builder, LLVMIntNE, left, right, "netmp");
                case JIR_BIN_LT: return LLVMBuildICmp(cg->builder, LLVMIntSLT, left, right, "lttmp");
                case JIR_BIN_LE: return LLVMBuildICmp(cg->builder, LLVMIntSLE, left, right, "letmp");
                case JIR_BIN_GT: return LLVMBuildICmp(cg->builder, LLVMIntSGT, left, right, "gttmp");
                case JIR_BIN_GE: return LLVMBuildICmp(cg->builder, LLVMIntSGE, left, right, "getmp");
            }
        }
        case JIR_EXPR_TERNARY: {
            LLVMBasicBlockRef then_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "ternary.then");
            LLVMBasicBlockRef else_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "ternary.else");
            LLVMBasicBlockRef merge_block = LLVMAppendBasicBlockInContext(cg->context, cg->llvm_function, "ternary.end");
            LLVMValueRef then_value = 0;
            LLVMValueRef else_value = 0;
            LLVMValueRef phi = 0;
            LLVMValueRef incoming_values[2];
            LLVMBasicBlockRef incoming_blocks[2];
            LLVMBuildCondBr(cg->builder, emit_expr(cg, expr->as.ternary.cond), then_block, else_block);
            LLVMPositionBuilderAtEnd(cg->builder, then_block);
            then_value = emit_expr(cg, expr->as.ternary.then_expr);
            LLVMBuildBr(cg->builder, merge_block);
            then_block = LLVMGetInsertBlock(cg->builder);
            LLVMPositionBuilderAtEnd(cg->builder, else_block);
            else_value = emit_expr(cg, expr->as.ternary.else_expr);
            LLVMBuildBr(cg->builder, merge_block);
            else_block = LLVMGetInsertBlock(cg->builder);
            LLVMPositionBuilderAtEnd(cg->builder, merge_block);
            phi = LLVMBuildPhi(cg->builder, llvm_type(cg->context, expr->type), "ternarytmp");
            incoming_values[0] = then_value;
            incoming_values[1] = else_value;
            incoming_blocks[0] = then_block;
            incoming_blocks[1] = else_block;
            LLVMAddIncoming(phi, incoming_values, incoming_blocks, 2);
            return phi;
        }
        case JIR_EXPR_ENUM_MEMBER:
            return llvm_const_expr(cg->context, expr);
        case JIR_EXPR_ENUM_VALUE:
            return 0;
        case JIR_EXPR_STRUCT: {
            LLVMValueRef value = LLVMGetUndef(llvm_type(cg->context, expr->type));
            for (i = 0; i < expr->as.struct_lit.fields.count; ++i) {
                int field_index = expr->as.struct_lit.fields.items[i].field_index;
                value = LLVMBuildInsertValue(cg->builder, value, emit_expr(cg, expr->as.struct_lit.fields.items[i].value), (unsigned)field_index, "struct.ins");
            }
            return value;
        }
        case JIR_EXPR_TUPLE: {
            LLVMValueRef value = LLVMGetUndef(llvm_type(cg->context, expr->type));
            for (i = 0; i < expr->as.tuple.items.count; ++i) {
                value = LLVMBuildInsertValue(cg->builder, value, emit_expr(cg, expr->as.tuple.items.items[i]), (unsigned)i, "tuple.ins");
            }
            return value;
        }
        case JIR_EXPR_VARIANT:
            return 0;
        case JIR_EXPR_UNION_PACK: {
            LLVMValueRef union_value = LLVMGetUndef(llvm_type(cg->context, expr->type));
            LLVMValueRef payload_array = LLVMConstNull(llvm_union_payload_type(cg->context, expr->type));
            union_value = LLVMBuildInsertValue(
                cg->builder,
                union_value,
                LLVMConstInt(LLVMInt64TypeInContext(cg->context), (unsigned long long)expr->as.union_pack.tag_value, 0),
                0,
                "union.tag");
            for (i = 0; i < expr->as.union_pack.payload_items.count; ++i) {
                LLVMValueRef item = emit_expr(cg, expr->as.union_pack.payload_items.items[i]);
                item = extend_union_payload_value(cg->builder, cg->context, item, expr->as.union_pack.payload_items.items[i]->type);
                payload_array = LLVMBuildInsertValue(cg->builder, payload_array, item, (unsigned)i, "union.payload.ins");
            }
            return LLVMBuildInsertValue(cg->builder, union_value, payload_array, 1, "union.value");
        }
        case JIR_EXPR_EXTRACT:
            switch (expr->as.extract.kind) {
                case JIR_EXTRACT_STRUCT_FIELD:
                case JIR_EXTRACT_TUPLE_ITEM:
                    return LLVMBuildExtractValue(cg->builder, emit_expr(cg, expr->as.extract.base), (unsigned)expr->as.extract.field_index, "extract");
                case JIR_EXTRACT_ARRAY_ITEM:
                    return LLVMBuildExtractValue(cg->builder, emit_expr(cg, expr->as.extract.base), (unsigned)expr->as.extract.field_index, "array.extract");
                case JIR_EXTRACT_UNION_TAG:
                    return LLVMBuildExtractValue(cg->builder, emit_expr(cg, expr->as.extract.base), 0, "union.tag");
                case JIR_EXTRACT_UNION_PAYLOAD: {
                    LLVMValueRef payload_array = LLVMBuildExtractValue(cg->builder, emit_expr(cg, expr->as.extract.base), 1, "union.payload");
                    LLVMValueRef item = LLVMBuildExtractValue(cg->builder, payload_array, (unsigned)expr->as.extract.field_index, "union.field");
                    return narrow_union_payload_value(cg->builder, cg->context, item, expr->type);
                }
            }
            return 0;
        case JIR_EXPR_UNION_TAG:
        case JIR_EXPR_UNION_FIELD:
        case JIR_EXPR_STRUCT_FIELD:
            return 0;
        case JIR_EXPR_ARRAY: {
            LLVMValueRef value = LLVMGetUndef(llvm_type(cg->context, expr->type));
            for (i = 0; i < expr->as.array.items.count; ++i) {
                value = LLVMBuildInsertValue(cg->builder, value, emit_expr(cg, expr->as.array.items.items[i]), (unsigned)i, "array.ins");
            }
            return value;
        }
        case JIR_EXPR_INDEX:
            if (expr->as.index.base->type->kind == JIR_TYPE_TUPLE) {
                return LLVMBuildExtractValue(cg->builder, emit_expr(cg, expr->as.index.base), (unsigned)expr->as.index.index->as.int_value, "tuple.idx");
            }
            if (expr->as.index.base->type->kind == JIR_TYPE_ARRAY) {
                if (expr->as.index.base->kind == JIR_EXPR_BINDING || expr->as.index.base->kind == JIR_EXPR_STRUCT_FIELD) {
                    LLVMValueRef base_ptr = emit_lvalue_ptr(cg, expr->as.index.base);
                    return LLVMBuildLoad2(
                        cg->builder,
                        llvm_type(cg->context, expr->type),
                        LLVMBuildInBoundsGEP2(cg->builder, llvm_type(cg->context, expr->as.index.base->type), base_ptr, (LLVMValueRef[]){
                            LLVMConstInt(LLVMInt32TypeInContext(cg->context), 0, 0),
                            emit_expr(cg, expr->as.index.index)
                        }, 2, "array.ptr"),
                        "array.idx");
                }
                if (expr->as.index.index->kind == JIR_EXPR_INT) {
                    return LLVMBuildExtractValue(cg->builder, emit_expr(cg, expr->as.index.base), (unsigned)expr->as.index.index->as.int_value, "array.idx");
                }
            }
            return 0;
    }
    return 0;
}

static int block_terminated(LLVMBasicBlockRef block) {
    return LLVMGetBasicBlockTerminator(block) ? 1 : 0;
}

static int emit_jir_inst(FunctionCodegen* cg, const JirInst* inst);

static int emit_jir_inst(FunctionCodegen* cg, const JirInst* inst) {
    switch (inst->kind) {
        case JIR_INST_PARAM:
            return 1;
        case JIR_INST_VAR_DECL:
            if (!inst->binding || !inst->value) {
                return 0;
            }
            LLVMBuildStore(cg->builder, emit_expr(cg, inst->value), find_alloca(cg, inst->binding));
            return 1;
        case JIR_INST_ASSIGN:
            if (!inst->value) {
                return 0;
            }
            if (inst->binding) {
                if (inst->binding->kind == JIR_BINDING_GLOBAL) {
                    LLVMBuildStore(cg->builder, emit_expr(cg, inst->value), llvm_global_for(cg->module, inst->binding));
                } else {
                    LLVMBuildStore(cg->builder, emit_expr(cg, inst->value), find_alloca(cg, inst->binding));
                }
            } else {
                if (!inst->target) {
                    return 0;
                }
                LLVMValueRef ptr = emit_lvalue_ptr(cg, inst->target);
                if (!ptr) {
                    return 0;
                }
                LLVMBuildStore(cg->builder, emit_expr(cg, inst->value), ptr);
            }
            return 1;
        case JIR_INST_EXPR:
            if (!inst->expr) {
                return 0;
            }
            (void)emit_expr(cg, inst->expr);
            return 1;
    }
    return 1;
}

static const JirLoweredFunction* find_lowered_function(const JirProgram* program, const JirFunction* function) {
    int i = 0;
    for (i = 0; i < program->lowered_functions.count; ++i) {
        if (program->lowered_functions.items[i].function == function) {
            return &program->lowered_functions.items[i];
        }
    }
    return 0;
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
    const JirLoweredFunction* lowered = 0;
    JirBlockMapEntry* block_map = 0;
    char* verify_error = 0;
    int i = 0;

    memset(&cg, 0, sizeof(cg));
    cg.program = program;
    cg.module = module;
    cg.context = context;
    cg.function = function;
    cg.llvm_function = LLVMGetNamedFunction(module, function->name);
    cg.builder = LLVMCreateBuilderInContext(context);
    cg.entry_builder = LLVMCreateBuilderInContext(context);
    lowered = find_lowered_function(program, function);
    if (!lowered || lowered->blocks.count <= 0) {
        return 0;
    }

    block_map = (JirBlockMapEntry*)calloc((size_t)lowered->blocks.count, sizeof(JirBlockMapEntry));
    if (!block_map) {
        *(&verify_error) = 0;
        return 0;
    }
    for (i = 0; i < lowered->blocks.count; ++i) {
        block_map[i].name = lowered->blocks.items[i].name;
        block_map[i].block = LLVMAppendBasicBlockInContext(context, cg.llvm_function, lowered->blocks.items[i].name);
    }
    LLVMPositionBuilderAtEnd(cg.builder, block_map[0].block);
    LLVMPositionBuilderAtEnd(cg.entry_builder, block_map[0].block);

    create_binding_allocas(&cg);
    for (i = 0; i < lowered->blocks.count; ++i) {
        int j = 0;
        const JirBasicBlock* block = &lowered->blocks.items[i];
        LLVMPositionBuilderAtEnd(cg.builder, block_map[i].block);
        for (j = 0; j < block->insts.count; ++j) {
            if (!emit_jir_inst(&cg, &block->insts.items[j])) {
                free(block_map);
                LLVMDisposeBuilder(cg.entry_builder);
                LLVMDisposeBuilder(cg.builder);
                free(cg.allocas);
                return 0;
            }
        }
        switch (block->term.kind) {
            case JIR_TERM_FALLTHROUGH:
                if (!block_terminated(LLVMGetInsertBlock(cg.builder))) {
                    if (function->return_type->kind == JIR_TYPE_VOID) {
                        LLVMBuildRetVoid(cg.builder);
                    } else {
                        LLVMBuildRet(cg.builder, LLVMConstNull(llvm_type(context, function->return_type)));
                    }
                }
                break;
            case JIR_TERM_RETURN:
                if (!block_terminated(LLVMGetInsertBlock(cg.builder))) {
                    if (!block->term.value || function->return_type->kind == JIR_TYPE_VOID) {
                        LLVMBuildRetVoid(cg.builder);
                    } else {
                        LLVMBuildRet(cg.builder, emit_expr(&cg, block->term.value));
                    }
                }
                break;
            case JIR_TERM_BRANCH:
                if (!block_terminated(LLVMGetInsertBlock(cg.builder))) {
                    LLVMBuildBr(cg.builder, block_map[block->term.then_target.index].block);
                }
                break;
            case JIR_TERM_COND_BRANCH:
                if (!block_terminated(LLVMGetInsertBlock(cg.builder))) {
                    LLVMBuildCondBr(
                        cg.builder,
                        emit_expr(&cg, block->term.cond),
                        block_map[block->term.then_target.index].block,
                        block_map[block->term.else_target.index].block);
                }
                break;
        }
    }

    if (LLVMVerifyFunction(cg.llvm_function, LLVMReturnStatusAction) != 0) {
        LLVMVerifyModule(module, LLVMReturnStatusAction, &verify_error);
        LLVMDisposeMessage(verify_error);
        LLVMDisposeBuilder(cg.entry_builder);
        LLVMDisposeBuilder(cg.builder);
        free(block_map);
        free(cg.allocas);
        return 0;
    }

    LLVMDisposeBuilder(cg.entry_builder);
    LLVMDisposeBuilder(cg.builder);
    free(block_map);
    free(cg.allocas);
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
