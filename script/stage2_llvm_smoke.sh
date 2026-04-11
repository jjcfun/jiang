#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_DIR="$BUILD_DIR/stage2_llvm_smoke"
LLVM_CONFIG="${LLVM_CONFIG:-llvm-config}"
LLVM_BINDIR="$($LLVM_CONFIG --bindir)"
LLVM_CLANG="$LLVM_BINDIR/clang"
LLVM_LLI="$LLVM_BINDIR/lli"

mkdir -p "$OUT_DIR"
cd "$PROJECT_ROOT"

bash "$PROJECT_ROOT/script/build_stage2.sh"

MINIMAL_LL="$OUT_DIR/minimal.ll"
MINIMAL_O="$OUT_DIR/minimal.o"
WHILE_LL="$OUT_DIR/while_minimal.ll"
WHILE_O="$OUT_DIR/while_minimal.o"
UINT8_LL="$OUT_DIR/uint8_minimal.ll"
UINT8_O="$OUT_DIR/uint8_minimal.o"
UINT8_SLICE_LL="$OUT_DIR/uint8_slice_minimal.ll"
UINT8_SLICE_O="$OUT_DIR/uint8_slice_minimal.o"
SLICE_LENGTH_LL="$OUT_DIR/slice_length_minimal.ll"
SLICE_LENGTH_O="$OUT_DIR/slice_length_minimal.o"
SLICE_INDEX_LL="$OUT_DIR/slice_index_minimal.ll"
SLICE_INDEX_O="$OUT_DIR/slice_index_minimal.o"
SLICE_ASSIGN_LL="$OUT_DIR/slice_assign_minimal.ll"
SLICE_ASSIGN_O="$OUT_DIR/slice_assign_minimal.o"
SLICE_RETURN_LENGTH_LL="$OUT_DIR/slice_return_length_minimal.ll"
SLICE_RETURN_LENGTH_O="$OUT_DIR/slice_return_length_minimal.o"
ARRAY_LL="$OUT_DIR/array_minimal.ll"
ARRAY_O="$OUT_DIR/array_minimal.o"
ARRAY_ASSIGN_LL="$OUT_DIR/array_assign_minimal.ll"
ARRAY_ASSIGN_O="$OUT_DIR/array_assign_minimal.o"
UINT8_ARRAY_STRING_LL="$OUT_DIR/uint8_array_string_minimal.ll"
UINT8_ARRAY_STRING_O="$OUT_DIR/uint8_array_string_minimal.o"
NESTED_ARRAY_LL="$OUT_DIR/nested_array_minimal.ll"
NESTED_ARRAY_O="$OUT_DIR/nested_array_minimal.o"
STRUCT_ARRAY_FIELD_LL="$OUT_DIR/struct_array_field_minimal.ll"
STRUCT_ARRAY_FIELD_O="$OUT_DIR/struct_array_field_minimal.o"
ENUM_LL="$OUT_DIR/enum_minimal.ll"
ENUM_O="$OUT_DIR/enum_minimal.o"
STRUCT_LL="$OUT_DIR/struct_minimal.ll"
STRUCT_O="$OUT_DIR/struct_minimal.o"
FIELDS_LL="$OUT_DIR/fields_minimal.ll"
FIELDS_O="$OUT_DIR/fields_minimal.o"
CALL_FIELD_LL="$OUT_DIR/call_result_field_minimal.ll"
CALL_FIELD_O="$OUT_DIR/call_result_field_minimal.o"
NESTED_FIELDS_LL="$OUT_DIR/nested_fields_minimal.ll"
NESTED_FIELDS_O="$OUT_DIR/nested_fields_minimal.o"
MULTI_FILE_LL="$OUT_DIR/multi_file_minimal.ll"
MULTI_FILE_O="$OUT_DIR/multi_file_minimal.o"
NAMESPACED_LL="$OUT_DIR/namespaced_import_minimal.ll"
NAMESPACED_O="$OUT_DIR/namespaced_import_minimal.o"
MULTI_STRUCT_LL="$OUT_DIR/multi_file_struct_minimal.ll"
MULTI_STRUCT_O="$OUT_DIR/multi_file_struct_minimal.o"
NS_STRUCT_LL="$OUT_DIR/namespaced_struct_import_minimal.ll"
NS_STRUCT_O="$OUT_DIR/namespaced_struct_import_minimal.o"
MULTI_ENUM_LL="$OUT_DIR/multi_file_enum_minimal.ll"
MULTI_ENUM_O="$OUT_DIR/multi_file_enum_minimal.o"
NS_ENUM_LL="$OUT_DIR/namespaced_enum_import_minimal.ll"
NS_ENUM_O="$OUT_DIR/namespaced_enum_import_minimal.o"
POINTER_LL="$OUT_DIR/pointer_minimal.ll"
POINTER_O="$OUT_DIR/pointer_minimal.o"
ARRAY_TO_SLICE_ARG_LL="$OUT_DIR/array_to_slice_arg_minimal.ll"
ARRAY_TO_SLICE_ARG_O="$OUT_DIR/array_to_slice_arg_minimal.o"
ARRAY_TO_SLICE_LOCAL_LL="$OUT_DIR/array_to_slice_local_minimal.ll"
ARRAY_TO_SLICE_LOCAL_O="$OUT_DIR/array_to_slice_local_minimal.o"
ARRAY_TO_SLICE_ASSIGN_LL="$OUT_DIR/array_to_slice_assign_minimal.ll"
ARRAY_TO_SLICE_ASSIGN_O="$OUT_DIR/array_to_slice_assign_minimal.o"
ARRAY_TO_SLICE_RETURN_LL="$OUT_DIR/array_to_slice_return_minimal.ll"
ARRAY_TO_SLICE_RETURN_O="$OUT_DIR/array_to_slice_return_minimal.o"

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/minimal.jiang" > "$MINIMAL_LL"
rg -q '^define i64 @add\(i64 %0, i64 %1\)' "$MINIMAL_LL"
rg -q '^define i32 @main\(\)' "$MINIMAL_LL"
rg -q 'add i64' "$MINIMAL_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MINIMAL_LL" -o "$MINIMAL_O"
set +e
"$LLVM_LLI" "$MINIMAL_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/while_minimal.jiang" > "$WHILE_LL"
rg -q '^define i32 @main\(\)' "$WHILE_LL"
rg -q 'br i1' "$WHILE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$WHILE_LL" -o "$WHILE_O"
set +e
"$LLVM_LLI" "$WHILE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 10 ]]; then
    echo "stage2 llvm smoke expected while_minimal exit code 10, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/uint8_minimal.jiang" > "$UINT8_LL"
