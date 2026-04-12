#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_PARSE_LOG="$BUILD_DIR/stage2_llvm_invalid_parse.log"
OUT_CALL_LOG="$BUILD_DIR/stage2_llvm_invalid_call.log"
OUT_SLICE_ASSIGN_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_slice_assign_type.log"
OUT_PRIVATE_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_private_type.log"
OUT_PRIVATE_FUNC_LOG="$BUILD_DIR/stage2_llvm_invalid_private_function.log"
OUT_PUBLIC_ALIAS_PRIVATE_FUNC_LOG="$BUILD_DIR/stage2_llvm_invalid_public_alias_private_function.log"
OUT_PUBLIC_ALIAS_PRIVATE_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_public_alias_private_type.log"
OUT_TRANSITIVE_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_transitive_type.log"
OUT_ALIAS_MEMBER_LOG="$BUILD_DIR/stage2_llvm_invalid_alias_member.log"
OUT_DEREF_NON_POINTER_LOG="$BUILD_DIR/stage2_llvm_invalid_deref_non_pointer.log"
OUT_ADDRESS_OF_EXPR_LOG="$BUILD_DIR/stage2_llvm_invalid_address_of_expr.log"
OUT_ARRAY_TO_SLICE_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_array_to_slice_type.log"
OUT_UNKNOWN_IDENT_LOG="$BUILD_DIR/stage2_llvm_invalid_unknown_ident.log"
OUT_CALL_NON_FUNCTION_LOG="$BUILD_DIR/stage2_llvm_invalid_call_non_function.log"
OUT_DUP_ENUM_MEMBER_LOG="$BUILD_DIR/stage2_llvm_invalid_duplicate_enum_member.log"
OUT_ENUM_VALUE_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_enum_value_type.log"
OUT_DUP_IMPORT_ALIAS_LOG="$BUILD_DIR/stage2_llvm_invalid_duplicate_import_alias.log"
OUT_TYPE_FUNC_CONFLICT_LOG="$BUILD_DIR/stage2_llvm_invalid_type_function_name_conflict.log"
OUT_ENUM_TYPE_CONFLICT_LOG="$BUILD_DIR/stage2_llvm_invalid_enum_type_name_conflict.log"
OUT_ALIAS_FUNC_CONFLICT_LOG="$BUILD_DIR/stage2_llvm_invalid_import_alias_function_conflict.log"
OUT_ALIAS_TYPE_CONFLICT_LOG="$BUILD_DIR/stage2_llvm_invalid_import_alias_type_conflict.log"
OUT_IMPORT_CYCLE_LOG="$BUILD_DIR/stage2_llvm_invalid_import_cycle.log"
OUT_INDEX_TARGET_LOG="$BUILD_DIR/stage2_llvm_invalid_index_target.log"
OUT_INDEX_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_index_type.log"
OUT_ARRAY_ARG_LENGTH_LOG="$BUILD_DIR/stage2_llvm_invalid_array_arg_length.log"
OUT_ARRAY_ASSIGN_LENGTH_LOG="$BUILD_DIR/stage2_llvm_invalid_array_assign_length.log"
OUT_ARRAY_RETURN_LENGTH_LOG="$BUILD_DIR/stage2_llvm_invalid_array_return_length.log"
OUT_BREAK_OUTSIDE_LOOP_LOG="$BUILD_DIR/stage2_llvm_invalid_break_outside_loop.log"
OUT_CONTINUE_OUTSIDE_LOOP_LOG="$BUILD_DIR/stage2_llvm_invalid_continue_outside_loop.log"
OUT_FOR_LOOP_VAR_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_for_loop_var_type.log"
OUT_GLOBAL_INIT_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_global_initializer_type.log"

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

