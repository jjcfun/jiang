#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JIANGC="${JIANGC:-$ROOT_DIR/build/bin/jiangc}"
TEST_ROOT="${TEST_ROOT:-${LANG_CHECK_ROOT:-test/lang}}"
TEST_WORK_ROOT="${TEST_WORK_ROOT:-$ROOT_DIR/build/test}"
TEST_KEEP_GOING="${TEST_KEEP_GOING:-0}"
TEST_FILTER="${TEST_FILTER:-}"
TEST_LIST="${TEST_LIST:-}"
TEST_RUN_FILTER="${TEST_RUN_FILTER:-${LANG_CHECK_RUN_FILTER:-}}"
TEST_SANITIZER="${TEST_SANITIZER:-${LANG_CHECK_SANITIZER:-}}"
TEST_SANITIZER_CLANG="${TEST_SANITIZER_CLANG:-${LANG_CHECK_SANITIZER_CLANG:-}}"

source "$ROOT_DIR/script/llvm_env.sh"

if [ ! -x "$JIANGC" ]; then
  echo "missing compiler: $JIANGC" >&2
  echo "set JIANGC=/path/to/jiangc or build $ROOT_DIR/build/bin/jiangc first" >&2
  exit 2
fi

cd "$ROOT_DIR"

status=0
llvm_link_args=()
jiang_llvm_link_args=()
sanitizer_args=()
sanitizer_env=()
run_clang="$LLVM_CLANG"

if [ -n "$TEST_LIST" ] && [ ! -f "$TEST_LIST" ]; then
  echo "missing test list: $TEST_LIST" >&2
  exit 2
fi

record_failure() {
  status=1
  if [ "$TEST_KEEP_GOING" != "1" ]; then
    exit 1
  fi
}

case_work_dir() {
  local kind="$1"
  local source="$2"
  local key="${source%.jiang}"
  key="${key//\//__}"
  printf '%s/%s/%s\n' "$TEST_WORK_ROOT" "$kind" "$key"
}

case_selected() {
  local source="$1"
  if [ -n "$TEST_LIST" ] && ! grep -Fqx "$source" "$TEST_LIST"; then
    return 1
  fi
  [ -z "$TEST_FILTER" ] || [[ "$source" =~ $TEST_FILTER ]]
}

