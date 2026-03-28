#ifndef JIANG_LLVM_GEN_H
#define JIANG_LLVM_GEN_H

#include "jir.h"
#include "codegen_info.h"
#include <stdbool.h>
#include <stddef.h>

bool llvm_generate_ir(JirModule* mod, const char* out_path, char* error_buf, size_t error_buf_size);
bool llvm_generate_ir_program(JirModule** mods, CodegenModuleInfo** infos,
                              size_t mod_count, size_t entry_index,
                              const char* out_path, char* error_buf, size_t error_buf_size);

#endif // JIANG_LLVM_GEN_H
