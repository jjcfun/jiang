#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
LLVM_CONFIG="${LLVM_CONFIG:-/opt/homebrew/opt/llvm@21/bin/llvm-config}"
STAGE2_BIN="${STAGE2_BIN:-$BUILD_DIR/jiangc}"
NEXT_BIN="${NEXT_BIN:-$BUILD_DIR/jiangc.next}"

mkdir -p "$BUILD_DIR"
cd "$ROOT_DIR"

clang_bin="$("$LLVM_CONFIG" --bindir)/clang"
next_ll="$BUILD_DIR/jiangc.next.ll"

test -x "$STAGE2_BIN"

printf '== build next: emit llvm with %s ==\n' "$STAGE2_BIN"
"$STAGE2_BIN" --emit-llvm -o "$next_ll" src/jiangc.jiang

printf '== build next: link %s ==\n' "$NEXT_BIN"
"$clang_bin" "$next_ll" -o "$NEXT_BIN" \
  $("$LLVM_CONFIG" --ldflags) \
  $("$LLVM_CONFIG" --libs all) \
  $("$LLVM_CONFIG" --system-libs)

test -x "$NEXT_BIN"
printf 'OK %s\n' "$NEXT_BIN"
