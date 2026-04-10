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

echo "stage2 llvm smoke passed"
