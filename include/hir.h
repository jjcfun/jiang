#ifndef JIANG_HIR_H
#define JIANG_HIR_H

#include <stdint.h>

typedef enum HirTypeKind {
    HIR_TYPE_INT = 0,
} HirTypeKind;

typedef struct HirExpr {
    HirTypeKind type_kind;
    int64_t int_value;
} HirExpr;

typedef struct HirReturnStmt {
    HirExpr expr;
} HirReturnStmt;

typedef struct HirProgram {
    HirReturnStmt return_stmt;
} HirProgram;

#endif
