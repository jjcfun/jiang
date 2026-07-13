#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JIANGC="${JIANGC:-$ROOT_DIR/build/bin/jiangc}"

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

run_check_case() {
  local source="$1"

  local code
  if "$JIANGC" --check "$source" >/tmp/jiang_lang_check.out 2>&1; then
    echo "PASS check $source"
    return
  else
    code=$?
  fi

  echo "FAIL check $source exited $code"
  sed -n '1,120p' /tmp/jiang_lang_check.out
  status=1
}

run_fail_case() {
  local source="$1"

  if "$JIANGC" --check "$source" >/tmp/jiang_lang_fail.out 2>&1; then
    echo "FAIL fail $source unexpectedly passed"
    status=1
    return
  fi

  local expected
  expected="$(sed -n 's/^.*expected:[[:space:]]*//p' "$source" | head -n 1)"
  if [ -n "$expected" ] && ! grep -q "$expected" /tmp/jiang_lang_fail.out; then
    echo "FAIL fail $source missing expected diagnostic: $expected"
    sed -n '1,120p' /tmp/jiang_lang_fail.out
    status=1
    return
  fi

  echo "PASS fail $source"
}

run_emit_case() {
  local source="$1"
  local output="/tmp/jiang_lang_emit_$(basename "$source").ll"

  if "$JIANGC" --emit-llvm -o "$output" "$source" >/tmp/jiang_lang_emit.out 2>&1; then
    echo "PASS emit $source"
    rm -f "$output"
    return
  fi

  local code=$?
  echo "FAIL emit $source exited $code"
  sed -n '1,120p' /tmp/jiang_lang_emit.out
  status=1
}

run_run_case() {
  local source="$1"
  local stem
  stem="$(basename "$source" .jiang)"
  local llvm_output="/tmp/jiang_lang_run_${stem}.ll"
  local executable="/tmp/jiang_lang_run_${stem}"
  local companion="${source%.jiang}.c"
  local companion_args=()
  if [ -f "$companion" ]; then
    companion_args+=("$companion")
  fi

  if ! "$JIANGC" --emit-llvm -o "$llvm_output" "$source" >/tmp/jiang_lang_run_emit.out 2>&1; then
    echo "FAIL run $source emit failed"
    sed -n '1,120p' /tmp/jiang_lang_run_emit.out
    status=1
    return
  fi

  if ! "$LLVM_CLANG" "$llvm_output" ${companion_args[@]+"${companion_args[@]}"} -o "$executable" \
    "${llvm_link_args[@]}" >/tmp/jiang_lang_run_link.out 2>&1; then
    echo "FAIL run $source link failed"
    sed -n '1,120p' /tmp/jiang_lang_run_link.out
    status=1
    return
  fi

  local expected
  expected="$(sed -n 's/^.*expected-exit:[[:space:]]*//p' "$source" | head -n 1)"
  if [ -z "$expected" ]; then
    expected=0
  fi

  set +e
  bash -c '"$1"; exit $?' _ "$executable" >/tmp/jiang_lang_run.out 2>&1
  local code=$?
  set -e
  rm -f "$llvm_output" "$executable"

  if [ "$code" = "$expected" ]; then
    echo "PASS run $source"
    return
  fi

  echo "FAIL run $source exited $code, expected $expected"
  sed -n '1,120p' /tmp/jiang_lang_run.out
  status=1
}

run_release_case() {
  local source="$1"
  local stem
  stem="$(basename "$source" .jiang)"
  local executable="/tmp/jiang_lang_release_${stem}"
  local companion="${source%.jiang}.c"
  local companion_object="/tmp/jiang_lang_release_${stem}_companion.o"
  local companion_link_args=()
  if [ -f "$companion" ]; then
    if ! "$LLVM_CLANG" -c "$companion" -o "$companion_object" >/tmp/jiang_lang_release_companion.out 2>&1; then
      echo "FAIL release-run $source companion compile failed"
      sed -n '1,120p' /tmp/jiang_lang_release_companion.out
      status=1
      return
    fi
    companion_link_args+=(--link-arg "$companion_object")
  fi

  if ! "$JIANGC" \
    --mode release \
    "${jiang_llvm_link_args[@]}" \
    ${companion_link_args[@]+"${companion_link_args[@]}"} \
    -o "$executable" \
    "$source" >/tmp/jiang_lang_release_build.out 2>&1; then
    echo "FAIL release-run $source build failed"
    sed -n '1,120p' /tmp/jiang_lang_release_build.out
    status=1
    return
  fi

  local expected
  expected="$(sed -n 's/^.*expected-exit:[[:space:]]*//p' "$source" | head -n 1)"
  if [ -z "$expected" ]; then
    expected=0
  fi

  set +e
  bash -c '"$1"; exit $?' _ "$executable" >/tmp/jiang_lang_release_run.out 2>&1
  local code=$?
  set -e
  rm -f "$executable" "$companion_object"

  if [ "$code" = "$expected" ]; then
    echo "PASS release-run $source"
    return
  fi

  echo "FAIL release-run $source exited $code, expected $expected"
  sed -n '1,120p' /tmp/jiang_lang_release_run.out
  status=1
}

while IFS= read -r source; do
  run_check_case "$source"
done < <(find test/lang -path '*/check/*.jiang' -type f | sort)

while IFS= read -r source; do
  run_fail_case "$source"
done < <(find test/lang -path '*/fail/*.jiang' -type f | sort)

while IFS= read -r source; do
  run_emit_case "$source"
done < <(find test/lang -path '*/emit/*.jiang' -type f | sort)

if [ -x "$LLVM_CLANG" ]; then
  while IFS= read -r source; do
    run_run_case "$source"
  done < <(find test/lang -path '*/run/*.jiang' -type f | sort)
else
  echo "SKIP run cases: missing LLVM_CLANG=$LLVM_CLANG"
fi

if [ "${LANG_CHECK_RELEASE_RUNS:-0}" = "1" ]; then
  while IFS= read -r source; do
    run_release_case "$source"
  done < <(find test/lang -path '*/run/*.jiang' -type f | sort)
fi

exit "$status"
