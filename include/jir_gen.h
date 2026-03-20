#ifndef JIANG_JIR_GEN_H
#define JIANG_JIR_GEN_H

#include "jir.h"

// Generates C code from a JIR Module. 
// out_path is the base path (e.g., "build/out.c").
// is_main indicates if this is the entry point module.
void jir_generate_c(JirModule* mod, ASTNode* root, const char* out_path, bool is_main);

#endif // JIANG_JIR_GEN_H
