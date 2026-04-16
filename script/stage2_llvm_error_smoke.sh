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
OUT_FOR_ITERABLE_TARGET_LOG="$BUILD_DIR/stage2_llvm_invalid_for_iterable_target.log"
OUT_SWITCH_CASE_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_switch_case_type.log"
OUT_SWITCH_DUPLICATE_CASE_LOG="$BUILD_DIR/stage2_llvm_invalid_switch_duplicate_case.log"
OUT_SWITCH_NON_EXHAUSTIVE_ENUM_LOG="$BUILD_DIR/stage2_llvm_invalid_switch_non_exhaustive_enum.log"
OUT_GLOBAL_INIT_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_global_initializer_type.log"
OUT_INFER_GLOBAL_MISSING_INIT_LOG="$BUILD_DIR/stage2_llvm_invalid_infer_global_missing_init.log"
OUT_INFER_ARRAY_LENGTH_MISSING_INIT_LOG="$BUILD_DIR/stage2_llvm_invalid_infer_array_length_missing_init.log"
OUT_TYPED_ARRAY_CONSTRUCTOR_NON_ARRAY_LOG="$BUILD_DIR/stage2_llvm_invalid_typed_array_constructor_non_array.log"
OUT_TYPED_ARRAY_CONSTRUCTOR_LENGTH_LOG="$BUILD_DIR/stage2_llvm_invalid_typed_array_constructor_length.log"
OUT_INFER_SHORTHAND_LOG="$BUILD_DIR/stage2_llvm_invalid_infer_shorthand_without_expected.log"
OUT_INFER_OPTIONAL_NULL_LOG="$BUILD_DIR/stage2_llvm_invalid_infer_optional_null.log"
OUT_OPTIONAL_NULL_NON_OPTIONAL_LOG="$BUILD_DIR/stage2_llvm_invalid_optional_null_non_optional.log"
OUT_OPTIONAL_COMPARE_NON_NULL_LOG="$BUILD_DIR/stage2_llvm_invalid_optional_compare_non_null.log"
OUT_OPTIONAL_NO_NARROW_NULL_BRANCH_LOG="$BUILD_DIR/stage2_llvm_invalid_optional_no_narrow_then_null_branch.log"
OUT_OPTIONAL_CHAIN_IMPURE_BASE_LOG="$BUILD_DIR/stage2_llvm_invalid_optional_chain_impure_base.log"
OUT_TERNARY_CONDITION_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_ternary_condition_type.log"
OUT_TERNARY_BRANCH_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_ternary_branch_type.log"
OUT_TERNARY_AGGREGATE_RESULT_LOG="$BUILD_DIR/stage2_llvm_invalid_ternary_aggregate_result.log"
OUT_VOID_KEYWORD_TYPE_LOG="$BUILD_DIR/stage2_llvm_invalid_void_keyword_type.log"
OUT_EMPTY_TUPLE_RETURN_NON_VOID_LOG="$BUILD_DIR/stage2_llvm_invalid_empty_tuple_return_non_void.log"
OUT_TUPLE_DESTRUCTURE_ARITY_LOG="$BUILD_DIR/stage2_llvm_invalid_tuple_destructure_arity.log"
OUT_TUPLE_DESTRUCTURE_RHS_LOG="$BUILD_DIR/stage2_llvm_invalid_tuple_destructure_rhs.log"
OUT_TUPLE_INDEX_NON_LITERAL_LOG="$BUILD_DIR/stage2_llvm_invalid_tuple_index_non_literal.log"
OUT_TUPLE_INDEX_OUT_OF_RANGE_LOG="$BUILD_DIR/stage2_llvm_invalid_tuple_index_out_of_range.log"
OUT_UNION_CTOR_ARG_LOG="$BUILD_DIR/stage2_llvm_invalid_union_ctor_arg.log"
OUT_UNION_BIND_VOID_LOG="$BUILD_DIR/stage2_llvm_invalid_union_bind_void.log"
OUT_UNION_SWITCH_NON_EXHAUSTIVE_LOG="$BUILD_DIR/stage2_llvm_invalid_union_switch_non_exhaustive.log"
OUT_UNION_PATTERN_NE_BIND_LOG="$BUILD_DIR/stage2_llvm_invalid_union_pattern_ne_bind.log"
OUT_UNION_TUPLE_BIND_ARITY_LOG="$BUILD_DIR/stage2_llvm_invalid_union_tuple_bind_arity.log"
OUT_UNION_TUPLE_BIND_NON_TUPLE_LOG="$BUILD_DIR/stage2_llvm_invalid_union_tuple_bind_non_tuple.log"
OUT_FOR_TUPLE_BIND_NON_TUPLE_LOG="$BUILD_DIR/stage2_llvm_invalid_for_tuple_binding_non_tuple.log"
OUT_FOR_TUPLE_BIND_ARITY_LOG="$BUILD_DIR/stage2_llvm_invalid_for_tuple_binding_arity.log"
OUT_FOR_INDEXED_ARITY_LOG="$BUILD_DIR/stage2_llvm_invalid_for_indexed_arity.log"
OUT_FOR_INDEXED_NESTED_INDEX_LOG="$BUILD_DIR/stage2_llvm_invalid_for_indexed_nested_index_binding.log"

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

