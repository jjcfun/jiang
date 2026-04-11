#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
HELP_OUT="$BUILD_DIR/stage2_help.out"
DEFAULT_OUT="$BUILD_DIR/stage2_default_emit.out"
EXPLICIT_OUT="$BUILD_DIR/stage2_explicit_emit.out"
HIR_OUT="$BUILD_DIR/stage2_bad_mode.out"

bash "$PROJECT_ROOT/script/build_stage2.sh"

if ! "$BUILD_DIR/stage2c" --help > "$HELP_OUT"; then
    echo "stage2 cli smoke: --help failed" >&2
    exit 1
fi

if [[ "$(<"$HELP_OUT")" != *"--emit-c"* ]]; then
    echo "stage2 cli smoke: help missing --emit-c" >&2
    exit 1
fi

if [[ "$(<"$HELP_OUT")" != *"--emit-llvm"* ]]; then
    echo "stage2 cli smoke: help missing --emit-llvm" >&2
    exit 1
fi

"$BUILD_DIR/stage2c" "$PROJECT_ROOT/compiler/tests/samples/minimal.jiang" > "$DEFAULT_OUT"
"$BUILD_DIR/stage2c" --emit-c "$PROJECT_ROOT/compiler/tests/samples/minimal.jiang" > "$EXPLICIT_OUT"

if ! cmp -s "$DEFAULT_OUT" "$EXPLICIT_OUT"; then
    echo "stage2 cli smoke: default emit-c output differs from explicit --emit-c" >&2
    exit 1
fi

set +e
"$BUILD_DIR/stage2c" --bad "$PROJECT_ROOT/compiler/tests/samples/minimal.jiang" > "$HIR_OUT" 2>&1
STATUS=$?
set -e

if [[ $STATUS -eq 0 ]]; then
    echo "stage2 cli smoke: invalid mode unexpectedly succeeded" >&2
    exit 1
fi

if [[ "$(<"$HIR_OUT")" != *"usage:"* ]]; then
    echo "stage2 cli smoke: invalid mode missing usage output" >&2
    exit 1
fi

echo "stage2 cli smoke passed"
