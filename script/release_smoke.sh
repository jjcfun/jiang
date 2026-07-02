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
TARGET="${TARGET:-macos-arm64}"
PACKAGE_NAME="jiang-$VERSION-$TARGET"
PACKAGE_ZIP="$DIST_DIR/$PACKAGE_NAME.zip"
PACKAGE_DIR="$UNPACK_DIR/$PACKAGE_NAME"

cd "$ROOT_DIR"

require_command() {
  local name="$1"
  if ! command -v "$name" >/dev/null 2>&1; then
    echo "missing required command: $name" >&2
    exit 2
  fi
}

check_macos_host() {
  if [ "$(uname -s)" != "Darwin" ]; then
    echo "release smoke currently supports macOS host only" >&2
    exit 2
  fi
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

check_no_llvm_dylib_dependency() {
  local binary="$1"
  local deps
  deps="$(otool -L "$binary")"
  if printf '%s\n' "$deps" | grep -E 'libLLVM|liblld' >/dev/null; then
    echo "release binary must not depend on LLVM/lld dylibs:" >&2
    printf '%s\n' "$deps" >&2
    exit 1
  fi
}

check_macos_host
require_command unzip
require_command zip
require_command otool

printf '== release smoke: LLVM toolchain ==\n'
bash ./script/install_llvm.sh >/dev/null
source ./script/llvm_env.sh
printf 'LLVM %s at %s\n' "$LLVM_VERSION" "$LLVM_ROOT"
printf 'macOS deployment target: %s\n' "${MACOSX_DEPLOYMENT_TARGET:-}"

printf '\n== release smoke: stable compiler ==\n'
BOOTSTRAP_DEPTH=stable VERIFY=none bash ./script/build_next.sh
check_binary_version "$BUILD_DIR/bin/jiangc"
check_no_llvm_dylib_dependency "$BUILD_DIR/bin/jiangc"

printf '\n== release smoke: package ==\n'
rm -rf "$SMOKE_DIR"
mkdir -p "$DIST_DIR" "$UNPACK_DIR"
DIST_DIR="$DIST_DIR" VERSION="$VERSION" TARGET="$TARGET" bash ./script/package_macos_release.sh
test -s "$PACKAGE_ZIP"

unzip -q "$PACKAGE_ZIP" -d "$UNPACK_DIR"
test -x "$PACKAGE_DIR/bin/jiangc"
check_binary_version "$PACKAGE_DIR/bin/jiangc"
check_no_llvm_dylib_dependency "$PACKAGE_DIR/bin/jiangc"

printf '\n== release smoke: install script ==\n'
PREFIX="$INSTALL_PREFIX" "$PACKAGE_DIR/install.sh" >/dev/null
test -x "$INSTALL_PREFIX/versions/$VERSION/bin/jiangc"
test -L "$INSTALL_PREFIX/bin/jiangc"
check_binary_version "$INSTALL_PREFIX/bin/jiangc"

printf '\nOK release smoke: %s\n' "$PACKAGE_ZIP"
