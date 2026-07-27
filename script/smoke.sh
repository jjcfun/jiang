#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
JIANGC="${JIANGC:-$BUILD_DIR/bin/jiangc}"

cd "$ROOT_DIR"

status=0
slow_smokes="${JIANG_SLOW_SMOKE:-}"
quick_profile="$ROOT_DIR/test/profile/compiler-smoke.txt"
slow_profile="$ROOT_DIR/test/profile/compiler-slow-smoke.txt"

printf '\n== compiler smoke profile ==\n'
if TEST_ROOT=test/compiler TEST_LIST="$quick_profile" JIANGC="$JIANGC" \
  bash "$ROOT_DIR/script/test.sh"
then
  echo "OK"
else
  status=1
fi

if [ "$slow_smokes" = "1" ]; then
  printf '\n== compiler slow smoke profile ==\n'
  if TEST_ROOT=test/compiler TEST_LIST="$slow_profile" JIANGC="$JIANGC" \
    bash "$ROOT_DIR/script/test.sh"
  then
    echo "OK"
  else
    status=1
  fi
else
  echo "SKIP slow smoke profile; set JIANG_SLOW_SMOKE=1 to run"
fi

exit "$status"
