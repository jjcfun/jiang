#include "lower.h"

int lower_hir_to_jir(const HirProgram* hir, JirProgram* jir, const char** error) {
    (void)error;
    *jir = *hir;
    return 1;
}