configure_sanitizer() {
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
    run_clang="$TEST_SANITIZER_CLANG"
  elif [ -n "$TEST_SANITIZER" ] && [ "$(uname -s)" = "Darwin" ]; then
    run_clang=/usr/bin/clang
  fi
  if [ ! -x "$run_clang" ]; then
    echo "missing run clang: $run_clang" >&2
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

collect_llvm_link_args
configure_sanitizer

run_check_case() {
  local source="$1"
  local work_dir
  work_dir="$(case_work_dir check "$source")"
  mkdir -p "$work_dir"
  local log="$work_dir/compiler.out"

  local code
  if "$JIANGC" --check "$source" >"$log" 2>&1; then
    echo "PASS check $source"
    return
  else
    code=$?
  fi

  echo "FAIL check $source exited $code"
  sed -n '1,120p' "$log"
  record_failure
}

run_fail_case() {
  local source="$1"
  local work_dir
  work_dir="$(case_work_dir fail "$source")"
  mkdir -p "$work_dir"
  local log="$work_dir/compiler.out"

  if "$JIANGC" --check "$source" >"$log" 2>&1; then
    echo "FAIL fail $source unexpectedly passed"
    record_failure
    return
  fi

  local expected
  expected="$(sed -n 's/^.*expected:[[:space:]]*//p' "$source" | head -n 1)"
  if [ -n "$expected" ] && ! grep -q "$expected" "$log"; then
    echo "FAIL fail $source missing expected diagnostic: $expected"
    sed -n '1,120p' "$log"
    record_failure
    return
  fi

  echo "PASS fail $source"
}

run_emit_case() {
  local source="$1"
  local work_dir
  work_dir="$(case_work_dir emit "$source")"
  mkdir -p "$work_dir"
  local output="$work_dir/output.ll"
  local log="$work_dir/compiler.out"

  if "$JIANGC" --emit-llvm -o "$output" "$source" >"$log" 2>&1; then
    echo "PASS emit $source"
    rm -f "$output"
    return
  fi

  local code=$?
  echo "FAIL emit $source exited $code"
  sed -n '1,120p' "$log"
  record_failure
}

run_run_case() {
  local source="$1"
  local work_dir
  work_dir="$(case_work_dir run "$source")"
  mkdir -p "$work_dir"
  local llvm_output="$work_dir/output.ll"
  local executable="$work_dir/test"
  local emit_log="$work_dir/emit.out"
  local link_log="$work_dir/link.out"
  local run_log="$work_dir/run.out"
  local companion="${source%.jiang}.c"
  local companion_args=()
  if [ -f "$companion" ]; then
    companion_args+=("$companion")
  fi

  if ! "$JIANGC" --emit-llvm -o "$llvm_output" "$source" >"$emit_log" 2>&1; then
    echo "FAIL run $source emit failed"
    sed -n '1,120p' "$emit_log"
    record_failure
    return
  fi

  if ! "$run_clang" "$llvm_output" ${companion_args[@]+"${companion_args[@]}"} -o "$executable" \
    ${sanitizer_args[@]+"${sanitizer_args[@]}"} "${llvm_link_args[@]}" \
    >"$link_log" 2>&1; then
    echo "FAIL run $source link failed"
    sed -n '1,120p' "$link_log"
    record_failure
    return
  fi

  local expected
  expected="$(sed -n 's/^.*expected-exit:[[:space:]]*//p' "$source" | head -n 1)"
  if [ -z "$expected" ]; then
    expected=0
  fi

  set +e
  ${sanitizer_env[@]+"${sanitizer_env[@]}"} bash -c '"$1"; exit $?' _ "$executable" \
    >"$run_log" 2>&1
  local code=$?
  set -e

  if [ "$code" = "$expected" ]; then
    rm -f "$llvm_output" "$executable"
    echo "PASS run $source"
    return
  fi

  echo "FAIL run $source exited $code, expected $expected"
  sed -n '1,120p' "$run_log"
  record_failure
}

run_release_case() {
  local source="$1"
  local work_dir
  work_dir="$(case_work_dir release "$source")"
  mkdir -p "$work_dir"
  local executable="$work_dir/test"
  local companion="${source%.jiang}.c"
  local companion_object="$work_dir/companion.o"
  local companion_log="$work_dir/companion.out"
  local build_log="$work_dir/build.out"
  local run_log="$work_dir/run.out"
  local companion_link_args=()
  if [ -f "$companion" ]; then
    if ! "$LLVM_CLANG" -c "$companion" -o "$companion_object" >"$companion_log" 2>&1; then
      echo "FAIL release-run $source companion compile failed"
      sed -n '1,120p' "$companion_log"
      record_failure
      return
    fi
    companion_link_args+=(--link-arg "$companion_object")
  fi

  if ! "$JIANGC" \
    --mode release \
    "${jiang_llvm_link_args[@]}" \
    ${companion_link_args[@]+"${companion_link_args[@]}"} \
    -o "$executable" \
    "$source" >"$build_log" 2>&1; then
    echo "FAIL release-run $source build failed"
    sed -n '1,120p' "$build_log"
    record_failure
    return
  fi

  local expected
  expected="$(sed -n 's/^.*expected-exit:[[:space:]]*//p' "$source" | head -n 1)"
  if [ -z "$expected" ]; then
    expected=0
  fi

  set +e
  bash -c '"$1"; exit $?' _ "$executable" >"$run_log" 2>&1
  local code=$?
  set -e

  if [ "$code" = "$expected" ]; then
    rm -f "$executable" "$companion_object"
    echo "PASS release-run $source"
    return
  fi

  echo "FAIL release-run $source exited $code, expected $expected"
  sed -n '1,120p' "$run_log"
  record_failure
}

if [ -z "$TEST_SANITIZER" ]; then
  while IFS= read -r source; do
    if ! case_selected "$source"; then continue; fi
    run_check_case "$source"
  done < <(find "$TEST_ROOT" -path '*/check/*.jiang' -type f | sort)

  while IFS= read -r source; do
    if ! case_selected "$source"; then continue; fi
    run_fail_case "$source"
  done < <(find "$TEST_ROOT" -path '*/fail/*.jiang' -type f | sort)

  while IFS= read -r source; do
    if ! case_selected "$source"; then continue; fi
    run_emit_case "$source"
  done < <(find "$TEST_ROOT" -path '*/emit/*.jiang' -type f | sort)
fi

if [ -x "$LLVM_CLANG" ]; then
  while IFS= read -r source; do
    if ! case_selected "$source"; then continue; fi
    if [ -n "$TEST_RUN_FILTER" ] && [[ ! "$source" =~ $TEST_RUN_FILTER ]]; then
      continue
    fi
    run_run_case "$source"
  done < <(find "$TEST_ROOT" -path '*/run/*.jiang' -type f | sort)
else
  echo "SKIP run cases: missing LLVM_CLANG=$LLVM_CLANG"
fi

if [ "${TEST_RELEASE_RUNS:-${LANG_CHECK_RELEASE_RUNS:-0}}" = "1" ]; then
  while IFS= read -r source; do
    if ! case_selected "$source"; then continue; fi
    run_release_case "$source"
  done < <(find "$TEST_ROOT" -path '*/run/*.jiang' -type f | sort)
fi

exit "$status"
