#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_LOG="$BUILD_DIR/stage2_invalid.log"
OUT_C="$BUILD_DIR/stage2_invalid.c"

bash "$PROJECT_ROOT/script/build_stage2.sh"

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_missing_semicolon.jiang" > "$OUT_LOG"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected non-zero exit" >&2
    exit 1
fi

if [[ "$(<"$OUT_LOG")" != *"error:"* ]]; then
    echo "stage2 error smoke missing diagnostic output" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/invalid_missing_semicolon.jiang" > "$OUT_C"
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 error smoke expected non-zero exit on redirected output" >&2
    exit 1
fi

if rg -q '^#include <stdint.h>$' "$OUT_C"; then
    echo "stage2 error smoke unexpectedly produced C output" >&2
    exit 1
fi

echo "stage2 error smoke passed"