rg -q '^define i8 @id\(i8 %0\)' "$UINT8_LL"
rg -q '^define i32 @main\(\)' "$UINT8_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UINT8_LL" -o "$UINT8_O"

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/uint8_slice_minimal.jiang" > "$UINT8_SLICE_LL"
rg -q '^%Slice_uint8_t = type \{ ptr, i64 \}' "$UINT8_SLICE_LL"
rg -q '^define %Slice_uint8_t @id\(%Slice_uint8_t %0\)' "$UINT8_SLICE_LL"
rg -q '^define i32 @main\(\)' "$UINT8_SLICE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UINT8_SLICE_LL" -o "$UINT8_SLICE_O"

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/slice_length_minimal.jiang" > "$SLICE_LENGTH_LL"
rg -q '^define i64 @len\(%Slice_uint8_t %0\)' "$SLICE_LENGTH_LL"
rg -q 'extractvalue %Slice_uint8_t .*?, 1' "$SLICE_LENGTH_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$SLICE_LENGTH_LL" -o "$SLICE_LENGTH_O"
set +e
"$LLVM_LLI" "$SLICE_LENGTH_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 3 ]]; then
    echo "stage2 llvm smoke expected slice_length_minimal exit code 3, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/slice_index_minimal.jiang" > "$SLICE_INDEX_LL"
