#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JIANGC="${JIANGC:-$ROOT_DIR/build/bin/jiangc.next}"
HOST_CC="${CC:-cc}"
JIANG_COMPILE_BENCH_BRANCHES="${JIANG_COMPILE_BENCH_BRANCHES:-32}"
JIANG_COMPILE_BENCH_RUNS="${JIANG_COMPILE_BENCH_RUNS:-3}"
JIANG_COMPILE_BENCH_SHAPE="${JIANG_COMPILE_BENCH_SHAPE:-branch_task}"

OUTPUT_DIR="$ROOT_DIR/build/benchmark"
SOURCE="$OUTPUT_DIR/coroutine_large_cfg.jiang"
LLVM_OUTPUT="$OUTPUT_DIR/coroutine_large_cfg.ll"
DRIVER="$OUTPUT_DIR/coroutine_compile_driver"

mkdir -p "$OUTPUT_DIR"

generate_source() {
  {
    printf '%s\n' 'struct CompileBenchDomain: Domain<kind = .serial> {}'
    printf '%s\n' 'const CompileBenchDomain CompileBench = CompileBenchDomain();'
    printf '%s\n' 'async [CompileBench] Int leaf(Int value) { value }'
    printf '%s\n' 'async [CompileBench] Int large_cfg(Int seed) {'
    printf '%s\n' '    Int total! = seed;'
    local index=0
    while [ "$index" -lt "$JIANG_COMPILE_BENCH_BRANCHES" ]; do
      if [ "$JIANG_COMPILE_BENCH_SHAPE" = "direct_branch" ]; then
        printf '%s\n' '    if (total >= 0) {'
        printf '        total = total + leaf(%s);\n' "$index"
        printf '%s\n' '    } else {'
        printf '        total = total - leaf(%s);\n' "$index"
        printf '%s\n' '    }'
      elif [ "$JIANG_COMPILE_BENCH_SHAPE" = "task" ]; then
        printf '%s\n' '    {'
        printf '        Task<Int> task = async [CompileBench] leaf(%s);\n' "$index"
        printf '%s\n' '        total = total + task.await();'
        printf '%s\n' '    }'
      else
        printf '%s\n' '    if (total >= 0) {'
        printf '        Task<Int> task = async [CompileBench] leaf(%s);\n' "$index"
        printf '%s\n' '        total = total + task.await();'
        printf '%s\n' '    } else {'
        printf '        total = total - leaf(%s);\n' "$index"
        printf '%s\n' '    }'
      fi
      index=$((index + 1))
    done
    printf '%s\n' '    total'
    printf '%s\n' '}'
    printf '%s\n' 'Int main() { sync [CompileBench] { large_cfg(0) } }'
  } >"$SOURCE"
}

run_case() {
  local label="$1"
  shift
  local run=1
  while [ "$run" -le "$JIANG_COMPILE_BENCH_RUNS" ]; do
    "$DRIVER" "$label" "$@"
    run=$((run + 1))
  done
}

generate_source
"$HOST_CC" -O2 "$ROOT_DIR/benchmark/coroutine/compile_driver.c" -o "$DRIVER"

printf 'shape=%s branches=%s runs=%s\n' \
  "$JIANG_COMPILE_BENCH_SHAPE" "$JIANG_COMPILE_BENCH_BRANCHES" "$JIANG_COMPILE_BENCH_RUNS"
run_case jil "$JIANGC" --mode release --jil-stats --check "$SOURCE"
run_case llvm "$JIANGC" --mode release --jil-stats --emit-llvm -o "$LLVM_OUTPUT" "$SOURCE"
