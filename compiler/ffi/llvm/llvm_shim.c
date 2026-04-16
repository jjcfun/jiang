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
#define JIANG_LLVM_STAGE2_API(name) _m_compiler_ffi_llvm_core_jiang__##name

#define JIANG_LLVM_WRAP_RET0(ret, name) \
ret JIANG_LLVM_STAGE2_API(name)(void) { \
    return JIANG_LLVM_API(name)(); \
}

#define JIANG_LLVM_WRAP_RET1(ret, name, t1, a1) \
ret JIANG_LLVM_STAGE2_API(name)(t1 a1) { \
    return JIANG_LLVM_API(name)(a1); \
}

#define JIANG_LLVM_WRAP_RET2(ret, name, t1, a1, t2, a2) \
ret JIANG_LLVM_STAGE2_API(name)(t1 a1, t2 a2) { \
    return JIANG_LLVM_API(name)(a1, a2); \
}

#define JIANG_LLVM_WRAP_RET3(ret, name, t1, a1, t2, a2, t3, a3) \
ret JIANG_LLVM_STAGE2_API(name)(t1 a1, t2 a2, t3 a3) { \
    return JIANG_LLVM_API(name)(a1, a2, a3); \
}

#define JIANG_LLVM_WRAP_RET4(ret, name, t1, a1, t2, a2, t3, a3, t4, a4) \
ret JIANG_LLVM_STAGE2_API(name)(t1 a1, t2 a2, t3 a3, t4 a4) { \
    return JIANG_LLVM_API(name)(a1, a2, a3, a4); \
}

#define JIANG_LLVM_WRAP_RET5(ret, name, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5) \
ret JIANG_LLVM_STAGE2_API(name)(t1 a1, t2 a2, t3 a3, t4 a4, t5 a5) { \
    return JIANG_LLVM_API(name)(a1, a2, a3, a4, a5); \
}

#define JIANG_LLVM_WRAP_VOID1(name, t1, a1) \
void JIANG_LLVM_STAGE2_API(name)(t1 a1) { \
    JIANG_LLVM_API(name)(a1); \
}

#define JIANG_LLVM_WRAP_VOID2(name, t1, a1, t2, a2) \
void JIANG_LLVM_STAGE2_API(name)(t1 a1, t2 a2) { \
    JIANG_LLVM_API(name)(a1, a2); \
}

#define JIANG_LLVM_WRAP_VOID3(name, t1, a1, t2, a2, t3, a3) \
void JIANG_LLVM_STAGE2_API(name)(t1 a1, t2 a2, t3 a3) { \
    JIANG_LLVM_API(name)(a1, a2, a3); \
}

#define JIANG_LLVM_WRAP_VOID4(name, t1, a1, t2, a2, t3, a3, t4, a4) \
void JIANG_LLVM_STAGE2_API(name)(t1 a1, t2 a2, t3 a3, t4 a4) { \
    JIANG_LLVM_API(name)(a1, a2, a3, a4); \
}

int64_t JIANG_LLVM_API(context_create)(void) {
    return jiang_llvm_wrap_ptr(LLVMContextCreate());
}

void JIANG_LLVM_API(context_dispose)(int64_t context) {
    LLVMContextDispose((LLVMContextRef)jiang_llvm_unwrap_ptr(context));
}

int64_t JIANG_LLVM_API(module_create)(int64_t context, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext(text ? text : "jiang_stage2",
                                                          (LLVMContextRef)jiang_llvm_unwrap_ptr(context));
    free(text);
    return jiang_llvm_wrap_ptr(mod);
}

void JIANG_LLVM_API(module_dispose)(int64_t module_ref) {
    LLVMDisposeModule((LLVMModuleRef)jiang_llvm_unwrap_ptr(module_ref));
}

int64_t JIANG_LLVM_API(builder_create)(int64_t context) {
    return jiang_llvm_wrap_ptr(LLVMCreateBuilderInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)));
}

void JIANG_LLVM_API(builder_dispose)(int64_t builder) {
    LLVMDisposeBuilder((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder));
}

int64_t JIANG_LLVM_API(type_void)(int64_t context) {
    return jiang_llvm_wrap_ptr(LLVMVoidTypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)));
}

