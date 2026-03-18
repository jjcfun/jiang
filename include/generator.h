#ifndef JIANG_GENERATOR_H
#define JIANG_GENERATOR_H

#include "ast.h"

// Translates the given AST into an executable C source file at out_path
void generate_c_code(ASTNode* root, const char* out_path);

#endif // JIANG_GENERATOR_H
