#!/usr/bin/env bash
set -euo pipefail

# Jiang 统一测试 runner。
#
# 父进程只负责发现、调度和按发现顺序汇总结果；每个 case 使用独立 work/cache
# 目录。并行 worker 不直接输出，避免完成顺序改变稳定日志。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JIANGC="${JIANGC:-$ROOT_DIR/build/bin/jiangc}"
TEST_ROOT="${TEST_ROOT:-${LANG_CHECK_ROOT:-test/lang}}"
TEST_WORK_ROOT="${TEST_WORK_ROOT:-$ROOT_DIR/build/test}"
TEST_KEEP_GOING="${TEST_KEEP_GOING:-0}"
TEST_KEEP_WORK="${TEST_KEEP_WORK:-0}"
TEST_FILTER="${TEST_FILTER:-}"
TEST_LIST="${TEST_LIST:-}"
TEST_RUN_FILTER="${TEST_RUN_FILTER:-${LANG_CHECK_RUN_FILTER:-}}"
TEST_SANITIZER="${TEST_SANITIZER:-${LANG_CHECK_SANITIZER:-}}"
TEST_SANITIZER_CLANG="${TEST_SANITIZER_CLANG:-${LANG_CHECK_SANITIZER_CLANG:-}}"
TEST_RELEASE_RUNS="${TEST_RELEASE_RUNS:-${LANG_CHECK_RELEASE_RUNS:-0}}"
TEST_TIMING="${TEST_TIMING:-0}"
TEST_TIMEOUT="${TEST_TIMEOUT:-0}"
TEST_RUN_ROOT="${TEST_RUN_ROOT:-}"
TEST_JOBS="${TEST_JOBS:-}"

CASE_KINDS=()
CASE_SOURCES=()
WORKER_PIDS=()
llvm_link_args=()
jiang_llvm_link_args=()
sanitizer_args=()
sanitizer_env=()
RUN_CLANG=""
RUN_ROOT=""
QUEUE_DIR=""
CLAIM_DIR=""
RESULT_DIR=""
FAIL_MARKER=""
LINK_ARGS_FILE="${LINK_ARGS_FILE:-}"
JIANG_LINK_ARGS_FILE="${JIANG_LINK_ARGS_FILE:-}"
COMPILER_TEST_BIN="${COMPILER_TEST_BIN:-}"
COMPILER_TEST_RELEASE_BIN="${COMPILER_TEST_RELEASE_BIN:-}"
TIMEOUT_BIN=""
SUITE_START=0
TEST_HOST_PLATFORM=""

default_test_jobs() {
  local count
  count="$(sysctl -n hw.logicalcpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1\n')"
  case "$count" in
    ""|*[!0-9]*) count=1 ;;
  esac
  if [ "$count" -gt 4 ]; then
    count=4
  fi
  printf '%s\n' "$count"
}

validate_boolean_option() {
  local name="$1"
  local value="$2"
  case "$value" in
    0|1) ;;
    *)
      echo "invalid $name=$value; expected 0 or 1" >&2
      exit 2
      ;;
  esac
}

validate_positive_integer() {
  local name="$1"
  local value="$2"
  case "$value" in
    ""|*[!0-9]*|0)
      echo "invalid $name=$value; expected a positive integer" >&2
      exit 2
      ;;
  esac
}

validate_nonnegative_integer() {
  local name="$1"
  local value="$2"
  case "$value" in
    ""|*[!0-9]*)
      echo "invalid $name=$value; expected a non-negative integer" >&2
      exit 2
      ;;
  esac
}

