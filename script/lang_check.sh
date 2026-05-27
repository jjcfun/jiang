#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JIANGC="${JIANGC:-$ROOT_DIR/build/jiangc}"

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

while IFS= read -r source; do
  run_check_case "$source"
done < <(find test/lang -path '*/check/*.jiang' -type f | sort)

while IFS= read -r source; do
  run_fail_case "$source"
done < <(find test/lang -path '*/fail/*.jiang' -type f | sort)

exit "$status"
