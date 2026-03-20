#ifndef JIANG_LOWER_H
#define JIANG_LOWER_H

#include "ast.h"
#include "jir.h"

// Entry point to lower a full program (root block) into a JIR Module
JirModule* lower_to_jir(ASTNode* root, Arena* arena);

#endif // JIANG_LOWER_H