rg -q '^define i8 @pick\(%Slice_uint8_t %0\)' "$SLICE_INDEX_LL"
rg -q 'getelementptr i8, ptr' "$SLICE_INDEX_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$SLICE_INDEX_LL" -o "$SLICE_INDEX_O"

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/slice_assign_minimal.jiang" > "$SLICE_ASSIGN_LL"
rg -q '^define void @poke\(%Slice_uint8_t %0, i8 %1\)' "$SLICE_ASSIGN_LL"
rg -q 'store i8 %' "$SLICE_ASSIGN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$SLICE_ASSIGN_LL" -o "$SLICE_ASSIGN_O"

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/slice_return_length_minimal.jiang" > "$SLICE_RETURN_LENGTH_LL"
rg -q '^define %Slice_uint8_t @id\(%Slice_uint8_t %0\)' "$SLICE_RETURN_LENGTH_LL"
rg -q 'extractvalue %Slice_uint8_t .*?, 1' "$SLICE_RETURN_LENGTH_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$SLICE_RETURN_LENGTH_LL" -o "$SLICE_RETURN_LENGTH_O"
set +e
"$LLVM_LLI" "$SLICE_RETURN_LENGTH_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 3 ]]; then
    echo "stage2 llvm smoke expected slice_return_length_minimal exit code 3, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/array_minimal.jiang" > "$ARRAY_LL"
rg -q '^define i32 @main\(\)' "$ARRAY_LL"
rg -q '\[5 x i64\]' "$ARRAY_LL"
rg -q 'store \[5 x i64\] \[i64 1, i64 2, i64 3, i64 4, i64 42\]' "$ARRAY_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ARRAY_LL" -o "$ARRAY_O"
set +e
"$LLVM_LLI" "$ARRAY_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected array_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/array_assign_minimal.jiang" > "$ARRAY_ASSIGN_LL"
rg -q 'getelementptr \[3 x i64\], ptr' "$ARRAY_ASSIGN_LL"
rg -q 'store i64 10' "$ARRAY_ASSIGN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ARRAY_ASSIGN_LL" -o "$ARRAY_ASSIGN_O"
set +e
"$LLVM_LLI" "$ARRAY_ASSIGN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 10 ]]; then
    echo "stage2 llvm smoke expected array_assign_minimal exit code 10, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/uint8_array_string_minimal.jiang" > "$UINT8_ARRAY_STRING_LL"
rg -q '\[3 x i8\]' "$UINT8_ARRAY_STRING_LL"
rg -q 'store \[3 x i8\] c"abc"' "$UINT8_ARRAY_STRING_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$UINT8_ARRAY_STRING_LL" -o "$UINT8_ARRAY_STRING_O"
set +e
"$LLVM_LLI" "$UINT8_ARRAY_STRING_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 98 ]]; then
    echo "stage2 llvm smoke expected uint8_array_string_minimal exit code 98, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/nested_array_minimal.jiang" > "$NESTED_ARRAY_LL"
rg -q '\[2 x \[3 x i64\]\]' "$NESTED_ARRAY_LL"
rg -q 'getelementptr \[2 x \[3 x i64\]\], ptr' "$NESTED_ARRAY_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NESTED_ARRAY_LL" -o "$NESTED_ARRAY_O"
set +e
"$LLVM_LLI" "$NESTED_ARRAY_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected nested_array_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/struct_array_field_minimal.jiang" > "$STRUCT_ARRAY_FIELD_LL"
rg -q '^%Buffer = type \{ \[3 x i8\] \}' "$STRUCT_ARRAY_FIELD_LL"
rg -q 'getelementptr %Buffer, ptr' "$STRUCT_ARRAY_FIELD_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$STRUCT_ARRAY_FIELD_LL" -o "$STRUCT_ARRAY_FIELD_O"
set +e
"$LLVM_LLI" "$STRUCT_ARRAY_FIELD_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 98 ]]; then
    echo "stage2 llvm smoke expected struct_array_field_minimal exit code 98, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/enum_minimal.jiang" > "$ENUM_LL"
