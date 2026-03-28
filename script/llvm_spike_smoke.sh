#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
LLVM_SAMPLE="$PROJECT_ROOT/tests/llvm_spike_sample.jiang"
LLVM_IR="$BUILD_DIR/llvm_spike_sample.ll"
LLVM_OBJ="$BUILD_DIR/llvm_spike_sample.o"
LLVM_STD_SAMPLE="$PROJECT_ROOT/tests/llvm_std_spike_sample.jiang"
LLVM_STD_IR="$BUILD_DIR/llvm_std_spike_sample.ll"
LLVM_STD_OBJ="$BUILD_DIR/llvm_std_spike_sample.o"
LLVM_FS_SAMPLE="$PROJECT_ROOT/tests/llvm_fs_spike_sample.jiang"
LLVM_FS_IR="$BUILD_DIR/llvm_fs_spike_sample.ll"
LLVM_FS_OBJ="$BUILD_DIR/llvm_fs_spike_sample.o"
LLVM_MULTI_SAMPLE="$PROJECT_ROOT/tests/llvm_multi_module_sample.jiang"
LLVM_MULTI_IR="$BUILD_DIR/llvm_multi_module_sample.ll"
LLVM_MULTI_OBJ="$BUILD_DIR/llvm_multi_module_sample.o"

mkdir -p "$BUILD_DIR"
cd "$PROJECT_ROOT"

"$BUILD_DIR/jiangc" --emit-llvm "$LLVM_SAMPLE"

test -f "$LLVM_IR"
rg -q "source_filename = \"jiang\"" "$LLVM_IR"
rg -q 'define i64 @twice' "$LLVM_IR"
rg -q 'define i32 @main' "$LLVM_IR"
rg -q 'add i64' "$LLVM_IR"
rg -q 'icmp slt i64' "$LLVM_IR"
rg -q 'call i64 @twice' "$LLVM_IR"
rg -q 'br i1' "$LLVM_IR"

clang -Wno-override-module -c -x ir "$LLVM_IR" -o "$LLVM_OBJ"
lli "$LLVM_IR"

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" --emit-llvm "$LLVM_STD_SAMPLE"

test -f "$LLVM_STD_IR"
rg -q "source_filename = \"jiang\"" "$LLVM_STD_IR"
rg -q '%Slice_uint8_t = type \{ ptr, i64 \}' "$LLVM_STD_IR"
rg -q 'declare i32 @printf' "$LLVM_STD_IR"
rg -q 'call i32 \(ptr, \.\.\.\) @printf' "$LLVM_STD_IR"
rg -q 'call void @abort' "$LLVM_STD_IR"

clang -Wno-override-module -c -x ir "$LLVM_STD_IR" -o "$LLVM_STD_OBJ"
lli "$LLVM_STD_IR"

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" --emit-llvm "$LLVM_FS_SAMPLE"

test -f "$LLVM_FS_IR"
rg -q "__jiang_rt_file_exists" "$LLVM_FS_IR"
rg -q "__jiang_rt_read_file" "$LLVM_FS_IR"
rg -q "declare ptr @fopen" "$LLVM_FS_IR"
rg -q "declare i64 @fread" "$LLVM_FS_IR"

clang -Wno-override-module -c -x ir "$LLVM_FS_IR" -o "$LLVM_FS_OBJ"
xcrun clang "$LLVM_FS_OBJ" -o "$BUILD_DIR/llvm_fs_spike_sample_bin"

"$BUILD_DIR/jiangc" --emit-llvm "$LLVM_MULTI_SAMPLE"

test -f "$LLVM_MULTI_IR"
rg -q 'define i64 @add' "$LLVM_MULTI_IR"
rg -q 'call i64 @add' "$LLVM_MULTI_IR"

clang -Wno-override-module -c -x ir "$LLVM_MULTI_IR" -o "$LLVM_MULTI_OBJ"
lli "$LLVM_MULTI_IR"

echo "llvm spike smoke passed"
