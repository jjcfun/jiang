#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_LOG="$BUILD_DIR/stage2_invalid.log"
OUT_C="$BUILD_DIR/stage2_invalid.c"
OUT_CALL_LOG="$BUILD_DIR/stage2_invalid_call.log"
OUT_FIELD_LOG="$BUILD_DIR/stage2_invalid_field.log"

bash "$PROJECT_ROOT/script/build_stage2.sh"

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_missing_semicolon.jiang" > "$OUT_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected non-zero exit" >&2
    exit 1
fi

if [[ "$(<"$OUT_LOG")" != *"error:"* ]]; then
    echo "stage2 error smoke missing diagnostic output" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_missing_semicolon.jiang" > "$OUT_C"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected non-zero exit on redirected output" >&2
    exit 1
fi

if rg -q '^#include <stdint.h>$' "$OUT_C"; then
    echo "stage2 error smoke unexpectedly produced C output" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_call_arg.jiang" > "$OUT_CALL_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_call_arg to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_CALL_LOG")" != *"argument type mismatch"* ]]; then
    echo "stage2 error smoke missing argument type mismatch diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_struct_field.jiang" > "$OUT_FIELD_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_struct_field to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_FIELD_LOG")" != *"unknown field"* ]]; then
    echo "stage2 error smoke missing unknown field diagnostic" >&2
    exit 1
fi

echo "stage2 error smoke passed"
