#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

LLVM_CONFIG="${LLVM_CONFIG:-}"
if [ -z "$LLVM_CONFIG" ] && [ -n "${JIANG_LLVM_ROOT:-}" ]; then
  LLVM_CONFIG="$JIANG_LLVM_ROOT/bin/llvm-config"
fi
LLVM_CONFIG="${LLVM_CONFIG:-llvm-config}"

STAGE1_OBJ="${TMPDIR:-/tmp}/jiangc.o"
STAGE1_BIN="${TMPDIR:-/tmp}/jiangc"
INPUT="${TMPDIR:-/tmp}/jiang-stage1-smoke-input.jiang"
OUTPUT="${TMPDIR:-/tmp}/jiang-stage1-smoke-out"

if [ ! -x ./build/stage0c ]; then
  bash script/build_stage0.sh >/dev/null
fi

./build/stage0c --emit-obj -o "$STAGE1_OBJ" compiler/jiangc.jiang

declare -a llvm_ldflags=()
declare -a llvm_libs=()
declare -a llvm_system_libs=()
read -r -a llvm_ldflags <<< "$("$LLVM_CONFIG" --ldflags)"
read -r -a llvm_libs <<< "$("$LLVM_CONFIG" --libs core analysis target native nativecodegen)"
system_libs="$("$LLVM_CONFIG" --system-libs)"
if [ -n "$system_libs" ]; then
  read -r -a llvm_system_libs <<< "$system_libs"
fi

if [ "${#llvm_system_libs[@]}" -eq 0 ]; then
  cc "$STAGE1_OBJ" -o "$STAGE1_BIN" \
    "${llvm_ldflags[@]}" \
    "${llvm_libs[@]}" \
    -lc++
else
  cc "$STAGE1_OBJ" -o "$STAGE1_BIN" \
    "${llvm_ldflags[@]}" \
    "${llvm_libs[@]}" \
    "${llvm_system_libs[@]}" \
    -lc++
fi

printf 'Int main() { return 42; }\n' > "$INPUT"
"$STAGE1_BIN" -o "$OUTPUT" "$INPUT"

set +e
"$OUTPUT"
status=$?
set -e

if [ "$status" -ne 42 ]; then
  echo "stage1 smoke failed: expected exit 42, got $status"
  exit 1
fi

echo "stage1 smoke ok"
