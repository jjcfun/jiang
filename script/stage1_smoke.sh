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
STAGE1_LL="${TMPDIR:-/tmp}/jiangc.ll"
STAGE1_BIN="${TMPDIR:-/tmp}/jiangc"
INPUT="${TMPDIR:-/tmp}/jiang-stage1-smoke-input.jiang"
OUTPUT="${TMPDIR:-/tmp}/jiang-stage1-smoke-out"

if [ ! -x ./build/stage0c ]; then
  bash script/build_stage0.sh >/dev/null
fi

./build/stage0c --emit-llvm compiler/jiangc.jiang > "$STAGE1_LL"

declare -a llvm_ldflags=()
declare -a llvm_libs=()
declare -a llvm_system_libs=()
read -r -a llvm_ldflags <<< "$("$LLVM_CONFIG" --ldflags)"
read -r -a llvm_libs <<< "$("$LLVM_CONFIG" --libs core analysis target native nativecodegen)"
system_libs="$("$LLVM_CONFIG" --system-libs)"
if [ -n "$system_libs" ]; then
  read -r -a llvm_system_libs <<< "$system_libs"
fi

if [ -x "$(dirname "$LLVM_CONFIG")/clang" ]; then
  CC_BIN="$(dirname "$LLVM_CONFIG")/clang"
else
  CC_BIN="${CC:-cc}"
fi

if [ "${#llvm_system_libs[@]}" -eq 0 ]; then
  "$CC_BIN" "$STAGE1_LL" -o "$STAGE1_BIN" \
    "${llvm_ldflags[@]}" \
    "${llvm_libs[@]}" \
    -lc++
else
  "$CC_BIN" "$STAGE1_LL" -o "$STAGE1_BIN" \
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

run_sample() {
  local sample="$1"
  local expected="$2"
  local exe="${TMPDIR:-/tmp}/jiang-stage1-${sample%.jiang}"
  "$STAGE1_BIN" -o "$exe" "tests/samples/$sample"
  set +e
  "$exe"
  local status=$?
  set -e
  if [ "$status" -ne "$expected" ]; then
    echo "stage1 smoke failed: $sample expected exit $expected, got $status"
    exit 1
  fi
}

run_sample minimal.jiang 42
run_sample locals_minimal.jiang 42
run_sample assign_minimal.jiang 5
run_sample binary_ops_minimal.jiang 1
run_sample logical_ops_minimal.jiang 140
run_sample optional_minimal.jiang 42
run_sample optional_coalesce_minimal.jiang 42
run_sample if_expr_minimal.jiang 42
run_sample while_minimal.jiang 10
run_sample for_range_minimal.jiang 8
run_sample array_minimal.jiang 42
run_sample tuple_value_minimal.jiang 42
run_sample enum_minimal.jiang 2
run_sample union_minimal.jiang 42
run_sample union_if_pattern_minimal.jiang 42
run_sample optional_some_pattern_minimal.jiang 42
run_sample struct_minimal.jiang 42
run_sample fields_minimal.jiang 3
run_sample struct_init_minimal.jiang 42

echo "stage1 smoke ok"
