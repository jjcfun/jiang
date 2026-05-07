#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build/stage1-tests}"
SAMPLES_DIR="$PROJECT_ROOT/tests/samples"
NEXT_SAMPLES_DIR="$PROJECT_ROOT/tests/samples_next"
STAGE1_BIN="${STAGE1_BIN:-$PROJECT_ROOT/build/stage1/jiangc}"

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

CC_BIN="${CC:-$(command -v cc)}"

if [[ -z "$CC_BIN" || ! -x "$CC_BIN" ]]; then
  echo "error: cc not found" >&2
  exit 1
fi

mkdir -p "$BUILD_DIR"

if [[ ! -x "$STAGE1_BIN" ]]; then
  echo "error: stage1 compiler not found: $STAGE1_BIN" >&2
  echo "hint: run LLVM_CONFIG=/path/to/llvm-config bash script/build_stage1.sh first" >&2
  exit 1
fi

run_sample() {
  local sample="$1"
  local expected="$2"
  local ir="$BUILD_DIR/${sample%.jiang}.ll"
  "$STAGE1_BIN" --emit-llvm "$SAMPLES_DIR/$sample" > "$ir"
  set +e
  "$LLI" "$ir"
  local status=$?
  set -e
  if [[ "$status" -ne "$expected" ]]; then
    echo "error: $sample exited $status, expected $expected" >&2
    exit 1
  fi
}

run_known_stage1_gap() {
  local sample="$1"
  local expected="$2"
  local ir="$BUILD_DIR/${sample%.jiang}.known-gap.ll"
  set +e
  /bin/sh -c '"$1" --emit-llvm "$2" > "$3" 2>/dev/null' sh "$STAGE1_BIN" "$SAMPLES_DIR/$sample" "$ir" >/dev/null 2>&1
  local compile_status=$?
  set -e
  if [[ "$compile_status" -ne 0 ]]; then
    echo "known Stage1 runtime/codegen gap still fails to compile: $sample" >&2
    return 0
  fi

  set +e
  "$LLI" "$ir" >/dev/null 2>&1
  local run_status=$?
  set -e
  if [[ "$run_status" -eq "$expected" ]]; then
    echo "known Stage1 runtime/codegen gap now passes; move to run_sample: $sample" >&2
  else
    echo "known Stage1 runtime/codegen gap still mismatches: $sample expected $expected got $run_status" >&2
  fi
}

run_next_sample() {
  local sample="$1"
  local expected="$2"
  local ir="$BUILD_DIR/next-${sample%.jiang}.ll"
  "$STAGE1_BIN" --emit-llvm "$NEXT_SAMPLES_DIR/$sample" > "$ir"
  set +e
  "$LLI" "$ir"
  local status=$?
  set -e
  if [[ "$status" -ne "$expected" ]]; then
    echo "error: samples_next/$sample exited $status, expected $expected" >&2
    exit 1
  fi
}

run_next_compile_fail() {
  local sample="$1"
  set +e
  /bin/sh -c '"$1" --emit-llvm "$2" >/dev/null 2>&1' sh "$STAGE1_BIN" "$NEXT_SAMPLES_DIR/$sample" >/dev/null 2>&1
  local status=$?
  set -e
  if [[ "$status" -eq 0 ]]; then
    echo "error: samples_next/$sample unexpectedly compiled" >&2
    exit 1
  fi
}

run_sample_nonzero() {
  local sample="$1"
  local ir="$BUILD_DIR/${sample%.jiang}.ll"
  "$STAGE1_BIN" --emit-llvm "$SAMPLES_DIR/$sample" > "$ir"
  set +e
  /bin/sh -c '"$1" "$2" >/dev/null 2>&1' sh "$LLI" "$ir" >/dev/null 2>&1
  local status=$?
  set -e
  if [[ "$status" -eq 0 ]]; then
    echo "error: $sample unexpectedly exited 0" >&2
    exit 1
  fi
}

