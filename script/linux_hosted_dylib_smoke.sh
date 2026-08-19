#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
JIANGC="${JIANGC:-$BUILD_DIR/bin/jiangc}"
PROFILE="$ROOT_DIR/test/profile/linux-hosted-dylib.txt"

source "$ROOT_DIR/script/llvm_env.sh"

if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
  echo "Linux hosted dylib smoke requires a Linux x86_64 host" >&2
  exit 2
fi
if [ ! -x "$JIANGC" ]; then
  echo "missing Linux Jiang compiler: $JIANGC" >&2
  exit 2
fi

cd "$ROOT_DIR"
TEST_ROOT=test/compiler \
TEST_LIST="$PROFILE" \
TEST_TIMEOUT=600 \
TEST_JOBS=1 \
JIANGC="$JIANGC" \
bash "$ROOT_DIR/script/test.sh"

echo "OK Linux hosted dylib smoke"