validate_options() {
  if [ -z "$TEST_JOBS" ]; then
    TEST_JOBS="$(default_test_jobs)"
  fi
  validate_positive_integer TEST_JOBS "$TEST_JOBS"
  validate_nonnegative_integer TEST_TIMEOUT "$TEST_TIMEOUT"
  validate_boolean_option TEST_KEEP_GOING "$TEST_KEEP_GOING"
  validate_boolean_option TEST_KEEP_WORK "$TEST_KEEP_WORK"
  validate_boolean_option TEST_RELEASE_RUNS "$TEST_RELEASE_RUNS"
  validate_boolean_option TEST_TIMING "$TEST_TIMING"

  if [ -n "$TEST_LIST" ] && [ ! -f "$TEST_LIST" ]; then
    echo "missing test list: $TEST_LIST" >&2
    exit 2
  fi
  if [ ! -x "$JIANGC" ]; then
    echo "missing compiler: $JIANGC" >&2
    echo "set JIANGC=/path/to/jiangc or build $ROOT_DIR/build/bin/jiangc first" >&2
    exit 2
  fi
  if [ "$TEST_TIMEOUT" -gt 0 ]; then
    TIMEOUT_BIN="${TEST_TIMEOUT_BIN:-$(command -v timeout || command -v gtimeout || true)}"
    if [ -z "$TIMEOUT_BIN" ]; then
      echo "TEST_TIMEOUT requires timeout or gtimeout" >&2
      exit 2
    fi
  fi
}

configure_host_platform() {
  case "$(uname -s)" in
    Darwin) TEST_HOST_PLATFORM=macos ;;
    Linux) TEST_HOST_PLATFORM=linux ;;
    *) TEST_HOST_PLATFORM=unsupported ;;
  esac
}

configure_sanitizer() {
  RUN_CLANG="$LLVM_CLANG"
  case "$TEST_SANITIZER" in
    "")
      ;;
    address)
      sanitizer_args=(-fsanitize=address -fno-omit-frame-pointer)
      if [ "$(uname -s)" = "Darwin" ]; then
        sanitizer_env=(env ASAN_OPTIONS=abort_on_error=1)
      else
        sanitizer_env=(env ASAN_OPTIONS=abort_on_error=1:detect_leaks=1)
      fi
      ;;
    thread)
      sanitizer_args=(-fsanitize=thread -fno-omit-frame-pointer)
      sanitizer_env=(env TSAN_OPTIONS=halt_on_error=1)
      ;;
    *)
      echo "unsupported TEST_SANITIZER=$TEST_SANITIZER" >&2
      exit 2
      ;;
  esac
  if [ -n "$TEST_SANITIZER_CLANG" ]; then
    RUN_CLANG="$TEST_SANITIZER_CLANG"
  elif [ -n "$TEST_SANITIZER" ] && [ "$(uname -s)" = "Darwin" ]; then
    RUN_CLANG=/usr/bin/clang
  fi
  if [ ! -x "$RUN_CLANG" ]; then
    echo "missing run clang: $RUN_CLANG" >&2
    exit 2
  fi
}

collect_llvm_link_args() {
  local arg
  for arg in \
    $("$LLVM_CONFIG" --link-static --ldflags) \
    $("$LLVM_CONFIG" --link-static --libs all) \
    $("$LLVM_CONFIG" --link-static --system-libs) \
    $(jiang_macos_sdkroot_link_args) \
    $(jiang_llvm_cxx_runtime_link_args)
  do
    llvm_link_args+=("$arg")
    jiang_llvm_link_args+=(--link-arg "$arg")
  done
}

case_selected() {
  local source="$1"
  local platform
  if [ -n "$TEST_LIST" ] && ! grep -Fqx "$source" "$TEST_LIST"; then
    return 1
  fi
  if [ -n "$TEST_FILTER" ] && [[ ! "$source" =~ $TEST_FILTER ]]; then
    return 1
  fi
  platform="$(sed -n 's/^.*test-platform:[[:space:]]*//p' "$source" | head -n 1)"
  [ -z "$platform" ] || [ "$platform" = "$TEST_HOST_PLATFORM" ]
}

add_case() {
  local kind="$1"
  local source="$2"
  CASE_KINDS+=("$kind")
  CASE_SOURCES+=("$source")
}

collect_kind_cases() {
  local kind="$1"
  local source
  while IFS= read -r source; do
    if case_selected "$source"; then
      add_case "$kind" "$source"
    fi
  done < <(find "$TEST_ROOT" -path "*/$kind/*.jiang" -type f | sort)
}

