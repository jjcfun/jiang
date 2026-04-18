#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
DIST_ROOT="$PROJECT_ROOT/dist"
TIMEOUT_BIN="${TIMEOUT_BIN:-timeout}"
OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"
TARGET="${OS}-${ARCH}"

STAGE2_BOOTSTRAP_STAGE2="${STAGE2_BOOTSTRAP_STAGE2:-}"
STAGE2_INSTALLED_JIANG="${STAGE2_INSTALLED_JIANG:-$HOME/.jiang/bin/jiang}"
STAGE2_DIST_ARCHIVE="${STAGE2_DIST_ARCHIVE:-}"
STAGE2_C_PATH="$BUILD_DIR/stage2_compiler.c"
STAGE2_DIST_EXTRACT_ROOT="$BUILD_DIR/stage2_dist_bootstrap"

# Bootstrap policy:
# 1. If STAGE2_BOOTSTRAP_STAGE2 is set, use it directly.
# 2. Otherwise prefer the newest local dist archive for this target.
# 3. Then fall back to ~/.jiang/bin/jiang if installed.
# 4. Then fall back to build/stage2c from local development.
# 5. Stage1 is not part of the default path; use build_stage2_legacy.sh only for recovery.

cd "$PROJECT_ROOT"

source "$PROJECT_ROOT/script/llvm_env.sh"
export_llvm_env

link_stage2() {
    local input_c="$1"
    local output_bin="$2"

    cc -std=c99 -I "$PROJECT_ROOT/include" \
        $LLVM_CFLAGS \
        "$input_c" \
        "$PROJECT_ROOT/runtime/host_runtime.c" \
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
    return 0
}

find_latest_dist_archive() {
    if [ -n "$STAGE2_DIST_ARCHIVE" ]; then
        if [ -f "$STAGE2_DIST_ARCHIVE" ]; then
            echo "$STAGE2_DIST_ARCHIVE"
            return 0
        fi
        return 1
    fi

    local archives=()
    shopt -s nullglob
    archives=("$DIST_ROOT"/v*/"jiang-v"*-"$TARGET".tar.xz)
    shopt -u nullglob

    if [ "${#archives[@]}" -eq 0 ]; then
        return 1
    fi

    printf '%s\n' "${archives[@]}" | LC_ALL=C sort -V | tail -n 1
}

prepare_dist_bootstrap() {
    local archive="$1"
    local archive_base
    local bootstrap_bin

    archive_base="$(basename "$archive" .tar.xz)"
    bootstrap_bin="$STAGE2_DIST_EXTRACT_ROOT/$archive_base/bin/jiang"

    rm -rf "$STAGE2_DIST_EXTRACT_ROOT"
    mkdir -p "$STAGE2_DIST_EXTRACT_ROOT"
    tar -C "$STAGE2_DIST_EXTRACT_ROOT" -xJf "$archive"

    if [ ! -x "$bootstrap_bin" ]; then
        echo "dist bootstrap archive missing executable: $archive" >&2
        exit 1
    fi

    echo "$bootstrap_bin"
}

resolve_bootstrap_stage2() {
    local archive

    if [ -n "$STAGE2_BOOTSTRAP_STAGE2" ]; then
        echo "$STAGE2_BOOTSTRAP_STAGE2"
        return 0
    fi

    if archive="$(find_latest_dist_archive)"; then
        prepare_dist_bootstrap "$archive"
        return 0
    fi

    if [ -x "$STAGE2_INSTALLED_JIANG" ]; then
        echo "$STAGE2_INSTALLED_JIANG"
        return 0
    fi

    echo "$BUILD_DIR/stage2c"
}

emit_stage2_compiler_c() {
    local bootstrap_bin="$1"
    local output_c="$2"

    if ! "$TIMEOUT_BIN" 60 "$bootstrap_bin" "compiler/entries/compiler.jiang" > "$output_c"; then
        echo "stage2 bootstrap compiler failed or timed out: $bootstrap_bin" >&2
        return 1
    fi
}

RESOLVED_STAGE2_BOOTSTRAP="$(resolve_bootstrap_stage2)"

if bootstrap_stage2_is_usable "$RESOLVED_STAGE2_BOOTSTRAP"; then
    echo "bootstrapping stage2 with existing compiler: $RESOLVED_STAGE2_BOOTSTRAP"
    emit_stage2_compiler_c "$RESOLVED_STAGE2_BOOTSTRAP" "$STAGE2_C_PATH"
else
    echo "no usable bootstrap stage2 compiler found: $RESOLVED_STAGE2_BOOTSTRAP" >&2
    echo "auto resolution order is: STAGE2_BOOTSTRAP_STAGE2 -> local dist archive -> ~/.jiang/bin/jiang -> build/stage2c" >&2
    echo "provide a working Stage2 compiler via STAGE2_BOOTSTRAP_STAGE2, install a release jiang under ~/.jiang/bin/jiang, or use script/build_stage2_legacy.sh for the explicit Stage1 recovery path" >&2
    exit 1
fi

echo "linking final stage2 compiler"
link_stage2 "$STAGE2_C_PATH" "$BUILD_DIR/stage2c"

echo "built $BUILD_DIR/stage2c"
