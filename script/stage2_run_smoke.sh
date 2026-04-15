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

compile_stage2_entry "compiler/tests/samples/empty_tuple_return_minimal.jiang" "empty_tuple_return_minimal"
set +e
"$OUT_DIR/empty_tuple_return_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 0 ]]; then
    echo "stage2 run smoke expected empty_tuple_return_minimal exit code 0, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/empty_tuple_bare_return_minimal.jiang" "empty_tuple_bare_return_minimal"
set +e
"$OUT_DIR/empty_tuple_bare_return_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 0 ]]; then
    echo "stage2 run smoke expected empty_tuple_bare_return_minimal exit code 0, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/break_continue_minimal.jiang" "break_continue_minimal"
set +e
"$OUT_DIR/break_continue_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 8 ]]; then
    echo "stage2 run smoke expected break_continue_minimal exit code 8, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/for_range_minimal.jiang" "for_range_minimal"
set +e
"$OUT_DIR/for_range_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 8 ]]; then
    echo "stage2 run smoke expected for_range_minimal exit code 8, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/for_infer_range_minimal.jiang" "for_infer_range_minimal"
set +e
"$OUT_DIR/for_infer_range_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 5 ]]; then
    echo "stage2 run smoke expected for_infer_range_minimal exit code 5, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/for_item_array_minimal.jiang" "for_item_array_minimal"
set +e
"$OUT_DIR/for_item_array_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected for_item_array_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/for_tuple_binding_minimal.jiang" "for_tuple_binding_minimal"
set +e
"$OUT_DIR/for_tuple_binding_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected for_tuple_binding_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/for_tuple_binding_typed_minimal.jiang" "for_tuple_binding_typed_minimal"
set +e
"$OUT_DIR/for_tuple_binding_typed_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected for_tuple_binding_typed_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/switch_enum_minimal.jiang" "switch_enum_minimal"
set +e
"$OUT_DIR/switch_enum_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected switch_enum_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/enum_shorthand_minimal.jiang" "enum_shorthand_minimal"
set +e
"$OUT_DIR/enum_shorthand_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected enum_shorthand_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/enum_shorthand_arg_minimal.jiang" "enum_shorthand_arg_minimal"
set +e
"$OUT_DIR/enum_shorthand_arg_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected enum_shorthand_arg_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/enum_switch_shorthand_minimal.jiang" "enum_switch_shorthand_minimal"
set +e
"$OUT_DIR/enum_switch_shorthand_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected enum_switch_shorthand_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/struct_enum_field_shorthand_minimal.jiang" "struct_enum_field_shorthand_minimal"
set +e
"$OUT_DIR/struct_enum_field_shorthand_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected struct_enum_field_shorthand_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/multi_file_enum_shorthand_minimal.jiang" "multi_file_enum_shorthand_minimal"
set +e
"$OUT_DIR/multi_file_enum_shorthand_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 run smoke expected multi_file_enum_shorthand_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/multi_file_enum_shorthand_arg_minimal.jiang" "multi_file_enum_shorthand_arg_minimal"
set +e
"$OUT_DIR/multi_file_enum_shorthand_arg_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 run smoke expected multi_file_enum_shorthand_arg_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/multi_file_enum_field_shorthand_minimal.jiang" "multi_file_enum_field_shorthand_minimal"
set +e
"$OUT_DIR/multi_file_enum_field_shorthand_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 run smoke expected multi_file_enum_field_shorthand_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/namespaced_enum_shorthand_minimal.jiang" "namespaced_enum_shorthand_minimal"
set +e
"$OUT_DIR/namespaced_enum_shorthand_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 run smoke expected namespaced_enum_shorthand_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/namespaced_enum_field_shorthand_minimal.jiang" "namespaced_enum_field_shorthand_minimal"
set +e
"$OUT_DIR/namespaced_enum_field_shorthand_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 run smoke expected namespaced_enum_field_shorthand_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/union_minimal.jiang" "union_minimal"
set +e
"$OUT_DIR/union_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected union_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/union_shorthand_minimal.jiang" "union_shorthand_minimal"
set +e
"$OUT_DIR/union_shorthand_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected union_shorthand_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/union_bind_minimal.jiang" "union_bind_minimal"
set +e
"$OUT_DIR/union_bind_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected union_bind_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/union_tuple_bind_minimal.jiang" "union_tuple_bind_minimal"
set +e
"$OUT_DIR/union_tuple_bind_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected union_tuple_bind_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/union_if_pattern_minimal.jiang" "union_if_pattern_minimal"
set +e
"$OUT_DIR/union_if_pattern_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected union_if_pattern_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/union_if_shorthand_pattern_minimal.jiang" "union_if_shorthand_pattern_minimal"
set +e
"$OUT_DIR/union_if_shorthand_pattern_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected union_if_shorthand_pattern_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/union_tuple_if_shorthand_pattern_minimal.jiang" "union_tuple_if_shorthand_pattern_minimal"
set +e
"$OUT_DIR/union_tuple_if_shorthand_pattern_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected union_tuple_if_shorthand_pattern_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/union_switch_shorthand_pattern_minimal.jiang" "union_switch_shorthand_pattern_minimal"
set +e
"$OUT_DIR/union_switch_shorthand_pattern_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected union_switch_shorthand_pattern_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/struct_union_field_shorthand_minimal.jiang" "struct_union_field_shorthand_minimal"
set +e
"$OUT_DIR/struct_union_field_shorthand_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected struct_union_field_shorthand_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/slice_length_minimal.jiang" "slice_length_minimal"
set +e
"$OUT_DIR/slice_length_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 3 ]]; then
    echo "stage2 run smoke expected slice_length_minimal exit code 3, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/slice_return_length_minimal.jiang" "slice_return_length_minimal"