collect_run_cases() {
  local source
  while IFS= read -r source; do
    if ! case_selected "$source"; then
      continue
    fi
    if [ -n "$TEST_RUN_FILTER" ] && [[ ! "$source" =~ $TEST_RUN_FILTER ]]; then
      continue
    fi
    add_case run "$source"
  done < <(find "$TEST_ROOT" -path '*/run/*.jiang' -type f | sort)
}

collect_release_cases() {
  local source
  while IFS= read -r source; do
    if case_selected "$source"; then
      add_case release "$source"
    fi
  done < <(find "$TEST_ROOT" -path '*/run/*.jiang' -type f | sort)
}

collect_cases() {
  if [ -z "$TEST_SANITIZER" ]; then
    collect_kind_cases check
    collect_kind_cases fail
    collect_kind_cases emit
  fi
  if [ -x "$LLVM_CLANG" ]; then
    collect_run_cases
  else
    echo "SKIP run cases: missing LLVM_CLANG=$LLVM_CLANG"
  fi
  if [ "$TEST_RELEASE_RUNS" = "1" ]; then
    collect_release_cases
  fi
}

case_key() {
  local source="$1"
  local key="${source#$ROOT_DIR/}"
  key="${key%.jiang}"
  key="${key//\//__}"
  key="${key// /_}"
  printf '%s\n' "$key"
}

case_work_dir() {
  local index="$1"
  local kind="$2"
  local source="$3"
  printf '%s/cases/%s-%s-%s\n' "$RUN_ROOT" "$index" "$kind" "$(case_key "$source")"
}

case_temp_dir() {
  local work_dir="$1"
  local run_key
  local case_key
  local temp_root="${TMPDIR:-/tmp}"
  run_key="$(basename "$(dirname "$(dirname "$work_dir")")")"
  case_key="$(basename "$work_dir")"
  printf '%s/jiang-test-%s-%s\n' "${temp_root%/}" "$run_key" "$case_key"
}

timing_line() {
  if [ "$TEST_TIMING" = "1" ]; then
    printf 'TIME %s\n' "$*"
  fi
}

print_log_prefix() {
  local path="$1"
  sed -n '1,120p' "$path"
}

run_check_case() {
  local source="$1"
  local work_dir="$2"
  local cache_dir="$work_dir/cache"
  local log="$work_dir/compiler.out"
  local started=$SECONDS
  local code

  if "$JIANGC" --artifact-cache-dir "$cache_dir" --check "$source" >"$log" 2>&1; then
    echo "PASS check $source"
    timing_line "check $source compile=$((SECONDS - started))s"
    return 0
  else
    code=$?
  fi
  echo "FAIL check $source exited $code"
  print_log_prefix "$log"
  timing_line "check $source compile=$((SECONDS - started))s"
  return 1
}

run_fail_case() {
  local source="$1"
  local work_dir="$2"
  local cache_dir="$work_dir/cache"
  local log="$work_dir/compiler.out"
  local started=$SECONDS
  local expected

  if "$JIANGC" --artifact-cache-dir "$cache_dir" --check "$source" >"$log" 2>&1; then
    echo "FAIL fail $source unexpectedly passed"
    timing_line "fail $source compile=$((SECONDS - started))s"
    return 1
  fi

  expected="$(sed -n 's/^.*expected:[[:space:]]*//p' "$source" | head -n 1)"
  if [ -n "$expected" ] && ! grep -q "$expected" "$log"; then
    echo "FAIL fail $source missing expected diagnostic: $expected"
    print_log_prefix "$log"
    timing_line "fail $source compile=$((SECONDS - started))s"
    return 1
  fi

  echo "PASS fail $source"
  timing_line "fail $source compile=$((SECONDS - started))s"
}

