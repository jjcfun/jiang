#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
SEED_PATH="${1:-$BUILD_DIR/stage2_seed.c}"
TMP_PATH="$BUILD_DIR/stage2_seed.refresh.c"

bash "$PROJECT_ROOT/script/build_stage2.sh"
"$BUILD_DIR/stage2c" "compiler/entries/compiler.jiang" > "$TMP_PATH"
mkdir -p "$(dirname "$SEED_PATH")"
mv "$TMP_PATH" "$SEED_PATH"

echo "refreshed $SEED_PATH"
