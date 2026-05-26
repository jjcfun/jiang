#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE1_BIN="${STAGE1_BIN:-$HOME/.jiang/stage1/bin/jiangc}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
SMOKE_BUILD_DIR="$BUILD_DIR/smoke/stage2"
LLVM_CONFIG="${LLVM_CONFIG:-/opt/homebrew/opt/llvm@21/bin/llvm-config}"

mkdir -p "$SMOKE_BUILD_DIR"
cd "$ROOT_DIR"

status=0
compile_only_smoke() {
  case "$1" in
    compiler_entry_smoke|pipeline_smoke|pipeline_source_binding_smoke|pipeline_source_hir_smoke)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

llvm_runtime_smoke() {
  case "$1" in
    backend_llvm_smoke)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

link_and_run_llvm_smoke() {
  local ll_file="$1"
  local output="$2"
  local clang_bin

  clang_bin="$("$LLVM_CONFIG" --bindir)/clang"
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
  if llvm_runtime_smoke "$name"; then
    if "$STAGE1_BIN" --emit-llvm "$source" >"$output.ll" && link_and_run_llvm_smoke "$output.ll" "$output"; then
      echo "OK"
    else
      code=$?
      echo "FAIL:$code"
      status=1
    fi
  elif compile_only_smoke "$name"; then
    if "$STAGE1_BIN" --emit-llvm "$source" >"$output.ll"; then
      echo "OK"
    else
      code=$?
      echo "FAIL:$code"
      status=1
    fi
  elif "$STAGE1_BIN" -o "$output" "$source"; then
    echo "OK"
  else
    code=$?
    echo "FAIL:$code"
    status=1
  fi
done

exit "$status"
