#!/usr/bin/env bash
set -euo pipefail

# P3 object artifact 的公共工作负载基线。
#
# 固定 bootstrap stable 与当前 compiler 编译同一份源码，避免把源码规模变化
# 误算成 compiler 性能变化；基准产物不作为后续自举种子。
# 每个 compiler/mode 使用独立源码副本和 cache。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STABLE_JIANGC="${STABLE_JIANGC:-$ROOT_DIR/../bootstrap-0.5.5/build/strict/bin/jiangc}"
CURRENT_JIANGC="${CURRENT_JIANGC:-$ROOT_DIR/build/bin/jiangc.next}"
BASELINE_FILTER="${BASELINE_FILTER:-}"
BASELINE_MODE_FILTER="${BASELINE_MODE_FILTER:-}"
BASELINE_SKIP_HOT="${BASELINE_SKIP_HOT:-0}"
BASELINE_SKIP_PRIVATE="${BASELINE_SKIP_PRIVATE:-0}"
BASELINE_SKIP_PUBLIC="${BASELINE_SKIP_PUBLIC:-0}"
BASELINE_JIL_STATS="${BASELINE_JIL_STATS:-0}"
BASELINE_KEEP_WORK="${BASELINE_KEEP_WORK:-0}"
mkdir -p "$ROOT_DIR/build"
WORK_DIR="$(mktemp -d "$ROOT_DIR/build/artifact-baseline.XXXXXX")"
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
cp "$ROOT_DIR/package.jiang" "$COMMON_SOURCE/package.jiang"
cp -R "$ROOT_DIR/src" "$COMMON_SOURCE/"

# 同一源码副本与工具身份写入报告；各场景只执行下面明确列出的受控改动。
python3 - "$ROOT_DIR" "$WORK_DIR" "$STABLE_JIANGC" "$CURRENT_JIANGC" "$LLVM_CLANG" "$LLVM_CONFIG" <<'PYTHON'
import hashlib
import json
import platform
from pathlib import Path
import subprocess
import sys

repo = Path(sys.argv[1])
work = Path(sys.argv[2])
source = work / "common"
files = [(str(path.relative_to(source)), hashlib.sha256(path.read_bytes()).hexdigest())
         for path in sorted(source.rglob("*")) if path.is_file()]
inputs = {}
for name, value in zip(("bootstrap", "current", "clang", "llvm_config"), sys.argv[3:]):
    path = Path(value).resolve()
    inputs[name] = {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                    "version": subprocess.check_output([str(path), "--version"], text=True)}
identity = {"platform": platform.platform(), "tools": inputs, "source_files": files,
            "source_sha256": hashlib.sha256(json.dumps(files).encode()).hexdigest(),
            "baseline_script_sha256": hashlib.sha256(
                (repo / "script/artifact_cache_baseline.sh").read_bytes()).hexdigest()}
(work / "identity.json").write_text(json.dumps(identity, indent=2) + "\n")
PYTHON

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

  if ! (cd "$work/source" && python3 - "$compiler" "${args[@]}" <<'PYTHON'
import resource
import subprocess
import sys
import time

started = time.perf_counter()
result = subprocess.run(sys.argv[1:])
elapsed = time.perf_counter() - started
peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
if sys.platform != "darwin":
    peak *= 1024
print(f"real {elapsed:.6f}", file=sys.stderr)
print(f"max_rss_bytes {peak}", file=sys.stderr)
raise SystemExit(result.returncode if result.returncode >= 0 else 128 - result.returncode)
PYTHON
  ) \
    >"$log" 2>"$timing"
  then
    cat "$log" >&2
    cat "$timing" >&2
    return 1
  fi
  local seconds
  local stats
  local peak_rss
  peak_rss="$(sed -n 's/^max_rss_bytes //p' "$timing" | tail -n 1)"
  seconds="$(sed -n 's/^real //p' "$timing" | tail -n 1)"
  stats="$(sed -n '/artifact_interface_hit=/p' "$timing" | tail -n 1)"
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$label" "$mode" "$scenario" "$seconds" "$peak_rss" "$stats"
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

printf 'compiler\tmode\tscenario\tseconds\tmax_rss_bytes\tartifact_stats\n'
run_compiler bootstrap-stable "$STABLE_JIANGC" 1
run_compiler current "$CURRENT_JIANGC" 1

SUCCESS=1
