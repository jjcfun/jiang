#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_LL="$BUILD_DIR/minimal.ll"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR"

"$BUILD_DIR/jiangc" --emit-llvm "$PROJECT_ROOT/tests/samples/minimal.jiang" > "$OUT_LL"

grep -q "define i64 @main()" "$OUT_LL"
grep -q "ret i64 42" "$OUT_LL"

echo "stage0 smoke passed"
