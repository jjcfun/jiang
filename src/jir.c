#include "lower.h"

int lower_hir_to_jir(const HirProgram* hir, JirModule* jir, const char** error) {
    (void)error;
    jir->main_fn.name = "main";
    jir->main_fn.return_value = hir->return_stmt.expr.int_value;
    return 1;
}
