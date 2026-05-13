#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE1_BIN="${STAGE1_BIN:-/Users/jjc/project/jiang/jiang/build/stage1/jiangc}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
SMOKE_BUILD_DIR="$BUILD_DIR/smoke/stage1"

mkdir -p "$SMOKE_BUILD_DIR"
cd "$ROOT_DIR"

status=0
for source in tests/smoke/*.jiang; do
  name="$(basename "$source" .jiang)"
  output="$SMOKE_BUILD_DIR/$name"

  printf '\n== %s ==\n' "$source"
  if "$STAGE1_BIN" -o "$output" "$source"; then
    echo "OK"
  else
    code=$?
    echo "FAIL:$code"
    status=1
  fi
done

exit "$status"