rg -q '^define i64 @code\(i64 %0\)' "$ENUM_LL"
rg -q 'icmp eq i64' "$ENUM_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ENUM_LL" -o "$ENUM_O"
set +e
"$LLVM_LLI" "$ENUM_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 2 ]]; then
    echo "stage2 llvm smoke expected enum_minimal exit code 2, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/struct_minimal.jiang" > "$STRUCT_LL"
rg -q '^%Pair = type \{ i64, i64 \}' "$STRUCT_LL"
rg -q '^define i64 @sum_pair\(%Pair %0\)' "$STRUCT_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$STRUCT_LL" -o "$STRUCT_O"
set +e
"$LLVM_LLI" "$STRUCT_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected struct_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/fields_minimal.jiang" > "$FIELDS_LL"
rg -q 'extractvalue %Pair' "$FIELDS_LL"
rg -q 'getelementptr %Pair, ptr' "$FIELDS_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$FIELDS_LL" -o "$FIELDS_O"
set +e
"$LLVM_LLI" "$FIELDS_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 3 ]]; then
    echo "stage2 llvm smoke expected fields_minimal exit code 3, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/call_result_field_minimal.jiang" > "$CALL_FIELD_LL"
rg -q '^define %Box @make_box\(\)' "$CALL_FIELD_LL"
rg -q 'extractvalue %Box' "$CALL_FIELD_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$CALL_FIELD_LL" -o "$CALL_FIELD_O"
set +e
"$LLVM_LLI" "$CALL_FIELD_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected call_result_field_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/nested_fields_minimal.jiang" > "$NESTED_FIELDS_LL"
rg -q '^%Box = type \{ %Point \}' "$NESTED_FIELDS_LL"
rg -q 'extractvalue %Point' "$NESTED_FIELDS_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NESTED_FIELDS_LL" -o "$NESTED_FIELDS_O"
set +e
"$LLVM_LLI" "$NESTED_FIELDS_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected nested_fields_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/multi_file_minimal.jiang" > "$MULTI_FILE_LL"
rg -q '^define i64 @helper_add\(i64 %0, i64 %1\)' "$MULTI_FILE_LL"
rg -q 'call i64 @helper_add' "$MULTI_FILE_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MULTI_FILE_LL" -o "$MULTI_FILE_O"
set +e
"$LLVM_LLI" "$MULTI_FILE_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected multi_file_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/namespaced_import_minimal.jiang" > "$NAMESPACED_LL"
rg -q '^define i64 @helper_add\(i64 %0, i64 %1\)' "$NAMESPACED_LL"
rg -q 'call i64 @helper_add' "$NAMESPACED_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NAMESPACED_LL" -o "$NAMESPACED_O"
set +e
"$LLVM_LLI" "$NAMESPACED_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected namespaced_import_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/multi_file_struct_minimal.jiang" > "$MULTI_STRUCT_LL"
rg -q '^%Pair = type \{ i64, i64 \}' "$MULTI_STRUCT_LL"
rg -q 'extractvalue %Pair' "$MULTI_STRUCT_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MULTI_STRUCT_LL" -o "$MULTI_STRUCT_O"
set +e
"$LLVM_LLI" "$MULTI_STRUCT_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected multi_file_struct_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/namespaced_struct_import_minimal.jiang" > "$NS_STRUCT_LL"
rg -q '^%Pair = type \{ i64, i64 \}' "$NS_STRUCT_LL"
rg -q 'extractvalue %Pair' "$NS_STRUCT_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NS_STRUCT_LL" -o "$NS_STRUCT_O"
set +e
"$LLVM_LLI" "$NS_STRUCT_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected namespaced_struct_import_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/multi_file_enum_minimal.jiang" > "$MULTI_ENUM_LL"
rg -q 'icmp eq i64' "$MULTI_ENUM_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$MULTI_ENUM_LL" -o "$MULTI_ENUM_O"
set +e
"$LLVM_LLI" "$MULTI_ENUM_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 llvm smoke expected multi_file_enum_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/namespaced_enum_import_minimal.jiang" > "$NS_ENUM_LL"
rg -q 'icmp eq i64' "$NS_ENUM_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$NS_ENUM_LL" -o "$NS_ENUM_O"
set +e
"$LLVM_LLI" "$NS_ENUM_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 1 ]]; then
    echo "stage2 llvm smoke expected namespaced_enum_import_minimal exit code 1, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/pointer_minimal.jiang" > "$POINTER_LL"
