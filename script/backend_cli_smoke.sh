#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE1_BIN="${STAGE1_BIN:-$HOME/.jiang/stage1/bin/jiangc}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
SMOKE_BUILD_DIR="$BUILD_DIR/smoke/stage2_backend_cli"
LLVM_CONFIG="${LLVM_CONFIG:-/opt/homebrew/opt/llvm@21/bin/llvm-config}"

mkdir -p "$SMOKE_BUILD_DIR"
cd "$ROOT_DIR"

clang_bin="$("$LLVM_CONFIG" --bindir)/clang"
compiler_ll="$SMOKE_BUILD_DIR/jiangc.ll"
compiler_bin="$SMOKE_BUILD_DIR/jiangc"
sample="$SMOKE_BUILD_DIR/minimal.jiang"
sample_ll="$SMOKE_BUILD_DIR/minimal.ll"
sample_obj="$SMOKE_BUILD_DIR/minimal.o"
sample_from_ll="$SMOKE_BUILD_DIR/minimal_from_ll"
sample_from_obj="$SMOKE_BUILD_DIR/minimal_from_obj"
sample_with_link_arg="$SMOKE_BUILD_DIR/minimal_with_link_arg"
sample_release_obj="$SMOKE_BUILD_DIR/minimal_release.o"
sample_from_release_obj="$SMOKE_BUILD_DIR/minimal_from_release_obj"
sample_release_bin="$SMOKE_BUILD_DIR/minimal_release"
package_release_bin="$SMOKE_BUILD_DIR/package_release"
field_sample="$SMOKE_BUILD_DIR/field_projection.jiang"
field_ll="$SMOKE_BUILD_DIR/field_projection.ll"
field_bin="$SMOKE_BUILD_DIR/field_projection"

printf 'Int main() { 0 }\n' >"$sample"
printf 'struct Pair { Int left; Int right; }\nInt get_left(Pair p) { p.left }\nInt main() { 0 }\n' >"$field_sample"

"$STAGE1_BIN" --emit-llvm src/jiangc.jiang >"$compiler_ll"
"$clang_bin" "$compiler_ll" -o "$compiler_bin" \
  $("$LLVM_CONFIG" --ldflags) \
  $("$LLVM_CONFIG" --libs all) \
  $("$LLVM_CONFIG" --system-libs)

"$compiler_bin" --emit-llvm -o "$sample_ll" "$sample"
test -s "$sample_ll"
"$clang_bin" "$sample_ll" -o "$sample_from_ll" \
  $("$LLVM_CONFIG" --ldflags) \
  $("$LLVM_CONFIG" --libs all) \
  $("$LLVM_CONFIG" --system-libs)
"$sample_from_ll"

"$compiler_bin" --emit-obj -o "$sample_obj" "$sample"
test -s "$sample_obj"
"$clang_bin" "$sample_obj" -o "$sample_from_obj" \
  $("$LLVM_CONFIG" --ldflags) \
  $("$LLVM_CONFIG" --libs all) \
  $("$LLVM_CONFIG" --system-libs)
"$sample_from_obj"

"$compiler_bin" --link-arg -Wl,-dead_strip -o "$sample_with_link_arg" "$sample"
"$sample_with_link_arg"

"$compiler_bin" --mode release --emit-obj -o "$sample_release_obj" "$sample"
test -s "$sample_release_obj"
"$clang_bin" "$sample_release_obj" -o "$sample_from_release_obj" \
  $("$LLVM_CONFIG" --ldflags) \
  $("$LLVM_CONFIG" --libs all) \
  $("$LLVM_CONFIG" --system-libs)
"$sample_from_release_obj"

"$compiler_bin" --mode release -o "$sample_release_bin" "$sample"
"$sample_release_bin"

"$compiler_bin" --mode release -o "$package_release_bin" test/lang/package/run/source_dependency_app
set +e
"$package_release_bin"
package_status=$?
set -e
test "$package_status" -eq 52

"$compiler_bin" --emit-llvm -o "$field_ll" "$field_sample"
test -s "$field_ll"
"$clang_bin" "$field_ll" -o "$field_bin" \
  $("$LLVM_CONFIG" --ldflags) \
  $("$LLVM_CONFIG" --libs all) \
  $("$LLVM_CONFIG" --system-libs)
"$field_bin"

echo "OK"
