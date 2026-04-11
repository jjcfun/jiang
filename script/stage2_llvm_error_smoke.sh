#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_PARSE_LOG="$BUILD_DIR/stage2_llvm_invalid_parse.log"
OUT_CALL_LOG="$BUILD_DIR/stage2_llvm_invalid_call.log"
OUT_SLICE_ASSIGN_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_slice_assign_type.log"
OUT_PRIVATE_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_private_type.log"
OUT_PRIVATE_FUNC_LOG="$BUILD_DIR/stage2_llvm_invalid_private_function.log"
OUT_TRANSITIVE_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_transitive_type.log"
OUT_ALIAS_MEMBER_LOG="$BUILD_DIR/stage2_llvm_invalid_alias_member.log"
OUT_DEREF_NON_POINTER_LOG="$BUILD_DIR/stage2_llvm_invalid_deref_non_pointer.log"
OUT_ADDRESS_OF_EXPR_LOG="$BUILD_DIR/stage2_llvm_invalid_address_of_expr.log"

bash "$PROJECT_ROOT/script/build_stage2.sh"

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_missing_semicolon.jiang" > "$OUT_PARSE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_missing_semicolon to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_PARSE_LOG")" != *"error:"* ]]; then
    echo "stage2 llvm error smoke missing parse diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_PARSE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_missing_semicolon" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_call_arg.jiang" > "$OUT_CALL_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_call_arg to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_CALL_LOG")" != *"argument type mismatch"* ]]; then
    echo "stage2 llvm error smoke missing argument type mismatch diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_CALL_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_call_arg" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_slice_assign_type.jiang" > "$OUT_SLICE_ASSIGN_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_slice_assign_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_SLICE_ASSIGN_TYPE_LOG")" != *"assignment type mismatch"* ]]; then
    echo "stage2 llvm error smoke missing slice assignment type mismatch diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_SLICE_ASSIGN_TYPE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_slice_assign_type" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_import_private_function.jiang" > "$OUT_PRIVATE_FUNC_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_import_private_function to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_PRIVATE_FUNC_LOG")" != *"call target must be a function"* ]]; then
    echo "stage2 llvm error smoke missing private function diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_PRIVATE_FUNC_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_import_private_function" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_import_private_type.jiang" > "$OUT_PRIVATE_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_import_private_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_PRIVATE_TYPE_LOG")" != *"unknown type"* ]]; then
    echo "stage2 llvm error smoke missing private type diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_PRIVATE_TYPE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_import_private_type" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_transitive_import_type.jiang" > "$OUT_TRANSITIVE_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_transitive_import_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_TRANSITIVE_TYPE_LOG")" != *"unknown type"* ]]; then
    echo "stage2 llvm error smoke missing transitive import type diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_TRANSITIVE_TYPE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_transitive_import_type" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_import_alias_missing_member.jiang" > "$OUT_ALIAS_MEMBER_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_import_alias_missing_member to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ALIAS_MEMBER_LOG")" != *"unknown member"* ]]; then
    echo "stage2 llvm error smoke missing alias member diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_ALIAS_MEMBER_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_import_alias_missing_member" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_deref_non_pointer.jiang" > "$OUT_DEREF_NON_POINTER_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_deref_non_pointer to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_DEREF_NON_POINTER_LOG")" != *"dereference requires pointer"* ]]; then
    echo "stage2 llvm error smoke missing dereference diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_DEREF_NON_POINTER_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_deref_non_pointer" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_address_of_expr.jiang" > "$OUT_ADDRESS_OF_EXPR_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_address_of_expr to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ADDRESS_OF_EXPR_LOG")" != *"address-of requires assignable target"* ]]; then
    echo "stage2 llvm error smoke missing address-of diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_ADDRESS_OF_EXPR_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_address_of_expr" >&2
    exit 1
fi

echo "stage2 llvm error smoke passed"
