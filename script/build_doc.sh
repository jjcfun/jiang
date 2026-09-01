#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
JIANGC="${JIANGC:-$BUILD_DIR/bin/jiangc.next}"
OUTPUT="${OUTPUT:-$BUILD_DIR/bin/jiangdoc}"
ARTIFACT_CACHE_DIR="${ARTIFACT_CACHE_DIR:-$BUILD_DIR/artifact-cache/jiangdoc}"

source "$ROOT_DIR/script/llvm_env.sh"

llvm_link_args=()
for arg in \
  $("$LLVM_CONFIG" --link-static --ldflags) \
  $("$LLVM_CONFIG" --link-static --libs all) \
  $("$LLVM_CONFIG" --link-static --system-libs) \
  $(jiang_macos_sdkroot_link_args) \
  $(jiang_llvm_cxx_runtime_link_args)
do
  llvm_link_args+=(--link-arg "$arg")
done

mkdir -p "$(dirname "$OUTPUT")"
"$JIANGC" \
  --artifact-cache-dir "$ARTIFACT_CACHE_DIR" \
  --linker "$LLVM_CLANG" \
  "${llvm_link_args[@]}" \
  -o "$OUTPUT" \
  "$ROOT_DIR/src/tool/doc/main.jiang"
chmod +x "$OUTPUT"
printf 'OK %s\n' "$OUTPUT"
