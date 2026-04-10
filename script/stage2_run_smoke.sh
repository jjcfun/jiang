#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_DIR="$BUILD_DIR/stage2_run_smoke"

mkdir -p "$OUT_DIR"
cd "$PROJECT_ROOT"

bash "$PROJECT_ROOT/script/build_stage2.sh"

compile_stage2_entry() {
    local source_path="$1"
    local stem="$2"
    local out_c="$OUT_DIR/${stem}.c"
    local out_bin="$OUT_DIR/${stem}"

    "$BUILD_DIR/stage2c" "$source_path" > "$out_c"
    cc -std=c99 -I "$PROJECT_ROOT/include" "$out_c" "$PROJECT_ROOT/runtime/stage1_host.c" -o "$out_bin"
}

compile_stage2_entry "compiler/tests/samples/minimal.jiang" "minimal"
set +e
"$OUT_DIR/minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/multi_file_minimal.jiang" "multi_file_minimal"
set +e
"$OUT_DIR/multi_file_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected multi_file_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/while_minimal.jiang" "while_minimal"
set +e
"$OUT_DIR/while_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 10 ]]; then
    echo "stage2 run smoke expected while_minimal exit code 10, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/call_result_field_minimal.jiang" "call_result_field_minimal"
set +e
"$OUT_DIR/call_result_field_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected call_result_field_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

echo "stage2 run smoke passed"