set +e
"$OUT_DIR/slice_return_length_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 3 ]]; then
    echo "stage2 run smoke expected slice_return_length_minimal exit code 3, got $STATUS" >&2
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

compile_stage2_entry "compiler/tests/samples/multi_file_struct_return_minimal.jiang" "multi_file_struct_return_minimal"
set +e
"$OUT_DIR/multi_file_struct_return_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected multi_file_struct_return_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/namespaced_struct_return_minimal.jiang" "namespaced_struct_return_minimal"
set +e
"$OUT_DIR/namespaced_struct_return_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected namespaced_struct_return_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/multi_file_struct_array_minimal.jiang" "multi_file_struct_array_minimal"
set +e
"$OUT_DIR/multi_file_struct_array_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 98 ]]; then
    echo "stage2 run smoke expected multi_file_struct_array_minimal exit code 98, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/namespaced_struct_array_minimal.jiang" "namespaced_struct_array_minimal"
set +e
"$OUT_DIR/namespaced_struct_array_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 98 ]]; then
    echo "stage2 run smoke expected namespaced_struct_array_minimal exit code 98, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/multi_file_slice_return_minimal.jiang" "multi_file_slice_return_minimal"
set +e
"$OUT_DIR/multi_file_slice_return_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 3 ]]; then
    echo "stage2 run smoke expected multi_file_slice_return_minimal exit code 3, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/namespaced_slice_return_minimal.jiang" "namespaced_slice_return_minimal"
set +e
"$OUT_DIR/namespaced_slice_return_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 3 ]]; then
    echo "stage2 run smoke expected namespaced_slice_return_minimal exit code 3, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/multi_file_slice_index_minimal.jiang" "multi_file_slice_index_minimal"
set +e
"$OUT_DIR/multi_file_slice_index_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected multi_file_slice_index_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/namespaced_slice_index_minimal.jiang" "namespaced_slice_index_minimal"
set +e
"$OUT_DIR/namespaced_slice_index_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected namespaced_slice_index_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/array_minimal.jiang" "array_minimal"
set +e
"$OUT_DIR/array_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected array_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/array_assign_minimal.jiang" "array_assign_minimal"
set +e
"$OUT_DIR/array_assign_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 10 ]]; then
    echo "stage2 run smoke expected array_assign_minimal exit code 10, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/global_minimal.jiang" "global_minimal"
set +e
"$OUT_DIR/global_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected global_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/unary_tuple_global_decl_minimal.jiang" "unary_tuple_global_decl_minimal"
set +e
"$OUT_DIR/unary_tuple_global_decl_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected unary_tuple_global_decl_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/infer_local_minimal.jiang" "infer_local_minimal"
set +e
"$OUT_DIR/infer_local_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected infer_local_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/unary_tuple_local_decl_minimal.jiang" "unary_tuple_local_decl_minimal"
set +e
"$OUT_DIR/unary_tuple_local_decl_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected unary_tuple_local_decl_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/unary_tuple_infer_local_decl_minimal.jiang" "unary_tuple_infer_local_decl_minimal"
set +e
"$OUT_DIR/unary_tuple_infer_local_decl_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected unary_tuple_infer_local_decl_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/tuple_destructure_minimal.jiang" "tuple_destructure_minimal"
set +e
"$OUT_DIR/tuple_destructure_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected tuple_destructure_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/tuple_destructure_infer_minimal.jiang" "tuple_destructure_infer_minimal"
set +e
"$OUT_DIR/tuple_destructure_infer_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected tuple_destructure_infer_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/tuple_destructure_global_minimal.jiang" "tuple_destructure_global_minimal"
set +e
"$OUT_DIR/tuple_destructure_global_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected tuple_destructure_global_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/tuple_destructure_return_minimal.jiang" "tuple_destructure_return_minimal"
set +e
"$OUT_DIR/tuple_destructure_return_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected tuple_destructure_return_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/tuple_value_minimal.jiang" "tuple_value_minimal"
set +e
"$OUT_DIR/tuple_value_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected tuple_value_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/tuple_return_minimal.jiang" "tuple_return_minimal"
set +e
"$OUT_DIR/tuple_return_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected tuple_return_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/tuple_infer_minimal.jiang" "tuple_infer_minimal"
set +e
"$OUT_DIR/tuple_infer_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected tuple_infer_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/infer_global_minimal.jiang" "infer_global_minimal"
set +e
"$OUT_DIR/infer_global_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected infer_global_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/unary_tuple_return_minimal.jiang" "unary_tuple_return_minimal"
set +e
"$OUT_DIR/unary_tuple_return_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected unary_tuple_return_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/uint8_array_string_minimal.jiang" "uint8_array_string_minimal"
set +e
"$OUT_DIR/uint8_array_string_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 98 ]]; then
    echo "stage2 run smoke expected uint8_array_string_minimal exit code 98, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/nested_array_minimal.jiang" "nested_array_minimal"