int64_t JIANG_LLVM_API(type_i1)(int64_t context) {
    return jiang_llvm_wrap_ptr(LLVMInt1TypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)));
}

int64_t JIANG_LLVM_API(type_i8)(int64_t context) {
    return jiang_llvm_wrap_ptr(LLVMInt8TypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)));
}

int64_t JIANG_LLVM_API(type_i32)(int64_t context) {
    return jiang_llvm_wrap_ptr(LLVMInt32TypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)));
}

int64_t JIANG_LLVM_API(type_i64)(int64_t context) {
    return jiang_llvm_wrap_ptr(LLVMInt64TypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)));
}

int64_t JIANG_LLVM_API(type_ptr)(int64_t llvm_type) {
    return jiang_llvm_wrap_ptr(LLVMPointerType((LLVMTypeRef)jiang_llvm_unwrap_ptr(llvm_type), 0));
}

int64_t JIANG_LLVM_API(array_type)(int64_t elem_type, int64_t count) {
    return jiang_llvm_wrap_ptr(LLVMArrayType((LLVMTypeRef)jiang_llvm_unwrap_ptr(elem_type), (unsigned)count));
}

int64_t JIANG_LLVM_API(type_struct_named)(int64_t context, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMTypeRef type = LLVMStructCreateNamed((LLVMContextRef)jiang_llvm_unwrap_ptr(context), text ? text : "struct");
    free(text);
    return jiang_llvm_wrap_ptr(type);
}

void JIANG_LLVM_API(type_struct_set_body)(int64_t struct_type, Slice_int64_t field_types) {
    LLVMTypeRef fields[64];
    unsigned count = 0;
    while (count < (unsigned)field_types.length && count < 64) {
        fields[count] = (LLVMTypeRef)jiang_llvm_unwrap_ptr(field_types.ptr[count]);
        count++;
    }
    LLVMStructSetBody((LLVMTypeRef)jiang_llvm_unwrap_ptr(struct_type), count == 0 ? NULL : fields, count, 0);
}

int64_t JIANG_LLVM_API(function_type)(int64_t return_type, Slice_int64_t param_types, int is_vararg) {
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

int64_t JIANG_LLVM_API(add_function)(int64_t module_ref, Slice_uint8_t name, int64_t fn_type) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef fn = LLVMAddFunction(
        (LLVMModuleRef)jiang_llvm_unwrap_ptr(module_ref),
        text ? text : "fn",
        (LLVMTypeRef)jiang_llvm_unwrap_ptr(fn_type)
    );
    free(text);
    return jiang_llvm_wrap_ptr(fn);
}

int64_t JIANG_LLVM_API(add_global)(int64_t module_ref, int64_t llvm_type, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef global = LLVMAddGlobal(
        (LLVMModuleRef)jiang_llvm_unwrap_ptr(module_ref),
        (LLVMTypeRef)jiang_llvm_unwrap_ptr(llvm_type),
        text ? text : "global"
    );
    free(text);
    return jiang_llvm_wrap_ptr(global);
}

int64_t JIANG_LLVM_API(get_param)(int64_t function_ref, int64_t index) {
    return jiang_llvm_wrap_ptr(LLVMGetParam((LLVMValueRef)jiang_llvm_unwrap_ptr(function_ref), (unsigned)index));
}

int64_t JIANG_LLVM_API(append_block)(int64_t context, int64_t function_ref, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMBasicBlockRef block = LLVMAppendBasicBlockInContext(
        (LLVMContextRef)jiang_llvm_unwrap_ptr(context),
        (LLVMValueRef)jiang_llvm_unwrap_ptr(function_ref),
        text ? text : "bb"
    );
    free(text);
    return jiang_llvm_wrap_ptr(block);
}

void JIANG_LLVM_API(position_builder)(int64_t builder, int64_t block_ref) {
    LLVMPositionBuilderAtEnd(
        (LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
        (LLVMBasicBlockRef)jiang_llvm_unwrap_ptr(block_ref)
    );
}

int64_t JIANG_LLVM_API(current_block)(int64_t builder) {
    return jiang_llvm_wrap_ptr(LLVMGetInsertBlock((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder)));
}

int JIANG_LLVM_API(block_has_terminator)(int64_t block_ref) {
    return LLVMGetBasicBlockTerminator((LLVMBasicBlockRef)jiang_llvm_unwrap_ptr(block_ref)) != NULL;
}

int64_t JIANG_LLVM_API(const_i1)(int64_t context, int value) {
    return jiang_llvm_wrap_ptr(LLVMConstInt(LLVMInt1TypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)), value ? 1 : 0, false));
}

