#ifndef JIANG_LLVM_EMIT_H
#define JIANG_LLVM_EMIT_H

#include "jir.h"

int emit_llvm_module(const JirModule* module, char** out_ir, const char** error);

#endif
