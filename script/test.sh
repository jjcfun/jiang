#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
SAMPLES_DIR="$PROJECT_ROOT/tests/samples"

if [[ -n "${LLVM_CONFIG:-}" ]]; then
  LLI="$(cd "$(dirname "$LLVM_CONFIG")" && pwd)/lli"
elif [[ -n "${JIANG_LLVM_ROOT:-}" ]]; then
  LLI="$JIANG_LLVM_ROOT/bin/lli"
else
  LLI="$(command -v lli)"
fi

if [[ -z "$LLI" || ! -x "$LLI" ]]; then
  echo "error: lli not found; set LLVM_CONFIG or JIANG_LLVM_ROOT to an LLVM 21.1.x toolchain" >&2
  exit 1
fi

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR"
"$BUILD_DIR/utils_test"

run_sample() {
  local sample="$1"
  local expected="$2"
  local ir="$BUILD_DIR/${sample%.jiang}.ll"
  "$BUILD_DIR/jiangc" --emit-llvm "$SAMPLES_DIR/$sample" > "$ir"
  set +e
  "$LLI" "$ir"
  local status=$?
  set -e
  if [[ "$status" -ne "$expected" ]]; then
    echo "error: $sample exited $status, expected $expected" >&2
    exit 1
  fi
}

run_sample_nonzero() {
  local sample="$1"
  local ir="$BUILD_DIR/${sample%.jiang}.ll"
  "$BUILD_DIR/jiangc" --emit-llvm "$SAMPLES_DIR/$sample" > "$ir"
  set +e
  "$LLI" "$ir" >/dev/null 2>&1
  local status=$?
  set -e
  if [[ "$status" -eq 0 ]]; then
    echo "error: $sample unexpectedly exited 0" >&2
    exit 1
  fi
}

run_compile_fail() {
  local sample="$1"
  set +e
  "$BUILD_DIR/jiangc" --emit-llvm "$SAMPLES_DIR/$sample" >/dev/null 2>&1
  local status=$?
  set -e
  if [[ "$status" -eq 0 ]]; then
    echo "error: $sample unexpectedly compiled" >&2
    exit 1
  fi
}

