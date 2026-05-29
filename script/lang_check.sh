#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JIANGC="${JIANGC:-$ROOT_DIR/build/jiangc}"
LLVM_CLANG="${LLVM_CLANG:-/opt/homebrew/opt/llvm@21/bin/clang}"
LLVM_LIB_DIR="${LLVM_LIB_DIR:-/opt/homebrew/opt/llvm@21/lib}"

if [ ! -x "$JIANGC" ]; then
  echo "missing compiler: $JIANGC" >&2
  echo "set JIANGC=/path/to/jiangc or build $ROOT_DIR/build/jiangc first" >&2
  exit 2
fi

cd "$ROOT_DIR"

status=0

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

  if ! "$JIANGC" --emit-llvm -o "$llvm_output" "$source" >/tmp/jiang_lang_run_emit.out 2>&1; then
    echo "FAIL run $source emit failed"
    sed -n '1,120p' /tmp/jiang_lang_run_emit.out
    status=1
    return
  fi

  if ! "$LLVM_CLANG" "$llvm_output" -o "$executable" -L"$LLVM_LIB_DIR" -lLLVM >/tmp/jiang_lang_run_link.out 2>&1; then
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
  "$executable" >/tmp/jiang_lang_run.out 2>&1
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

exit "$status"