int64_t JIANG_LLVM_API(const_i8)(int64_t context, int64_t value) {
    return jiang_llvm_wrap_ptr(LLVMConstInt(LLVMInt8TypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)), (unsigned long long)value, false));
}

int64_t JIANG_LLVM_API(const_i32)(int64_t context, int64_t value) {
    return jiang_llvm_wrap_ptr(LLVMConstInt(LLVMInt32TypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)), (unsigned long long)value, true));
}

int64_t JIANG_LLVM_API(const_i64)(int64_t context, int64_t value) {
    return jiang_llvm_wrap_ptr(LLVMConstInt(LLVMInt64TypeInContext((LLVMContextRef)jiang_llvm_unwrap_ptr(context)), (unsigned long long)value, true));
}

int64_t JIANG_LLVM_API(const_null)(int64_t llvm_type) {
    return jiang_llvm_wrap_ptr(LLVMConstNull((LLVMTypeRef)jiang_llvm_unwrap_ptr(llvm_type)));
}

int64_t JIANG_LLVM_API(const_undef)(int64_t llvm_type) {
    return jiang_llvm_wrap_ptr(LLVMGetUndef((LLVMTypeRef)jiang_llvm_unwrap_ptr(llvm_type)));
}

int64_t JIANG_LLVM_API(const_named_struct)(int64_t llvm_type, Slice_int64_t values) {
    LLVMValueRef fields[16];
    unsigned count = 0;
    while (count < (unsigned)values.length && count < 16) {
        fields[count] = (LLVMValueRef)jiang_llvm_unwrap_ptr(values.ptr[count]);
        count++;
    }
    return jiang_llvm_wrap_ptr(
        LLVMConstNamedStruct(
            (LLVMTypeRef)jiang_llvm_unwrap_ptr(llvm_type),
            count == 0 ? NULL : fields,
            count
        )
    );
}

void JIANG_LLVM_API(set_initializer)(int64_t global_ref, int64_t value) {
    LLVMSetInitializer(
        (LLVMValueRef)jiang_llvm_unwrap_ptr(global_ref),
        (LLVMValueRef)jiang_llvm_unwrap_ptr(value)
    );
}

