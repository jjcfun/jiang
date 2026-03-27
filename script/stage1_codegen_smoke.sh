#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_DIR="$BUILD_DIR/stage1_out"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make

cd "$PROJECT_ROOT"
"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler.jiang"

mkdir -p "$OUT_DIR"
"$BUILD_DIR/compiler" > "$OUT_DIR/codegen_sample.c"
cc -std=c99 "$OUT_DIR/codegen_sample.c" -o "$OUT_DIR/codegen_sample"

OUTPUT="$("$OUT_DIR/codegen_sample")"
printf '%s\n' "$OUTPUT"

if [ "$OUTPUT" != "8" ]; then
    echo "stage1 codegen output differs: expected 8" >&2
    exit 1
fi
