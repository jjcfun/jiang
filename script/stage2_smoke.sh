#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE1_BIN="${STAGE1_BIN:-$HOME/.jiang/stage1/bin/jiangc}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
SMOKE_BUILD_DIR="$BUILD_DIR/smoke/stage2"

mkdir -p "$SMOKE_BUILD_DIR"
cd "$ROOT_DIR"

status=0
compile_only_smoke() {
  case "$1" in
    backend_llvm_smoke|compiler_entry_smoke|pipeline_smoke|pipeline_source_binding_smoke|pipeline_source_hir_smoke)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

for source in test/smoke/*.jiang; do
  name="$(basename "$source" .jiang)"
  output="$SMOKE_BUILD_DIR/$name"

  printf '\n== %s ==\n' "$source"
  if compile_only_smoke "$name"; then
    # 导入 backend LLVM FFI 的 smoke 先做编译期验证；
    # 运行期链接 LLVM C library 后再切回可执行 smoke。
    if "$STAGE1_BIN" --emit-llvm "$source" >"$output.ll"; then
      echo "OK"
    else
      code=$?
      echo "FAIL:$code"
      status=1
    fi
  elif "$STAGE1_BIN" -o "$output" "$source"; then
    echo "OK"
  else
    code=$?
    echo "FAIL:$code"
    status=1
  fi
done

exit "$status"
