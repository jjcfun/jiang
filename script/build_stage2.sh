#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

STAGE2_C_PATH="$BUILD_DIR/stage2_compiler.c"

cd "$PROJECT_ROOT"
"$BUILD_DIR/stage1c" --mode emit-c "compiler/entries/compiler.jiang" > "$STAGE2_C_PATH"

cc -std=c99 -I "$PROJECT_ROOT/include" \
    "$STAGE2_C_PATH" \
    "$PROJECT_ROOT/runtime/stage1_host.c" \
    "$PROJECT_ROOT/compiler/stage2_driver.c" \
    -o "$BUILD_DIR/stage2c"

echo "built $BUILD_DIR/stage2c"
