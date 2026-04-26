#!/bin/sh

set -eu

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
COMPILER_SAMPLES_DIR="$PROJECT_ROOT/tests/compiler"

if [ -n "${LLVM_CONFIG:-}" ]; then
  LLI="$(cd "$(dirname "$LLVM_CONFIG")" && pwd)/lli"
elif [ -n "${JIANG_LLVM_ROOT:-}" ]; then
  LLI="$JIANG_LLVM_ROOT/bin/lli"
else
  LLI="$(command -v lli || true)"
fi

if [ -z "$LLI" ] || [ ! -x "$LLI" ]; then
  echo "error: lli not found; set LLVM_CONFIG or JIANG_LLVM_ROOT to an LLVM 21.1.x toolchain" >&2
  exit 1
fi

"$PROJECT_ROOT/script/build_stage0.sh" >/dev/null

run_compiler_sample() {
  sample="$1"
  expected="$2"
  ir="$BUILD_DIR/compiler_${sample%.jiang}.ll"
  "$BUILD_DIR/jiangc" --emit-llvm "$COMPILER_SAMPLES_DIR/$sample" > "$ir"
  set +e
  "$LLI" "$ir"
  status=$?
  set -e
  if [ "$status" -ne "$expected" ]; then
    echo "error: compiler/$sample exited $status, expected $expected" >&2
    exit 1
  fi
}

run_compiler_compile_fail() {
  sample="$1"
  set +e
  /bin/sh -c '"$1" --emit-llvm "$2" >/dev/null 2>&1' sh "$BUILD_DIR/jiangc" "$COMPILER_SAMPLES_DIR/$sample" >/dev/null 2>&1
  status=$?
  set -e
  if [ "$status" -eq 0 ]; then
    echo "error: compiler/$sample unexpectedly compiled" >&2
    exit 1
  fi
}

run_compiler_sample hash_map_minimal.jiang 20
run_compiler_sample hash_map_collision_minimal.jiang 30
run_compiler_sample hash_map_remove_minimal.jiang 24
run_compiler_sample hash_map_deleted_reuse_minimal.jiang 52
run_compiler_sample hash_map_get_ptr_minimal.jiang 45
run_compiler_sample hash_map_get_or_put_minimal.jiang 18
run_compiler_sample hash_map_reserve_clear_minimal.jiang 42
run_compiler_sample hash_map_optional_value_has_minimal.jiang 42

run_compiler_sample array_list_minimal.jiang 60
run_compiler_sample array_list_pointer_minimal.jiang 48
run_compiler_sample array_list_capacity_minimal.jiang 42
run_compiler_sample arena_list_minimal.jiang 60
run_compiler_sample arena_list_capacity_minimal.jiang 45
run_compiler_sample subscriptable_inferred_get_minimal.jiang 42
run_compiler_sample subscriptable_inferred_set_minimal.jiang 42
run_compiler_compile_fail invalid_array_list_set_immutable_type_arg.jiang

echo "compiler samples passed"