int64_t JIANG_LLVM_API(build_alloca)(int64_t builder, int64_t llvm_type, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef value = LLVMBuildAlloca(
        (LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
        (LLVMTypeRef)jiang_llvm_unwrap_ptr(llvm_type),
        text ? text : "tmp"
    );
    free(text);
    return jiang_llvm_wrap_ptr(value);
}

int64_t JIANG_LLVM_API(build_load)(int64_t builder, int64_t llvm_type, int64_t ptr_value, Slice_uint8_t name) {
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

void JIANG_LLVM_API(build_store)(int64_t builder, int64_t value, int64_t ptr_value) {
    LLVMBuildStore(
        (LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
        (LLVMValueRef)jiang_llvm_unwrap_ptr(value),
        (LLVMValueRef)jiang_llvm_unwrap_ptr(ptr_value)
    );
}

int64_t JIANG_LLVM_API(build_gep)(int64_t builder, int64_t llvm_type, int64_t ptr_value, Slice_int64_t indices, Slice_uint8_t name) {
    LLVMValueRef llvm_indices[8];
    unsigned count = 0;
    while (count < (unsigned)indices.length && count < 8) {
        llvm_indices[count] = (LLVMValueRef)jiang_llvm_unwrap_ptr(indices.ptr[count]);
        count++;
    }
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef value = LLVMBuildGEP2(
        (LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
        (LLVMTypeRef)jiang_llvm_unwrap_ptr(llvm_type),
        (LLVMValueRef)jiang_llvm_unwrap_ptr(ptr_value),
        count == 0 ? NULL : llvm_indices,
        count,
        text ? text : "geptmp"
    );
    free(text);
    return jiang_llvm_wrap_ptr(value);
}

int64_t JIANG_LLVM_API(build_extract_value)(int64_t builder, int64_t aggregate, int64_t index, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef value = LLVMBuildExtractValue(
        (LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
        (LLVMValueRef)jiang_llvm_unwrap_ptr(aggregate),
        (unsigned)index,
        text ? text : "extracttmp"
    );
    free(text);
    return jiang_llvm_wrap_ptr(value);
}

int64_t JIANG_LLVM_API(build_insert_value)(int64_t builder, int64_t aggregate, int64_t value, int64_t index, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef out = LLVMBuildInsertValue(
        (LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
        (LLVMValueRef)jiang_llvm_unwrap_ptr(aggregate),
        (LLVMValueRef)jiang_llvm_unwrap_ptr(value),
        (unsigned)index,
        text ? text : "inserttmp"
    );
    free(text);
    return jiang_llvm_wrap_ptr(out);
}

int64_t JIANG_LLVM_API(build_global_string_ptr)(int64_t builder, Slice_uint8_t value, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(value);
    char* label = jiang_llvm_to_cstr(name);
    LLVMValueRef out = LLVMBuildGlobalStringPtr(
        (LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
        text ? text : "",
        label ? label : "str"
    );
    free(text);
    free(label);
    return jiang_llvm_wrap_ptr(out);
}

int64_t JIANG_LLVM_API(build_add)(int64_t builder, int64_t left, int64_t right, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef value = LLVMBuildAdd((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
                                      (LLVMValueRef)jiang_llvm_unwrap_ptr(left),
                                      (LLVMValueRef)jiang_llvm_unwrap_ptr(right),
                                      text ? text : "addtmp");
    free(text);
    return jiang_llvm_wrap_ptr(value);
}

int64_t JIANG_LLVM_API(build_and)(int64_t builder, int64_t left, int64_t right, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef value = LLVMBuildAnd((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
                                      (LLVMValueRef)jiang_llvm_unwrap_ptr(left),
                                      (LLVMValueRef)jiang_llvm_unwrap_ptr(right),
                                      text ? text : "andtmp");
    free(text);
    return jiang_llvm_wrap_ptr(value);
}

int64_t JIANG_LLVM_API(build_sub)(int64_t builder, int64_t left, int64_t right, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef value = LLVMBuildSub((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
                                      (LLVMValueRef)jiang_llvm_unwrap_ptr(left),
                                      (LLVMValueRef)jiang_llvm_unwrap_ptr(right),
                                      text ? text : "subtmp");
    free(text);
    return jiang_llvm_wrap_ptr(value);
}

int64_t JIANG_LLVM_API(build_mul)(int64_t builder, int64_t left, int64_t right, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef value = LLVMBuildMul((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
                                      (LLVMValueRef)jiang_llvm_unwrap_ptr(left),
                                      (LLVMValueRef)jiang_llvm_unwrap_ptr(right),
                                      text ? text : "multmp");
    free(text);
    return jiang_llvm_wrap_ptr(value);
}

int64_t JIANG_LLVM_API(build_sdiv)(int64_t builder, int64_t left, int64_t right, Slice_uint8_t name) {
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

JIANG_LLVM_ICMP(JIANG_LLVM_API(build_icmp_eq), LLVMIntEQ)
JIANG_LLVM_ICMP(JIANG_LLVM_API(build_icmp_ne), LLVMIntNE)
JIANG_LLVM_ICMP(JIANG_LLVM_API(build_icmp_slt), LLVMIntSLT)
JIANG_LLVM_ICMP(JIANG_LLVM_API(build_icmp_sle), LLVMIntSLE)
JIANG_LLVM_ICMP(JIANG_LLVM_API(build_icmp_sgt), LLVMIntSGT)
JIANG_LLVM_ICMP(JIANG_LLVM_API(build_icmp_sge), LLVMIntSGE)

int64_t JIANG_LLVM_API(build_call)(int64_t builder, int64_t fn_type, int64_t function_ref, Slice_int64_t args, Slice_uint8_t name) {
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

int64_t JIANG_LLVM_API(build_trunc)(int64_t builder, int64_t value, int64_t llvm_type, Slice_uint8_t name) {
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

int64_t JIANG_LLVM_API(build_zext)(int64_t builder, int64_t value, int64_t llvm_type, Slice_uint8_t name) {
    char* text = jiang_llvm_to_cstr(name);
    LLVMValueRef out = LLVMBuildZExt(
        (LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
        (LLVMValueRef)jiang_llvm_unwrap_ptr(value),
        (LLVMTypeRef)jiang_llvm_unwrap_ptr(llvm_type),
        text ? text : "zexttmp"
    );
    free(text);
    return jiang_llvm_wrap_ptr(out);
}

void JIANG_LLVM_API(build_ret)(int64_t builder, int64_t value) {
    LLVMBuildRet((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder), (LLVMValueRef)jiang_llvm_unwrap_ptr(value));
}

void JIANG_LLVM_API(build_ret_void)(int64_t builder) {
    LLVMBuildRetVoid((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder));
}

void JIANG_LLVM_API(build_br)(int64_t builder, int64_t target_block) {
    LLVMBuildBr((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder), (LLVMBasicBlockRef)jiang_llvm_unwrap_ptr(target_block));
}

void JIANG_LLVM_API(build_cond_br)(int64_t builder, int64_t cond_value, int64_t then_block, int64_t else_block) {
    LLVMBuildCondBr((LLVMBuilderRef)jiang_llvm_unwrap_ptr(builder),
                    (LLVMValueRef)jiang_llvm_unwrap_ptr(cond_value),
                    (LLVMBasicBlockRef)jiang_llvm_unwrap_ptr(then_block),
                    (LLVMBasicBlockRef)jiang_llvm_unwrap_ptr(else_block));
}

int JIANG_LLVM_API(verify_module)(int64_t module_ref) {
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

void JIANG_LLVM_API(print_module)(int64_t module_ref) {
    char* text = LLVMPrintModuleToString((LLVMModuleRef)jiang_llvm_unwrap_ptr(module_ref));
    if (!text) {
        return;
    }
    fputs(text, stdout);
    LLVMDisposeMessage(text);
}

JIANG_LLVM_WRAP_RET0(int64_t, context_create)
JIANG_LLVM_WRAP_VOID1(context_dispose, int64_t, context)
JIANG_LLVM_WRAP_RET2(int64_t, module_create, int64_t, context, Slice_uint8_t, name)
JIANG_LLVM_WRAP_VOID1(module_dispose, int64_t, module_ref)
JIANG_LLVM_WRAP_RET1(int64_t, builder_create, int64_t, context)
JIANG_LLVM_WRAP_VOID1(builder_dispose, int64_t, builder)
JIANG_LLVM_WRAP_RET1(int64_t, type_void, int64_t, context)
JIANG_LLVM_WRAP_RET1(int64_t, type_i1, int64_t, context)
JIANG_LLVM_WRAP_RET1(int64_t, type_i8, int64_t, context)
JIANG_LLVM_WRAP_RET1(int64_t, type_i32, int64_t, context)
JIANG_LLVM_WRAP_RET1(int64_t, type_i64, int64_t, context)
JIANG_LLVM_WRAP_RET1(int64_t, type_ptr, int64_t, llvm_type)
JIANG_LLVM_WRAP_RET2(int64_t, array_type, int64_t, elem_type, int64_t, count)
JIANG_LLVM_WRAP_RET2(int64_t, type_struct_named, int64_t, context, Slice_uint8_t, name)
JIANG_LLVM_WRAP_VOID2(type_struct_set_body, int64_t, struct_type, Slice_int64_t, field_types)
JIANG_LLVM_WRAP_RET3(int64_t, function_type, int64_t, return_type, Slice_int64_t, param_types, int, is_vararg)
JIANG_LLVM_WRAP_RET3(int64_t, add_function, int64_t, module_ref, Slice_uint8_t, name, int64_t, fn_type)
JIANG_LLVM_WRAP_RET3(int64_t, add_global, int64_t, module_ref, int64_t, llvm_type, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET2(int64_t, get_param, int64_t, function_ref, int64_t, index)
JIANG_LLVM_WRAP_RET3(int64_t, append_block, int64_t, context, int64_t, function_ref, Slice_uint8_t, name)
JIANG_LLVM_WRAP_VOID2(position_builder, int64_t, builder, int64_t, block_ref)
JIANG_LLVM_WRAP_RET1(int64_t, current_block, int64_t, builder)
JIANG_LLVM_WRAP_RET1(int, block_has_terminator, int64_t, block_ref)
JIANG_LLVM_WRAP_RET2(int64_t, const_i1, int64_t, context, int, value)
JIANG_LLVM_WRAP_RET2(int64_t, const_i8, int64_t, context, int64_t, value)
JIANG_LLVM_WRAP_RET2(int64_t, const_i32, int64_t, context, int64_t, value)
JIANG_LLVM_WRAP_RET2(int64_t, const_i64, int64_t, context, int64_t, value)
JIANG_LLVM_WRAP_RET1(int64_t, const_null, int64_t, llvm_type)
JIANG_LLVM_WRAP_RET1(int64_t, const_undef, int64_t, llvm_type)
JIANG_LLVM_WRAP_RET2(int64_t, const_named_struct, int64_t, llvm_type, Slice_int64_t, values)
JIANG_LLVM_WRAP_VOID2(set_initializer, int64_t, global_ref, int64_t, value)
JIANG_LLVM_WRAP_RET3(int64_t, build_alloca, int64_t, builder, int64_t, llvm_type, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET4(int64_t, build_load, int64_t, builder, int64_t, llvm_type, int64_t, ptr_value, Slice_uint8_t, name)
JIANG_LLVM_WRAP_VOID3(build_store, int64_t, builder, int64_t, value, int64_t, ptr_value)
JIANG_LLVM_WRAP_RET5(int64_t, build_gep, int64_t, builder, int64_t, llvm_type, int64_t, ptr_value, Slice_int64_t, indices, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET4(int64_t, build_extract_value, int64_t, builder, int64_t, aggregate, int64_t, index, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET5(int64_t, build_insert_value, int64_t, builder, int64_t, aggregate, int64_t, value, int64_t, index, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET3(int64_t, build_global_string_ptr, int64_t, builder, Slice_uint8_t, value, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET4(int64_t, build_add, int64_t, builder, int64_t, left, int64_t, right, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET4(int64_t, build_and, int64_t, builder, int64_t, left, int64_t, right, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET4(int64_t, build_sub, int64_t, builder, int64_t, left, int64_t, right, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET4(int64_t, build_mul, int64_t, builder, int64_t, left, int64_t, right, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET4(int64_t, build_sdiv, int64_t, builder, int64_t, left, int64_t, right, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET4(int64_t, build_icmp_eq, int64_t, builder, int64_t, left, int64_t, right, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET4(int64_t, build_icmp_ne, int64_t, builder, int64_t, left, int64_t, right, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET4(int64_t, build_icmp_slt, int64_t, builder, int64_t, left, int64_t, right, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET4(int64_t, build_icmp_sle, int64_t, builder, int64_t, left, int64_t, right, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET4(int64_t, build_icmp_sgt, int64_t, builder, int64_t, left, int64_t, right, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET4(int64_t, build_icmp_sge, int64_t, builder, int64_t, left, int64_t, right, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET5(int64_t, build_call, int64_t, builder, int64_t, fn_type, int64_t, function_ref, Slice_int64_t, args, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET4(int64_t, build_trunc, int64_t, builder, int64_t, value, int64_t, llvm_type, Slice_uint8_t, name)
JIANG_LLVM_WRAP_RET4(int64_t, build_zext, int64_t, builder, int64_t, value, int64_t, llvm_type, Slice_uint8_t, name)
JIANG_LLVM_WRAP_VOID2(build_ret, int64_t, builder, int64_t, value)
JIANG_LLVM_WRAP_VOID1(build_ret_void, int64_t, builder)
JIANG_LLVM_WRAP_VOID2(build_br, int64_t, builder, int64_t, target_block)
JIANG_LLVM_WRAP_VOID4(build_cond_br, int64_t, builder, int64_t, cond_value, int64_t, then_block, int64_t, else_block)
JIANG_LLVM_WRAP_RET1(int, verify_module, int64_t, module_ref)
JIANG_LLVM_WRAP_VOID1(print_module, int64_t, module_ref)
