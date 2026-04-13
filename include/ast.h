#ifndef JIANG_AST_H
#define JIANG_AST_H

#include <stdint.h>

typedef struct AstExpr {
    int64_t int_value;
} AstExpr;

typedef struct AstReturnStmt {
    AstExpr expr;
} AstReturnStmt;

typedef struct AstProgram {
    AstReturnStmt return_stmt;
} AstProgram;

#endif
