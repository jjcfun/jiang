#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
LLVM_CONFIG="${LLVM_CONFIG:-llvm-config}"
TIMEOUT_BIN="${TIMEOUT_BIN:-timeout}"

STAGE2_BOOTSTRAP_STAGE2="${STAGE2_BOOTSTRAP_STAGE2:-$BUILD_DIR/stage2c}"
STAGE2_ALLOW_STAGE1_FALLBACK="${STAGE2_ALLOW_STAGE1_FALLBACK:-0}"
STAGE2_BOOTSTRAP_C_PATH="$BUILD_DIR/stage2_compiler.bootstrap.c"
STAGE2_BOOTSTRAP_BIN="$BUILD_DIR/stage2c.bootstrap"
STAGE2_C_PATH="$BUILD_DIR/stage2_compiler.c"

# Bootstrap policy:
# 1. During active development, prefer a previously built local stage2c.
# 2. After releases exist, callers can point STAGE2_BOOTSTRAP_STAGE2 at a
#    stage2c from the previous release.
# 3. Stage1 is now a legacy fallback, not the default main path.

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

bootstrap_stage2_is_usable() {
    local bootstrap_bin="$1"

    if [ ! -x "$bootstrap_bin" ]; then
        return 1
    fi

    if "$TIMEOUT_BIN" 5 "$bootstrap_bin" --help >/dev/null 2>&1; then
        return 0
    fi

    return 1
}

emit_stage2_compiler_c() {
    local bootstrap_bin="$1"
    local output_c="$2"

    if ! "$TIMEOUT_BIN" 60 "$bootstrap_bin" "compiler/entries/compiler.jiang" > "$output_c"; then
        echo "stage2 bootstrap compiler failed or timed out: $bootstrap_bin" >&2
        return 1
    fi
}

if bootstrap_stage2_is_usable "$STAGE2_BOOTSTRAP_STAGE2"; then
    echo "bootstrapping stage2 with existing compiler: $STAGE2_BOOTSTRAP_STAGE2"
    emit_stage2_compiler_c "$STAGE2_BOOTSTRAP_STAGE2" "$STAGE2_C_PATH"
else
    if [ "$STAGE2_ALLOW_STAGE1_FALLBACK" != "1" ]; then
        echo "no usable bootstrap stage2 compiler found: $STAGE2_BOOTSTRAP_STAGE2" >&2
        echo "during development this is usually your last successful build/stage2c; after releases exist it should be the previous release's stage2c" >&2
        echo "provide that compiler via STAGE2_BOOTSTRAP_STAGE2, or set STAGE2_ALLOW_STAGE1_FALLBACK=1 to force the legacy stage1 fallback" >&2
        exit 1
    fi
    echo "bootstrapping stage2 via stage1 fallback"
    "$BUILD_DIR/stage1c" --mode emit-c "compiler/entries/compiler.jiang" > "$STAGE2_BOOTSTRAP_C_PATH"
    link_stage2 "$STAGE2_BOOTSTRAP_C_PATH" "$STAGE2_BOOTSTRAP_BIN"
    emit_stage2_compiler_c "$STAGE2_BOOTSTRAP_BIN" "$STAGE2_C_PATH"
fi

echo "linking final stage2 compiler"
link_stage2 "$STAGE2_C_PATH" "$BUILD_DIR/stage2c"

echo "built $BUILD_DIR/stage2c"
