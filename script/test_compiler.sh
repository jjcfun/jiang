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

if [ "${SKIP_STAGE0_BUILD:-0}" != "1" ]; then
  "$PROJECT_ROOT/script/build_stage0.sh" >/dev/null
fi

run_compiler_sample() {
  sample="$1"
  expected="$2"
  ir="$BUILD_DIR/compiler_${sample%.jiang}.ll"
  printf 'compiler/%s ... ' "$sample"
  "$BUILD_DIR/jiangc" --emit-llvm "$COMPILER_SAMPLES_DIR/$sample" > "$ir"
  set +e
  "$LLI" "$ir"
  status=$?
  set -e
  if [ "$status" -ne "$expected" ]; then
    echo "failed"
    echo "error: compiler/$sample exited $status, expected $expected" >&2
    exit 1
  fi
  echo "ok"
}

run_compiler_compile_fail() {
  sample="$1"
  printf 'compiler/%s ... ' "$sample"
  set +e
  /bin/sh -c '"$1" --emit-llvm "$2" >/dev/null 2>&1' sh "$BUILD_DIR/jiangc" "$COMPILER_SAMPLES_DIR/$sample" >/dev/null 2>&1
  status=$?
  set -e
  if [ "$status" -eq 0 ]; then
    echo "failed"
    echo "error: compiler/$sample unexpectedly compiled" >&2
    exit 1
  fi
  echo "ok"
}

run_named_sample() {
  sample="$1"
  case "$sample" in
    invalid_array_list_set_immutable_type_arg.jiang)
      run_compiler_compile_fail "$sample"
      ;;
    *.jiang)
      run_compiler_sample "$sample" 0
      ;;
    *)
      echo "error: expected a .jiang compiler sample, got '$sample'" >&2
      exit 1
      ;;
  esac
}

run_all_compiler_samples() {
  run_compiler_sample arena_minimal.jiang 0
  run_compiler_sample list_minimal.jiang 0
  run_compiler_sample hash_minimal.jiang 0

  run_compiler_sample imported_type_field_minimal.jiang 0
  run_compiler_sample string_util_minimal.jiang 0
  run_compiler_sample subscriptable_inferred_get_minimal.jiang 0
  run_compiler_sample subscriptable_inferred_set_minimal.jiang 0
  run_compiler_sample token_minimal.jiang 0
  run_compiler_sample interner_minimal.jiang 0
  run_compiler_sample ast_minimal.jiang 0
  run_compiler_sample lexer_minimal.jiang 0
  run_compiler_sample parser_minimal.jiang 0
  run_compiler_sample resolve_minimal.jiang 0
  run_compiler_sample module_graph_minimal.jiang 0
  run_compiler_compile_fail invalid_array_list_set_immutable_type_arg.jiang
}

if [ "$#" -gt 0 ]; then
  for sample in "$@"; do
    run_named_sample "$sample"
  done
else
  run_all_compiler_samples
fi

echo "compiler samples passed"
