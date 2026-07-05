#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
JIANGC="${JIANGC:-$BUILD_DIR/bin/jiangc}"
SMOKE_BUILD_DIR="$BUILD_DIR/smoke/stage2"

source "$ROOT_DIR/script/llvm_env.sh"

mkdir -p "$SMOKE_BUILD_DIR"
cd "$ROOT_DIR"

status=0
slow_smokes="${JIANG_SLOW_SMOKE:-}"
link_and_run_llvm_smoke() {
  local ll_file="$1"
  local output="$2"
  local clang_bin

  clang_bin="$LLVM_CLANG"
  "$clang_bin" "$ll_file" -o "$output" \
    $("$LLVM_CONFIG" --link-static --ldflags) \
    $("$LLVM_CONFIG" --link-static --libs all) \
    $("$LLVM_CONFIG" --link-static --system-libs) \
    $(jiang_macos_sdkroot_link_args) \
    $(jiang_llvm_cxx_runtime_link_args)
  "$output"
}

for source in test/smoke/*.jiang; do
  name="$(basename "$source" .jiang)"
  output="$SMOKE_BUILD_DIR/$name"

  if [ "$name" = "lang_dylib_smoke" ] && [ "$slow_smokes" != "1" ]; then
    printf '\n== %s ==\n' "$source"
    echo "SKIP slow smoke; set JIANG_SLOW_SMOKE=1 to run"
    continue
  fi

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
