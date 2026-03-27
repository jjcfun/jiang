#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
MANIFEST="$PROJECT_ROOT/bootstrap/jiang.build"
OUTPUT_BIN="$PROJECT_ROOT/bootstrap/build/lexer"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make

cd "$PROJECT_ROOT"

"$BUILD_DIR/jiangc" build --manifest "$MANIFEST"

if [[ ! -f "$OUTPUT_BIN" ]]; then
    echo "stage1 build system smoke missing bootstrap output binary" >&2
    exit 1
fi

LEXER_OUT="$("$OUTPUT_BIN")"
printf '%s\n' "$LEXER_OUT"

if [[ "$LEXER_OUT" != *"bootstrap lexer token dump:"* ]]; then
    echo "stage1 build system smoke missing lexer dump header" >&2
    exit 1
fi

if [[ "$LEXER_OUT" != *"eof"* ]]; then
    echo "stage1 build system smoke missing lexer eof token" >&2
    exit 1
fi

echo "stage1 build system smoke passed"
