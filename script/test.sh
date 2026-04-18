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
"$BUILD_DIR/utils_test"

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

run_compile_fail() {
  local sample="$1"
  set +e
  "$BUILD_DIR/jiangc" --emit-llvm "$SAMPLES_DIR/$sample" >/dev/null 2>&1
  local status=$?
  set -e
  if [[ "$status" -eq 0 ]]; then
    echo "error: $sample unexpectedly compiled" >&2
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
run_sample enum_minimal.jiang 2
run_sample enum_shorthand_minimal.jiang 42
run_sample enum_shorthand_arg_minimal.jiang 42
run_sample enum_value_minimal.jiang 42
run_sample enum_switch_shorthand_minimal.jiang 42
run_sample switch_enum_minimal.jiang 42
run_sample assert_minimal.jiang 42
run_sample print_minimal.jiang 42
run_sample tuple_value_minimal.jiang 42
run_sample tuple_return_minimal.jiang 42
run_sample tuple_infer_minimal.jiang 42
run_sample tuple_destructure_minimal.jiang 42
run_sample tuple_destructure_infer_minimal.jiang 42
run_sample tuple_destructure_mutable_infer_minimal.jiang 42
run_sample tuple_destructure_return_minimal.jiang 42
run_sample tuple_destructure_global_minimal.jiang 42
run_sample unary_tuple_local_decl_minimal.jiang 42
run_sample unary_tuple_infer_local_decl_minimal.jiang 42
run_sample unary_tuple_global_decl_minimal.jiang 42
run_sample unary_tuple_return_minimal.jiang 42
run_sample empty_tuple_return_minimal.jiang 0
run_sample empty_tuple_bare_return_minimal.jiang 0
run_sample for_tuple_binding_minimal.jiang 42
run_sample for_tuple_binding_typed_minimal.jiang 42
run_sample for_indexed_tuple_binding_minimal.jiang 42
run_sample for_indexed_mutable_tuple_binding_minimal.jiang 42
run_sample union_tuple_bind_minimal.jiang 42
run_sample union_tuple_switch_mutable_binding_minimal.jiang 42
run_sample union_tuple_if_shorthand_pattern_minimal.jiang 42
run_sample union_tuple_if_mutable_shorthand_pattern_minimal.jiang 42
run_sample union_minimal.jiang 42
run_sample union_shorthand_minimal.jiang 42
run_sample union_bind_minimal.jiang 42
run_sample union_if_pattern_minimal.jiang 42
run_sample union_if_mutable_binding_minimal.jiang 42
run_sample union_if_shorthand_pattern_minimal.jiang 42
run_sample union_switch_mutable_binding_minimal.jiang 42
run_sample union_switch_shorthand_pattern_minimal.jiang 42
run_compile_fail invalid_tuple_index_non_literal.jiang
run_compile_fail invalid_tuple_index_out_of_range.jiang
run_compile_fail invalid_tuple_destructure_arity.jiang
run_compile_fail invalid_tuple_destructure_rhs.jiang
run_compile_fail invalid_empty_tuple_return_non_void.jiang
run_compile_fail invalid_for_tuple_binding_non_tuple.jiang
run_compile_fail invalid_for_tuple_binding_arity.jiang
run_compile_fail invalid_switch_case_type.jiang
run_compile_fail invalid_duplicate_enum.jiang
run_compile_fail invalid_duplicate_enum_member.jiang
run_compile_fail invalid_union_bind_void.jiang
run_compile_fail invalid_union_ctor_arg.jiang
run_compile_fail invalid_union_pattern_ne_bind.jiang
run_compile_fail invalid_union_tuple_bind_non_tuple.jiang
run_compile_fail invalid_union_tuple_bind_arity.jiang
run_sample_nonzero panic_minimal.jiang

echo "stage0 batch A smoke passed"
