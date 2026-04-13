#ifndef JIANG_JIR_H
#define JIANG_JIR_H

#include <stdint.h>

typedef struct JirFunction {
    const char* name;
    int64_t return_value;
} JirFunction;

typedef struct JirModule {
    JirFunction main_fn;
} JirModule;

#endif
