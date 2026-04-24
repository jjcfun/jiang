#ifndef JIANG_LLVM_EMIT_H
#define JIANG_LLVM_EMIT_H

#include "jir.h"

int emit_llvm_ir(const JirProgram* program, char** out_ir, const char** error);
int emit_object_file(const JirProgram* program, const char* output_path, const char** error);

#endif
