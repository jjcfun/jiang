#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
COMPILER="$BUILD_DIR/jiangc"
INPUT="$PROJECT_ROOT/compiler/compiler.jiang"
OUTPUT_BIN="$BUILD_DIR/compiler"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make

cd "$PROJECT_ROOT"

rm -f "$OUTPUT_BIN"
"$COMPILER" --stdlib-dir "$PROJECT_ROOT/std" "$INPUT" >/dev/null

if [[ ! -f "$OUTPUT_BIN" ]]; then
    echo "compiler mainline smoke missing output binary: $OUTPUT_BIN" >&2
    exit 1
fi

OUT="$("$OUTPUT_BIN")"

if [[ "$OUT" != *"stage2 semantic dump:"* ]]; then
    echo "compiler mainline smoke missing semantic dump header" >&2
    exit 1
fi

if [[ "$OUT" != *"program"* ]]; then
    echo "compiler mainline smoke missing program node" >&2
    exit 1
fi

if [[ "$OUT" != *"struct_decl public name=Box"* ]]; then
    echo "compiler mainline smoke missing semantic struct decl" >&2
    exit 1
fi

if [[ "$OUT" != *"field_decl name=value type=Int"* ]]; then
    echo "compiler mainline smoke missing semantic first field decl" >&2
    exit 1
fi

if [[ "$OUT" != *"field_decl name=count type=Int"* ]]; then
    echo "compiler mainline smoke missing semantic second field decl" >&2
    exit 1
fi

if [[ "$OUT" != *"struct_decl name=Point"* ]]; then
    echo "compiler mainline smoke missing semantic second struct decl" >&2
    exit 1
fi

if [[ "$OUT" != *"field_decl name=x type=UInt8"* ]]; then
    echo "compiler mainline smoke missing semantic Point.x field decl" >&2
    exit 1
fi

if [[ "$OUT" != *"field_decl name=y type=Bool"* ]]; then
    echo "compiler mainline smoke missing semantic Point.y field decl" >&2
    exit 1
fi

if [[ "$OUT" != *"func_decl return=Int name=add"* ]]; then
    echo "compiler mainline smoke missing semantic function decl" >&2
    exit 1
fi

if [[ "$OUT" != *"param_decl name=left type=Int"* ]]; then
    echo "compiler mainline smoke missing semantic first param" >&2
    exit 1
fi

if [[ "$OUT" != *"param_decl name=right type=Int"* ]]; then
    echo "compiler mainline smoke missing semantic second param" >&2
    exit 1
fi

if [[ "$OUT" != *"return_stmt value=left + right"* ]]; then
    echo "compiler mainline smoke missing semantic return stmt" >&2
    exit 1
fi

if [[ "$OUT" != *"stage2 hir dump:"* ]]; then
    echo "compiler mainline smoke missing hir dump header" >&2
    exit 1
fi

if [[ "$OUT" != *"struct_decl public sym=Box"* ]]; then
    echo "compiler mainline smoke missing hir struct decl" >&2
    exit 1
fi

if [[ "$OUT" != *"struct_decl sym=Point"* ]]; then
    echo "compiler mainline smoke missing hir second struct decl" >&2
    exit 1
fi

if [[ "$OUT" != *"func_decl return=Int sym=add"* ]]; then
    echo "compiler mainline smoke missing hir function decl" >&2
    exit 1
fi

if [[ "$OUT" != *"stage2 jir dump:"* ]]; then
    echo "compiler mainline smoke missing jir dump header" >&2
    exit 1
fi

if [[ "$OUT" != *"field_decl name=count type=Int"* ]]; then
    echo "compiler mainline smoke missing jir second field decl" >&2
    exit 1
fi

if [[ "$OUT" != *"field_decl name=x type=UInt8"* ]]; then
    echo "compiler mainline smoke missing jir Point.x field decl" >&2
    exit 1
fi

if [[ "$OUT" != *"field_decl name=y type=Bool"* ]]; then
    echo "compiler mainline smoke missing jir Point.y field decl" >&2
    exit 1
fi

if [[ "$OUT" != *"func_decl return=Int sym=add"* ]]; then
    echo "compiler mainline smoke missing jir function decl" >&2
    exit 1
fi

if [[ "$OUT" != *"stage2 emit_c dump:"* ]]; then
    echo "compiler mainline smoke missing emit_c dump header" >&2
    exit 1
fi

if [[ "$OUT" != *"#include <stdint.h>"* ]]; then
    echo "compiler mainline smoke missing c include" >&2
    exit 1
fi

if [[ "$OUT" != *"typedef struct Box {"* ]]; then
    echo "compiler mainline smoke missing c struct decl" >&2
    exit 1
fi

if [[ "$OUT" != *"typedef struct Point {"* ]]; then
    echo "compiler mainline smoke missing Point c struct decl" >&2
    exit 1
fi

if [[ "$OUT" != *"int64_t value;"* ]]; then
    echo "compiler mainline smoke missing first c field" >&2
    exit 1
fi

if [[ "$OUT" != *"int64_t count;"* ]]; then
    echo "compiler mainline smoke missing second c field" >&2
    exit 1
fi

if [[ "$OUT" != *"Box Box_new(int64_t value, int64_t count)"* ]]; then
    echo "compiler mainline smoke missing c ctor" >&2
    exit 1
fi

if [[ "$OUT" != *"result.count = count;"* ]]; then
    echo "compiler mainline smoke missing c field assignment" >&2
    exit 1
fi

if [[ "$OUT" != *"uint8_t x;"* ]]; then
    echo "compiler mainline smoke missing Point.x c field" >&2
    exit 1
fi

if [[ "$OUT" != *"int64_t y;"* ]]; then
    echo "compiler mainline smoke missing Point.y c field" >&2
    exit 1
fi

if [[ "$OUT" != *"Point Point_new(uint8_t x, int64_t y)"* ]]; then
    echo "compiler mainline smoke missing Point c ctor" >&2
    exit 1
fi

if [[ "$OUT" != *"result.y = y;"* ]]; then
    echo "compiler mainline smoke missing Point c field assignment" >&2
    exit 1
fi

if [[ "$OUT" != *"int64_t add(int64_t left, int64_t right)"* ]]; then
    echo "compiler mainline smoke missing function c signature" >&2
    exit 1
fi

if [[ "$OUT" != *"return left + right;"* ]]; then
    echo "compiler mainline smoke missing function c return" >&2
    exit 1
fi

echo "compiler mainline smoke passed"