run_emit_case() {
  local source="$1"
  local work_dir="$2"
  local cache_dir="$work_dir/cache"
  local output="$work_dir/output.ll"
  local log="$work_dir/compiler.out"
  local started=$SECONDS
  local code

  if "$JIANGC" --artifact-cache-dir "$cache_dir" --emit-llvm -o "$output" "$source" >"$log" 2>&1; then
    echo "PASS emit $source"
    timing_line "emit $source compile=$((SECONDS - started))s"
    return 0
  else
    code=$?
  fi
  echo "FAIL emit $source exited $code"
  print_log_prefix "$log"
  timing_line "emit $source compile=$((SECONDS - started))s"
  return 1
}

expected_exit_code() {
  local source="$1"
  local expected
  expected="$(sed -n 's/^.*expected-exit:[[:space:]]*//p' "$source" | head -n 1)"
  if [ -z "$expected" ]; then
    expected=0
  fi
  printf '%s\n' "$expected"
}

exit_code_matches() {
  local expected="$1"
  local actual="$2"
  if [ "$expected" = "trap" ]; then
    [ "$actual" = "132" ] || [ "$actual" = "133" ]
    return
  fi
  [ "$actual" = "$expected" ]
}

run_run_case() {
  local source="$1"
  local work_dir="$2"
  local cache_dir="$work_dir/cache"
  local llvm_output="$work_dir/output.ll"
  local executable="$work_dir/test"
  local emit_log="$work_dir/emit.out"
  local link_log="$work_dir/link.out"
  local run_log="$work_dir/run.out"
  local companion="${source%.jiang}.c"
  local companion_args=()
  local emit_started=$SECONDS
  local emit_time
  local link_started
  local link_time
  local run_started
  local run_time
  local expected
  local code
  local temp_dir

  if [ -f "$companion" ]; then
    companion_args+=("$companion")
  fi
  if ! "$JIANGC" --artifact-cache-dir "$cache_dir" \
    --emit-llvm -o "$llvm_output" "$source" >"$emit_log" 2>&1
  then
    echo "FAIL run $source emit failed"
    print_log_prefix "$emit_log"
    timing_line "run $source emit=$((SECONDS - emit_started))s"
    return 1
  fi
  emit_time=$((SECONDS - emit_started))

  link_started=$SECONDS
  if ! "$RUN_CLANG" "$llvm_output" ${companion_args[@]+"${companion_args[@]}"} -o "$executable" \
    ${sanitizer_args[@]+"${sanitizer_args[@]}"} "${llvm_link_args[@]}" \
    >"$link_log" 2>&1
  then
    echo "FAIL run $source link failed"
    print_log_prefix "$link_log"
    timing_line "run $source emit=${emit_time}s link=$((SECONDS - link_started))s"
    return 1
  fi
  link_time=$((SECONDS - link_started))

  expected="$(expected_exit_code "$source")"
  temp_dir="$(case_temp_dir "$work_dir")"
  mkdir -p "$temp_dir"
  run_started=$SECONDS
  set +e
  env JIANG_TEST_WORK_DIR="$work_dir" \
    JIANG_TEST_TEMP_DIR="$temp_dir" \
    ${sanitizer_env[@]+"${sanitizer_env[@]}"} \
    bash -c '"$1"; exit $?' _ "$executable" \
    >"$run_log" 2>&1
  code=$?
  set -e
  run_time=$((SECONDS - run_started))

  if exit_code_matches "$expected" "$code"; then
    rm -rf "$temp_dir"
    echo "PASS run $source"
    timing_line "run $source emit=${emit_time}s link=${link_time}s execute=${run_time}s"
    return 0
  fi
  echo "FAIL run $source exited $code, expected $expected"
  echo "test temp: $temp_dir"
  print_log_prefix "$run_log"
  timing_line "run $source emit=${emit_time}s link=${link_time}s execute=${run_time}s"
  return 1
}

