#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
JIANGC="${JIANGC:-$BUILD_DIR/bin/jiangc}"
SMOKE_DIR="${SMOKE_DIR:-$BUILD_DIR/smoke/linux-hosted-process}"

source "$ROOT_DIR/script/llvm_env.sh"

if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
  echo "Linux hosted process smoke requires a Linux x86_64 host" >&2
  exit 2
fi
if [ ! -x "$JIANGC" ]; then
  echo "missing Linux Jiang compiler: $JIANGC" >&2
  exit 2
fi

mkdir -p "$SMOKE_DIR"
cd "$ROOT_DIR"

TEST_ROOT=test/compiler \
TEST_FILTER='system/run/process\.jiang' \
TEST_TIMEOUT=60 \
JIANGC="$JIANGC" \
bash "$ROOT_DIR/script/test.sh"

process_filter='process/run/process_(arguments|path_lookup)'
process_filter="$process_filter|system/run/process_(pipe_stdout|pipe_large_stdout|stderr_discard)"
TEST_ROOT=test/lang \
TEST_FILTER="$process_filter" \
TEST_TIMEOUT=60 \
JIANGC="$JIANGC" \
bash "$ROOT_DIR/script/test.sh"

discard_bin="$SMOKE_DIR/process_stderr_discard"
discard_stdout="$SMOKE_DIR/process_stderr_discard.stdout"
discard_stderr="$SMOKE_DIR/process_stderr_discard.stderr"
"$JIANGC" -o "$discard_bin" test/lang/system/run/process_stderr_discard.jiang
"$discard_bin" >"$discard_stdout" 2>"$discard_stderr"
test "$(wc -c <"$discard_stdout" | tr -d ' ')" = "0"
test ! -s "$discard_stderr"

echo "OK Linux hosted process smoke"
