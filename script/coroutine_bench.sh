#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JIANGC="${JIANGC:-$ROOT_DIR/build/bin/jiangc.next}"
JIANG_BENCH_RUNS="${JIANG_BENCH_RUNS:-5}"

source "$ROOT_DIR/script/llvm_env.sh"

SOURCE="$ROOT_DIR/benchmark/coroutine/task_baseline.jiang"
COMPANION="$ROOT_DIR/benchmark/coroutine/task_baseline.c"
OUTPUT_DIR="$ROOT_DIR/build/benchmark"
LLVM_OUTPUT="$OUTPUT_DIR/coroutine_task_baseline.ll"
EXECUTABLE="$OUTPUT_DIR/coroutine_task_baseline"

if [ ! -x "$JIANGC" ]; then
  echo "missing compiler: $JIANGC" >&2
  exit 2
fi

mkdir -p "$OUTPUT_DIR"

llvm_link_args=()
for arg in \
  $("$LLVM_CONFIG" --link-static --ldflags) \
  $("$LLVM_CONFIG" --link-static --libs all) \
  $("$LLVM_CONFIG" --link-static --system-libs) \
  $(jiang_macos_sdkroot_link_args) \
  $(jiang_llvm_cxx_runtime_link_args)
do
  llvm_link_args+=("$arg")
done

"$JIANGC" --mode release --emit-llvm -o "$LLVM_OUTPUT" "$SOURCE"
"$LLVM_CLANG" -O3 "$LLVM_OUTPUT" "$COMPANION" -o "$EXECUTABLE" "${llvm_link_args[@]}"

run=1
while [ "$run" -le "$JIANG_BENCH_RUNS" ]; do
  printf 'run=%s\n' "$run"
  "$EXECUTABLE"
  run=$((run + 1))
done
