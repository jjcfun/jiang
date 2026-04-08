#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_C="$BUILD_DIR/stage2_minimal.c"
OUT_O="$BUILD_DIR/stage2_minimal.o"
LARGE_C="$BUILD_DIR/stage2_large_minimal.c"
LARGE_O="$BUILD_DIR/stage2_large_minimal.o"

bash "$PROJECT_ROOT/script/build_stage2.sh"

"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/minimal.jiang" > "$OUT_C"

rg -q '^#include <stdint.h>$' "$OUT_C"
rg -q 'long long add\(long long a, long long b\);' "$OUT_C"
rg -q 'int main\(void\);' "$OUT_C"
rg -q 'return \(a \+ b\);' "$OUT_C"
rg -q 'return add\(40, 2\);' "$OUT_C"

cc -x c -std=c99 -Wall -Wextra -Werror -c "$OUT_C" -o "$OUT_O"

"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/large_minimal.jiang" > "$LARGE_C"
rg -q 'long long add15\(long long x\);' "$LARGE_C"
rg -q 'return add15' "$LARGE_C"
cc -x c -std=c99 -Wall -Wextra -Werror -c "$LARGE_C" -o "$LARGE_O"

echo "stage2 emit-c smoke passed"
