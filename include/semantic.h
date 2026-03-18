#ifndef JIANG_SEMANTIC_H
#define JIANG_SEMANTIC_H

#include <stdbool.h>
#include "ast.h"

// Performs semantic analysis on the AST. Returns true if valid, false otherwise.
bool semantic_check(Arena* arena, ASTNode* root, const char* path);

#endif // JIANG_SEMANTIC_H
