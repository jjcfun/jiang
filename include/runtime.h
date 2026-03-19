#ifndef JIANG_RUNTIME_H
#define JIANG_RUNTIME_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Common types for Jiang runtime
typedef struct {
    int64_t* ptr;
    int64_t length;
} Slice_int64_t;

#endif // JIANG_RUNTIME_H
