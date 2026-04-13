#ifndef JIANG_LOWER_H
#define JIANG_LOWER_H

#include "ast.h"
#include "hir.h"
#include "jir.h"

int lower_ast_to_hir(const AstProgram* ast, HirProgram* hir, const char** error);
int lower_hir_to_jir(const HirProgram* hir, JirModule* jir, const char** error);

#endif
