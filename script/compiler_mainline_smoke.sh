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

if [[ "$OUT" != *"stage2 hir dump:"* ]]; then
    echo "compiler mainline smoke missing hir dump header" >&2
    exit 1
fi

if [[ "$OUT" != *"struct_decl public sym=Box"* ]]; then
    echo "compiler mainline smoke missing hir struct decl" >&2
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

echo "compiler mainline smoke passed"
