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
rg -q 'insertvalue %Pair' "$FIELDS_LL"
rg -q 'extractvalue %Pair' "$FIELDS_LL"
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

echo "stage2 llvm smoke passed"