run_compiler_case() {
  local source="$1"
  local work_dir="$2"
  local executable="$3"
  local label="$4"
  local run_log="$work_dir/run.out"
  local relative="${source#$ROOT_DIR/}"
  local case_name="${relative#test/compiler/}"
  local started=$SECONDS
  local expected
  local code
  local temp_dir

  mkdir -p "$work_dir"
  temp_dir="$(case_temp_dir "$work_dir")"
  mkdir -p "$temp_dir"
  expected="$(expected_exit_code "$source")"
  set +e
  env \
    JIANG_TEST_WORK_DIR="$work_dir" \
    JIANG_TEST_TEMP_DIR="$temp_dir" \
    "$executable" "$case_name" >"$run_log" 2>&1
  code=$?
  set -e
  if exit_code_matches "$expected" "$code"; then
    rm -rf "$temp_dir"
    echo "PASS $label $source"
    timing_line "$label $source execute=$((SECONDS - started))s"
    return 0
  fi
  echo "FAIL $label $source exited $code, expected $expected"
  echo "test temp: $temp_dir"
  print_log_prefix "$run_log"
  timing_line "$label $source execute=$((SECONDS - started))s"
  return 1
}

run_release_case() {
  local source="$1"
  local work_dir="$2"
  local cache_dir="$work_dir/cache"
  local executable="$work_dir/test"
  local companion="${source%.jiang}.c"
  local companion_object="$work_dir/companion.o"
  local companion_log="$work_dir/companion.out"
  local build_log="$work_dir/build.out"
  local run_log="$work_dir/run.out"
  local companion_link_args=()
  local build_started=$SECONDS
  local build_time
  local run_started
  local run_time
  local expected
  local code
  local temp_dir

  if [ -f "$companion" ]; then
    if ! "$LLVM_CLANG" -c "$companion" -o "$companion_object" >"$companion_log" 2>&1; then
      echo "FAIL release-run $source companion compile failed"
      print_log_prefix "$companion_log"
      return 1
    fi
    companion_link_args+=(--link-arg "$companion_object")
  fi

  if ! "$JIANGC" \
    --artifact-cache-dir "$cache_dir" \
    --mode release \
    "${jiang_llvm_link_args[@]}" \
    ${companion_link_args[@]+"${companion_link_args[@]}"} \
    -o "$executable" \
    "$source" >"$build_log" 2>&1
  then
    echo "FAIL release-run $source build failed"
    print_log_prefix "$build_log"
    timing_line "release $source build=$((SECONDS - build_started))s"
    return 1
  fi
  build_time=$((SECONDS - build_started))

  expected="$(expected_exit_code "$source")"
  temp_dir="$(case_temp_dir "$work_dir")"
  mkdir -p "$temp_dir"
  run_started=$SECONDS
  set +e
  env JIANG_TEST_WORK_DIR="$work_dir" \
    JIANG_TEST_TEMP_DIR="$temp_dir" \
    bash -c '"$1"; exit $?' _ "$executable" >"$run_log" 2>&1
  code=$?
  set -e
  run_time=$((SECONDS - run_started))

  if exit_code_matches "$expected" "$code"; then
    rm -rf "$temp_dir"
    echo "PASS release-run $source"
    timing_line "release $source build=${build_time}s execute=${run_time}s"
    return 0
  fi
  echo "FAIL release-run $source exited $code, expected $expected"
  echo "test temp: $temp_dir"
  print_log_prefix "$run_log"
  timing_line "release $source build=${build_time}s execute=${run_time}s"
  return 1
}

run_case() {
  local kind="$1"
  local source="$2"
  local work_dir="$3"
  local started=$SECONDS
  local code

  mkdir -p "$work_dir/cache"
  printf '%s\n' "$source" >"$work_dir/source.txt"
  if run_case_kind "$kind" "$source" "$work_dir"; then
    code=0
  else
    code=$?
  fi
  timing_line "case $kind $source total=$((SECONDS - started))s"
  return "$code"
}

