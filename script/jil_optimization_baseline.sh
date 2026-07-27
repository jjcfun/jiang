#!/usr/bin/env bash
set -euo pipefail

# P4 JIL/backend 优化的可复现基线。
#
# 输出固定记录 compiler/target、冷编译时间、产物大小、运行时间以及
# 普通 tail 与 coroutine musttail 的 LLVM 结构计数。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JIANGC="${JIANGC:-$ROOT_DIR/build/bin/jiangc.next}"
HOST_CC="${HOST_CC:-cc}"
JIANG_P4_RUNS="${JIANG_P4_RUNS:-3}"
OUTPUT_DIR="${JIANG_P4_OUTPUT_DIR:-$ROOT_DIR/build/benchmark/p4}"
TAIL_SOURCE="$ROOT_DIR/benchmark/jil/tail_recursion.jiang"
COROUTINE_SOURCE="$ROOT_DIR/benchmark/coroutine/task_baseline.jiang"
DRIVER_SOURCE="$ROOT_DIR/benchmark/coroutine/compile_driver.c"

fail() {
  printf 'JIL optimization baseline failed: %s\n' "$*" >&2
  exit 1
}

count_ir_pattern() {
  local pattern="$1"
  local path="$2"
  local count
  count="$(grep -c "$pattern" "$path" || true)"
  printf '%s\n' "$count"
}

run_repeated() {
  local driver="$1"
  local label="$2"
  local executable="$3"
  local run=1
  "$executable"
  while [ "$run" -le "$JIANG_P4_RUNS" ]; do
    "$driver" "${label}-${run}" "$executable"
    run=$((run + 1))
  done
}

[ -x "$JIANGC" ] || fail "missing compiler: $JIANGC"
command -v "$HOST_CC" >/dev/null 2>&1 || fail "missing host C compiler: $HOST_CC"

mkdir -p "$OUTPUT_DIR"
CACHE_DIR="$(mktemp -d "$OUTPUT_DIR/cache.XXXXXX")"
"$HOST_CC" -O2 "$DRIVER_SOURCE" -o "$OUTPUT_DIR/measure"

printf 'compiler=%s\n' "$("$JIANGC" --version | tr '\n' ' ')"
printf 'host=%s %s\n' "$(uname -s)" "$(uname -m)"
printf 'runs=%s\n' "$JIANG_P4_RUNS"

"$OUTPUT_DIR/measure" compile-debug \
  "$JIANGC" --artifact-cache-dir "$CACHE_DIR/debug" \
  -o "$OUTPUT_DIR/tail-debug" "$TAIL_SOURCE"
"$OUTPUT_DIR/measure" compile-release \
  "$JIANGC" --artifact-cache-dir "$CACHE_DIR/release" --mode release \
  -o "$OUTPUT_DIR/tail-release" "$TAIL_SOURCE"
"$OUTPUT_DIR/measure" emit-tail-llvm \
  "$JIANGC" --artifact-cache-dir "$CACHE_DIR/tail-llvm" --mode release \
  --emit-llvm -o "$OUTPUT_DIR/tail.ll" "$TAIL_SOURCE"
"$OUTPUT_DIR/measure" emit-coroutine-llvm \
  "$JIANGC" --artifact-cache-dir "$CACHE_DIR/coroutine-llvm" --mode release \
  --emit-llvm -o "$OUTPUT_DIR/coroutine.ll" "$COROUTINE_SOURCE"

printf 'size_debug_bytes=%s\n' "$(wc -c <"$OUTPUT_DIR/tail-debug" | tr -d ' ')"
printf 'size_release_bytes=%s\n' "$(wc -c <"$OUTPUT_DIR/tail-release" | tr -d ' ')"
printf 'tail_ir_plain_tail_calls=%s\n' \
  "$(count_ir_pattern '  tail call' "$OUTPUT_DIR/tail.ll")"
printf 'tail_ir_musttail_calls=%s\n' "$(count_ir_pattern 'musttail call' "$OUTPUT_DIR/tail.ll")"
printf 'coroutine_ir_musttail_calls=%s\n' \
  "$(count_ir_pattern 'musttail call' "$OUTPUT_DIR/coroutine.ll")"

run_repeated "$OUTPUT_DIR/measure" runtime-debug "$OUTPUT_DIR/tail-debug"
run_repeated "$OUTPUT_DIR/measure" runtime-release "$OUTPUT_DIR/tail-release"
