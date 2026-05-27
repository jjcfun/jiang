#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE1_BIN="${STAGE1_BIN:-$HOME/.jiang/stage1/bin/jiangc}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
SMOKE_BUILD_DIR="$BUILD_DIR/smoke/stage2_self_host"
LLVM_CONFIG="${LLVM_CONFIG:-/opt/homebrew/opt/llvm@21/bin/llvm-config}"

mkdir -p "$SMOKE_BUILD_DIR"
cd "$ROOT_DIR"

minimal_sample="$SMOKE_BUILD_DIR/minimal.jiang"
field_sample="$SMOKE_BUILD_DIR/field_projection.jiang"
printf 'Int main() { 0 }\n' >"$minimal_sample"
printf 'struct Pair { Int left; Int right; }\nInt get_left(Pair p) { p.left }\nInt main() { 0 }\n' >"$field_sample"

default_sources=(
  "$minimal_sample"
  "$field_sample"
  "test/lang/ownership/check/borrow_reference_basic.jiang"
  "test/lang/ownership/check/ownership_explicit_move.jiang"
)

clang_bin="$("$LLVM_CONFIG" --bindir)/clang"
compiler_ll="$SMOKE_BUILD_DIR/jiangc.stage2.ll"
compiler_bin="$SMOKE_BUILD_DIR/jiangc.stage2"

"$STAGE1_BIN" --emit-llvm src/jiangc.jiang >"$compiler_ll"
"$clang_bin" "$compiler_ll" -o "$compiler_bin" \
  $("$LLVM_CONFIG" --ldflags) \
  $("$LLVM_CONFIG" --libs all) \
  $("$LLVM_CONFIG" --system-libs)

if [[ -n "${STAGE2_SELF_HOST_SOURCES:-}" ]]; then
  read -r -a sources <<<"$STAGE2_SELF_HOST_SOURCES"
else
  sources=("${default_sources[@]}")
fi

for source in "${sources[@]}"; do
  name="$(basename "$source" .jiang)"
  output="$SMOKE_BUILD_DIR/$name.ll"
  printf '\n== stage2 self-host: %s ==\n' "$source"
  "$compiler_bin" --emit-llvm -o "$output" "$source"
  test -s "$output"
  echo "OK"
done

echo "OK"
