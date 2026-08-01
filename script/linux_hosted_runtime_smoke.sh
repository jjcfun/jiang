#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
JIANGC="${JIANGC:-$BUILD_DIR/bin/jiangc}"

source "$ROOT_DIR/script/llvm_env.sh"

if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
  echo "Linux hosted runtime smoke requires a Linux x86_64 host" >&2
  exit 2
fi
if [ ! -x "$JIANGC" ]; then
  echo "missing Linux Jiang compiler: $JIANGC" >&2
  exit 2
fi

cd "$ROOT_DIR"
runtime_filter='coroutine/run/main_domain_(round_trip|external_resume|shutdown_waits|stress)'
runtime_filter="$runtime_filter|coroutine/run/serial_domain_(isolation|resume_fast_path)"
runtime_filter="$runtime_filter|coroutine/run/concurrent_domain_parallel"
runtime_filter="$runtime_filter|coroutine/run/task_(blocking_wait_interrupted|cross_thread_waiter_stress)"

TEST_ROOT=test/lang \
TEST_FILTER="$runtime_filter" \
TEST_TIMEOUT=60 \
JIANGC="$JIANGC" \
bash "$ROOT_DIR/script/test.sh"

echo "OK Linux hosted runtime smoke"
