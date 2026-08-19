#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${ROOT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
SEED_DIR="${SEED_DIR:-$BUILD_DIR/linux-port-seed}"
SEED_OBJECT="${SEED_OBJECT:-$SEED_DIR/jiangc-x86_64-linux-gnu.o}"
SEED_BIN="${SEED_BIN:-$BUILD_DIR/bin/jiangc.linux-port-seed}"
SEED_MANIFEST="${SEED_MANIFEST:-$SEED_DIR/jiangc-x86_64-linux-gnu.manifest}"
ABI_PROBE_BIN="${ABI_PROBE_BIN:-$SEED_DIR/linux-hosted-abi-probe}"
BOOTSTRAP_RELEASE_VERSION="${BOOTSTRAP_RELEASE_VERSION:-0.5.1}"
JIANG_HOME="${JIANG_HOME:-$HOME/.jiang}"
BOOTSTRAP_BIN="${BOOTSTRAP_BIN:-$JIANG_HOME/versions/$BOOTSTRAP_RELEASE_VERSION/bin/jiangc}"
EXPECTED_LLVM_VERSION="${EXPECTED_LLVM_VERSION:-22.1.8}"
EXPECTED_LLVM_REVISION="${EXPECTED_LLVM_REVISION:-ca7933e47d3a3451d81e72ac174dcb5aa28b59d1}"
TARGET="x86_64-unknown-linux-gnu"

