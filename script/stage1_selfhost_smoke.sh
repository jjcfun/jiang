#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_DIR="$BUILD_DIR/stage1_selfhost_smoke"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make

mkdir -p "$OUT_DIR"
cd "$PROJECT_ROOT"

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_compiler_core.jiang"
"$BUILD_DIR/compiler_compiler_core" > "$OUT_DIR/compiler_compiler_core.c"

cat > "$OUT_DIR/runtime_shim.c" <<'EOF'
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

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
    if (!text) return NULL;
    if (value.length > 0 && value.ptr) memcpy(text, value.ptr, (size_t)value.length);
    text[value.length] = '\0';
    return text;
}

void __intrinsic_print(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void __intrinsic_assert(int cond) {
    if (cond) return;
    fprintf(stderr, "stage1 intrinsic assert failed\n");
    abort();
}

int __intrinsic_file_exists(Slice_uint8_t path) {
    char* path_text = jiang_runtime_to_cstr(path);
    if (!path_text) return 0;
    FILE* file = fopen(path_text, "rb");
    free(path_text);
    if (!file) return 0;
    fclose(file);
    return 1;
}

Slice_uint8_t __intrinsic_read_file(Slice_uint8_t path) {
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

Slice_int64_t __intrinsic_alloc_ints(int64_t length) {
    Slice_int64_t result = {0};
    if (length <= 0) length = 1;
    result.ptr = (int64_t*)calloc((size_t)length, sizeof(int64_t));
    if (!result.ptr) return result;
    result.length = length;
    return result;
}

Slice_uint8_t __intrinsic_alloc_bytes(int64_t length) {
    Slice_uint8_t result = {0};
    if (length <= 0) length = 1;
    result.ptr = (uint8_t*)calloc((size_t)length, sizeof(uint8_t));
    if (!result.ptr) return result;
    result.length = length;
    return result;
}
EOF

cat > "$OUT_DIR/driver.c" <<'EOF'
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t* ptr;
    int64_t length;
} Slice_uint8_t;

extern void compile_entry(Slice_uint8_t entry_path, long long mode);
extern long long mode_emit_c;

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <entry>\n", argv[0]);
        return 1;
    }
    compile_entry((Slice_uint8_t){(uint8_t*)argv[1], (int64_t)strlen(argv[1])}, mode_emit_c);
    return 0;
}
EOF

cc -std=c99 -Wall -Wextra -Werror "$OUT_DIR/compiler_compiler_core.c" "$OUT_DIR/runtime_shim.c" "$OUT_DIR/driver.c" -o "$OUT_DIR/stage1_compiler"

"$OUT_DIR/stage1_compiler" "bootstrap/compiler_core.jiang" > "$OUT_DIR/compiler_core_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/compiler_core_from_stage1.c" -o "$OUT_DIR/compiler_core_from_stage1.o"
if [[ "$(<"$OUT_DIR/compiler_core_from_stage1.c")" != *"compile_entry"* ]]; then
    echo "stage1 selfhost output missing compile_entry symbol" >&2
    exit 1
fi

"$OUT_DIR/stage1_compiler" "bootstrap/source_loader.jiang" > "$OUT_DIR/source_loader_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/source_loader_from_stage1.c" -o "$OUT_DIR/source_loader_from_stage1.o"
if [[ "$(<"$OUT_DIR/source_loader_from_stage1.c")" != *"read_source"* ]]; then
    echo "stage1 selfhost output missing read_source symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/source_loader_from_stage1.c")" != *"is_supported_module_import"* ]]; then
    echo "stage1 selfhost output missing is_supported_module_import symbol" >&2
    exit 1
fi

echo "stage1 selfhost smoke passed"