run_case_kind() {
  local kind="$1"
  local source="$2"
  local work_dir="$3"
  case "$kind" in
    check) run_check_case "$source" "$work_dir" ;;
    fail) run_fail_case "$source" "$work_dir" ;;
    emit) run_emit_case "$source" "$work_dir" ;;
    run)
      if [ -n "$COMPILER_TEST_BIN" ] && ! grep -Fqx '// compiler-integration' "$source"; then
        run_compiler_case "$source" "$work_dir" "$COMPILER_TEST_BIN" run
      else
        run_run_case "$source" "$work_dir"
      fi
      ;;
    release)
      if [ -n "$COMPILER_TEST_RELEASE_BIN" ] && ! grep -Fqx '// compiler-integration' "$source"; then
        run_compiler_case "$source" "$work_dir" "$COMPILER_TEST_RELEASE_BIN" release-run
      else
        run_release_case "$source" "$work_dir"
      fi
      ;;
    *)
      echo "FAIL unknown test kind: $kind"
      return 1
      ;;
  esac
}

write_arg_file() {
  local path="$1"
  shift
  : >"$path"
  local arg
  for arg in "$@"; do
    printf '%s\n' "$arg" >>"$path"
  done
}

load_arg_file() {
  local path="$1"
  local target="$2"
  local arg
  while IFS= read -r arg || [ -n "$arg" ]; do
    if [ "$target" = llvm ]; then
      llvm_link_args+=("$arg")
    else
      jiang_llvm_link_args+=("$arg")
    fi
  done <"$path"
}

prepare_run_root() {
  mkdir -p "$TEST_WORK_ROOT"
  if [ -n "$TEST_RUN_ROOT" ]; then
    RUN_ROOT="$TEST_RUN_ROOT"
    if [ -e "$RUN_ROOT" ]; then
      echo "test run root already exists: $RUN_ROOT" >&2
      exit 2
    fi
    mkdir -p "$RUN_ROOT"
  else
    RUN_ROOT="$(mktemp -d "$TEST_WORK_ROOT/run.XXXXXX")"
  fi
  QUEUE_DIR="$RUN_ROOT/queue"
  CLAIM_DIR="$RUN_ROOT/claims"
  RESULT_DIR="$RUN_ROOT/results"
  FAIL_MARKER="$RUN_ROOT/failure"
  LINK_ARGS_FILE="$RUN_ROOT/llvm-link-args"
  JIANG_LINK_ARGS_FILE="$RUN_ROOT/jiang-link-args"
  mkdir -p "$QUEUE_DIR" "$CLAIM_DIR" "$RESULT_DIR" "$RUN_ROOT/cases"
  write_arg_file "$LINK_ARGS_FILE" "${llvm_link_args[@]}"
  write_arg_file "$JIANG_LINK_ARGS_FILE" "${jiang_llvm_link_args[@]}"
}

uses_compiler_test_runner() {
  [ "$TEST_ROOT" = "test/compiler" ] || [ "$TEST_ROOT" = "$ROOT_DIR/test/compiler" ]
}

compiler_cases_need_aggregate() {
  local index=0
  while [ "$index" -lt "${#CASE_KINDS[@]}" ]; do
    case "${CASE_KINDS[$index]}" in
      run|release)
        if ! grep -Fqx '// compiler-integration' "${CASE_SOURCES[$index]}"; then
          return 0
        fi
        ;;
    esac
    index=$((index + 1))
  done
  return 1
}

build_compiler_test_executable() {
  local mode="$1"
  local output="$2"
  local source="$ROOT_DIR/test/compiler/compiler.jiang"
  local cache_dir="$RUN_ROOT/compiler-$mode-cache"
  local build_log="$RUN_ROOT/compiler-$mode-build.out"
  local started=$SECONDS
  local mode_args=()
  if [ "$mode" = release ]; then
    mode_args=(--mode release)
  fi
  if [ -n "$TEST_SANITIZER" ] && [ "$mode" = debug ]; then
    if build_sanitized_compiler_test_executable "$source" "$cache_dir" "$output" "$build_log"; then
      timing_line "compiler test executable mode=$mode sanitizer=$TEST_SANITIZER build=$((SECONDS - started))s"
      return 0
    fi
    return 1
  fi
  if "$JIANGC" \
    --artifact-cache-dir "$cache_dir" \
    ${mode_args[@]+"${mode_args[@]}"} \
    --linker "$RUN_CLANG" \
    "${jiang_llvm_link_args[@]}" \
    -o "$output" \
    "$source" >"$build_log" 2>&1
  then
    timing_line "compiler test executable mode=$mode build=$((SECONDS - started))s"
    return 0
  fi
  echo "FAIL compiler test executable mode=$mode build"
  print_log_prefix "$build_log"
  return 1
}

