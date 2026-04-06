#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_DIR="$BUILD_DIR/stage1_build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make

mkdir -p "$OUT_DIR"
cd "$PROJECT_ROOT"

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_compiler_core.jiang"
"$BUILD_DIR/compiler_compiler_core" > "$OUT_DIR/compiler_compiler_core.c"

cc -std=c99 -Wall -Wextra -Werror \
    "$OUT_DIR/compiler_compiler_core.c" \
    "$PROJECT_ROOT/runtime/stage1_host.c" \
    "$PROJECT_ROOT/bootstrap/stage1_driver.c" \
    -o "$BUILD_DIR/stage1c"

echo "built $BUILD_DIR/stage1c"
