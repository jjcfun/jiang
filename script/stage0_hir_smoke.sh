#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
GOLDEN="$PROJECT_ROOT/tests/stage0_hir.golden"
ACTUAL="$BUILD_DIR/stage0_hir.actual"

if [ ! -x "$BUILD_DIR/jiangc" ]; then
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake ..
    make
fi

: > "$ACTUAL"

dump_one() {
    local rel="$1"
    {
        echo "=== $rel ==="
        "$BUILD_DIR/jiangc" --dump-hir "$PROJECT_ROOT/$rel"
    } >> "$ACTUAL"
}

dump_one "tests/namespace_test.jiang"
dump_one "tests/struct_test.jiang"
dump_one "tests/loop_test.jiang"
dump_one "tests/switch_test.jiang"

diff -u "$GOLDEN" "$ACTUAL"
echo "stage0 hir smoke passed"