run_known_stage1_nonzero_gap() {
  local sample="$1"
  local ir="$BUILD_DIR/${sample%.jiang}.known-gap.ll"
  set +e
  /bin/sh -c '"$1" --emit-llvm "$2" > "$3" 2>/dev/null' sh "$STAGE1_BIN" "$SAMPLES_DIR/$sample" "$ir" >/dev/null 2>&1
  local compile_status=$?
  set -e
  if [[ "$compile_status" -ne 0 ]]; then
    echo "known Stage1 runtime/nonzero gap still fails to compile: $sample" >&2
    return 0
  fi

  set +e
  /bin/sh -c '"$1" "$2" >/dev/null 2>&1' sh "$LLI" "$ir" >/dev/null 2>&1
  local run_status=$?
  set -e
  if [[ "$run_status" -eq 0 ]]; then
    echo "known Stage1 runtime/nonzero gap still exits 0: $sample" >&2
  else
    echo "known Stage1 runtime/nonzero gap now exits nonzero; move to run_sample_nonzero: $sample" >&2
  fi
}

run_compile_fail() {
  local sample="$1"
  set +e
  /bin/sh -c '"$1" --emit-llvm "$2" >/dev/null 2>&1' sh "$STAGE1_BIN" "$SAMPLES_DIR/$sample" >/dev/null 2>&1
  local status=$?
  set -e
  if [[ "$status" -eq 0 ]]; then
    echo "error: $sample unexpectedly compiled" >&2
    exit 1
  fi
}

run_compile_only() {
  local sample="$1"
  local ir="$BUILD_DIR/${sample%.jiang}.ll"
  "$STAGE1_BIN" --emit-llvm "$SAMPLES_DIR/$sample" > "$ir"
}

run_imported_extern_symbol_sample() {
  local sample="imported_extern_symbol_minimal.jiang"
  local ir="$BUILD_DIR/${sample%.jiang}.ll"
  "$STAGE1_BIN" --emit-llvm "$SAMPLES_DIR/$sample" > "$ir"
  if grep -q "imported_extern_helper.puts" "$ir"; then
    echo "error: imported extern leaked module-qualified C symbol" >&2
    exit 1
  fi
  if ! grep -q "@puts" "$ir"; then
    echo "error: imported extern did not emit raw C symbol" >&2
    exit 1
  fi
}

run_object_sample() {
  local sample="$1"
  local expected="$2"
  local obj="$BUILD_DIR/${sample%.jiang}.o"
  local exe="$BUILD_DIR/${sample%.jiang}.obj.out"
  "$STAGE1_BIN" --emit-obj -o "$obj" "$SAMPLES_DIR/$sample"
  "$CC_BIN" "$obj" -o "$exe"
  set +e
  "$exe"
  local status=$?
  set -e
  if [[ "$status" -ne "$expected" ]]; then
    echo "error: object sample $sample exited $status, expected $expected" >&2
    exit 1
  fi
}

run_executable_sample() {
  local sample="$1"
  local expected="$2"
  local exe="$BUILD_DIR/${sample%.jiang}.exe.out"
  "$STAGE1_BIN" -o "$exe" "$SAMPLES_DIR/$sample"
  set +e
  "$exe"
  local status=$?
  set -e
  if [[ "$status" -ne "$expected" ]]; then
    echo "error: executable sample $sample exited $status, expected $expected" >&2
    exit 1
  fi
}

