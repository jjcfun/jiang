#ifndef JIANG_GENERATOR_H
#define JIANG_GENERATOR_H

#include "jir.h"
#include "codegen_info.h"

void generate_c_code_from_jir(JirModule* mod, CodegenModuleInfo* info, const char* out_path, bool is_main);

#endif // JIANG_GENERATOR_H
