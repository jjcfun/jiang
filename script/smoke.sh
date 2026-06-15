#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
JIANGC="${JIANGC:-$BUILD_DIR/jiangc}"
SMOKE_BUILD_DIR="$BUILD_DIR/smoke/stage2"

source "$ROOT_DIR/script/llvm_env.sh"

mkdir -p "$SMOKE_BUILD_DIR"
cd "$ROOT_DIR"

status=0
link_and_run_llvm_smoke() {
  local ll_file="$1"
  local output="$2"
  local clang_bin

  clang_bin="$LLVM_CLANG"
  "$clang_bin" "$ll_file" -o "$output" \
    $("$LLVM_CONFIG" --ldflags) \
    $("$LLVM_CONFIG" --libs all) \
    $("$LLVM_CONFIG" --system-libs)
  "$output"
}

for source in test/smoke/*.jiang; do
  name="$(basename "$source" .jiang)"
  output="$SMOKE_BUILD_DIR/$name"

  printf '\n== %s ==\n' "$source"
  if "$JIANGC" --check "$source"; then
    echo "OK"
  else
    code=$?
    echo "FAIL:$code"
    status=1
  fi
done

exit "$status"
