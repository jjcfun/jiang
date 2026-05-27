#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE1_BIN="${STAGE1_BIN:-$HOME/.jiang/stage1/bin/jiangc}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
LLVM_CONFIG="${LLVM_CONFIG:-/opt/homebrew/opt/llvm@21/bin/llvm-config}"
STAGE2_BIN="${STAGE2_BIN:-$BUILD_DIR/jiangc}"

mkdir -p "$BUILD_DIR"
cd "$ROOT_DIR"

clang_bin="$("$LLVM_CONFIG" --bindir)/clang"
stage2_ll="$BUILD_DIR/jiangc.stage2.ll"

printf '== build stage2: emit llvm ==\n'
"$STAGE1_BIN" --emit-llvm src/jiangc.jiang >"$stage2_ll"

printf '== build stage2: link %s ==\n' "$STAGE2_BIN"
"$clang_bin" "$stage2_ll" -o "$STAGE2_BIN" \
  $("$LLVM_CONFIG" --ldflags) \
  $("$LLVM_CONFIG" --libs all) \
  $("$LLVM_CONFIG" --system-libs)

test -x "$STAGE2_BIN"
printf 'OK %s\n' "$STAGE2_BIN"
