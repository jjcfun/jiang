#ifndef JIANG_GENERATOR_H
#define JIANG_GENERATOR_H

#include "ast.h"
#include "jir.h"

// Translates the given AST into an executable C source file at out_path
void generate_c_code(ASTNode* root, const char* out_path, bool is_main);
void generate_c_code_from_jir(JirModule* mod, ASTNode* root, const char* out_path, bool is_main);

#endif // JIANG_GENERATOR_H
