#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
COMPILER="$BUILD_DIR/jiangc"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make

cd "$PROJECT_ROOT"

check_expected_failure() {
    local input="$1"
    local output_bin="$2"

    rm -f "$output_bin"
    if ! "$COMPILER" --stdlib-dir "$PROJECT_ROOT/std" "$input" >/dev/null 2>"$BUILD_DIR/semantic_err.log"; then
        echo "compiler semantic smoke compile failed unexpectedly: $input" >&2
        cat "$BUILD_DIR/semantic_err.log" >&2
        exit 1
    fi

    if [[ ! -f "$output_bin" ]]; then
        echo "compiler semantic smoke missing output binary: $output_bin" >&2
        exit 1
    fi

    if "$output_bin" >/dev/null 2>"$BUILD_DIR/semantic_run_err.log"; then
        echo "compiler semantic smoke expected runtime semantic failure: $input" >&2
        exit 1
    fi
}

check_expected_failure "$PROJECT_ROOT/compiler/semantic_invalid_dup_struct.jiang" "$BUILD_DIR/semantic_invalid_dup_struct"
check_expected_failure "$PROJECT_ROOT/compiler/semantic_invalid_dup_field.jiang" "$BUILD_DIR/semantic_invalid_dup_field"
check_expected_failure "$PROJECT_ROOT/compiler/semantic_invalid_type.jiang" "$BUILD_DIR/semantic_invalid_type"

echo "compiler semantic smoke passed"
