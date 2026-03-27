#ifndef JIANG_RUNTIME_H
#define JIANG_RUNTIME_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Common types for Jiang runtime.
// Stage1 string values remain UTF-8 encoded byte slices: length counts bytes
// and indexing observes raw bytes instead of Unicode code points.
typedef struct {
    int64_t* ptr;
    int64_t length;
} Slice_int64_t;

typedef struct {
    uint8_t* ptr;
    int64_t length;
} Slice_uint8_t;

// Stage1 runtime ABI is intentionally small and frozen for now.
// The only host intrinsics exposed to Jiang code are:
// - __intrinsic_print
// - __intrinsic_assert
// - __intrinsic_read_file
// - __intrinsic_file_exists
// - __intrinsic_alloc_ints
// - __intrinsic_alloc_bytes
//
// libc usage remains concentrated in this header and the host compiler
// implementation. Stage1 does not try to remove libc yet; it only fixes the
// boundary so later LLVM/native backend work has a stable runtime surface.
//
// Current libc touchpoints used here:
// - file IO: fopen / fread / fclose
// - allocation: malloc / calloc / free
// - byte copying: memcpy

static inline char* jiang_runtime_to_cstr(Slice_uint8_t value) {
    char* text = (char*)malloc((size_t)value.length + 1);
    if (!text) return NULL;
    if (value.length > 0 && value.ptr) memcpy(text, value.ptr, (size_t)value.length);
    text[value.length] = '\0';
    return text;
}

static inline bool __intrinsic_file_exists(Slice_uint8_t path) {
    char* path_text = jiang_runtime_to_cstr(path);
    if (!path_text) return false;
    FILE* file = fopen(path_text, "rb");
    free(path_text);
    if (!file) return false;
    fclose(file);
    return true;
}

static inline Slice_uint8_t __intrinsic_read_file(Slice_uint8_t path) {
    Slice_uint8_t result = {0};
    char* path_text = jiang_runtime_to_cstr(path);
    if (!path_text) return result;

    FILE* file = fopen(path_text, "rb");
    free(path_text);
    if (!file) return result;

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    if (size < 0) {
        fclose(file);
        return result;
    }

    uint8_t* buffer = (uint8_t*)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return result;
    }

    size_t read_size = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[read_size] = '\0';
    result.ptr = buffer;
    result.length = (int64_t)read_size;
    return result;
}

static inline Slice_int64_t __intrinsic_alloc_ints(int64_t length) {
    Slice_int64_t result = {0};
    if (length <= 0) length = 1;
    result.ptr = (int64_t*)calloc((size_t)length, sizeof(int64_t));
    if (!result.ptr) return result;
    result.length = length;
    return result;
}

static inline Slice_uint8_t __intrinsic_alloc_bytes(int64_t length) {
    Slice_uint8_t result = {0};
    if (length <= 0) length = 1;
    result.ptr = (uint8_t*)calloc((size_t)length, sizeof(uint8_t));
    if (!result.ptr) return result;
    result.length = length;
    return result;
}

#endif // JIANG_RUNTIME_H
