#ifndef JIANG_JIR_GEN_H
#define JIANG_JIR_GEN_H

#include "jir.h"
#include "ast.h"

// Generate C code without relying on an unstable Module struct pointer
void jir_generate_c(JirModule* mod, ASTNode* root, const char* out_path, bool is_main, const char* mod_name, const char* mod_id);

#endif
