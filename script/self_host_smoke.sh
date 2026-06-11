#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
SMOKE_BUILD_DIR="$BUILD_DIR/smoke/stage2_self_host"
LLVM_CONFIG="${LLVM_CONFIG:-/opt/homebrew/opt/llvm@21/bin/llvm-config}"

mkdir -p "$SMOKE_BUILD_DIR"
cd "$ROOT_DIR"

COMPILER_UNDER_TEST="${JIANGC:-}"
if [ -z "$COMPILER_UNDER_TEST" ]; then
  COMPILER_UNDER_TEST="$(command -v jiangc || true)"
  if [ -z "$COMPILER_UNDER_TEST" ] || [ ! -x "$COMPILER_UNDER_TEST" ]; then
    echo "missing compiler: set JIANGC or put jiangc on PATH" >&2
    echo "install Jiang 0.2 so jiangc is on PATH" >&2
    exit 2
  fi
  COMPILER_VERSION="$("$COMPILER_UNDER_TEST" --version | sed -n '1p')"
  case "$COMPILER_VERSION" in
    "jiang 0.2"|"jiang 0.2."*) ;;
    *)
      echo "unsupported bootstrap compiler: $COMPILER_VERSION" >&2
      echo "install Jiang 0.2 so jiangc is on PATH, or pass JIANGC=<compiler>" >&2
      exit 2
      ;;
  esac
elif [ ! -x "$COMPILER_UNDER_TEST" ]; then
  echo "missing compiler: $COMPILER_UNDER_TEST" >&2
  exit 2
fi
COMPILER_VERSION="$("$COMPILER_UNDER_TEST" --version | sed -n '1p')"

minimal_sample="$SMOKE_BUILD_DIR/minimal.jiang"
field_sample="$SMOKE_BUILD_DIR/field_projection.jiang"
printf 'Int main() { 0 }\n' >"$minimal_sample"
printf 'struct Pair { Int left; Int right; }\nInt get_left(Pair p) { p.left }\nInt main() { 0 }\n' >"$field_sample"

default_sources=(
  "$minimal_sample"
  "$field_sample"
  "test/lang/ownership/check/borrow_reference_basic.jiang"
  "test/lang/ownership/check/ownership_explicit_move.jiang"
)

clang_bin="$("$LLVM_CONFIG" --bindir)/clang"
compiler_ll="$SMOKE_BUILD_DIR/jiangc.stage2.ll"
compiler_bin="$SMOKE_BUILD_DIR/jiangc.stage2"

printf '== self-host smoke: build compiler with %s (%s) ==\n' "$COMPILER_UNDER_TEST" "$COMPILER_VERSION"
"$COMPILER_UNDER_TEST" --emit-llvm src/jiangc.jiang >"$compiler_ll"
"$clang_bin" "$compiler_ll" -o "$compiler_bin" \
  $("$LLVM_CONFIG" --ldflags) \
  $("$LLVM_CONFIG" --libs all) \
  $("$LLVM_CONFIG" --system-libs)

if [[ -n "${STAGE2_SELF_HOST_SOURCES:-}" ]]; then
  read -r -a sources <<<"$STAGE2_SELF_HOST_SOURCES"
else
  sources=("${default_sources[@]}")
fi

for source in "${sources[@]}"; do
  name="$(basename "$source" .jiang)"
  output="$SMOKE_BUILD_DIR/$name.ll"
  printf '\n== stage2 self-host: %s ==\n' "$source"
  "$compiler_bin" --emit-llvm -o "$output" "$source"
  test -s "$output"
  echo "OK"
done

echo "OK"
