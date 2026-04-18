#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int64_t* ptr;
    int64_t length;
} Slice_int64_t;

typedef struct {
    uint8_t* ptr;
    int64_t length;
} Slice_uint8_t;

static char* jiang_runtime_to_cstr(Slice_uint8_t value) {
    char* text = (char*)malloc((size_t)value.length + 1);
    if (!text) {
        return NULL;
    }
    if (value.length > 0 && value.ptr) {
        memcpy(text, value.ptr, (size_t)value.length);
    }
    text[value.length] = '\0';
    return text;
}

void __intrinsic_print(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void __intrinsic_write_stdout(Slice_uint8_t value) {
    if (value.length <= 0 || !value.ptr) {
        return;
    }
    fwrite(value.ptr, 1, (size_t)value.length, stdout);
}

void __intrinsic_assert(int cond) {
    if (cond) {
        return;
    }
    fprintf(stderr, "jiang intrinsic assert failed\n");
    abort();
}

int __intrinsic_file_exists(Slice_uint8_t path) {
    char* path_text = jiang_runtime_to_cstr(path);
    if (!path_text) {
        return 0;
    }

    FILE* file = fopen(path_text, "rb");
    free(path_text);
    if (!file) {
        return 0;
    }

    fclose(file);
    return 1;
}

Slice_uint8_t __intrinsic_read_file(Slice_uint8_t path) {
    Slice_uint8_t result = {0};
    char* path_text = jiang_runtime_to_cstr(path);
    if (!path_text) {
        return result;
    }

    FILE* file = fopen(path_text, "rb");
    free(path_text);
    if (!file) {
        return result;
    }

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

uint8_t* __intrinsic_malloc(int64_t size) {
    if (size <= 0) {
        size = 1;
    }
    return (uint8_t*)malloc((size_t)size);
}

void __intrinsic_free(uint8_t* ptr) {
    if (!ptr) {
        return;
    }
    free(ptr);
}

uint8_t* __intrinsic_memmove(uint8_t* dst, uint8_t* src, int64_t bytes) {
    if (bytes <= 0) {
        return dst;
    }
    if (!dst || !src) {
        return dst;
    }
    memmove(dst, src, (size_t)bytes);
    return dst;
}

Slice_int64_t __intrinsic_alloc_ints(int64_t length) {
    Slice_int64_t result = {0};
    if (length <= 0) {
        length = 1;
    }
    result.ptr = (int64_t*)calloc((size_t)length, sizeof(int64_t));
    if (!result.ptr) {
        return result;
    }
    result.length = length;
    return result;
}

Slice_uint8_t __intrinsic_alloc_bytes(int64_t length) {
    Slice_uint8_t result = {0};
    if (length <= 0) {
        length = 1;
    }
    result.ptr = (uint8_t*)calloc((size_t)length, sizeof(uint8_t));
    if (!result.ptr) {
        return result;
    }
    result.length = length;
    return result;
}
