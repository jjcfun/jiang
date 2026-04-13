#include "lower.h"

int lower_ast_to_hir(const AstProgram* ast, HirProgram* hir, const char** error) {
    (void)error;
    hir->return_stmt.expr.type_kind = HIR_TYPE_INT;
    hir->return_stmt.expr.int_value = ast->return_stmt.expr.int_value;
    return 1;
}
