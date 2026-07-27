#!/usr/bin/env bash
set -euo pipefail

# 验证统一测试 runner 的调度、结果判定、隔离和稳定输出契约。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIXTURE_COMPILER="$ROOT_DIR/test/runner/fake_jiangc.sh"
FIXTURE_CLANG="$ROOT_DIR/test/runner/fake_clang.sh"
SELF_TEST_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/jiang-test-runner.XXXXXX")"
FIXTURE_ROOT="$SELF_TEST_ROOT/fixture"
FAKE_JIANGC_LOG="$SELF_TEST_ROOT/compiler.log"

cleanup() {
  case "$SELF_TEST_ROOT" in
    "${TMPDIR:-/tmp}"/jiang-test-runner.*|/tmp/jiang-test-runner.*)
      rm -rf "$SELF_TEST_ROOT"
      ;;
  esac
}
trap cleanup EXIT

fail_self_test() {
  echo "FAIL runner self-test: $*" >&2
  exit 1
}

write_case() {
  local path="$1"
  local content="${2:-}"
  mkdir -p "$(dirname "$path")"
  printf '%s\n' "$content" >"$path"
}

run_fixture_suite() {
  local fixture="$1"
  local run_root="$2"
  local jobs="$3"
  local keep_going="$4"
  local timeout="$5"
  local release_runs="$6"
  local output="$7"

  set +e
  TEST_ROOT="$fixture" \
    TEST_RUN_ROOT="$run_root" \
    TEST_JOBS="$jobs" \
    TEST_KEEP_GOING="$keep_going" \
    TEST_KEEP_WORK=1 \
    TEST_TIMEOUT="$timeout" \
    TEST_RELEASE_RUNS="$release_runs" \
    TEST_TIMING=0 \
    JIANGC="$FIXTURE_COMPILER" \
    LLVM_CLANG="$FIXTURE_CLANG" \
    FAKE_JIANGC_LOG="$FAKE_JIANGC_LOG" \
    bash "$ROOT_DIR/script/test.sh" >"$output" 2>&1
  FIXTURE_STATUS=$?
  set -e
}

result_lines() {
  sed '/^test artifacts:/d' "$1"
}

assert_contains() {
  local path="$1"
  local expected="$2"
  if ! grep -Fq "$expected" "$path"; then
    sed -n '1,160p' "$path" >&2
    fail_self_test "missing output: $expected"
  fi
}

assert_not_contains() {
  local path="$1"
  local unexpected="$2"
  if grep -Fq "$unexpected" "$path"; then
    sed -n '1,160p' "$path" >&2
    fail_self_test "unexpected output: $unexpected"
  fi
}

prepare_success_fixture() {
  local root="$1"
  write_case "$root/basic/check/slow_success.jiang"
  write_case "$root/basic/fail/expected_failure.jiang" "// expected: E_EXPECTED"
  write_case "$root/basic/emit/emit_success.jiang"
  write_case "$root/basic/run/run_success.jiang"
  write_case "$root/basic/run/run_exit.jiang" "// expected-exit: 7"
}

check_stable_parallel_output() {
  local fixture="$FIXTURE_ROOT/success"
  local serial_root="$SELF_TEST_ROOT/serial"
  local parallel_root="$SELF_TEST_ROOT/parallel"
  local serial_output="$SELF_TEST_ROOT/serial.out"
  local parallel_output="$SELF_TEST_ROOT/parallel.out"
  local serial_results="$SELF_TEST_ROOT/serial.results"
  local parallel_results="$SELF_TEST_ROOT/parallel.results"

  prepare_success_fixture "$fixture"
  : >"$FAKE_JIANGC_LOG"
  run_fixture_suite "$fixture" "$serial_root" 1 0 0 1 "$serial_output"
  [ "$FIXTURE_STATUS" = "0" ] || fail_self_test "serial success suite failed"
  result_lines "$serial_output" >"$serial_results"

  : >"$FAKE_JIANGC_LOG"
  run_fixture_suite "$fixture" "$parallel_root" 4 0 0 1 "$parallel_output"
  [ "$FIXTURE_STATUS" = "0" ] || fail_self_test "parallel success suite failed"
  result_lines "$parallel_output" >"$parallel_results"
  if ! cmp -s "$serial_results" "$parallel_results"; then
    diff -u "$serial_results" "$parallel_results" >&2 || true
    fail_self_test "serial and parallel output differ"
  fi

  assert_contains "$parallel_output" "PASS check"
  assert_contains "$parallel_output" "PASS fail"
  assert_contains "$parallel_output" "PASS emit"
  assert_contains "$parallel_output" "PASS run"
  assert_contains "$parallel_output" "PASS release-run"

  local invocation_count
  local cache_count
  invocation_count="$(wc -l <"$FAKE_JIANGC_LOG" | tr -d ' ')"
  cache_count="$(cut -d'|' -f2 "$FAKE_JIANGC_LOG" | sort -u | wc -l | tr -d ' ')"
  [ "$invocation_count" = "7" ] || fail_self_test "expected 7 compiler invocations, got $invocation_count"
  [ "$cache_count" = "$invocation_count" ] || fail_self_test "compiler invocations shared mutable cache"
  if cut -d'|' -f2 "$FAKE_JIANGC_LOG" | grep -Fv "$parallel_root/cases/" >/dev/null; then
    fail_self_test "cache directory escaped the per-case run root"
  fi

  local diagnostic_log
  diagnostic_log="$(find "$parallel_root/cases" -path '*fail*compiler.out' -type f | head -n 1)"
  [ -n "$diagnostic_log" ] || fail_self_test "missing fail-case diagnostic log"
  assert_contains "$diagnostic_log" "E_EXPECTED"
}

