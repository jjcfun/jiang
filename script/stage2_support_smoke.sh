#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BIN="$BUILD_DIR/stage2_support_smoke"

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/compiler/tests/samples/support_smoke.jiang"
mv "$BUILD_DIR/support_smoke" "$BIN"

OUTPUT="$("$BIN")"
if [[ "$OUTPUT" != *"stage2 support smoke passed"* ]]; then
    echo "stage2 support smoke output mismatch" >&2
    exit 1
fi

echo "stage2 support smoke passed"
