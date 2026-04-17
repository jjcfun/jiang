#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

source "$PROJECT_ROOT/script/llvm_env.sh"
export_llvm_env

bash "$PROJECT_ROOT/script/build_stage2.sh"

SELFHOST_C="$BUILD_DIR/stage2_selfhost_compiler.c"
SELFHOST_BIN="$BUILD_DIR/stage2c.selfhost"
ROUND2_C="$BUILD_DIR/stage2_selfhost_round2.c"
ROUNDTRIP_BIN="$BUILD_DIR/stage2c.roundtrip"
MINIMAL_C="$BUILD_DIR/stage2_selfhost_minimal.c"

"$BUILD_DIR/stage2c" "compiler/entries/compiler.jiang" > "$SELFHOST_C"

cc -std=c99 -I "$PROJECT_ROOT/include" \
    $LLVM_CFLAGS \
    "$SELFHOST_C" \
    "$PROJECT_ROOT/runtime/stage1_host.c" \
    "$PROJECT_ROOT/compiler/stage2_driver.c" \
    "$PROJECT_ROOT/compiler/ffi/llvm/llvm_shim.c" \
    $LLVM_LDFLAGS \
    $LLVM_LIBS \
    -o "$SELFHOST_BIN"

"$SELFHOST_BIN" "compiler/entries/compiler.jiang" > "$ROUND2_C"
cc -std=c99 -I "$PROJECT_ROOT/include" -c "$ROUND2_C" -o "$BUILD_DIR/stage2_selfhost_round2.o"
cc -std=c99 -I "$PROJECT_ROOT/include" \
    $LLVM_CFLAGS \
    "$ROUND2_C" \
    "$PROJECT_ROOT/runtime/stage1_host.c" \
    "$PROJECT_ROOT/compiler/stage2_driver.c" \
    "$PROJECT_ROOT/compiler/ffi/llvm/llvm_shim.c" \
    $LLVM_LDFLAGS \
    $LLVM_LIBS \
    -o "$ROUNDTRIP_BIN"

"$ROUNDTRIP_BIN" "compiler/tests/samples/minimal.jiang" > "$MINIMAL_C"
cc -std=c99 -I "$PROJECT_ROOT/include" -c "$MINIMAL_C" -o "$BUILD_DIR/stage2_selfhost_minimal.o"

echo "stage2 selfhost smoke passed"
