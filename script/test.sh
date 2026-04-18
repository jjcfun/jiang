#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
SAMPLES_DIR="$PROJECT_ROOT/tests/samples"

if [[ -n "${LLVM_CONFIG:-}" ]]; then
  LLI="$(cd "$(dirname "$LLVM_CONFIG")" && pwd)/lli"
elif [[ -n "${JIANG_LLVM_ROOT:-}" ]]; then
  LLI="$JIANG_LLVM_ROOT/bin/lli"
else
  LLI="$(command -v lli)"
fi

if [[ -z "$LLI" || ! -x "$LLI" ]]; then
  echo "error: lli not found; set LLVM_CONFIG or JIANG_LLVM_ROOT to an LLVM 21.1.x toolchain" >&2
  exit 1
fi

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR"

run_sample() {
  local sample="$1"
  local expected="$2"
  local ir="$BUILD_DIR/${sample%.jiang}.ll"
  "$BUILD_DIR/jiangc" --emit-llvm "$SAMPLES_DIR/$sample" > "$ir"
  set +e
  "$LLI" "$ir"
  local status=$?
  set -e
  if [[ "$status" -ne "$expected" ]]; then
    echo "error: $sample exited $status, expected $expected" >&2
    exit 1
  fi
}

run_sample_nonzero() {
  local sample="$1"
  local ir="$BUILD_DIR/${sample%.jiang}.ll"
  "$BUILD_DIR/jiangc" --emit-llvm "$SAMPLES_DIR/$sample" > "$ir"
  set +e
  "$LLI" "$ir" >/dev/null 2>&1
  local status=$?
  set -e
  if [[ "$status" -eq 0 ]]; then
    echo "error: $sample unexpectedly exited 0" >&2
    exit 1
  fi
}

run_sample minimal.jiang 42
run_sample locals_minimal.jiang 42
run_sample assign_minimal.jiang 5
run_sample if_minimal.jiang 2
run_sample global_minimal.jiang 42
run_sample while_minimal.jiang 10
run_sample binary_ops_minimal.jiang 1
run_sample for_range_minimal.jiang 8
run_sample infer_global_minimal.jiang 42
run_sample infer_local_minimal.jiang 42
run_sample infer_mutable_local_minimal.jiang 42
run_sample assert_minimal.jiang 42
run_sample print_minimal.jiang 42
run_sample_nonzero panic_minimal.jiang

echo "stage0 batch A smoke passed"
