#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
LLVM_CONFIG="${LLVM_CONFIG:-llvm-config}"

STAGE2_SEED_C_PATH="${STAGE2_SEED_C:-$BUILD_DIR/stage2_seed.c}"
STAGE2_SEED_BIN="$BUILD_DIR/stage2c.seed"
STAGE2_BOOTSTRAP_C_PATH="$BUILD_DIR/stage2_compiler.bootstrap.c"
STAGE2_BOOTSTRAP_BIN="$BUILD_DIR/stage2c.bootstrap"
STAGE2_C_PATH="$BUILD_DIR/stage2_compiler.c"

cd "$PROJECT_ROOT"

LLVM_CFLAGS="$($LLVM_CONFIG --cflags)"
LLVM_LDFLAGS="$($LLVM_CONFIG --ldflags)"
LLVM_LIBS="$($LLVM_CONFIG --libs core analysis --system-libs)"

link_stage2() {
    local input_c="$1"
    local output_bin="$2"

    cc -std=c99 -I "$PROJECT_ROOT/include" \
        $LLVM_CFLAGS \
        "$input_c" \
        "$PROJECT_ROOT/runtime/stage1_host.c" \
        "$PROJECT_ROOT/compiler/stage2_driver.c" \
        "$PROJECT_ROOT/compiler/ffi/llvm/llvm_shim.c" \
        $LLVM_LDFLAGS \
        $LLVM_LIBS \
        -o "$output_bin"
}

if [ -f "$STAGE2_SEED_C_PATH" ]; then
    link_stage2 "$STAGE2_SEED_C_PATH" "$STAGE2_SEED_BIN"
    "$STAGE2_SEED_BIN" "compiler/entries/compiler.jiang" > "$STAGE2_C_PATH"
else
    "$BUILD_DIR/stage1c" --mode emit-c "compiler/entries/compiler.jiang" > "$STAGE2_BOOTSTRAP_C_PATH"
    link_stage2 "$STAGE2_BOOTSTRAP_C_PATH" "$STAGE2_BOOTSTRAP_BIN"
    "$STAGE2_BOOTSTRAP_BIN" "compiler/entries/compiler.jiang" > "$STAGE2_C_PATH"
fi

link_stage2 "$STAGE2_C_PATH" "$BUILD_DIR/stage2c"

echo "built $BUILD_DIR/stage2c"
