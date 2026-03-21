#ifndef JIANG_SEMANTIC_H
#define JIANG_SEMANTIC_H

#include <stdbool.h>
#include "ast.h"

// Forward declaration of Module structure (defined internally in semantic.c)
typedef struct Module Module;

// Performs semantic analysis on the AST. Returns the root Module pointer if valid, NULL otherwise.
Module* semantic_check(Arena* arena, ASTNode* root, const char* path);

// Reset semantic state between runs
void semantic_reset(void);

// Helper to get module info for code generation
const char* module_get_name(Module* mod);
const char* module_get_id(Module* mod);
ASTNode* module_get_root(Module* mod);

// Get all modules discovered during analysis (for mass code generation)
int semantic_get_modules(Module** mods);

#endif // JIANG_SEMANTIC_H
