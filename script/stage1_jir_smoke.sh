#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make

cd "$PROJECT_ROOT"
"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/jir.jiang"

OUTPUT="$("$BUILD_DIR/jir")"
printf '%s\n' "$OUTPUT"

EXPECTED="$PROJECT_ROOT/bootstrap/jir.golden"
if [ "$OUTPUT" != "$(cat "$EXPECTED")" ]; then
    echo "stage1 jir output differs from golden: $EXPECTED" >&2
    diff -u "$EXPECTED" <(printf '%s\n' "$OUTPUT") || true
    exit 1
fi
