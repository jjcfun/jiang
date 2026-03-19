#ifndef JIANG_SEMANTIC_H
#define JIANG_SEMANTIC_H

#include <stdbool.h>
#include "ast.h"

// Performs semantic analysis on the AST. Returns true if valid, false otherwise.
bool semantic_check(Arena* arena, ASTNode* root, const char* path);

// Get the list of all C files generated during the process
int semantic_get_generated_files(char files[128][4096]);

#endif // JIANG_SEMANTIC_H
