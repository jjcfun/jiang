#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_DIR="$BUILD_DIR/stage1_link_smoke"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make

mkdir -p "$OUT_DIR"
cd "$PROJECT_ROOT"

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_token_store.jiang"
"$BUILD_DIR/compiler_token_store" > "$OUT_DIR/compiler_token_store.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/compiler_token_store.c" -o "$OUT_DIR/compiler_token_store.o"

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_compiler_core.jiang"
"$BUILD_DIR/compiler_compiler_core" > "$OUT_DIR/compiler_compiler_core.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/compiler_compiler_core.c" -o "$OUT_DIR/compiler_compiler_core.o"

echo "stage1 link smoke passed"
