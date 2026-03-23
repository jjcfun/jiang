#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make

cd "$PROJECT_ROOT"
"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/lexer.jiang"

OUTPUT="$("$BUILD_DIR/lexer")"
printf '%s\n' "$OUTPUT"

printf '%s' "$OUTPUT" | grep -q "bootstrap lexer token dump:"
printf '%s' "$OUTPUT" | grep -q "KW 0 6"
printf '%s' "$OUTPUT" | grep -q "INT 25 27"
printf '%s' "$OUTPUT" | grep -q "STRING 56 62"
printf '%s' "$OUTPUT" | grep -q "bootstrap lexer smoke passed"
