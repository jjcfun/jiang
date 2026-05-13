#!/bin/sh

set -eu

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
COMPILER_TESTS_DIR="$PROJECT_ROOT/tests/compiler"
STAGE0_BIN="${STAGE0_BIN:-$BUILD_DIR/stage0c}"
STAGE1_BIN="${STAGE1_BIN:-$HOME/.jiang/stage1/bin/jiangc}"

LLVM_CONFIG_BIN="${LLVM_CONFIG:-}"
if [ -z "$LLVM_CONFIG_BIN" ] && [ -n "${JIANG_LLVM_ROOT:-}" ]; then
  LLVM_CONFIG_BIN="$JIANG_LLVM_ROOT/bin/llvm-config"
fi
if [ -z "$LLVM_CONFIG_BIN" ]; then
  LLVM_CONFIG_BIN="$(command -v llvm-config || true)"
fi

if [ -n "$LLVM_CONFIG_BIN" ]; then
  LLVM_BIN_DIR="$(cd "$(dirname "$LLVM_CONFIG_BIN")" && pwd)"
  LLI="$LLVM_BIN_DIR/lli"
  if [ -x "$LLVM_BIN_DIR/clang" ]; then
    CLANG="$LLVM_BIN_DIR/clang"
  else
    CLANG="${CC:-cc}"
  fi
elif [ -n "${JIANG_LLVM_ROOT:-}" ]; then
  LLI="$JIANG_LLVM_ROOT/bin/lli"
  CLANG="${CC:-cc}"
else
  LLI="$(command -v lli || true)"
  CLANG="${CC:-cc}"
fi

if [ -z "$LLI" ] || [ ! -x "$LLI" ]; then
  echo "error: lli not found; set LLVM_CONFIG or JIANG_LLVM_ROOT to an LLVM 21.1.x toolchain" >&2
  exit 1
fi

mkdir -p "$BUILD_DIR"

if [ "${SKIP_STAGE0_BUILD:-0}" != "1" ]; then
  "$PROJECT_ROOT/script/build_stage0.sh" >/dev/null
fi

if [ ! -x "$STAGE1_BIN" ]; then
  "$PROJECT_ROOT/script/build_stage1.sh" >/dev/null
fi

run_compiler_test() {
  test_file="$1"
  expected="$2"
  ir="$BUILD_DIR/compiler_${test_file%.jiang}.ll"
  printf 'compiler/%s ... ' "$test_file"
  "$STAGE0_BIN" --emit-llvm "$COMPILER_TESTS_DIR/$test_file" > "$ir"
  set +e
  "$LLI" --jit-kind=mcjit "$ir"
  status=$?
  set -e
  if [ "$status" -ne "$expected" ]; then
    echo "failed"
    echo "error: compiler/$test_file exited $status, expected $expected" >&2
    exit 1
  fi
  echo "ok"
}

run_compiler_link_test() {
  test_file="$1"
  expected="$2"
  ir="$BUILD_DIR/compiler_${test_file%.jiang}.ll"
  exe="$BUILD_DIR/compiler_${test_file%.jiang}"
  printf 'compiler/%s link ... ' "$test_file"
  if [ -z "$LLVM_CONFIG_BIN" ] || [ ! -x "$LLVM_CONFIG_BIN" ]; then
    echo "failed"
    echo "error: llvm-config not found; set LLVM_CONFIG or JIANG_LLVM_ROOT for linked compiler tests" >&2
    exit 1
  fi
  "$STAGE0_BIN" --emit-llvm "$COMPILER_TESTS_DIR/$test_file" > "$ir"
  "$CLANG" "$ir" -o "$exe" \
    $("$LLVM_CONFIG_BIN" --ldflags) \
    $("$LLVM_CONFIG_BIN" --libs core analysis target native nativecodegen) \
    $("$LLVM_CONFIG_BIN" --system-libs) \
    -lc++
  set +e
  "$exe"
  status=$?
  set -e
  if [ "$status" -ne "$expected" ]; then
    echo "failed"
    echo "error: compiler/$test_file exited $status, expected $expected" >&2
    exit 1
  fi
  echo "ok"
}