prepare_failure_fixture() {
  local root="$1"
  write_case "$root/basic/check/00_compile_error.jiang"
  write_case "$root/basic/check/99_after_failure.jiang"
}

check_failure_modes() {
  local fixture="$FIXTURE_ROOT/failure"
  local fail_fast_output="$SELF_TEST_ROOT/fail-fast.out"
  local keep_going_output="$SELF_TEST_ROOT/keep-going.out"

  prepare_failure_fixture "$fixture"
  : >"$FAKE_JIANGC_LOG"
  run_fixture_suite "$fixture" "$SELF_TEST_ROOT/fail-fast" 1 0 0 0 "$fail_fast_output"
  [ "$FIXTURE_STATUS" = "1" ] || fail_self_test "fail-fast suite returned $FIXTURE_STATUS"
  assert_contains "$fail_fast_output" "FAIL check"
  assert_contains "$fail_fast_output" "SKIP check"
  assert_not_contains "$fail_fast_output" "PASS check"
  [ "$(wc -l <"$FAKE_JIANGC_LOG" | tr -d ' ')" = "1" ] || fail_self_test "fail-fast dispatched extra work"

  : >"$FAKE_JIANGC_LOG"
  run_fixture_suite "$fixture" "$SELF_TEST_ROOT/keep-going" 2 1 0 0 "$keep_going_output"
  [ "$FIXTURE_STATUS" = "1" ] || fail_self_test "keep-going suite returned $FIXTURE_STATUS"
  assert_contains "$keep_going_output" "FAIL check"
  assert_contains "$keep_going_output" "PASS check"
  assert_not_contains "$keep_going_output" "SKIP check"
  [ "$(wc -l <"$FAKE_JIANGC_LOG" | tr -d ' ')" = "2" ] || fail_self_test "keep-going skipped selected work"
}

check_timeout() {
  local fixture="$FIXTURE_ROOT/timeout"
  local output="$SELF_TEST_ROOT/timeout.out"

  write_case "$fixture/basic/check/hang.jiang"
  : >"$FAKE_JIANGC_LOG"
  run_fixture_suite "$fixture" "$SELF_TEST_ROOT/timeout-run" 1 0 1 0 "$output"
  [ "$FIXTURE_STATUS" = "1" ] || fail_self_test "timeout suite returned $FIXTURE_STATUS"
  assert_contains "$output" "timed out after 1s"
  local compiler_log
  compiler_log="$(find "$SELF_TEST_ROOT/timeout-run/cases" -name compiler.out -type f | head -n 1)"
  [ -f "$compiler_log" ] || fail_self_test "timeout did not retain compiler log"
  assert_contains "$compiler_log" "compiler output before timeout"
}

check_empty_and_invalid_options() {
  local fixture="$FIXTURE_ROOT/empty"
  local empty_output="$SELF_TEST_ROOT/empty.out"
  local invalid_output="$SELF_TEST_ROOT/invalid.out"

  mkdir -p "$fixture"
  : >"$FAKE_JIANGC_LOG"
  run_fixture_suite "$fixture" "$SELF_TEST_ROOT/empty-run" 1 0 0 0 "$empty_output"
  [ "$FIXTURE_STATUS" = "0" ] || fail_self_test "empty suite failed"

  set +e
  TEST_ROOT="$fixture" TEST_JOBS=0 JIANGC="$FIXTURE_COMPILER" \
    bash "$ROOT_DIR/script/test.sh" >"$invalid_output" 2>&1
  local status=$?
  set -e
  [ "$status" = "2" ] || fail_self_test "invalid TEST_JOBS returned $status"
  assert_contains "$invalid_output" "invalid TEST_JOBS=0"
}

chmod +x "$FIXTURE_COMPILER" "$FIXTURE_CLANG"
check_stable_parallel_output
check_failure_modes
check_timeout
check_empty_and_invalid_options
echo "PASS test runner self-test"