run_sample minimal.jiang 42
run_sample locals_minimal.jiang 42
run_sample assign_minimal.jiang 5
run_sample if_minimal.jiang 2
run_sample global_minimal.jiang 42
run_sample while_minimal.jiang 10
run_sample binary_ops_minimal.jiang 1
run_sample bool_minimal.jiang 1
run_sample uint8_minimal.jiang 0
run_sample uint8_slice_minimal.jiang 0
run_sample pointer_minimal.jiang 42
run_sample free_minimal.jiang 0
run_sample multi_file_minimal.jiang 42
run_sample namespaced_import_minimal.jiang 42
run_sample public_import_function_minimal.jiang 42
run_sample mutable_qualifier_minimal.jiang 42
run_sample mutable_array_qualifier_minimal.jiang 42
run_sample break_continue_minimal.jiang 8
run_sample for_range_minimal.jiang 8
run_sample for_infer_range_minimal.jiang 5
run_sample infer_global_minimal.jiang 42
run_sample infer_local_minimal.jiang 42
run_sample infer_mutable_local_minimal.jiang 42
run_sample large_minimal.jiang 15
run_sample enum_minimal.jiang 2
run_sample enum_shorthand_minimal.jiang 42
run_sample enum_shorthand_arg_minimal.jiang 42
run_sample enum_value_minimal.jiang 42
run_sample enum_switch_shorthand_minimal.jiang 42
run_sample switch_enum_minimal.jiang 42
run_sample ternary_enum_minimal.jiang 42
run_sample ternary_minimal.jiang 42
run_sample struct_minimal.jiang 42
run_sample fields_minimal.jiang 3
run_sample nested_fields_minimal.jiang 42
run_sample call_result_field_minimal.jiang 42
run_sample struct_init_minimal.jiang 42
run_sample struct_init_with_defaults_minimal.jiang 18
run_sample struct_init_branch_complete_minimal.jiang 42
run_sample struct_init_mutable_default_override_minimal.jiang 19
run_sample public_import_type_minimal.jiang 42
run_sample struct_constructor_sugar_minimal.jiang 42
run_sample struct_literal_with_init_minimal.jiang 42
run_sample struct_new_constructor_minimal.jiang 42
run_sample struct_new_literal_with_init_minimal.jiang 42
run_sample multi_file_struct_minimal.jiang 42
run_sample namespaced_struct_import_minimal.jiang 42
run_sample struct_enum_field_shorthand_minimal.jiang 42
run_sample struct_union_field_shorthand_minimal.jiang 42
run_sample struct_instance_method_minimal.jiang 42
run_sample struct_instance_method_with_args_minimal.jiang 42
run_sample struct_static_method_minimal.jiang 42
run_sample struct_method_calls_method_minimal.jiang 42
run_sample struct_static_calls_static_minimal.jiang 42
run_sample enum_instance_method_minimal.jiang 42
run_sample enum_static_method_minimal.jiang 42
run_sample union_instance_method_minimal.jiang 42
run_sample union_static_method_minimal.jiang 42
run_sample assert_minimal.jiang 42
run_sample print_minimal.jiang 42
run_sample tuple_value_minimal.jiang 42
run_sample tuple_return_minimal.jiang 42
run_sample tuple_infer_minimal.jiang 42
run_sample tuple_destructure_minimal.jiang 42
run_sample tuple_destructure_infer_minimal.jiang 42
run_sample tuple_destructure_mutable_infer_minimal.jiang 42
run_sample tuple_destructure_return_minimal.jiang 42
run_sample tuple_destructure_global_minimal.jiang 42
run_sample unary_tuple_local_decl_minimal.jiang 42
run_sample unary_tuple_infer_local_decl_minimal.jiang 42
run_sample unary_tuple_global_decl_minimal.jiang 42
run_sample unary_tuple_return_minimal.jiang 42
run_sample array_minimal.jiang 42
run_sample array_assign_minimal.jiang 10
run_sample array_repeat_init_minimal.jiang 6
run_sample nested_array_minimal.jiang 42
run_sample infer_array_length_minimal.jiang 42
run_sample uint8_array_string_minimal.jiang 98
run_sample infer_uint8_array_string_minimal.jiang 98
run_sample struct_array_field_minimal.jiang 98
run_sample multi_file_pointer_minimal.jiang 42
run_sample namespaced_pointer_minimal.jiang 42
run_sample slice_index_minimal.jiang 0
run_sample slice_assign_minimal.jiang 0
run_sample slice_length_minimal.jiang 3
run_sample slice_return_length_minimal.jiang 3
run_sample array_to_slice_local_minimal.jiang 42
run_sample array_to_slice_assign_minimal.jiang 42
run_sample array_to_slice_arg_minimal.jiang 42
run_sample array_to_slice_return_minimal.jiang 2
run_sample typed_array_constructor_minimal.jiang 42
run_sample typed_array_constructor_infer_minimal.jiang 42
run_sample empty_tuple_return_minimal.jiang 0
run_sample empty_tuple_bare_return_minimal.jiang 0
run_sample for_item_array_minimal.jiang 42
run_sample for_mutable_binding_minimal.jiang 42
run_sample for_indexed_minimal.jiang 40
run_sample for_indexed_typed_minimal.jiang 40
run_sample for_tuple_binding_minimal.jiang 42
run_sample for_tuple_binding_typed_minimal.jiang 42
run_sample for_indexed_tuple_binding_minimal.jiang 42
run_sample for_indexed_mutable_tuple_binding_minimal.jiang 42
run_sample union_tuple_bind_minimal.jiang 42
run_sample union_tuple_switch_mutable_binding_minimal.jiang 42
run_sample union_tuple_if_shorthand_pattern_minimal.jiang 42
run_sample union_tuple_if_mutable_shorthand_pattern_minimal.jiang 42
run_sample union_minimal.jiang 42
run_sample union_shorthand_minimal.jiang 42
run_sample union_implicit_tag_minimal.jiang 42
run_sample union_bind_minimal.jiang 42
run_sample union_if_pattern_minimal.jiang 42
run_sample union_if_mutable_binding_minimal.jiang 42
run_sample union_if_shorthand_pattern_minimal.jiang 42
run_sample union_switch_mutable_binding_minimal.jiang 42
run_sample union_switch_shorthand_pattern_minimal.jiang 42
run_compile_fail invalid_tuple_index_non_literal.jiang
run_compile_fail invalid_tuple_index_out_of_range.jiang
run_compile_fail invalid_array_length.jiang
run_compile_fail invalid_array_assign_length.jiang
run_compile_fail invalid_array_arg_length.jiang
run_compile_fail invalid_array_return_length.jiang
run_compile_fail invalid_infer_array_length_missing_init.jiang
run_compile_fail invalid_infer_global_missing_init.jiang
run_compile_fail invalid_typed_array_constructor_length.jiang
run_compile_fail invalid_typed_array_constructor_non_array.jiang
run_compile_fail invalid_index_target.jiang
run_compile_fail invalid_index_type.jiang
run_compile_fail invalid_address_of_expr.jiang
run_compile_fail invalid_deref_non_pointer.jiang
run_compile_fail invalid_free_non_pointer.jiang
run_compile_fail invalid_use_after_free.jiang
run_compile_fail invalid_import_private_function.jiang
run_compile_fail invalid_import_private_type.jiang
run_compile_fail invalid_slice_assign_type.jiang
run_compile_fail invalid_array_to_slice_type.jiang
run_compile_fail invalid_tuple_destructure_arity.jiang
run_compile_fail invalid_tuple_destructure_rhs.jiang
run_compile_fail invalid_empty_tuple_return_non_void.jiang
run_compile_fail invalid_break_outside_loop.jiang
run_compile_fail invalid_continue_outside_loop.jiang
run_compile_fail invalid_for_iterable_target.jiang
run_compile_fail invalid_for_loop_var_type.jiang
run_compile_fail invalid_for_indexed_arity.jiang
run_compile_fail invalid_for_indexed_nested_index_binding.jiang
run_compile_fail invalid_for_tuple_binding_non_tuple.jiang
run_compile_fail invalid_for_tuple_binding_arity.jiang
run_compile_fail invalid_switch_case_type.jiang
run_compile_fail invalid_switch_duplicate_case.jiang
run_compile_fail invalid_switch_non_exhaustive_enum.jiang
run_compile_fail invalid_unknown_ident.jiang
run_compile_fail invalid_assign_target.jiang
run_compile_fail invalid_assign_field_type.jiang
run_compile_fail invalid_call_non_function.jiang
run_compile_fail invalid_duplicate_enum.jiang
run_compile_fail invalid_duplicate_enum_member.jiang
run_compile_fail invalid_duplicate_type.jiang
run_compile_fail invalid_duplicate_function.jiang
run_compile_fail invalid_duplicate_field_decl.jiang
run_compile_fail invalid_duplicate_local.jiang
run_compile_fail invalid_duplicate_param.jiang
run_compile_fail invalid_struct_duplicate_field.jiang
run_compile_fail invalid_struct_duplicate_method.jiang
run_compile_fail invalid_struct_field.jiang
run_compile_fail invalid_struct_missing_field.jiang
run_compile_fail invalid_struct_method_before_feature_misparse.jiang
run_compile_fail invalid_struct_init_immutable_default_override.jiang
run_compile_fail invalid_struct_init_immutable_reassign.jiang
run_compile_fail invalid_struct_init_missing_field.jiang
run_compile_fail invalid_struct_init_read_before_init.jiang
run_compile_fail invalid_struct_init_return_value.jiang
run_compile_fail invalid_struct_instance_call_through_type.jiang
run_compile_fail invalid_struct_static_call_through_instance.jiang
run_compile_fail invalid_struct_static_init.jiang
run_compile_fail invalid_struct_static_uses_self.jiang
run_compile_fail invalid_struct_field_method_name_conflict.jiang
run_compile_fail invalid_enum_instance_call_through_type.jiang
run_compile_fail invalid_enum_static_call_through_instance.jiang
run_compile_fail invalid_union_instance_call_through_type.jiang
run_compile_fail invalid_union_static_call_through_instance.jiang
run_compile_fail invalid_missing_semicolon.jiang
run_compile_fail invalid_void_keyword_type.jiang
run_compile_fail invalid_type_function_name_conflict.jiang
run_compile_fail invalid_enum_type_name_conflict.jiang
run_compile_fail invalid_enum_value_type.jiang
run_compile_fail invalid_union_bind_void.jiang
run_compile_fail invalid_union_ctor_arg.jiang
run_compile_fail invalid_union_pattern_ne_bind.jiang
run_compile_fail invalid_union_switch_non_exhaustive.jiang
run_compile_fail invalid_union_tuple_bind_non_tuple.jiang
run_compile_fail invalid_union_tuple_bind_arity.jiang
run_compile_fail invalid_infer_shorthand_without_expected.jiang
run_compile_fail invalid_ternary_aggregate_result.jiang
run_compile_fail invalid_ternary_branch_type.jiang
run_compile_fail invalid_ternary_condition_type.jiang
run_sample_nonzero panic_minimal.jiang

echo "stage0 batch A smoke passed"
