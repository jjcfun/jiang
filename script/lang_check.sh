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

  if "$JIANGC" --check "$source" >/tmp/jiang_lang_check.out 2>&1; then
    echo "OK check $source"
    return
  fi

  local code=$?
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

  echo "OK fail $source"
}

for source in test/lang/check/*.jiang; do
  [ -e "$source" ] || continue
  run_check_case "$source"
done

for source in test/lang/fail/*.jiang; do
  [ -e "$source" ] || continue
  run_fail_case "$source"
done

exit "$status"
