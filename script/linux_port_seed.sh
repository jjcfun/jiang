#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
SEED_DIR="${SEED_DIR:-$BUILD_DIR/linux-port-seed}"
SEED_OBJECT="${SEED_OBJECT:-$SEED_DIR/jiangc-x86_64-linux-gnu.o}"
SEED_BIN="${SEED_BIN:-$BUILD_DIR/bin/jiangc.linux-port-seed}"
ABI_PROBE_BIN="${ABI_PROBE_BIN:-$SEED_DIR/linux-hosted-abi-probe}"
BOOTSTRAP_RELEASE_VERSION="${BOOTSTRAP_RELEASE_VERSION:-0.5.0}"
JIANG_HOME="${JIANG_HOME:-$HOME/.jiang}"
BOOTSTRAP_BIN="${BOOTSTRAP_BIN:-$JIANG_HOME/versions/$BOOTSTRAP_RELEASE_VERSION/bin/jiangc}"
TARGET="x86_64-unknown-linux-gnu"

usage() {
  echo "usage: bash ./script/linux_port_seed.sh emit-object|link" >&2
}

sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1"
    return
  fi
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1"
    return
  fi
  echo "missing sha256sum or shasum" >&2
  exit 2
}

require_bootstrap() {
  if [ ! -x "$BOOTSTRAP_BIN" ]; then
    echo "missing Jiang $BOOTSTRAP_RELEASE_VERSION stable compiler: $BOOTSTRAP_BIN" >&2
    exit 2
  fi
  local actual_version
  actual_version="$("$BOOTSTRAP_BIN" --version | sed -n '1p')"
  if [ "$actual_version" != "jiang $BOOTSTRAP_RELEASE_VERSION" ]; then
    echo "unsupported bootstrap compiler: $actual_version" >&2
    exit 2
  fi
}

emit_object() {
  require_bootstrap
  mkdir -p "$SEED_DIR"
  cd "$ROOT_DIR"
  "$BOOTSTRAP_BIN" \
    --mode release \
    --target "$TARGET" \
    --emit-obj \
    -o "$SEED_OBJECT" \
    src/jiangc.jiang
  test -s "$SEED_OBJECT"
  file "$SEED_OBJECT" | grep -Eq 'ELF 64-bit.*x86-64'
  sha256_file "$SEED_OBJECT"
  printf 'OK Linux port seed object: %s\n' "$SEED_OBJECT"
}

collect_llvm_link_args() {
  LLVM_LINK_ARGS=()
  local arg
  for arg in \
    $("$LLVM_CONFIG" --link-static --ldflags) \
    $("$LLVM_CONFIG" --link-static --libs all) \
    $("$LLVM_CONFIG" --link-static --system-libs) \
    $(jiang_llvm_cxx_runtime_link_args)
  do
    LLVM_LINK_ARGS+=("$arg")
  done
}

link_seed() {
  if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
    echo "link requires a Linux x86_64 host" >&2
    exit 2
  fi
  if [ ! -s "$SEED_OBJECT" ]; then
    echo "missing Linux port seed object: $SEED_OBJECT" >&2
    exit 2
  fi
  source "$ROOT_DIR/script/llvm_env.sh"
  collect_llvm_link_args
  mkdir -p "$BUILD_DIR/bin" "$SEED_DIR"
  "$LLVM_CLANG" \
    "$ROOT_DIR/test/compiler/fixture/linux_hosted_abi.c" \
    -pthread \
    -ldl \
    -o "$ABI_PROBE_BIN"
  "$ABI_PROBE_BIN"
  cp "$ROOT_DIR/package.ini" "$BUILD_DIR/package.ini"
  "$LLVM_CLANG" -no-pie "$SEED_OBJECT" "${LLVM_LINK_ARGS[@]}" -o "$SEED_BIN"
  chmod +x "$SEED_BIN"
  sha256_file "$SEED_BIN" | awk '{print $1}' >"$SEED_BIN.build-id"
  "$SEED_BIN" --version
  sha256_file "$SEED_BIN"
  printf 'OK Linux port seed compiler: %s\n' "$SEED_BIN"
}

case "${1:-}" in
  emit-object)
    emit_object
    ;;
  link)
    link_seed
    ;;
  *)
    usage
    exit 2
    ;;
esac
