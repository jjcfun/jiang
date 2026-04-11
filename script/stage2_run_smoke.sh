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
