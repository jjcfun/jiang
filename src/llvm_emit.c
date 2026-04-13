#include "llvm_emit.h"

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

int emit_llvm_module(const JirModule* module, char** out_ir, const char** error) {
    LLVMContextRef context = LLVMContextCreate();
    LLVMModuleRef llvm_module = LLVMModuleCreateWithNameInContext("jiang", context);
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(context);
    LLVMTypeRef i64_type = LLVMInt64TypeInContext(context);
    LLVMTypeRef fn_type = LLVMFunctionType(i64_type, 0, 0, 0);
    LLVMValueRef fn = LLVMAddFunction(llvm_module, module->main_fn.name, fn_type);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(context, fn, "entry");
    char* verify_error = 0;

    LLVMPositionBuilderAtEnd(builder, entry);
    LLVMBuildRet(builder, LLVMConstInt(i64_type, (unsigned long long)module->main_fn.return_value, 1));

    if (LLVMVerifyModule(llvm_module, LLVMReturnStatusAction, &verify_error) != 0) {
        *error = verify_error;
        LLVMDisposeBuilder(builder);
        LLVMDisposeModule(llvm_module);
        LLVMContextDispose(context);
        return 0;
    }

    *out_ir = LLVMPrintModuleToString(llvm_module);
    LLVMDisposeBuilder(builder);
    LLVMDisposeModule(llvm_module);
    LLVMContextDispose(context);
    return 1;
}
