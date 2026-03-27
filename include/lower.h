#ifndef JIANG_LOWER_H
#define JIANG_LOWER_H

#include "hir.h"
#include "jir.h"

JirModule* lower_hir_to_jir(HIRModule* module, Arena* arena);

#endif // JIANG_LOWER_H
