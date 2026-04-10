#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

typedef struct {
    int64_t* ptr;
    int64_t length;
} Slice_int64_t;

typedef struct {
    uint8_t* ptr;
    int64_t length;
} Slice_uint8_t;

static char* jiang_llvm_to_cstr(Slice_uint8_t value) {
    char* text = (char*)malloc((size_t)value.length + 1);
    if (!text) {
        return NULL;
    }
    if (value.ptr && value.length > 0) {
        memcpy(text, value.ptr, (size_t)value.length);
    }
    text[value.length] = '\0';
    return text;
}

static int64_t jiang_llvm_wrap_ptr(void* ptr) {
    return (int64_t)(intptr_t)ptr;
}

static void* jiang_llvm_unwrap_ptr(int64_t value) {
    return (void*)(intptr_t)value;
}

#define JIANG_LLVM_API(name) _m64_##name

int64_t JIANG_LLVM_API(jiang_llvm_context_create)(void) {
    return jiang_llvm_wrap_ptr(LLVMContextCreate());
}

void JIANG_LLVM_API(jiang_llvm_context_dispose)(int64_t context) {
    LLVMContextDispose((LLVMContextRef)jiang_llvm_unwrap_ptr(context));
}

int64_t JIANG_LLVM_API(jiang_llvm_module_create)(int64_t context, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext(text ? text : "jiang_stage2",
                                                          (LLVMContextRef)jiang_llvm_unwrap_ptr(context));
    free(text);
    return jiang_llvm_wrap_ptr(mod);
}

void JIANG_LLVM_API(jiang_llvm_module_dispose)(int64_t module_ref) {
    LLVMDisposeModule((LLVMModuleRef)jiang_llvm_unwrap_ptr(module_ref));
}

int64_t JIANG_LLVM_API(jiang_llvm_builder_create)(int64_t context) {
    return jiang_llvm_wrap_ptr(LLVMCreateBuilderInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)));
}

void JIANG_LLVM_API(jiang_llvm_builder_dispose)(int64_t builder) {
    LLVMDisposeBuilder((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder));
}

int64_t JIANG_LLVM_API(jiang_llvm_type_void)(int64_t context) {
    return jiang_llvm_wrap_ptr(LLVMVoidTypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)));
}

int64_t JIANG_LLVM_API(jiang_llvm_type_i1)(int64_t context) {
    return jiang_llvm_wrap_ptr(LLVMInt1TypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)));
}

int64_t JIANG_LLVM_API(jiang_llvm_type_i32)(int64_t context) {
    return jiang_llvm_wrap_ptr(LLVMInt32TypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)));
}

int64_t JIANG_LLVM_API(jiang_llvm_type_i64)(int64_t context) {
    return jiang_llvm_wrap_ptr(LLVMInt64TypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)));
}

int64_t JIANG_LLVM_API(jiang_llvm_function_type)(int64_t return_type, Slice_int64_t param_types, int is_vararg) {
    LLVMTypeRef params[64];
    unsigned count = 0;
    while (count < (unsigned)param_types.length && count < 64) {
        params[count] = (LLVMTypeRef)jiang_llvm_unwrap_ptr(param_types.ptr[count]);
        count++;
    }
    return jiang_llvm_wrap_ptr(
        LLVMFunctionType(
            (LLVMTypeRef)jiang_llvm_unwrap_ptr(return_type),
            count == 0 ? NULL : params,
            count,
            is_vararg ? 1 : 0
        )
    );
}

int64_t JIANG_LLVM_API(jiang_llvm_add_function)(int64_t module_ref, Slice_uint8_t name, int64_t fn_type) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef fn = LLVMAddFunction(
        (LLVMModuleRef)jiang_llvm_unwrap_ptr(module_ref),
        text ? text : "fn",
        (LLVMTypeRef)jiang_llvm_unwrap_ptr(fn_type)
    );
    free(text);
    return jiang_llvm_wrap_ptr(fn);
}

int64_t JIANG_LLVM_API(jiang_llvm_get_param)(int64_t function_ref, int64_t index) {
    return jiang_llvm_wrap_ptr(LLVMGetParam((LLVMValueRef)jiang_llvm_unwrap_ptr(function_ref), (unsigned)index));
}

int64_t JIANG_LLVM_API(jiang_llvm_append_block)(int64_t context, int64_t function_ref, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMBasicBlockRef block = LLVMAppendBasicBlockInContext(
        (LLVMContextRef)jiang_llvm_unwrap_ptr(context),
        (LLVMValueRef)jiang_llvm_unwrap_ptr(function_ref),
        text ? text : "bb"
    );
    free(text);
    return jiang_llvm_wrap_ptr(block);
}