set +e
"$OUT_DIR/nested_array_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected nested_array_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/struct_array_field_minimal.jiang" "struct_array_field_minimal"
set +e
"$OUT_DIR/struct_array_field_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 98 ]]; then
    echo "stage2 run smoke expected struct_array_field_minimal exit code 98, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/public_import_function_minimal.jiang" "public_import_function_minimal"
set +e
"$OUT_DIR/public_import_function_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected public_import_function_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/public_import_type_minimal.jiang" "public_import_type_minimal"
set +e
"$OUT_DIR/public_import_type_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected public_import_type_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/alias_import_function_minimal.jiang" "alias_import_function_minimal"
set +e
"$OUT_DIR/alias_import_function_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected alias_import_function_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/public_alias_function_minimal.jiang" "public_alias_function_minimal"
set +e
"$OUT_DIR/public_alias_function_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected public_alias_function_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/alias_import_type_minimal.jiang" "alias_import_type_minimal"
set +e
"$OUT_DIR/alias_import_type_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected alias_import_type_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/public_alias_type_minimal.jiang" "public_alias_type_minimal"
set +e
"$OUT_DIR/public_alias_type_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected public_alias_type_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/namespaced_import_minimal.jiang" "namespaced_import_minimal"
set +e
"$OUT_DIR/namespaced_import_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected namespaced_import_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/namespaced_struct_import_minimal.jiang" "namespaced_struct_import_minimal"
set +e
"$OUT_DIR/namespaced_struct_import_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected namespaced_struct_import_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/namespaced_enum_import_minimal.jiang" "namespaced_enum_import_minimal"
set +e
"$OUT_DIR/namespaced_enum_import_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 run smoke expected namespaced_enum_import_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/enum_value_minimal.jiang" "enum_value_minimal"
set +e
"$OUT_DIR/enum_value_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected enum_value_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/multi_file_enum_value_minimal.jiang" "multi_file_enum_value_minimal"
set +e
"$OUT_DIR/multi_file_enum_value_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 7 ]]; then
    echo "stage2 run smoke expected multi_file_enum_value_minimal exit code 7, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/namespaced_enum_value_minimal.jiang" "namespaced_enum_value_minimal"
set +e
"$OUT_DIR/namespaced_enum_value_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 3 ]]; then
    echo "stage2 run smoke expected namespaced_enum_value_minimal exit code 3, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/pointer_minimal.jiang" "pointer_minimal"
set +e
"$OUT_DIR/pointer_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected pointer_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/multi_file_pointer_minimal.jiang" "multi_file_pointer_minimal"
set +e
"$OUT_DIR/multi_file_pointer_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected multi_file_pointer_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/namespaced_pointer_minimal.jiang" "namespaced_pointer_minimal"
set +e
"$OUT_DIR/namespaced_pointer_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected namespaced_pointer_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/array_to_slice_arg_minimal.jiang" "array_to_slice_arg_minimal"
set +e
"$OUT_DIR/array_to_slice_arg_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected array_to_slice_arg_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/array_to_slice_local_minimal.jiang" "array_to_slice_local_minimal"
set +e
"$OUT_DIR/array_to_slice_local_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected array_to_slice_local_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

compile_stage2_entry "compiler/tests/samples/array_to_slice_assign_minimal.jiang" "array_to_slice_assign_minimal"
set +e
"$OUT_DIR/array_to_slice_assign_minimal"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 run smoke expected array_to_slice_assign_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

echo "stage2 run smoke passed"