run_sample minimal.jiang 42
run_object_sample minimal.jiang 42
run_executable_sample minimal.jiang 42
run_imported_extern_symbol_sample
run_sample locals_minimal.jiang 42
run_sample assign_minimal.jiang 5
run_sample if_minimal.jiang 2
run_next_sample if_no_parens_minimal.jiang 42
run_sample if_expr_minimal.jiang 42
run_next_compile_fail invalid_if_expr_bare_minimal.jiang
run_next_compile_fail invalid_if_stmt_bare_body.jiang
run_sample if_expr_block_multi_stmt_minimal.jiang 42
run_sample if_expr_nested_minimal.jiang 42
run_sample if_expr_return_branch_minimal.jiang 42
run_sample if_expr_union_result_minimal.jiang 42
run_sample global_minimal.jiang 42
run_sample while_minimal.jiang 10
run_sample defer_minimal.jiang 57
run_sample defer_block_minimal.jiang 54
run_sample defer_return_minimal.jiang 21
run_sample defer_throw_try_minimal.jiang 123
run_sample defer_propagate_minimal.jiang 12
run_compile_fail invalid_defer_return_minimal.jiang
run_sample binary_ops_minimal.jiang 1
run_sample bitwise_minimal.jiang 42
run_sample logical_ops_minimal.jiang 140
run_sample bool_minimal.jiang 1
run_sample uint8_minimal.jiang 0
run_sample uint8_char_literal_init_minimal.jiang 42
run_sample uint8_char_literal_expected_minimal.jiang 42
run_sample float_minimal.jiang 7
run_sample double_minimal.jiang 8
run_sample int16_minimal.jiang 27
run_sample uint16_minimal.jiang 28
run_sample float16_minimal.jiang 29
run_sample float32_minimal.jiang 30
run_sample float64_minimal.jiang 31
run_sample character_minimal.jiang 21
run_sample character_equal_minimal.jiang 22
run_sample character_hashable_minimal.jiang 65
run_sample character_unicode_minimal.jiang 23
run_sample character_single_quote_minimal.jiang 24
run_sample character_single_quote_unicode_minimal.jiang 25
run_sample character_escape_minimal.jiang 42
run_sample int_char_literal_expected_minimal.jiang 42
run_sample int_float_add_minimal.jiang 24
run_sample int_double_add_minimal.jiang 25
run_sample float_double_compare_minimal.jiang 26
run_sample primitive_init_minimal.jiang 42
run_sample negative_int_minimal.jiang 1
run_sample mod_minimal.jiang 2
run_sample uint8_slice_minimal.jiang 0
run_sample pointer_minimal.jiang 42
run_sample pointer_default_field_minimal.jiang 0
run_compile_fail invalid_init_self_field_ptr_escape.jiang
run_sample pointer_offset_uint8_minimal.jiang 101
run_sample pointer_offset_int_minimal.jiang 42
run_sample many_pointer_assign_minimal.jiang 10
run_next_sample raw_pointer_minimal.jiang 42
run_sample as_addr_minimal.jiang 2
run_sample as_pointer_reinterpret_minimal.jiang 42
run_sample free_minimal.jiang 0
run_sample optional_implicit_free_minimal.jiang 42
run_executable_sample new_primitive_constructor_minimal.jiang 123
run_sample new_array_repeat_init_minimal.jiang 6
run_sample multi_file_minimal.jiang 42
run_object_sample multi_file_minimal.jiang 42
run_executable_sample multi_file_minimal.jiang 42
run_sample package_default 42
run_object_sample package_default 42
run_executable_sample package_default 42
run_sample package_override 44
run_compile_fail package_invalid_name
run_compile_fail invalid_package_import_with_quotes
run_sample namespaced_import_minimal.jiang 42
run_sample normal_import_reexport_minimal.jiang 42
run_compile_fail invalid_normal_import_reexport_namespace.jiang
# Function pointer builtin `Fn<...>` is not wired into stage1 type lowering yet.
# Overload resolution/mangling is not part of the current bootstrap smoke.
# Unit field/literal checking is not complete in stage1 yet.
# Errorable ABI/lowering is not complete in stage1 codegen yet.
# try/catch over errorable values is deferred with errorable ABI support.
# General try/catch lowering is deferred with the JIR control-flow cleanup.
# Self static dispatch with Fn values is deferred.
# Cross-module enum variants still parse/lower as field expressions.
# Function aliases, public-import re-export functions, and trait conformance
# diagnostics are outside the current stage1 bootstrap smoke.
# Overload ambiguity diagnostics are deferred with overload resolution.
run_sample mutable_qualifier_minimal.jiang 42
run_sample mutable_array_qualifier_minimal.jiang 42
run_sample record_minimal.jiang 42
# Record shorthand requires expected-type parsing for `{ ... }` expressions.
# It is not needed by compiler bootstrap yet.
# Grouped field/local declarations are parser sugar and deferred for bootstrap.
run_compile_fail invalid_record_call_syntax_minimal.jiang
run_compile_fail invalid_struct_brace_literal_minimal.jiang
run_compile_fail invalid_if_expr_missing_else.jiang
run_compile_fail invalid_if_expr_branch_type_mismatch.jiang
run_compile_fail invalid_switch_expr_branch_type_mismatch.jiang
run_compile_fail invalid_switch_expr_binding_pattern.jiang
run_compile_fail invalid_switch_expr_errorable_value.jiang
run_compile_fail invalid_switch_expr_missing_semicolon.jiang
run_sample switch_expr_return_branch_minimal.jiang 42
run_sample switch_expr_union_result_minimal.jiang 42
run_compile_fail invalid_try_expr_catch_type_mismatch.jiang
run_compile_fail invalid_bitwise_float_operand.jiang
run_compile_fail invalid_bitwise_mismatched_integer_types.jiang
# Grouped var declaration diagnostics are deferred with grouped declarations.
run_sample break_continue_minimal.jiang 8
run_sample for_range_minimal.jiang 8
run_sample for_infer_range_minimal.jiang 5
# Named Range value support depends on prelude modeling; for-range/slice syntax is covered above.
run_sample switch_expr_scalar_minimal.jiang 42
run_sample switch_expr_block_multi_stmt_minimal.jiang 42
run_sample switch_expr_enum_minimal.jiang 42
run_sample switch_expr_optional_minimal.jiang 42
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
run_sample optional_minimal.jiang 42
run_sample optional_null_compare_minimal.jiang 42
run_compile_fail optional_if_narrow_minimal.jiang
run_compile_fail optional_else_narrow_minimal.jiang
run_sample optional_coalesce_minimal.jiang 42
run_sample coalesce_value_minimal.jiang 42
run_sample coalesce_fallback_call_minimal.jiang 42
run_sample coalesce_return_minimal.jiang 42
run_next_sample coalesce_throw_minimal.jiang 42
run_sample coalesce_break_minimal.jiang 3
run_sample coalesce_continue_minimal.jiang 13
run_sample invalid_coalesce_return_value.jiang 1
run_sample optional_chain_member_minimal.jiang 42
run_sample optional_chain_index_minimal.jiang 40
run_sample optional_chain_nested_pure_base_minimal.jiang 42
run_sample optional_some_minimal.jiang 42
run_sample optional_some_pattern_minimal.jiang 42
run_sample optional_if_mutable_pattern_minimal.jiang 42
run_sample optional_while_is_pattern_minimal.jiang 6
run_sample optional_switch_pattern_minimal.jiang 42
run_sample optional_nested_array_minimal.jiang 42
run_sample size_of_minimal.jiang 8
run_sample align_of_minimal.jiang 17
run_executable_sample max_align_alloc_minimal.jiang 0
run_sample generic_decl_minimal.jiang 42
run_sample generic_func_call_minimal.jiang 42
run_sample generic_func_infer_minimal.jiang 42
run_sample generic_method_on_self_field_minimal.jiang 42
run_sample concept_generic_minimal.jiang 42
run_sample concept_method_minimal.jiang 42
run_sample where_amp_minimal.jiang 42
run_sample trait_inherit_minimal.jiang 42
run_known_stage1_gap trait_inherit_where_minimal.jiang 42
run_sample trait_multi_inherit_minimal.jiang 41
run_sample trait_diamond_inherit_minimal.jiang 42
run_sample trait_assoc_type_minimal.jiang 42
run_sample trait_assoc_type_bound_minimal.jiang 42
run_sample trait_assoc_type_inherit_minimal.jiang 42
run_sample trait_assoc_type_where_minimal.jiang 42
run_sample invalid_trait_assoc_ambiguous_binding.jiang 42
run_sample subscriptable_readonly_minimal.jiang 42
run_sample subscriptable_mutable_minimal.jiang 42
run_compile_fail invalid_subscriptable_write_readonly.jiang
run_known_stage1_gap extend_trait_inherit_minimal.jiang 63
run_sample extend_trait_assoc_type_minimal.jiang 42
run_sample builtin_concept_method_minimal.jiang 17
run_sample enum_concept_decl_minimal.jiang 42
run_compile_only extern_minimal.jiang
run_compile_only extern_call_minimal.jiang
run_compile_only extern_global_minimal.jiang
run_compile_only extern_single_function_minimal.jiang
run_compile_only extern_single_global_minimal.jiang
run_compile_only extern_cstring_minimal.jiang
run_compile_only cstring_local_minimal.jiang
run_sample generic_import_func_call_minimal.jiang 42
run_sample generic_import_func_infer_minimal.jiang 42
run_sample generic_struct_instantiation_minimal.jiang 42
run_sample mutable_generic_minimal.jiang 42
run_sample type_modifier_canonical_minimal.jiang 7
run_sample generic_import_struct_minimal.jiang 42
run_sample struct_minimal.jiang 42
run_sample fields_minimal.jiang 3
run_sample nested_fields_minimal.jiang 42
run_sample call_result_field_minimal.jiang 42
run_sample struct_init_minimal.jiang 42
run_executable_sample struct_init_overload_minimal.jiang 43
run_sample struct_init_with_defaults_minimal.jiang 18
run_sample struct_init_mixed_params_minimal.jiang 65
run_sample struct_init_branch_complete_minimal.jiang 42
run_sample struct_init_failable_minimal.jiang 42
run_sample struct_init_failable_return_success_minimal.jiang 43
run_sample struct_named_init_minimal.jiang 64
run_sample struct_init_mutable_default_override_minimal.jiang 19
run_sample alias_import_type_minimal.jiang 42
run_sample public_alias_type_minimal.jiang 42
run_sample public_import_type_minimal.jiang 42
run_sample struct_constructor_sugar_minimal.jiang 42
run_sample struct_literal_with_init_minimal.jiang 42
run_sample struct_optional_field_minimal.jiang 42
run_sample struct_optional_field_omitted_minimal.jiang 18
run_sample struct_init_optional_omitted_minimal.jiang 1
run_sample struct_init_optional_assign_minimal.jiang 42
run_executable_sample struct_new_constructor_minimal.jiang 42
run_executable_sample struct_new_literal_with_init_minimal.jiang 42
run_executable_sample deinit_minimal.jiang 42
run_sample else_if_minimal.jiang 20
run_sample multi_file_struct_return_minimal.jiang 42
run_sample multi_file_struct_minimal.jiang 42
run_sample namespaced_struct_import_minimal.jiang 42
run_sample namespaced_struct_return_minimal.jiang 42
run_sample struct_enum_field_shorthand_minimal.jiang 42
run_sample multi_file_enum_field_shorthand_minimal.jiang 1
run_sample namespaced_enum_field_shorthand_minimal.jiang 1
run_sample struct_union_field_shorthand_minimal.jiang 42
run_sample struct_instance_method_minimal.jiang 42
run_sample extend_struct_minimal.jiang 42
run_sample extend_trait_minimal.jiang 42
run_sample struct_instance_method_with_args_minimal.jiang 42
run_executable_sample struct_instance_method_pointer_base_minimal.jiang 42
run_sample struct_static_method_minimal.jiang 42
run_sample struct_method_calls_method_minimal.jiang 42
run_sample struct_nested_field_method_minimal.jiang 42
run_sample struct_static_calls_static_minimal.jiang 42
run_sample private_method_called_by_public_method_minimal.jiang 42
run_sample public_import_instance_method_minimal.jiang 42
run_sample public_import_static_method_minimal.jiang 42
run_sample enum_instance_method_minimal.jiang 42
run_sample enum_static_method_minimal.jiang 42
run_sample union_concept_decl_minimal.jiang 42
run_sample union_instance_method_minimal.jiang 42
run_sample union_static_method_minimal.jiang 42
run_known_stage1_gap assert_minimal.jiang 42
run_known_stage1_gap print_minimal.jiang 42
run_sample tuple_value_minimal.jiang 42
run_sample tuple_return_minimal.jiang 42
run_sample tuple_infer_minimal.jiang 42
run_known_stage1_gap tuple_destructure_minimal.jiang 42
run_known_stage1_gap tuple_destructure_infer_minimal.jiang 42
run_known_stage1_gap tuple_destructure_mutable_infer_minimal.jiang 42
run_known_stage1_gap tuple_destructure_return_minimal.jiang 42
run_known_stage1_gap tuple_destructure_global_minimal.jiang 42
run_known_stage1_gap unary_tuple_local_decl_minimal.jiang 42
run_known_stage1_gap unary_tuple_infer_local_decl_minimal.jiang 42
run_known_stage1_gap unary_tuple_global_decl_minimal.jiang 42
run_sample unary_tuple_return_minimal.jiang 42
run_sample array_minimal.jiang 42
run_sample array_assign_minimal.jiang 10
run_known_stage1_gap array_repeat_init_minimal.jiang 6
run_sample nested_array_minimal.jiang 42
run_sample infer_array_length_minimal.jiang 42
run_sample uint8_array_string_minimal.jiang 98
run_sample infer_uint8_array_string_minimal.jiang 98
run_sample struct_array_field_minimal.jiang 98
run_sample multi_file_struct_array_minimal.jiang 98
run_sample namespaced_struct_array_minimal.jiang 98
run_sample multi_file_pointer_minimal.jiang 42
run_sample namespaced_pointer_minimal.jiang 42
run_sample slice_index_minimal.jiang 0
run_sample slice_assign_minimal.jiang 0
run_sample slice_length_minimal.jiang 3
run_sample slice_return_length_minimal.jiang 3
run_sample string_slice_condition_after_branch_minimal.jiang 42
run_sample multi_file_slice_return_minimal.jiang 3
run_sample multi_file_slice_index_minimal.jiang 42
run_sample namespaced_slice_return_minimal.jiang 3
run_sample namespaced_slice_index_minimal.jiang 42
run_sample array_to_slice_local_minimal.jiang 42
run_sample array_to_slice_assign_minimal.jiang 42
run_sample array_to_slice_arg_minimal.jiang 42
run_sample array_to_slice_return_minimal.jiang 2
run_sample typed_array_constructor_minimal.jiang 42
run_sample typed_array_constructor_infer_minimal.jiang 42
run_sample empty_tuple_return_minimal.jiang 0
run_sample empty_tuple_bare_return_minimal.jiang 0
run_sample for_item_array_minimal.jiang 42
run_known_stage1_gap for_mutable_binding_minimal.jiang 42
run_known_stage1_gap for_indexed_minimal.jiang 40
run_known_stage1_gap for_indexed_typed_minimal.jiang 40
run_sample for_tuple_binding_minimal.jiang 42
run_known_stage1_gap for_tuple_binding_typed_minimal.jiang 42
run_known_stage1_gap for_indexed_tuple_binding_minimal.jiang 42
run_known_stage1_gap for_indexed_mutable_tuple_binding_minimal.jiang 42
run_known_stage1_gap union_tuple_bind_minimal.jiang 42
run_known_stage1_gap union_tuple_switch_mutable_binding_minimal.jiang 42
run_known_stage1_gap union_tuple_if_shorthand_pattern_minimal.jiang 42
run_known_stage1_gap union_tuple_if_mutable_shorthand_pattern_minimal.jiang 42
run_sample union_minimal.jiang 42
run_sample union_default_field_minimal.jiang 42
run_sample union_shorthand_minimal.jiang 42
run_known_stage1_gap union_implicit_tag_minimal.jiang 42
run_known_stage1_gap union_grouped_variant_minimal.jiang 42
run_known_stage1_gap union_payload_comprehensive_minimal.jiang 42
run_known_stage1_gap generic_union_minimal.jiang 42
run_known_stage1_gap generic_union_fn_payload_minimal.jiang 42
run_known_stage1_gap union_bind_minimal.jiang 42
run_sample union_if_pattern_minimal.jiang 42
run_sample union_if_mutable_binding_minimal.jiang 42
run_sample union_if_shorthand_pattern_minimal.jiang 42
run_known_stage1_gap union_pattern_expected_type_minimal.jiang 42
run_sample union_switch_mutable_binding_minimal.jiang 42
run_known_stage1_gap union_switch_shorthand_pattern_minimal.jiang 42
run_compile_fail invalid_tuple_index_non_literal.jiang
run_compile_fail invalid_tuple_index_out_of_range.jiang
run_compile_fail invalid_array_length.jiang
run_compile_fail invalid_array_assign_length.jiang
run_compile_fail invalid_array_arg_length.jiang
run_compile_fail invalid_array_return_length.jiang
run_compile_fail invalid_infer_optional_null.jiang
run_compile_fail invalid_optional_null_non_optional.jiang
run_compile_fail invalid_optional_coalesce_non_optional.jiang
run_compile_fail invalid_optional_coalesce_impure_left.jiang
run_compile_fail invalid_coalesce_break_outside_loop.jiang
run_compile_fail invalid_coalesce_continue_outside_loop.jiang
run_compile_fail invalid_coalesce_return_value_type.jiang
run_next_compile_fail invalid_coalesce_throw_outside_errorable.jiang
run_next_compile_fail invalid_coalesce_throw_type_mismatch.jiang
run_compile_fail invalid_coalesce_non_optional_exit.jiang
run_compile_fail invalid_coalesce_exit_in_call_arg.jiang
run_compile_fail invalid_optional_no_narrow_then_null_branch.jiang
run_compile_fail invalid_optional_chain_impure_base.jiang
run_compile_fail invalid_optional_chain_impure_member_base.jiang
run_compile_fail invalid_optional_legacy_dot_some_pattern.jiang
run_compile_fail invalid_optional_legacy_option_some_pattern.jiang
run_compile_fail invalid_optional_pattern_label.jiang
run_compile_fail invalid_optional_pattern_non_optional.jiang
run_compile_fail invalid_optional_switch_non_exhaustive.jiang
run_compile_fail invalid_optional_coalesce_fallback_type.jiang
run_compile_fail invalid_optional_compare_non_null.jiang
run_compile_fail invalid_struct_init_self_escape.jiang
run_compile_fail invalid_struct_init_positional_after_labeled.jiang
run_compile_fail invalid_default_struct_init_positional.jiang
run_compile_fail invalid_call_arg.jiang
run_compile_fail invalid_param_default_value.jiang
run_compile_fail invalid_global_initializer_type.jiang
run_compile_fail invalid_infer_array_length_missing_init.jiang
run_compile_fail invalid_infer_global_missing_init.jiang
run_compile_fail invalid_typed_array_constructor_length.jiang
run_compile_fail invalid_implicit_numeric_arithmetic.jiang
run_compile_fail invalid_implicit_numeric_compare.jiang
run_compile_fail invalid_character_literal_empty.jiang
run_compile_fail invalid_character_literal_multi.jiang
run_compile_fail invalid_character_literal_escape.jiang
run_compile_fail invalid_character_literal_unicode.jiang
run_compile_fail invalid_character_single_quote_empty.jiang
run_compile_fail invalid_character_single_quote_multi.jiang
run_compile_fail invalid_uint8_char_literal_out_of_range.jiang
run_compile_fail invalid_optional_uint8_char_literal_out_of_range.jiang
run_compile_fail invalid_uint8_string_literal_implicit.jiang
run_compile_fail invalid_int_string_literal_implicit.jiang
run_compile_fail invalid_uint8_string_literal_compare.jiang
run_compile_fail invalid_double_mod_minimal.jiang
run_compile_fail invalid_double_to_float_assign_minimal.jiang
run_compile_fail invalid_float_to_int_assign_minimal.jiang
run_compile_fail invalid_as_target_mutable_type.jiang
run_compile_fail invalid_as_target_mutable_type_from_mutable_source.jiang
run_compile_fail invalid_uint8_double_add_minimal.jiang
run_compile_fail invalid_index_target.jiang
run_compile_fail invalid_index_type.jiang
run_compile_fail invalid_address_of_expr.jiang
run_compile_fail invalid_deref_non_pointer.jiang
run_compile_fail invalid_free_non_pointer.jiang
run_compile_fail invalid_new_non_construct_expr.jiang
run_compile_fail invalid_pointer_offset_requires_many_pointer.jiang
run_compile_fail invalid_many_pointer_assign_immutable.jiang
run_compile_fail invalid_use_after_free.jiang
run_compile_fail invalid_import_private_function.jiang
run_compile_fail invalid_import_private_type.jiang
run_compile_fail invalid_duplicate_import_alias.jiang
run_compile_fail invalid_import_alias_function_conflict.jiang
run_compile_fail invalid_import_alias_type_conflict.jiang
run_compile_fail invalid_import_alias_missing_member.jiang
run_compile_fail invalid_concept_unknown.jiang
run_compile_fail invalid_concept_unsatisfied.jiang
run_compile_fail invalid_concept_method_missing.jiang
run_compile_fail invalid_extend_unknown_type.jiang
run_compile_fail invalid_extend_init.jiang
run_compile_fail invalid_concept_not_declared.jiang
run_compile_fail invalid_enum_concept_method_missing.jiang
run_compile_fail invalid_union_concept_not_declared.jiang
run_compile_fail invalid_where_unknown_param.jiang
run_compile_fail invalid_import_cycle_a.jiang
run_compile_fail invalid_import_private_instance_method.jiang
run_compile_fail invalid_import_private_static_method.jiang
run_compile_fail invalid_import_private_trait_method.jiang
run_compile_fail invalid_extern_nested.jiang
run_compile_fail invalid_extern_with_body.jiang
run_compile_fail invalid_extern_non_function.jiang
run_compile_fail invalid_extern_global_init.jiang
run_compile_fail invalid_extern_single_with_body.jiang
run_compile_fail invalid_public_alias_private_function.jiang
run_compile_fail invalid_public_alias_private_type.jiang
run_compile_fail invalid_transitive_import_type.jiang
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
run_compile_fail invalid_pointer_mutable_pointee_arg.jiang
run_compile_fail invalid_slice_mutable_arg.jiang
run_compile_fail invalid_call_non_function.jiang
run_compile_fail invalid_fn_pointer_signature_mismatch.jiang
run_compile_fail invalid_throw_outside_errorable.jiang
run_compile_fail invalid_throw_type_mismatch.jiang
run_compile_fail invalid_errorable_return_mismatch.jiang
run_compile_fail invalid_errorable_plain_use.jiang
run_compile_fail invalid_errorable_catch_fallback_non_errorable.jiang
run_compile_fail invalid_errorable_catch_fallback_type_mismatch.jiang
run_compile_fail invalid_errorable_catch_handler_non_errorable.jiang
run_compile_fail invalid_throw_expr.jiang
run_compile_fail invalid_try_without_catch.jiang
run_compile_fail invalid_try_duplicate_catch.jiang
run_compile_fail invalid_try_uncaught_error_type.jiang
run_compile_fail invalid_try_expr_context.jiang
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
run_compile_fail invalid_struct_init_duplicate_overload.jiang
run_compile_fail invalid_struct_init_immutable_reassign.jiang
run_compile_fail invalid_struct_init_missing_field.jiang
run_compile_fail invalid_struct_init_read_before_init.jiang
run_compile_fail invalid_struct_init_return_value.jiang
run_compile_fail invalid_struct_init_null_non_failable.jiang
run_compile_fail invalid_struct_init_failable_assign_non_optional.jiang
run_compile_fail invalid_struct_named_init_default_call.jiang
run_compile_fail invalid_struct_named_init_duplicate.jiang
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
run_compile_fail invalid_union_pattern_eq_bind.jiang
run_compile_fail invalid_union_switch_non_exhaustive.jiang
run_compile_fail invalid_union_tuple_bind_non_tuple.jiang
run_compile_fail invalid_union_tuple_bind_arity.jiang
run_compile_fail invalid_infer_shorthand_without_expected.jiang
run_compile_fail invalid_ternary_aggregate_result.jiang
run_compile_fail invalid_ternary_branch_type.jiang
run_compile_fail invalid_ternary_condition_type.jiang
run_known_stage1_nonzero_gap panic_minimal.jiang

echo "stage1 compiler tests passed"