usage() {
  echo "usage: bash ./script/linux_port_seed.sh emit-object|link|bootstrap" >&2
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

sha256_digest() {
  sha256_file "$1" | awk '{print $1}'
}

manifest_value() {
  local key="$1"
  sed -n "s/^${key}=//p" "$SEED_MANIFEST" | head -n 1
}

require_manifest_value() {
  local key="$1"
  local expected="$2"
  local actual
  actual="$(manifest_value "$key")"
  if [ "$actual" != "$expected" ]; then
    echo "Linux port seed manifest $key mismatch: expected $expected, got $actual" >&2
    exit 2
  fi
}

source_revision() {
  git -C "$ROOT_DIR" rev-parse HEAD
}

require_clean_source() {
  if [ -n "$(git -C "$ROOT_DIR" status --porcelain)" ]; then
    echo "Linux port seed requires a clean source revision" >&2
    exit 2
  fi
}

require_bootstrap() {
  if [ ! -x "$BOOTSTRAP_BIN" ]; then
    echo "missing Jiang $BOOTSTRAP_RELEASE_VERSION bootstrap compiler: $BOOTSTRAP_BIN" >&2
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
  require_clean_source
  mkdir -p "$SEED_DIR"
  cd "$ROOT_DIR"
  "$BOOTSTRAP_BIN" \
    --target "$TARGET" \
    --emit-obj \
    -o "$SEED_OBJECT" \
    src/jiangc.jiang
  test -s "$SEED_OBJECT"
  file "$SEED_OBJECT" | grep -Eq 'ELF 64-bit.*x86-64'
  {
    printf 'format=1\n'
    printf 'source_revision=%s\n' "$(source_revision)"
    printf 'target=%s\n' "$TARGET"
    printf 'bootstrap_version=%s\n' "$BOOTSTRAP_RELEASE_VERSION"
    printf 'bootstrap_sha256=%s\n' "$(sha256_digest "$BOOTSTRAP_BIN")"
    printf 'emit_command=jiangc --target %s --emit-obj src/jiangc.jiang\n' "$TARGET"
    printf 'object_sha256=%s\n' "$(sha256_digest "$SEED_OBJECT")"
  } >"$SEED_MANIFEST"
  sha256_file "$SEED_OBJECT"
  printf 'OK Linux port seed object: %s\n' "$SEED_OBJECT"
  printf 'OK Linux port seed manifest: %s\n' "$SEED_MANIFEST"
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

require_seed_inputs() {
  if [ ! -s "$SEED_OBJECT" ]; then
    echo "missing Linux port seed object: $SEED_OBJECT" >&2
    exit 2
  fi
  if [ ! -s "$SEED_MANIFEST" ]; then
    echo "missing Linux port seed manifest: $SEED_MANIFEST" >&2
    exit 2
  fi
  require_manifest_value format 1
  require_manifest_value source_revision "$(source_revision)"
  require_manifest_value target "$TARGET"
  require_manifest_value bootstrap_version "$BOOTSTRAP_RELEASE_VERSION"
  require_manifest_value object_sha256 "$(sha256_digest "$SEED_OBJECT")"
}

require_seed_llvm() {
  if [ "$LLVM_VERSION" != "$EXPECTED_LLVM_VERSION" ]; then
    echo "Linux port seed requires LLVM $EXPECTED_LLVM_VERSION, got $LLVM_VERSION" >&2
    exit 2
  fi
}

append_link_manifest() {
  local manifest_tmp
  manifest_tmp="$(mktemp "$SEED_DIR/seed-manifest.XXXXXX")"
  sed '/^llvm_version=/,$d' "$SEED_MANIFEST" >"$manifest_tmp"
  {
    printf 'llvm_version=%s\n' "$LLVM_VERSION"
    printf 'llvm_revision=%s\n' "$EXPECTED_LLVM_REVISION"
    printf 'clang=%s\n' "$LLVM_CLANG"
    printf 'link_command=clang -no-pie <seed-object> <static-llvm-libs> -lstdc++\n'
    printf 'linux_kernel=%s\n' "$(uname -r)"
    printf 'glibc=%s\n' "$(ldd --version | sed -n '1p')"
    printf 'seed_sha256=%s\n' "$(sha256_digest "$SEED_BIN")"
  } >>"$manifest_tmp"
  mv "$manifest_tmp" "$SEED_MANIFEST"
}

append_bootstrap_manifest() {
  local manifest_tmp
  manifest_tmp="$(mktemp "$SEED_DIR/bootstrap-manifest.XXXXXX")"
  sed '/^next_sha256=/,$d' "$SEED_MANIFEST" >"$manifest_tmp"
  {
    printf 'next_sha256=%s\n' "$(sha256_digest "$BUILD_DIR/bin/jiangc.next")"
    printf 'stable_sha256=%s\n' "$(sha256_digest "$BUILD_DIR/bin/jiangc")"
  } >>"$manifest_tmp"
  mv "$manifest_tmp" "$SEED_MANIFEST"
}

link_seed() {
  if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
    echo "link requires a Linux x86_64 host" >&2
    exit 2
  fi
  require_clean_source
  require_seed_inputs
  source "$ROOT_DIR/script/llvm_env.sh"
  require_seed_llvm
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
  sha256_digest "$SEED_BIN" >"$SEED_BIN.build-id"
  "$SEED_BIN" --version
  append_link_manifest
  sha256_file "$SEED_BIN"
  printf 'OK Linux port seed compiler: %s\n' "$SEED_BIN"
  printf 'OK Linux port seed manifest: %s\n' "$SEED_MANIFEST"
}

bootstrap_seed() {
  link_seed
  BOOTSTRAP_BIN="$SEED_BIN" \
  BOOTSTRAP_RELEASE_VERSION="$BOOTSTRAP_RELEASE_VERSION" \
  BOOTSTRAP_DEPTH=stable \
  VERIFY=none \
    bash "$ROOT_DIR/script/build_next.sh"
  "$BUILD_DIR/bin/jiangc.next" --version
  "$BUILD_DIR/bin/jiangc" --version
  append_bootstrap_manifest
  printf 'OK Linux native seed -> next -> stable bootstrap\n'
}

case "${1:-}" in
  emit-object)
    emit_object
    ;;
  link)
    link_seed
    ;;
  bootstrap)
    bootstrap_seed
    ;;
  *)
    usage
    exit 2
    ;;
esac
