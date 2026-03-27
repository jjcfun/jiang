#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
MANIFEST="$PROJECT_ROOT/examples/build_demo/jiang.build"
OUTPUT_BIN="$PROJECT_ROOT/examples/build_demo/build/build_demo"
ALT_BIN="$PROJECT_ROOT/examples/build_demo/build/build_demo_alt"

echo "--- Build System Smoke: build ---"
"$BUILD_DIR/jiangc" build --manifest "$MANIFEST"
test -f "$OUTPUT_BIN"

echo "--- Build System Smoke: run ---"
"$BUILD_DIR/jiangc" run --manifest "$MANIFEST"

echo "--- Build System Smoke: build alt target ---"
"$BUILD_DIR/jiangc" build --manifest "$MANIFEST" --target alt
test -f "$ALT_BIN"

echo "--- Build System Smoke: run alt target ---"
"$BUILD_DIR/jiangc" run --manifest "$MANIFEST" --target alt

echo "--- Build System Smoke: test ---"
"$BUILD_DIR/jiangc" test --manifest "$MANIFEST"

echo "--- Build System Smoke: test alt target ---"
"$BUILD_DIR/jiangc" test --manifest "$MANIFEST" --target alt
