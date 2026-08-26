#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
SMOKE_DIR="${RELEASE_SMOKE_DIR:-$BUILD_DIR/release-smoke}"
DIST_DIR="${DIST_DIR:-$SMOKE_DIR/dist}"
UNPACK_DIR="$SMOKE_DIR/unpack"
INSTALL_PREFIX="$SMOKE_DIR/prefix"
PACKAGE_VERSION="$(sed -n 's/^[[:space:]]*version[[:space:]]*=[[:space:]]*//p' "$ROOT_DIR/package.ini" | head -n 1)"
VERSION="${VERSION:-$PACKAGE_VERSION}"
RELEASE_SMOKE_BUILD="${RELEASE_SMOKE_BUILD:-1}"
COMPILER_BUILD_MODE="${COMPILER_BUILD_MODE:-release}"
JIANGC_BIN="${JIANGC_BIN:-$BUILD_DIR/bin/jiangc}"
LINKER="${RELEASE_SMOKE_LINKER:-cc}"

TARGET=""
PACKAGE_NAME=""
PACKAGE_ARCHIVE=""
PACKAGE_DIR=""
PACKAGE_SCRIPT=""

require_command() {
  local name="$1"
  if ! command -v "$name" >/dev/null 2>&1; then
    echo "missing required command: $name" >&2
    exit 2
  fi
}

configure_host() {
  case "$(uname -s):$(uname -m)" in
    Darwin:arm64|Darwin:aarch64)
      TARGET="macos-arm64"
      PACKAGE_ARCHIVE="$DIST_DIR/jiang-$VERSION-$TARGET.zip"
      PACKAGE_SCRIPT="$ROOT_DIR/script/package_macos_release.sh"
      require_command unzip
      require_command otool
      ;;
    Linux:x86_64|Linux:amd64)
      TARGET="linux-x86_64"
      PACKAGE_ARCHIVE="$DIST_DIR/jiang-$VERSION-$TARGET.tar.gz"
      PACKAGE_SCRIPT="$ROOT_DIR/script/package_linux_release.sh"
      require_command tar
      require_command readelf
      require_command ldd
      ;;
    *)
      echo "unsupported release smoke host: $(uname -s) $(uname -m)" >&2
      exit 2
      ;;
  esac
  PACKAGE_NAME="jiang-$VERSION-$TARGET"
  PACKAGE_DIR="$UNPACK_DIR/$PACKAGE_NAME"
}

check_binary_version() {
  local binary="$1"
  local actual
  actual="$("$binary" --version | sed -n '1p')"
  if [ "$actual" != "jiang $VERSION" ]; then
    echo "compiler version mismatch: expected 'jiang $VERSION', got '$actual'" >&2
    exit 1
  fi
}

check_dynamic_dependencies() {
  local binary="$1"
  local dependencies
  if [ "$TARGET" = "macos-arm64" ]; then
    dependencies="$(otool -L "$binary")"
  else
    readelf -d "$binary"
    dependencies="$(ldd "$binary")"
  fi
  printf '%s\n' "$dependencies"
  if grep -E 'lib(LLVM|lld)' <<<"$dependencies" >/dev/null; then
    echo "release compiler must not dynamically depend on LLVM/lld" >&2
    exit 1
  fi
}

unpack_archive() {
  if [ "$TARGET" = "macos-arm64" ]; then
    unzip -q "$PACKAGE_ARCHIVE" -d "$UNPACK_DIR"
    return
  fi
  tar -xzf "$PACKAGE_ARCHIVE" -C "$UNPACK_DIR"
}

compile_and_run_samples() {
  local compiler="$1"
  local hello="$SMOKE_DIR/hello"
  local capability="$SMOKE_DIR/hosted-capability"
  local hello_output

  "$compiler" --linker "$LINKER" -o "$hello" "$ROOT_DIR/test/release/hello.jiang"
  hello_output="$("$hello")"
  if [ "$hello_output" != "Hello from Jiang" ]; then
    echo "unexpected Hello output: $hello_output" >&2
    exit 1
  fi

  "$compiler" --linker "$LINKER" -o "$capability" \
    "$ROOT_DIR/test/release/hosted_capability.jiang"
  "$capability" release-smoke
}

cd "$ROOT_DIR"
configure_host
require_command "$LINKER"

case "$RELEASE_SMOKE_BUILD" in
  0|1) ;;
  *)
    echo "invalid RELEASE_SMOKE_BUILD=$RELEASE_SMOKE_BUILD; expected 0 or 1" >&2
    exit 2
    ;;
esac

printf '== release smoke: LLVM toolchain ==\n'
if ! bash ./script/llvm_env.sh >/dev/null 2>&1; then
  bash ./script/install_llvm.sh --local >/dev/null
fi
source ./script/llvm_env.sh
printf 'LLVM %s at %s\n' "$LLVM_VERSION" "$LLVM_ROOT"

if [ "$RELEASE_SMOKE_BUILD" = "1" ]; then
  printf '\n== release smoke: stable compiler ==\n'
  COMPILER_BUILD_MODE="$COMPILER_BUILD_MODE" \
  BOOTSTRAP_CHECK_MODE=audit \
  BOOTSTRAP_DEPTH=stable \
  VERIFY=none \
    bash ./script/build_next.sh
fi
check_binary_version "$JIANGC_BIN"
check_dynamic_dependencies "$JIANGC_BIN"

printf '\n== release smoke: package ==\n'
rm -rf "$SMOKE_DIR"
mkdir -p "$DIST_DIR" "$UNPACK_DIR"
BUILD_DIR="$BUILD_DIR" DIST_DIR="$DIST_DIR" VERSION="$VERSION" \
  JIANGC_BIN="$JIANGC_BIN" bash "$PACKAGE_SCRIPT"
test -s "$PACKAGE_ARCHIVE"

unpack_archive
test -x "$PACKAGE_DIR/bin/jiangc"
check_binary_version "$PACKAGE_DIR/bin/jiangc"
check_dynamic_dependencies "$PACKAGE_DIR/bin/jiangc"

printf '\n== release smoke: isolated install ==\n'
PREFIX="$INSTALL_PREFIX" "$PACKAGE_DIR/install.sh" >/dev/null
test -x "$INSTALL_PREFIX/versions/$VERSION/bin/jiangc"
test -L "$INSTALL_PREFIX/bin/jiangc"
check_binary_version "$INSTALL_PREFIX/bin/jiangc"
check_dynamic_dependencies "$INSTALL_PREFIX/bin/jiangc"
compile_and_run_samples "$INSTALL_PREFIX/bin/jiangc"

printf '\nOK release smoke: %s\n' "$PACKAGE_ARCHIVE"