if [[ "$(<"$OUT_INDEX_TARGET_LOG")" != *"index target must be array, slice, or tuple"* ]]; then
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
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_for_iterable_target.jiang" > "$OUT_FOR_ITERABLE_TARGET_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_for_iterable_target to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_FOR_ITERABLE_TARGET_LOG")" != *"for iterable must be array or slice"* ]]; then
    echo "stage2 llvm error smoke missing for iterable target diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_FOR_ITERABLE_TARGET_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_for_iterable_target" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_for_tuple_binding_non_tuple.jiang" > "$OUT_FOR_TUPLE_BIND_NON_TUPLE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_for_tuple_binding_non_tuple to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_FOR_TUPLE_BIND_NON_TUPLE_LOG")" != *"for tuple binding requires tuple element"* ]]; then
    echo "stage2 llvm error smoke missing for tuple binding non-tuple diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_FOR_TUPLE_BIND_NON_TUPLE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_for_tuple_binding_non_tuple" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_for_tuple_binding_arity.jiang" > "$OUT_FOR_TUPLE_BIND_ARITY_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_for_tuple_binding_arity to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_FOR_TUPLE_BIND_ARITY_LOG")" != *"for tuple binding arity mismatch"* ]]; then
    echo "stage2 llvm error smoke missing for tuple binding arity diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_FOR_TUPLE_BIND_ARITY_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_for_tuple_binding_arity" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_for_indexed_arity.jiang" > "$OUT_FOR_INDEXED_ARITY_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_for_indexed_arity to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_FOR_INDEXED_ARITY_LOG")" != *"for indexed binding arity mismatch"* ]]; then
    echo "stage2 llvm error smoke missing for indexed binding arity diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_FOR_INDEXED_ARITY_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_for_indexed_arity" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_for_indexed_nested_index_binding.jiang" > "$OUT_FOR_INDEXED_NESTED_INDEX_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_for_indexed_nested_index_binding to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_FOR_INDEXED_NESTED_INDEX_LOG")" != *"for indexed binding index must be single name"* ]]; then
    echo "stage2 llvm error smoke missing indexed nested index diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_FOR_INDEXED_NESTED_INDEX_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_for_indexed_nested_index_binding" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_switch_case_type.jiang" > "$OUT_SWITCH_CASE_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_switch_case_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_SWITCH_CASE_TYPE_LOG")" != *"switch case type mismatch"* ]]; then
    echo "stage2 llvm error smoke missing switch case type mismatch diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_SWITCH_CASE_TYPE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_switch_case_type" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_switch_duplicate_case.jiang" > "$OUT_SWITCH_DUPLICATE_CASE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_switch_duplicate_case to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_SWITCH_DUPLICATE_CASE_LOG")" != *"duplicate switch case"* ]]; then
    echo "stage2 llvm error smoke missing duplicate switch case diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_SWITCH_DUPLICATE_CASE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_switch_duplicate_case" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_switch_non_exhaustive_enum.jiang" > "$OUT_SWITCH_NON_EXHAUSTIVE_ENUM_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_switch_non_exhaustive_enum to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_SWITCH_NON_EXHAUSTIVE_ENUM_LOG")" != *"non-exhaustive switch"* ]]; then
    echo "stage2 llvm error smoke missing non-exhaustive enum switch diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_SWITCH_NON_EXHAUSTIVE_ENUM_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_switch_non_exhaustive_enum" >&2
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

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_infer_global_missing_init.jiang" > "$OUT_INFER_GLOBAL_MISSING_INIT_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_infer_global_missing_init to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_INFER_GLOBAL_MISSING_INIT_LOG")" != *"inferred global requires initializer"* ]]; then
    echo "stage2 llvm error smoke missing inferred global initializer diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_INFER_GLOBAL_MISSING_INIT_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_infer_global_missing_init" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_infer_array_length_missing_init.jiang" > "$OUT_INFER_ARRAY_LENGTH_MISSING_INIT_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_infer_array_length_missing_init to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_INFER_ARRAY_LENGTH_MISSING_INIT_LOG")" != *"inferred array length requires initializer"* ]]; then
    echo "stage2 llvm error smoke missing inferred array length initializer diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_INFER_ARRAY_LENGTH_MISSING_INIT_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_infer_array_length_missing_init" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_typed_array_constructor_non_array.jiang" > "$OUT_TYPED_ARRAY_CONSTRUCTOR_NON_ARRAY_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_typed_array_constructor_non_array to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_TYPED_ARRAY_CONSTRUCTOR_NON_ARRAY_LOG")" != *"typed array constructor requires array type"* ]]; then
    echo "stage2 llvm error smoke missing typed array constructor non-array diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_TYPED_ARRAY_CONSTRUCTOR_NON_ARRAY_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_typed_array_constructor_non_array" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_typed_array_constructor_length.jiang" > "$OUT_TYPED_ARRAY_CONSTRUCTOR_LENGTH_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_typed_array_constructor_length to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_TYPED_ARRAY_CONSTRUCTOR_LENGTH_LOG")" != *"array literal length mismatch"* ]]; then
    echo "stage2 llvm error smoke missing typed array constructor length diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_TYPED_ARRAY_CONSTRUCTOR_LENGTH_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_typed_array_constructor_length" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_infer_shorthand_without_expected.jiang" > "$OUT_INFER_SHORTHAND_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_infer_shorthand_without_expected to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_INFER_SHORTHAND_LOG")" != *"cannot infer local type"* ]]; then
    echo "stage2 llvm error smoke missing infer shorthand diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_INFER_SHORTHAND_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_infer_shorthand_without_expected" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_infer_optional_null.jiang" > "$OUT_INFER_OPTIONAL_NULL_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_infer_optional_null to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_INFER_OPTIONAL_NULL_LOG")" != *"cannot infer local type"* ]]; then
    echo "stage2 llvm error smoke missing infer optional null diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_INFER_OPTIONAL_NULL_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_infer_optional_null" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_optional_null_non_optional.jiang" > "$OUT_OPTIONAL_NULL_NON_OPTIONAL_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_optional_null_non_optional to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_OPTIONAL_NULL_NON_OPTIONAL_LOG")" != *"local initializer type mismatch"* ]]; then
    echo "stage2 llvm error smoke missing optional null mismatch diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_OPTIONAL_NULL_NON_OPTIONAL_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_optional_null_non_optional" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_optional_compare_non_null.jiang" > "$OUT_OPTIONAL_COMPARE_NON_NULL_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_optional_compare_non_null to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_OPTIONAL_COMPARE_NON_NULL_LOG")" != *"optional comparison currently requires null"* ]]; then
    echo "stage2 llvm error smoke missing optional comparison diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_OPTIONAL_COMPARE_NON_NULL_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_optional_compare_non_null" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_optional_no_narrow_then_null_branch.jiang" > "$OUT_OPTIONAL_NO_NARROW_NULL_BRANCH_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_optional_no_narrow_then_null_branch to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_OPTIONAL_NO_NARROW_NULL_BRANCH_LOG")" != *"return type mismatch"* ]]; then
    echo "stage2 llvm error smoke missing optional no-narrow diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_OPTIONAL_NO_NARROW_NULL_BRANCH_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_optional_no_narrow_then_null_branch" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_optional_chain_impure_base.jiang" > "$OUT_OPTIONAL_CHAIN_IMPURE_BASE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_optional_chain_impure_base to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_OPTIONAL_CHAIN_IMPURE_BASE_LOG")" != *"optional chain currently requires simple base expression"* ]]; then
    echo "stage2 llvm error smoke missing optional chain impure base diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_OPTIONAL_CHAIN_IMPURE_BASE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_optional_chain_impure_base" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_ternary_condition_type.jiang" > "$OUT_TERNARY_CONDITION_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_ternary_condition_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_TERNARY_CONDITION_TYPE_LOG")" != *"ternary condition must be Bool"* ]]; then
    echo "stage2 llvm error smoke missing ternary condition diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_TERNARY_CONDITION_TYPE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_ternary_condition_type" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_ternary_branch_type.jiang" > "$OUT_TERNARY_BRANCH_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_ternary_branch_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_TERNARY_BRANCH_TYPE_LOG")" != *"ternary branch type mismatch"* ]]; then
    echo "stage2 llvm error smoke missing ternary branch diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_TERNARY_BRANCH_TYPE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_ternary_branch_type" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_ternary_aggregate_result.jiang" > "$OUT_TERNARY_AGGREGATE_RESULT_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_ternary_aggregate_result to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_TERNARY_AGGREGATE_RESULT_LOG")" != *"ternary result currently requires scalar type"* ]]; then
    echo "stage2 llvm error smoke missing ternary aggregate diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_TERNARY_AGGREGATE_RESULT_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_ternary_aggregate_result" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_void_keyword_type.jiang" > "$OUT_VOID_KEYWORD_TYPE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_void_keyword_type to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_VOID_KEYWORD_TYPE_LOG")" != *"unknown type"* ]]; then
    echo "stage2 llvm error smoke missing void keyword unknown type diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_VOID_KEYWORD_TYPE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_void_keyword_type" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_empty_tuple_return_non_void.jiang" > "$OUT_EMPTY_TUPLE_RETURN_NON_VOID_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_empty_tuple_return_non_void to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_EMPTY_TUPLE_RETURN_NON_VOID_LOG")" != *"return type mismatch"* ]]; then
    echo "stage2 llvm error smoke missing empty tuple return mismatch diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_EMPTY_TUPLE_RETURN_NON_VOID_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_empty_tuple_return_non_void" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_tuple_destructure_arity.jiang" > "$OUT_TUPLE_DESTRUCTURE_ARITY_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_tuple_destructure_arity to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_TUPLE_DESTRUCTURE_ARITY_LOG")" != *"tuple destructuring arity mismatch"* ]]; then
    echo "stage2 llvm error smoke missing tuple destructuring arity diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_TUPLE_DESTRUCTURE_ARITY_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_tuple_destructure_arity" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_tuple_destructure_rhs.jiang" > "$OUT_TUPLE_DESTRUCTURE_RHS_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_tuple_destructure_rhs to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_TUPLE_DESTRUCTURE_RHS_LOG")" != *"tuple destructuring requires tuple value"* ]]; then
    echo "stage2 llvm error smoke missing tuple destructuring rhs diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_TUPLE_DESTRUCTURE_RHS_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_tuple_destructure_rhs" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_tuple_index_non_literal.jiang" > "$OUT_TUPLE_INDEX_NON_LITERAL_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_tuple_index_non_literal to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_TUPLE_INDEX_NON_LITERAL_LOG")" != *"tuple index must be integer literal"* ]]; then
    echo "stage2 llvm error smoke missing tuple index non-literal diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_TUPLE_INDEX_NON_LITERAL_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_tuple_index_non_literal" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_tuple_index_out_of_range.jiang" > "$OUT_TUPLE_INDEX_OUT_OF_RANGE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_tuple_index_out_of_range to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_TUPLE_INDEX_OUT_OF_RANGE_LOG")" != *"tuple index out of range"* ]]; then
    echo "stage2 llvm error smoke missing tuple index out-of-range diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_TUPLE_INDEX_OUT_OF_RANGE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_tuple_index_out_of_range" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_union_ctor_arg.jiang" > "$OUT_UNION_CTOR_ARG_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_union_ctor_arg to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_UNION_CTOR_ARG_LOG")" != *"argument type mismatch"* ]]; then
    echo "stage2 llvm error smoke missing union constructor argument type mismatch diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_UNION_CTOR_ARG_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_union_ctor_arg" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_union_bind_void.jiang" > "$OUT_UNION_BIND_VOID_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_union_bind_void to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_UNION_BIND_VOID_LOG")" != *"union case has no payload"* ]]; then
    echo "stage2 llvm error smoke missing union bind void diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_UNION_BIND_VOID_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_union_bind_void" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_union_switch_non_exhaustive.jiang" > "$OUT_UNION_SWITCH_NON_EXHAUSTIVE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_union_switch_non_exhaustive to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_UNION_SWITCH_NON_EXHAUSTIVE_LOG")" != *"non-exhaustive switch"* ]]; then
    echo "stage2 llvm error smoke missing non-exhaustive union switch diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_UNION_SWITCH_NON_EXHAUSTIVE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_union_switch_non_exhaustive" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_union_pattern_ne_bind.jiang" > "$OUT_UNION_PATTERN_NE_BIND_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_union_pattern_ne_bind to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_UNION_PATTERN_NE_BIND_LOG")" != *"union pattern binding requires == condition"* ]]; then
    echo "stage2 llvm error smoke missing union pattern binding diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_UNION_PATTERN_NE_BIND_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_union_pattern_ne_bind" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_union_tuple_bind_arity.jiang" > "$OUT_UNION_TUPLE_BIND_ARITY_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_union_tuple_bind_arity to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_UNION_TUPLE_BIND_ARITY_LOG")" != *"union tuple binding arity mismatch"* ]]; then
    echo "stage2 llvm error smoke missing union tuple binding arity diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_UNION_TUPLE_BIND_ARITY_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_union_tuple_bind_arity" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/invalid_union_tuple_bind_non_tuple.jiang" > "$OUT_UNION_TUPLE_BIND_NON_TUPLE_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 llvm error smoke expected invalid_union_tuple_bind_non_tuple to fail" >&2
    exit 1
fi

if [[ "$(<"$OUT_UNION_TUPLE_BIND_NON_TUPLE_LOG")" != *"union tuple binding requires tuple payload"* ]]; then
    echo "stage2 llvm error smoke missing union tuple binding payload diagnostic" >&2
    exit 1
fi

if rg -q '^; ModuleID = ' "$OUT_UNION_TUPLE_BIND_NON_TUPLE_LOG"; then
    echo "stage2 llvm error smoke unexpectedly produced llvm ir for invalid_union_tuple_bind_non_tuple" >&2
    exit 1
fi

echo "stage2 llvm error smoke passed"