if [[ "$(<"$OUT_PRIVATE_FUNC_LOG")" != *"unknown symbol"* ]]; then
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
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_public_alias_private_function.jiang" > "$OUT_PUBLIC_ALIAS_PRIVATE_FUNC_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_public_alias_private_function to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_PUBLIC_ALIAS_PRIVATE_FUNC_LOG")" != *"public alias target must be public"* ]]; then
    echo "stage2 llvm error smoke missing public alias private function diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_PUBLIC_ALIAS_PRIVATE_FUNC_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_public_alias_private_function" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_public_alias_private_type.jiang" > "$OUT_PUBLIC_ALIAS_PRIVATE_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_public_alias_private_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_PUBLIC_ALIAS_PRIVATE_TYPE_LOG")" != *"public alias target must be public"* ]]; then
    echo "stage2 llvm error smoke missing public alias private type diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_PUBLIC_ALIAS_PRIVATE_TYPE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_public_alias_private_type" >&2
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

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_array_to_slice_type.jiang" > "$OUT_ARRAY_TO_SLICE_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_array_to_slice_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ARRAY_TO_SLICE_TYPE_LOG")" != *"local initializer type mismatch"* ]]; then
    echo "stage2 llvm error smoke missing array-to-slice type mismatch diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_ARRAY_TO_SLICE_TYPE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_array_to_slice_type" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_unknown_ident.jiang" > "$OUT_UNKNOWN_IDENT_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_unknown_ident to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_UNKNOWN_IDENT_LOG")" != *"unknown symbol"* ]]; then
    echo "stage2 llvm error smoke missing unknown symbol diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_UNKNOWN_IDENT_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_unknown_ident" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_call_non_function.jiang" > "$OUT_CALL_NON_FUNCTION_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_call_non_function to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_CALL_NON_FUNCTION_LOG")" != *"call target must be a function"* ]]; then
    echo "stage2 llvm error smoke missing non-function call diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_CALL_NON_FUNCTION_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_call_non_function" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_duplicate_enum_member.jiang" > "$OUT_DUP_ENUM_MEMBER_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_duplicate_enum_member to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_DUP_ENUM_MEMBER_LOG")" != *"duplicate enum member"* ]]; then
    echo "stage2 llvm error smoke missing duplicate enum member diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_DUP_ENUM_MEMBER_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_duplicate_enum_member" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_enum_value_type.jiang" > "$OUT_ENUM_VALUE_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_enum_value_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ENUM_VALUE_TYPE_LOG")" != *"enum value must be Int"* ]]; then
    echo "stage2 llvm error smoke missing enum value type diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_ENUM_VALUE_TYPE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_enum_value_type" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_duplicate_import_alias.jiang" > "$OUT_DUP_IMPORT_ALIAS_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_duplicate_import_alias to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_DUP_IMPORT_ALIAS_LOG")" != *"duplicate import alias"* ]]; then
    echo "stage2 llvm error smoke missing duplicate import alias diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_DUP_IMPORT_ALIAS_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_duplicate_import_alias" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_type_function_name_conflict.jiang" > "$OUT_TYPE_FUNC_CONFLICT_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_type_function_name_conflict to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_TYPE_FUNC_CONFLICT_LOG")" != *"duplicate function"* ]]; then
    echo "stage2 llvm error smoke missing type/function name conflict diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_TYPE_FUNC_CONFLICT_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_type_function_name_conflict" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_enum_type_name_conflict.jiang" > "$OUT_ENUM_TYPE_CONFLICT_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_enum_type_name_conflict to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ENUM_TYPE_CONFLICT_LOG")" != *"duplicate enum"* ]]; then
    echo "stage2 llvm error smoke missing enum/type name conflict diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_ENUM_TYPE_CONFLICT_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_enum_type_name_conflict" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_import_alias_function_conflict.jiang" > "$OUT_ALIAS_FUNC_CONFLICT_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_import_alias_function_conflict to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ALIAS_FUNC_CONFLICT_LOG")" != *"duplicate function"* ]]; then
    echo "stage2 llvm error smoke missing import alias/function conflict diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_ALIAS_FUNC_CONFLICT_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_import_alias_function_conflict" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_import_alias_type_conflict.jiang" > "$OUT_ALIAS_TYPE_CONFLICT_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_import_alias_type_conflict to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ALIAS_TYPE_CONFLICT_LOG")" != *"duplicate type"* ]]; then
    echo "stage2 llvm error smoke missing import alias/type conflict diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_ALIAS_TYPE_CONFLICT_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_import_alias_type_conflict" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_import_cycle_a.jiang" > "$OUT_IMPORT_CYCLE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_import_cycle_a to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_IMPORT_CYCLE_LOG")" != *"import cycle"* ]]; then
    echo "stage2 llvm error smoke missing import cycle diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_IMPORT_CYCLE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_import_cycle_a" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_index_target.jiang" > "$OUT_INDEX_TARGET_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_index_target to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_INDEX_TARGET_LOG")" != *"index target must be array or slice"* ]]; then
    echo "stage2 llvm error smoke missing index target diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_INDEX_TARGET_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_index_target" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_index_type.jiang" > "$OUT_INDEX_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_index_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_INDEX_TYPE_LOG")" != *"index requires Int"* ]]; then
    echo "stage2 llvm error smoke missing index type diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_INDEX_TYPE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_index_type" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_array_arg_length.jiang" > "$OUT_ARRAY_ARG_LENGTH_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_array_arg_length to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ARRAY_ARG_LENGTH_LOG")" != *"array literal length mismatch"* ]]; then
    echo "stage2 llvm error smoke missing array arg length diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_ARRAY_ARG_LENGTH_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_array_arg_length" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_array_assign_length.jiang" > "$OUT_ARRAY_ASSIGN_LENGTH_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_array_assign_length to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ARRAY_ASSIGN_LENGTH_LOG")" != *"array literal length mismatch"* ]]; then
    echo "stage2 llvm error smoke missing array assign length diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_ARRAY_ASSIGN_LENGTH_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_array_assign_length" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_array_return_length.jiang" > "$OUT_ARRAY_RETURN_LENGTH_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_array_return_length to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ARRAY_RETURN_LENGTH_LOG")" != *"array literal length mismatch"* ]]; then
    echo "stage2 llvm error smoke missing array return length diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_ARRAY_RETURN_LENGTH_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_array_return_length" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_break_outside_loop.jiang" > "$OUT_BREAK_OUTSIDE_LOOP_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_break_outside_loop to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_BREAK_OUTSIDE_LOOP_LOG")" != *"break outside loop"* ]]; then
    echo "stage2 llvm error smoke missing break outside loop diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_BREAK_OUTSIDE_LOOP_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_break_outside_loop" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_continue_outside_loop.jiang" > "$OUT_CONTINUE_OUTSIDE_LOOP_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_continue_outside_loop to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_CONTINUE_OUTSIDE_LOOP_LOG")" != *"continue outside loop"* ]]; then
    echo "stage2 llvm error smoke missing continue outside loop diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_CONTINUE_OUTSIDE_LOOP_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_continue_outside_loop" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_for_loop_var_type.jiang" > "$OUT_FOR_LOOP_VAR_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_for_loop_var_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_FOR_LOOP_VAR_TYPE_LOG")" != *"for loop variable type must be Int"* ]]; then
    echo "stage2 llvm error smoke missing for loop variable type diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_FOR_LOOP_VAR_TYPE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_for_loop_var_type" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_global_initializer_type.jiang" > "$OUT_GLOBAL_INIT_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_global_initializer_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_GLOBAL_INIT_TYPE_LOG")" != *"global initializer type mismatch"* ]]; then
    echo "stage2 llvm error smoke missing global initializer type mismatch diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_GLOBAL_INIT_TYPE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_global_initializer_type" >&2
    exit 1
fi

echo "stage2 llvm error smoke passed"
