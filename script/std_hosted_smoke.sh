#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
JIANGC="${JIANGC:-$BUILD_DIR/bin/jiangc}"
PROFILE="$ROOT_DIR/test/profile/std-hosted-smoke.txt"

case "$(uname -s):$(uname -m)" in
  Darwin:arm64|Darwin:aarch64|Linux:x86_64) ;;
  *)
    echo "std hosted smoke requires a supported macOS arm64 or Linux x86_64 host" >&2
    exit 2
    ;;
esac
if [ ! -x "$JIANGC" ]; then
  echo "missing hosted Jiang compiler: $JIANGC" >&2
  exit 2
fi

test_timeout=0
if command -v timeout >/dev/null 2>&1 || command -v gtimeout >/dev/null 2>&1; then
  test_timeout=120
fi

cd "$ROOT_DIR"
TEST_ROOT=test/lang \
TEST_LIST="$PROFILE" \
TEST_TIMEOUT="$test_timeout" \
JIANGC="$JIANGC" \
bash "$ROOT_DIR/script/test.sh"

echo "OK std hosted smoke"
