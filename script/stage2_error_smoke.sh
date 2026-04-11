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
OUT_SLICE_ASSIGN_TYPE_LOG="$BUILD_DIR/stage2_invalid_slice_assign_type.log"
OUT_ARRAY_LENGTH_LOG="$BUILD_DIR/stage2_invalid_array_length.log"
OUT_DUP_FUNC_LOG="$BUILD_DIR/stage2_invalid_duplicate_function.log"
OUT_DUP_FIELD_DECL_LOG="$BUILD_DIR/stage2_invalid_duplicate_field_decl.log"
OUT_DUP_PARAM_LOG="$BUILD_DIR/stage2_invalid_duplicate_param.log"
OUT_DUP_LOCAL_LOG="$BUILD_DIR/stage2_invalid_duplicate_local.log"
OUT_IMPORT_DUP_FUNC_LOG="$BUILD_DIR/stage2_invalid_import_duplicate_function.log"
OUT_DUP_TYPE_LOG="$BUILD_DIR/stage2_invalid_duplicate_type.log"
OUT_DUP_ENUM_LOG="$BUILD_DIR/stage2_invalid_duplicate_enum.log"
OUT_DUP_ENUM_MEMBER_LOG="$BUILD_DIR/stage2_invalid_duplicate_enum_member.log"
OUT_DUP_IMPORT_ALIAS_LOG="$BUILD_DIR/stage2_invalid_duplicate_import_alias.log"
OUT_TYPE_FUNC_CONFLICT_LOG="$BUILD_DIR/stage2_invalid_type_function_name_conflict.log"
OUT_ENUM_TYPE_CONFLICT_LOG="$BUILD_DIR/stage2_invalid_enum_type_name_conflict.log"
OUT_ALIAS_FUNC_CONFLICT_LOG="$BUILD_DIR/stage2_invalid_import_alias_function_conflict.log"
OUT_ALIAS_TYPE_CONFLICT_LOG="$BUILD_DIR/stage2_invalid_import_alias_type_conflict.log"
OUT_TRANSITIVE_TYPE_LOG="$BUILD_DIR/stage2_invalid_transitive_import_type.log"
OUT_PRIVATE_FUNC_LOG="$BUILD_DIR/stage2_invalid_import_private_function.log"
OUT_PRIVATE_TYPE_LOG="$BUILD_DIR/stage2_invalid_import_private_type.log"
OUT_ALIAS_MEMBER_LOG="$BUILD_DIR/stage2_invalid_import_alias_missing_member.log"
OUT_DEREF_NON_POINTER_LOG="$BUILD_DIR/stage2_invalid_deref_non_pointer.log"
OUT_ADDRESS_OF_EXPR_LOG="$BUILD_DIR/stage2_invalid_address_of_expr.log"
OUT_ARRAY_TO_SLICE_TYPE_LOG="$BUILD_DIR/stage2_invalid_array_to_slice_type.log"
OUT_UNKNOWN_IDENT_LOG="$BUILD_DIR/stage2_invalid_unknown_ident.log"
OUT_CALL_NON_FUNCTION_LOG="$BUILD_DIR/stage2_invalid_call_non_function.log"
OUT_INDEX_TARGET_LOG="$BUILD_DIR/stage2_invalid_index_target.log"
OUT_INDEX_TYPE_LOG="$BUILD_DIR/stage2_invalid_index_type.log"
OUT_ARRAY_ARG_LENGTH_LOG="$BUILD_DIR/stage2_invalid_array_arg_length.log"
OUT_ARRAY_ASSIGN_LENGTH_LOG="$BUILD_DIR/stage2_invalid_array_assign_length.log"
OUT_ARRAY_RETURN_LENGTH_LOG="$BUILD_DIR/stage2_invalid_array_return_length.log"

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
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_slice_assign_type.jiang" > "$OUT_SLICE_ASSIGN_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_slice_assign_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_SLICE_ASSIGN_TYPE_LOG")" != *"assignment type mismatch"* ]]; then
    echo "stage2 error smoke missing slice assignment type mismatch diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_array_length.jiang" > "$OUT_ARRAY_LENGTH_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_array_length to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ARRAY_LENGTH_LOG")" != *"array literal length mismatch"* ]]; then
    echo "stage2 error smoke missing array length mismatch diagnostic" >&2
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

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_duplicate_param.jiang" > "$OUT_DUP_PARAM_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_duplicate_param to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_DUP_PARAM_LOG")" != *"duplicate parameter"* ]]; then
    echo "stage2 error smoke missing duplicate parameter diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_duplicate_local.jiang" > "$OUT_DUP_LOCAL_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_duplicate_local to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_DUP_LOCAL_LOG")" != *"duplicate local"* ]]; then
    echo "stage2 error smoke missing duplicate local diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_import_duplicate_function.jiang" > "$OUT_IMPORT_DUP_FUNC_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_import_duplicate_function to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_IMPORT_DUP_FUNC_LOG")" != *"duplicate function"* ]]; then
    echo "stage2 error smoke missing imported duplicate function diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_duplicate_type.jiang" > "$OUT_DUP_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_duplicate_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_DUP_TYPE_LOG")" != *"duplicate type"* ]]; then
    echo "stage2 error smoke missing duplicate type diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_duplicate_enum.jiang" > "$OUT_DUP_ENUM_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_duplicate_enum to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_DUP_ENUM_LOG")" != *"duplicate enum"* ]]; then
    echo "stage2 error smoke missing duplicate enum diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_duplicate_enum_member.jiang" > "$OUT_DUP_ENUM_MEMBER_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_duplicate_enum_member to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_DUP_ENUM_MEMBER_LOG")" != *"duplicate enum member"* ]]; then
    echo "stage2 error smoke missing duplicate enum member diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_duplicate_import_alias.jiang" > "$OUT_DUP_IMPORT_ALIAS_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_duplicate_import_alias to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_DUP_IMPORT_ALIAS_LOG")" != *"duplicate import alias"* ]]; then
    echo "stage2 error smoke missing duplicate import alias diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_type_function_name_conflict.jiang" > "$OUT_TYPE_FUNC_CONFLICT_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_type_function_name_conflict to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_TYPE_FUNC_CONFLICT_LOG")" != *"duplicate function"* ]]; then
    echo "stage2 error smoke missing type/function name conflict diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_enum_type_name_conflict.jiang" > "$OUT_ENUM_TYPE_CONFLICT_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_enum_type_name_conflict to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ENUM_TYPE_CONFLICT_LOG")" != *"duplicate enum"* ]]; then
    echo "stage2 error smoke missing enum/type name conflict diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_import_alias_function_conflict.jiang" > "$OUT_ALIAS_FUNC_CONFLICT_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_import_alias_function_conflict to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ALIAS_FUNC_CONFLICT_LOG")" != *"duplicate function"* ]]; then
    echo "stage2 error smoke missing import alias/function conflict diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_import_alias_type_conflict.jiang" > "$OUT_ALIAS_TYPE_CONFLICT_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_import_alias_type_conflict to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ALIAS_TYPE_CONFLICT_LOG")" != *"duplicate type"* ]]; then
    echo "stage2 error smoke missing import alias/type conflict diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_transitive_import_type.jiang" > "$OUT_TRANSITIVE_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_transitive_import_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_TRANSITIVE_TYPE_LOG")" != *"unknown type"* ]]; then
    echo "stage2 error smoke missing unknown type diagnostic for transitive import" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_import_private_function.jiang" > "$OUT_PRIVATE_FUNC_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_import_private_function to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_PRIVATE_FUNC_LOG")" != *"unknown symbol"* ]]; then
    echo "stage2 error smoke missing private function visibility diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_import_private_type.jiang" > "$OUT_PRIVATE_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_import_private_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_PRIVATE_TYPE_LOG")" != *"unknown type"* ]]; then
    echo "stage2 error smoke missing private type visibility diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_import_alias_missing_member.jiang" > "$OUT_ALIAS_MEMBER_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_import_alias_missing_member to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ALIAS_MEMBER_LOG")" != *"unknown member"* ]]; then
    echo "stage2 error smoke missing import alias missing member diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_deref_non_pointer.jiang" > "$OUT_DEREF_NON_POINTER_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_deref_non_pointer to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_DEREF_NON_POINTER_LOG")" != *"dereference requires pointer"* ]]; then
    echo "stage2 error smoke missing dereference diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_address_of_expr.jiang" > "$OUT_ADDRESS_OF_EXPR_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_address_of_expr to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ADDRESS_OF_EXPR_LOG")" != *"address-of requires assignable target"* ]]; then
    echo "stage2 error smoke missing address-of diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_array_to_slice_type.jiang" > "$OUT_ARRAY_TO_SLICE_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_array_to_slice_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ARRAY_TO_SLICE_TYPE_LOG")" != *"local initializer type mismatch"* ]]; then
    echo "stage2 error smoke missing array-to-slice type mismatch diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_unknown_ident.jiang" > "$OUT_UNKNOWN_IDENT_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_unknown_ident to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_UNKNOWN_IDENT_LOG")" != *"unknown symbol"* ]]; then
    echo "stage2 error smoke missing unknown symbol diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_call_non_function.jiang" > "$OUT_CALL_NON_FUNCTION_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_call_non_function to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_CALL_NON_FUNCTION_LOG")" != *"call target must be a function"* ]]; then
    echo "stage2 error smoke missing non-function call diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_index_target.jiang" > "$OUT_INDEX_TARGET_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_index_target to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_INDEX_TARGET_LOG")" != *"index target must be array or slice"* ]]; then
    echo "stage2 error smoke missing index target diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_index_type.jiang" > "$OUT_INDEX_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_index_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_INDEX_TYPE_LOG")" != *"index requires Int"* ]]; then
    echo "stage2 error smoke missing index type diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_array_arg_length.jiang" > "$OUT_ARRAY_ARG_LENGTH_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_array_arg_length to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ARRAY_ARG_LENGTH_LOG")" != *"array literal length mismatch"* ]]; then
    echo "stage2 error smoke missing array arg length diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_array_assign_length.jiang" > "$OUT_ARRAY_ASSIGN_LENGTH_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_array_assign_length to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ARRAY_ASSIGN_LENGTH_LOG")" != *"array literal length mismatch"* ]]; then
    echo "stage2 error smoke missing array assign length diagnostic" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_array_return_length.jiang" > "$OUT_ARRAY_RETURN_LENGTH_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected invalid_array_return_length to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_ARRAY_RETURN_LENGTH_LOG")" != *"array literal length mismatch"* ]]; then
    echo "stage2 error smoke missing array return length diagnostic" >&2
    exit 1
fi

echo "stage2 error smoke passed"
