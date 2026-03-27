#ifndef JIANG_JIR_GEN_H
#define JIANG_JIR_GEN_H

#include "jir.h"
#include "codegen_info.h"

void jir_generate_c(JirModule* mod, CodegenModuleInfo* info, const char* out_path, bool is_main,
                    const char* mod_name, const char* mod_id);

#endif