void JIANG_LLVM_API(jiang_llvm_position_builder)(int64_t builder, int64_t block_ref) {
    LLVMPositionBuilderAtEnd(
        (LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
        (LLVMBasicBlockRef)jiang_llvm_unwrap_ptr(block_ref)
    );
}

int JIANG_LLVM_API(jiang_llvm_block_has_terminator)(int64_t block_ref) {
    return LLVMGetBasicBlockTerminator((LLVMBasicBlockRef)jiang_llvm_unwrap_ptr(block_ref)) != NULL;
}

int64_t JIANG_LLVM_API(jiang_llvm_const_i1)(int64_t context, int value) {
    return jiang_llvm_wrap_ptr(LLVMConstInt(LLVMInt1TypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)), value ? 1 : 0, false));
}

int64_t JIANG_LLVM_API(jiang_llvm_const_i32)(int64_t context, int64_t value) {
    return jiang_llvm_wrap_ptr(LLVMConstInt(LLVMInt32TypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)), (unsigned long long)value, true));
}

int64_t JIANG_LLVM_API(jiang_llvm_const_i64)(int64_t context, int64_t value) {
    return jiang_llvm_wrap_ptr(LLVMConstInt(LLVMInt64TypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)), (unsigned long long)value, true));
}

int64_t JIANG_LLVM_API(jiang_llvm_build_alloca)(int64_t builder, int64_t llvm_type, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef value = LLVMBuildAlloca(
        (LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
        (LLVMTypeRef)jiang_llvm_unwrap_ptr(llvm_type),
        text ? text : "tmp"
    );
    free(text);
    return jiang_llvm_wrap_ptr(value);
}

int64_t JIANG_LLVM_API(jiang_llvm_build_load)(int64_t builder, int64_t llvm_type, int64_t ptr_value, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef value = LLVMBuildLoad2(
        (LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
        (LLVMTypeRef)jiang_llvm_unwrap_ptr(llvm_type),
        (LLVMValueRef)jiang_llvm_unwrap_ptr(ptr_value),
        text ? text : "loadtmp"
    );
    free(text);
    return jiang_llvm_wrap_ptr(value);
}

void JIANG_LLVM_API(jiang_llvm_build_store)(int64_t builder, int64_t value, int64_t ptr_value) {
    LLVMBuildStore(
        (LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
        (LLVMValueRef)jiang_llvm_unwrap_ptr(value),
        (LLVMValueRef)jiang_llvm_unwrap_ptr(ptr_value)
    );
}

int64_t JIANG_LLVM_API(jiang_llvm_build_add)(int64_t builder, int64_t left, int64_t right, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef value = LLVMBuildAdd((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
                                      (LLVMValueRef)jiang_llvm_unwrap_ptr(left),
                                      (LLVMValueRef)jiang_llvm_unwrap_ptr(right),
                                      text ? text : "addtmp");
    free(text);
    return jiang_llvm_wrap_ptr(value);
}

int64_t JIANG_LLVM_API(jiang_llvm_build_sub)(int64_t builder, int64_t left, int64_t right, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef value = LLVMBuildSub((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
                                      (LLVMValueRef)jiang_llvm_unwrap_ptr(left),
                                      (LLVMValueRef)jiang_llvm_unwrap_ptr(right),
                                      text ? text : "subtmp");
    free(text);
    return jiang_llvm_wrap_ptr(value);
}

int64_t JIANG_LLVM_API(jiang_llvm_build_mul)(int64_t builder, int64_t left, int64_t right, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef value = LLVMBuildMul((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
                                      (LLVMValueRef)jiang_llvm_unwrap_ptr(left),
                                      (LLVMValueRef)jiang_llvm_unwrap_ptr(right),
                                      text ? text : "multmp");
    free(text);
    return jiang_llvm_wrap_ptr(value);
}

int64_t JIANG_LLVM_API(jiang_llvm_build_sdiv)(int64_t builder, int64_t left, int64_t right, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef value = LLVMBuildSDiv((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
                                       (LLVMValueRef)jiang_llvm_unwrap_ptr(left),
                                       (LLVMValueRef)jiang_llvm_unwrap_ptr(right),
                                       text ? text : "divtmp");
    free(text);
    return jiang_llvm_wrap_ptr(value);
}

#define JIANG_LLVM_ICMP(name, pred) \
int64_t name(int64_t builder, int64_t left, int64_t right, Slice_uint8_t text_name) { \
    char* text = jiang_llvm_to_cstr(text_name); \
    LLVMValueRef value = LLVMBuildICmp((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder), pred, \
                                       (LLVMValueRef)jiang_llvm_unwrap_ptr(left), \
                                       (LLVMValueRef)jiang_llvm_unwrap_ptr(right), \
                                       text ? text : "cmptmp"); \
    free(text); \
    return jiang_llvm_wrap_ptr(value); \
}

JIANG_LLVM_ICMP(JIANG_LLVM_API(jiang_llvm_build_icmp_eq), LLVMIntEQ)
JIANG_LLVM_ICMP(JIANG_LLVM_API(jiang_llvm_build_icmp_ne), LLVMIntNE)
JIANG_LLVM_ICMP(JIANG_LLVM_API(jiang_llvm_build_icmp_slt), LLVMIntSLT)
JIANG_LLVM_ICMP(JIANG_LLVM_API(jiang_llvm_build_icmp_sle), LLVMIntSLE)
JIANG_LLVM_ICMP(JIANG_LLVM_API(jiang_llvm_build_icmp_sgt), LLVMIntSGT)
JIANG_LLVM_ICMP(JIANG_LLVM_API(jiang_llvm_build_icmp_sge), LLVMIntSGE)

int64_t JIANG_LLVM_API(jiang_llvm_build_call)(int64_t builder, int64_t fn_type, int64_t function_ref, Slice_int64_t args, Slice_uint8_t name) {
    LLVMValueRef call_args[64];
    unsigned count = 0;
    while (count < (unsigned)args.length && count < 64) {
        call_args[count] = (LLVMValueRef)jiang_llvm_unwrap_ptr(args.ptr[count]);
        count++;
    }
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef value = LLVMBuildCall2(
        (LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
        (LLVMTypeRef)jiang_llvm_unwrap_ptr(fn_type),
        (LLVMValueRef)jiang_llvm_unwrap_ptr(function_ref),
        count == 0 ? NULL : call_args,
        count,
        text ? text : "calltmp"
    );
    free(text);
    return jiang_llvm_wrap_ptr(value);
}

int64_t JIANG_LLVM_API(jiang_llvm_build_trunc)(int64_t builder, int64_t value, int64_t llvm_type, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef out = LLVMBuildTrunc(
        (LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
        (LLVMValueRef)jiang_llvm_unwrap_ptr(value),
        (LLVMTypeRef)jiang_llvm_unwrap_ptr(llvm_type),
        text ? text : "trunctmp"
    );
    free(text);
    return jiang_llvm_wrap_ptr(out);
}

void JIANG_LLVM_API(jiang_llvm_build_ret)(int64_t builder, int64_t value) {
    LLVMBuildRet((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder), (LLVMValueRef)jiang_llvm_unwrap_ptr(value));
}

void JIANG_LLVM_API(jiang_llvm_build_ret_void)(int64_t builder) {
    LLVMBuildRetVoid((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder));
}

void JIANG_LLVM_API(jiang_llvm_build_br)(int64_t builder, int64_t target_block) {
    LLVMBuildBr((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder), (LLVMBasicBlockRef)jiang_llvm_unwrap_ptr(target_block));
}

void JIANG_LLVM_API(jiang_llvm_build_cond_br)(int64_t builder, int64_t cond_value, int64_t then_block, int64_t else_block) {
    LLVMBuildCondBr((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
                    (LLVMValueRef)jiang_llvm_unwrap_ptr(cond_value),
                    (LLVMBasicBlockRef)jiang_llvm_unwrap_ptr(then_block),
                    (LLVMBasicBlockRef)jiang_llvm_unwrap_ptr(else_block));
}

int JIANG_LLVM_API(jiang_llvm_verify_module)(int64_t module_ref) {
    char* error = NULL;
    int failed = LLVMVerifyModule((LLVMModuleRef)jiang_llvm_unwrap_ptr(module_ref), LLVMReturnStatusAction, &error);
    if (failed != 0 && error) {
        fprintf(stderr, "%s\n", error);
    }
    if (error) {
        LLVMDisposeMessage(error);
    }
    return failed == 0;
}

void JIANG_LLVM_API(jiang_llvm_print_module)(int64_t module_ref) {
    char* text = LLVMPrintModuleToString((LLVMModuleRef)jiang_llvm_unwrap_ptr(module_ref));
    if (!text) {
        return;
    }
    fputs(text, stdout);
    LLVMDisposeMessage(text);
}
