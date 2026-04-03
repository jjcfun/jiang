#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
COMPILER="$BUILD_DIR/jiangc"
INPUT="$PROJECT_ROOT/compiler/parser_driver.jiang"
OUTPUT_BIN="$BUILD_DIR/parser_driver"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make

cd "$PROJECT_ROOT"

rm -f "$OUTPUT_BIN"
"$COMPILER" --stdlib-dir "$PROJECT_ROOT/std" "$INPUT" >/dev/null

if [[ ! -f "$OUTPUT_BIN" ]]; then
    echo "compiler parser smoke missing output binary: $OUTPUT_BIN" >&2
    exit 1
fi

OUT="$("$OUTPUT_BIN")"

if [[ "$OUT" != *"stage2 parser dump:"* ]]; then
    echo "compiler parser smoke missing parser dump header" >&2
    exit 1
fi

if [[ "$OUT" != *"program"* ]]; then
    echo "compiler parser smoke missing program node" >&2
    exit 1
fi

if [[ "$OUT" != *"struct_decl public Box"* ]]; then
    echo "compiler parser smoke missing struct decl" >&2
    exit 1
fi

if [[ "$OUT" != *"field Int value"* ]]; then
    echo "compiler parser smoke missing first field" >&2
    exit 1
fi

if [[ "$OUT" != *"field Int count"* ]]; then
    echo "compiler parser smoke missing second field" >&2
    exit 1
fi

if [[ "$OUT" != *"struct_decl Point"* ]]; then
    echo "compiler parser smoke missing second struct decl" >&2
    exit 1
fi

if [[ "$OUT" != *"field UInt8 x"* ]]; then
    echo "compiler parser smoke missing Point.x field" >&2
    exit 1
fi

if [[ "$OUT" != *"field Bool y"* ]]; then
    echo "compiler parser smoke missing Point.y field" >&2
    exit 1
fi

if [[ "$OUT" != *"func_decl Int add"* ]]; then
    echo "compiler parser smoke missing function decl" >&2
    exit 1
fi

if [[ "$OUT" != *"param Int left"* ]]; then
    echo "compiler parser smoke missing first param" >&2
    exit 1
fi

if [[ "$OUT" != *"param Int right"* ]]; then
    echo "compiler parser smoke missing second param" >&2
    exit 1
fi

if [[ "$OUT" != *"return left + right"* ]]; then
    echo "compiler parser smoke missing return expr" >&2
    exit 1
fi

echo "compiler parser smoke passed"
