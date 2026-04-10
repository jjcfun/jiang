#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
LLVM_CONFIG="${LLVM_CONFIG:-llvm-config}"

STAGE2_C_PATH="$BUILD_DIR/stage2_compiler.c"

cd "$PROJECT_ROOT"
"$BUILD_DIR/stage1c" --mode emit-c "compiler/entries/compiler.jiang" > "$STAGE2_C_PATH"

LLVM_CFLAGS="$($LLVM_CONFIG --cflags)"
LLVM_LDFLAGS="$($LLVM_CONFIG --ldflags)"
LLVM_LIBS="$($LLVM_CONFIG --libs core analysis --system-libs)"

cc -std=c99 -I "$PROJECT_ROOT/include" \
    $LLVM_CFLAGS \
    "$STAGE2_C_PATH" \
    "$PROJECT_ROOT/runtime/stage1_host.c" \
    "$PROJECT_ROOT/compiler/stage2_driver.c" \
    "$PROJECT_ROOT/compiler/ffi/llvm/llvm_shim.c" \
    $LLVM_LDFLAGS \
    $LLVM_LIBS \
    -o "$BUILD_DIR/stage2c"

echo "built $BUILD_DIR/stage2c"