rg -q '^define i64 @read\(ptr %0\)' "$POINTER_LL"
rg -q 'store ptr %' "$POINTER_LL"
rg -q 'load i64, ptr %' "$POINTER_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$POINTER_LL" -o "$POINTER_O"
set +e
"$LLVM_LLI" "$POINTER_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 42 ]]; then
    echo "stage2 llvm smoke expected pointer_minimal exit code 42, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/array_to_slice_arg_minimal.jiang" > "$ARRAY_TO_SLICE_ARG_LL"
rg -q '^define i8 @pick\(%Slice_uint8_t %0\)' "$ARRAY_TO_SLICE_ARG_LL"
rg -q 'insertvalue %Slice_uint8_t' "$ARRAY_TO_SLICE_ARG_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ARRAY_TO_SLICE_ARG_LL" -o "$ARRAY_TO_SLICE_ARG_O"
set +e
"$LLVM_LLI" "$ARRAY_TO_SLICE_ARG_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 98 ]]; then
    echo "stage2 llvm smoke expected array_to_slice_arg_minimal exit code 98, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/array_to_slice_local_minimal.jiang" > "$ARRAY_TO_SLICE_LOCAL_LL"
rg -q 'insertvalue %Slice_uint8_t' "$ARRAY_TO_SLICE_LOCAL_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ARRAY_TO_SLICE_LOCAL_LL" -o "$ARRAY_TO_SLICE_LOCAL_O"
set +e
"$LLVM_LLI" "$ARRAY_TO_SLICE_LOCAL_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 99 ]]; then
    echo "stage2 llvm smoke expected array_to_slice_local_minimal exit code 99, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/array_to_slice_assign_minimal.jiang" > "$ARRAY_TO_SLICE_ASSIGN_LL"
rg -q 'store %Slice_uint8_t' "$ARRAY_TO_SLICE_ASSIGN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ARRAY_TO_SLICE_ASSIGN_LL" -o "$ARRAY_TO_SLICE_ASSIGN_O"
set +e
"$LLVM_LLI" "$ARRAY_TO_SLICE_ASSIGN_LL"
STATUS=$?
set -e
if [[ $STATUS -ne 97 ]]; then
    echo "stage2 llvm smoke expected array_to_slice_assign_minimal exit code 97, got $STATUS" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" --emit-llvm "$PROJECT_ROOT/compiler/tests/samples/array_to_slice_return_minimal.jiang" > "$ARRAY_TO_SLICE_RETURN_LL"
rg -q '^define %Slice_uint8_t @expose\(\)' "$ARRAY_TO_SLICE_RETURN_LL"
"$LLVM_CLANG" -Wno-override-module -x ir -c "$ARRAY_TO_SLICE_RETURN_LL" -o "$ARRAY_TO_SLICE_RETURN_O"

echo "stage2 llvm smoke passed"