build_sanitized_compiler_test_executable() {
  local source="$1"
  local cache_dir="$2"
  local output="$3"
  local build_log="$4"
  local llvm_output="$RUN_ROOT/compiler-debug.ll"
  if ! "$JIANGC" \
    --artifact-cache-dir "$cache_dir" \
    --emit-llvm -o "$llvm_output" \
    "$source" >"$build_log" 2>&1
  then
    echo "FAIL compiler test executable sanitizer emit"
    print_log_prefix "$build_log"
    return 1
  fi
  if "$RUN_CLANG" \
    "$llvm_output" \
    -o "$output" \
    "${sanitizer_args[@]}" \
    "${llvm_link_args[@]}" >>"$build_log" 2>&1
  then
    return 0
  fi
  echo "FAIL compiler test executable sanitizer link"
  print_log_prefix "$build_log"
  return 1
}

prepare_compiler_test_runner() {
  if ! uses_compiler_test_runner || ! compiler_cases_need_aggregate; then
    return 0
  fi
  COMPILER_TEST_BIN="$RUN_ROOT/compiler-tests"
  if ! build_compiler_test_executable debug "$COMPILER_TEST_BIN"; then
    return 1
  fi
  if [ "$TEST_RELEASE_RUNS" != 1 ]; then
    return 0
  fi
  COMPILER_TEST_RELEASE_BIN="$RUN_ROOT/compiler-tests-release"
  build_compiler_test_executable release "$COMPILER_TEST_RELEASE_BIN"
}

write_jobs() {
  local index=0
  local count="${#CASE_KINDS[@]}"
  while [ "$index" -lt "$count" ]; do
    printf '%s\n%s\n%s\n' "$index" "${CASE_KINDS[$index]}" "${CASE_SOURCES[$index]}" \
      >"$QUEUE_DIR/$index.job"
    index=$((index + 1))
  done
}

