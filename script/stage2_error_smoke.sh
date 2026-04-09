#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_LOG="$BUILD_DIR/stage2_invalid.log"
OUT_C="$BUILD_DIR/stage2_invalid.c"
OUT_CALL_LOG="$BUILD_DIR/stage2_invalid_call.log"
OUT_FIELD_LOG="$BUILD_DIR/stage2_invalid_field.log"
OUT_MISSING_FIELD_LOG="$BUILD_DIR/stage2_invalid_missing_field.log"
OUT_DUP_FIELD_LOG="$BUILD_DIR/stage2_invalid_duplicate_field.log"
OUT_ASSIGN_TARGET_LOG="$BUILD_DIR/stage2_invalid_assign_target.log"
OUT_ASSIGN_FIELD_TYPE_LOG="$BUILD_DIR/stage2_invalid_assign_field_type.log"
OUT_DUP_FUNC_LOG="$BUILD_DIR/stage2_invalid_duplicate_function.log"
OUT_DUP_FIELD_DECL_LOG="$BUILD_DIR/stage2_invalid_duplicate_field_decl.log"

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

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_struct_missing_field.jiang" > "$OUT_MISSING_FIELD_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_struct_missing_field to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_MISSING_FIELD_LOG")" != *"missing field"* ]]; then
    echo "stage2 error smoke missing missing field diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_struct_duplicate_field.jiang" > "$OUT_DUP_FIELD_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_struct_duplicate_field to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_DUP_FIELD_LOG")" != *"duplicate field"* ]]; then
    echo "stage2 error smoke missing duplicate field diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_assign_target.jiang" > "$OUT_ASSIGN_TARGET_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_assign_target to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ASSIGN_TARGET_LOG")" != *"assignment target is not assignable"* ]]; then
    echo "stage2 error smoke missing assignment target diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_assign_field_type.jiang" > "$OUT_ASSIGN_FIELD_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_assign_field_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ASSIGN_FIELD_TYPE_LOG")" != *"assignment type mismatch"* ]]; then
    echo "stage2 error smoke missing assignment type mismatch diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_duplicate_function.jiang" > "$OUT_DUP_FUNC_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_duplicate_function to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_DUP_FUNC_LOG")" != *"duplicate function"* ]]; then
    echo "stage2 error smoke missing duplicate function diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_duplicate_field_decl.jiang" > "$OUT_DUP_FIELD_DECL_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_duplicate_field_decl to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_DUP_FIELD_DECL_LOG")" != *"duplicate field"* ]]; then
    echo "stage2 error smoke missing duplicate field declaration diagnostic" >&2
    exit 1
fi

echo "stage2 error smoke passed"