run_compiler_compile_fail() {
  test_file="$1"
  printf 'compiler/%s ... ' "$test_file"
  set +e
  /bin/sh -c '"$1" --emit-llvm "$2" >/dev/null 2>&1' sh "$STAGE0_BIN" "$COMPILER_TESTS_DIR/$test_file" >/dev/null 2>&1
  status=$?
  set -e
  if [ "$status" -eq 0 ]; then
    echo "failed"
    echo "error: compiler/$test_file unexpectedly compiled" >&2
    exit 1
  fi
  echo "ok"
}

run_compiler_compile_only() {
  test_file="$1"
  run_compiler_compile_only_with "$test_file" "$STAGE0_BIN"
}

run_compiler_compile_only_stage1() {
  test_file="$1"
  run_compiler_compile_only_with "$test_file" "$STAGE1_BIN"
}

run_compiler_compile_only_with() {
  test_file="$1"
  compiler_bin="$2"
  ir="$BUILD_DIR/compiler_${test_file%.jiang}.ll"
  printf 'compiler/%s compile ... ' "$test_file"
  "$compiler_bin" --emit-llvm "$COMPILER_TESTS_DIR/$test_file" > "$ir"
  if [ ! -s "$ir" ]; then
    echo "failed"
    echo "error: compiler/$test_file generated empty IR" >&2
    exit 1
  fi
  echo "ok"
}

run_compiler_ir_regression_check() {
  test_file="$1"
  ir="$BUILD_DIR/compiler_${test_file%.jiang}.regression.ll"
  printf 'compiler/%s ir regression ... ' "$test_file"
  "$STAGE0_BIN" --emit-llvm "$COMPILER_TESTS_DIR/$test_file" > "$ir"
  function_count="$(grep -c '^define ' "$ir" || true)"
  if [ "$function_count" -ge 4000 ]; then
    echo "failed"
    echo "error: compiler/$test_file generated $function_count functions, expected fewer than 4000" >&2
    exit 1
  fi
  if grep -q 'type_check\.resolve\.scope\.ast\.token' "$ir"; then
    echo "failed"
    echo "error: compiler/$test_file generated transitive import clone names" >&2
    exit 1
  fi
  echo "ok"
}

run_named_test() {
  test_file="$1"
  case "$test_file" in
    compiler_bootstrap_smoke.jiang)
      run_compiler_compile_only_stage1 "$test_file"
      ;;
    llvm_api_minimal.jiang)
      run_compiler_link_test "$test_file" 0
      ;;
    *.jiang)
      run_compiler_test "$test_file" 0
      ;;
    *)
      echo "error: expected a .jiang compiler test, got '$test_file'" >&2
      exit 1
      ;;
  esac
}

run_all_compiler_tests() {
  run_compiler_test arena_minimal.jiang 0
  run_compiler_test list_minimal.jiang 0
  run_compiler_test blake3_minimal.jiang 0
  run_compiler_test equatable_binary_minimal.jiang 0
  run_compiler_test hash_minimal.jiang 0

  run_compiler_test imported_type_field_minimal.jiang 0
  run_compiler_test string_util_minimal.jiang 0
  run_compiler_test subscriptable_inferred_get_minimal.jiang 0
  run_compiler_test subscriptable_inferred_set_minimal.jiang 0
  run_compiler_test token_minimal.jiang 0
  run_compiler_test source_manager_minimal.jiang 0
  run_compiler_test package_manifest_minimal.jiang 0
  run_compiler_test interner_minimal.jiang 0
  run_compiler_test ast_minimal.jiang 0
  run_compiler_test lexer_minimal.jiang 0
  run_compiler_test parser_minimal.jiang 0
  run_compiler_test resolve_minimal.jiang 0
  run_compiler_test module_graph_minimal.jiang 0
  run_compiler_ir_regression_check type_minimal.jiang
  run_compiler_test type_minimal.jiang 0
  run_compiler_test lower_hir_minimal.jiang 0
  run_compiler_test lower_jir_minimal.jiang 0
  run_compiler_link_test llvm_api_minimal.jiang 0
  run_compiler_compile_only_stage1 compiler_bootstrap_smoke.jiang
}

if [ "$#" -gt 0 ]; then
  for test_file in "$@"; do
    run_named_test "$test_file"
  done
else
  run_all_compiler_tests
fi

echo "compiler tests passed"