claim_next_job() {
  local worker_id="$1"
  local job
  local claim
  CLAIMED_JOB=""
  for job in "$QUEUE_DIR"/*.job; do
    if [ ! -f "$job" ]; then
      continue
    fi
    claim="$CLAIM_DIR/$(basename "$job").$worker_id"
    if mv "$job" "$claim" 2>/dev/null; then
      CLAIMED_JOB="$claim"
      return 0
    fi
  done
  return 1
}

run_case_with_timeout() {
  local kind="$1"
  local source="$2"
  local work_dir="$3"
  if [ "$TEST_TIMEOUT" = "0" ]; then
    run_case "$kind" "$source" "$work_dir"
    return
  fi

  export JIANGC TEST_TIMING TEST_SANITIZER TEST_SANITIZER_CLANG
  export COMPILER_TEST_BIN COMPILER_TEST_RELEASE_BIN
  export LLVM_CLANG RUN_CLANG LINK_ARGS_FILE JIANG_LINK_ARGS_FILE
  "$TIMEOUT_BIN" "$TEST_TIMEOUT" bash "$ROOT_DIR/script/test.sh" \
    --internal-case "$kind" "$source" "$work_dir"
}

run_claimed_job() {
  local claim="$1"
  local index
  local kind
  local source
  local work_dir
  local output
  local code

  index="$(sed -n '1p' "$claim")"
  kind="$(sed -n '2p' "$claim")"
  source="$(sed -n '3p' "$claim")"
  work_dir="$(case_work_dir "$index" "$kind" "$source")"
  output="$RESULT_DIR/$index.out"

  if run_case_with_timeout "$kind" "$source" "$work_dir" >"$output" 2>&1; then
    code=0
  else
    code=$?
  fi
  if [ "$code" = "124" ]; then
    printf 'FAIL %s %s timed out after %ss\n' "$kind" "$source" "$TEST_TIMEOUT" >>"$output"
  fi
  printf '%s\n' "$code" >"$RESULT_DIR/$index.status"
  if [ "$code" != "0" ] && [ "$TEST_KEEP_GOING" != "1" ]; then
    mkdir "$FAIL_MARKER" 2>/dev/null || true
  fi
}

worker_loop() {
  local worker_id="$1"
  local claim
  while true; do
    if [ "$TEST_KEEP_GOING" != "1" ] && [ -d "$FAIL_MARKER" ]; then
      return 0
    fi
    if ! claim_next_job "$worker_id"; then
      return 0
    fi
    claim="$CLAIMED_JOB"
    run_claimed_job "$claim"
  done
}

run_workers() {
  local count="${#CASE_KINDS[@]}"
  local worker_count="$TEST_JOBS"
  local worker=0
  local pid
  if [ "$worker_count" -gt "$count" ]; then
    worker_count="$count"
  fi
  while [ "$worker" -lt "$worker_count" ]; do
    worker_loop "$worker" &
    WORKER_PIDS+=("$!")
    worker=$((worker + 1))
  done
  for pid in "${WORKER_PIDS[@]}"; do
    wait "$pid"
  done
}

print_results() {
  local count="${#CASE_KINDS[@]}"
  local index=0
  local code
  local status=0
  while [ "$index" -lt "$count" ]; do
    if [ ! -f "$RESULT_DIR/$index.status" ]; then
      echo "SKIP ${CASE_KINDS[$index]} ${CASE_SOURCES[$index]} after earlier failure"
      index=$((index + 1))
      continue
    fi
    cat "$RESULT_DIR/$index.out"
    code="$(sed -n '1p' "$RESULT_DIR/$index.status")"
    if [ "$code" != "0" ]; then
      status=1
    fi
    index=$((index + 1))
  done
  timing_line "suite cases=$count jobs=$TEST_JOBS wall=$((SECONDS - SUITE_START))s"
  return "$status"
}

cleanup_success_run() {
  if [ "$TEST_KEEP_WORK" = "1" ]; then
    echo "test artifacts: $RUN_ROOT"
    return
  fi
  case "$RUN_ROOT" in
    "$TEST_WORK_ROOT"/*) rm -rf "$RUN_ROOT" ;;
    *)
      echo "refusing to remove test run root outside TEST_WORK_ROOT: $RUN_ROOT" >&2
      return 1
      ;;
  esac
}

run_internal_case() {
  local kind="$1"
  local source="$2"
  local work_dir="$3"
  llvm_link_args=()
  jiang_llvm_link_args=()
  configure_sanitizer
  load_arg_file "$LINK_ARGS_FILE" llvm
  load_arg_file "$JIANG_LINK_ARGS_FILE" jiang
  cd "$ROOT_DIR"
  run_case "$kind" "$source" "$work_dir"
}

run_suite() {
  validate_options
  configure_host_platform
  source "$ROOT_DIR/script/llvm_env.sh"
  configure_sanitizer
  collect_llvm_link_args
  cd "$ROOT_DIR"
  collect_cases
  prepare_run_root
  write_jobs
  SUITE_START=$SECONDS
  if ! prepare_compiler_test_runner; then
    echo "test artifacts: $RUN_ROOT"
    return 1
  fi

  if [ "${#CASE_KINDS[@]}" -gt 0 ]; then
    run_workers
  fi
  if print_results; then
    cleanup_success_run
    return 0
  fi
  echo "test artifacts: $RUN_ROOT"
  return 1
}

if [ "${1:-}" = "--internal-case" ]; then
  if [ "$#" != "4" ]; then
    echo "invalid internal case invocation" >&2
    exit 2
  fi
  run_internal_case "$2" "$3" "$4"
  exit $?
fi

run_suite
