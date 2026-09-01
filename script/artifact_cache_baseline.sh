#!/usr/bin/env bash
set -euo pipefail

# P3 object artifact 的公共工作负载基线。
#
# 三个 compiler 都编译当前 worktree 的同一份 compiler 源码，避免把源码规模变化
# 误算成 compiler 性能变化。当前源码必须保持可由 0.4.9 bootstrap 读取。
# 每个 compiler/mode 使用独立源码副本和 cache。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STABLE_JIANGC="${STABLE_JIANGC:-$HOME/.jiang/versions/0.4.9/bin/jiangc}"
BRIDGE_JIANGC="${BRIDGE_JIANGC:-$ROOT_DIR/../bootstrap-0.5.0/build/bin/jiangc.next}"
CURRENT_JIANGC="${CURRENT_JIANGC:-$ROOT_DIR/build/bin/jiangc.next}"
BASELINE_FILTER="${BASELINE_FILTER:-}"
BASELINE_MODE_FILTER="${BASELINE_MODE_FILTER:-}"
BASELINE_SKIP_HOT="${BASELINE_SKIP_HOT:-0}"
BASELINE_SKIP_PRIVATE="${BASELINE_SKIP_PRIVATE:-0}"
BASELINE_SKIP_PUBLIC="${BASELINE_SKIP_PUBLIC:-0}"
BASELINE_JIL_STATS="${BASELINE_JIL_STATS:-0}"
BASELINE_KEEP_WORK="${BASELINE_KEEP_WORK:-0}"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/jiang-artifact-baseline.XXXXXX")"
COMMON_SOURCE="$WORK_DIR/common"
SUCCESS=0

cleanup() {
  if [ "$SUCCESS" = "1" ] && [ "$BASELINE_KEEP_WORK" != "1" ]; then
    rm -rf "$WORK_DIR"
    return
  fi
  printf 'artifact baseline files: %s\n' "$WORK_DIR" >&2
}

trap cleanup EXIT

source "$ROOT_DIR/script/llvm_env.sh"

LLVM_LINK_ARGS=()
for arg in \
  $("$LLVM_CONFIG" --link-static --ldflags) \
  $("$LLVM_CONFIG" --link-static --libs all) \
  $("$LLVM_CONFIG" --link-static --system-libs) \
  $(jiang_macos_sdkroot_link_args) \
  $(jiang_llvm_cxx_runtime_link_args)
do
  LLVM_LINK_ARGS+=(--link-arg "$arg")
done

mkdir -p "$COMMON_SOURCE"
cp "$ROOT_DIR/package.ini" "$COMMON_SOURCE/package.ini"
cp -R "$ROOT_DIR/src" "$COMMON_SOURCE/"
# 0.4.9 的 compiler-known core 入口仍是旧路径；只转换入口的相对路径，
# 不维护旧语义。
sed 's|"core/|"|g' \
  "$COMMON_SOURCE/src/compiler/core.jiang" >"$COMMON_SOURCE/src/compiler/core/core.jiang"

append_benchmark_function() {
  local source="$1/src/compiler/support/hash.jiang"
  printf '%s\n' \
    '' \
    'Int artifact_benchmark_private() {' \
    '    1' \
    '}' >>"$source"
}

change_private_body() {
  local source="$1/src/compiler/support/hash.jiang"
  perl -0pi -e \
    's/Int artifact_benchmark_private\(\) \{\n    1\n\}/Int artifact_benchmark_private() {\n    2\n}/' \
    "$source"
  grep -Fq '    2' "$source"
}

change_public_interface() {
  local source="$1/src/compiler/support/hash.jiang"
  perl -0pi -e \
    's/Int artifact_benchmark_private\(\)/public Int artifact_benchmark_private()/' \
    "$source"
  grep -Fq 'public Int artifact_benchmark_private()' "$source"
}

compile_once() {
  local label="$1"
  local compiler="$2"
  local supports_artifacts="$3"
  local mode="$4"
  local scenario="$5"
  local work="$6"
  local log="$work/${scenario}.log"
  local timing="$work/${scenario}.time"
  local args=(--target "$JIANG_HOST_TARGET" --linker "$LLVM_CLANG")
  args+=("${LLVM_LINK_ARGS[@]}")
  if [ "$supports_artifacts" = "1" ]; then
    args+=(--artifact-cache-dir "$work/cache" --artifact-stats)
  fi
  if [ "$BASELINE_JIL_STATS" = "1" ]; then
    args+=(--jil-stats)
  fi
  if [ "$mode" = "release" ]; then
    args+=(--mode release)
  fi
  args+=(-o "$work/jiangc" src/compiler/jiangc.jiang)

  if ! (cd "$work/source" && /usr/bin/time -p "$compiler" "${args[@]}") \
    >"$log" 2>"$timing"
  then
    cat "$log" >&2
    cat "$timing" >&2
    return 1
  fi
  local seconds
  local stats
  seconds="$(sed -n 's/^real //p' "$timing" | tail -n 1)"
  stats="$(sed -n '/artifact_interface_hit=/p' "$timing" | tail -n 1)"
  printf '%s\t%s\t%s\t%s\t%s\n' "$label" "$mode" "$scenario" "$seconds" "$stats"
  if [ "$BASELINE_JIL_STATS" = "1" ]; then
    sed -n -e '/^jil_stage=/p' -e '/^model_/p' -e '/^backend_/p' "$timing" >&2
  fi
}

run_mode() {
  local label="$1"
  local compiler="$2"
  local supports_artifacts="$3"
  local mode="$4"
  if [ -n "$BASELINE_MODE_FILTER" ] && [[ ! "$mode" =~ $BASELINE_MODE_FILTER ]]; then
    return
  fi
  local work="$WORK_DIR/$label-$mode"
  mkdir -p "$work/source"
  cp -R "$COMMON_SOURCE/." "$work/source/"
  append_benchmark_function "$work/source"

  compile_once "$label" "$compiler" "$supports_artifacts" "$mode" cold "$work"
  if [ "$BASELINE_SKIP_HOT" != "1" ]; then
    compile_once "$label" "$compiler" "$supports_artifacts" "$mode" hot "$work"
  fi
  if [ "$BASELINE_SKIP_PRIVATE" = "1" ]; then
    return
  fi
  change_private_body "$work/source"
  compile_once "$label" "$compiler" "$supports_artifacts" "$mode" private-body "$work"
  if [ "$BASELINE_SKIP_PUBLIC" = "1" ]; then
    return
  fi
  change_public_interface "$work/source"
  compile_once "$label" "$compiler" "$supports_artifacts" "$mode" public-interface "$work"
}

run_compiler() {
  local label="$1"
  local compiler="$2"
  local supports_artifacts="$3"
  if [ -n "$BASELINE_FILTER" ] && [[ ! "$label" =~ $BASELINE_FILTER ]]; then
    return
  fi
  [ -x "$compiler" ] || {
    printf 'missing compiler: %s\n' "$compiler" >&2
    exit 2
  }
  run_mode "$label" "$compiler" "$supports_artifacts" debug
  run_mode "$label" "$compiler" "$supports_artifacts" release
}

printf 'compiler\tmode\tscenario\tseconds\tartifact_stats\n'
run_compiler stable-0.4.9 "$STABLE_JIANGC" 0
run_compiler whole-package-bridge "$BRIDGE_JIANGC" 1
run_compiler current "$CURRENT_JIANGC" 1

SUCCESS=1
