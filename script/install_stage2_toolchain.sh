#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST_ROOT="$PROJECT_ROOT/dist"

VERSION="${1:-v0.2.0}"
OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"
TARGET="${OS}-${ARCH}"

ARCHIVE="${2:-$DIST_ROOT/$VERSION/jiang-$VERSION-$TARGET.tar.xz}"
TOOLCHAIN_ROOT="${JIANG_TOOLCHAIN_ROOT:-$HOME/.jiang/toolchains}"
BIN_LINK_DIR="${JIANG_BIN_DIR:-$HOME/.jiang/bin}"
INSTALL_DIR="$TOOLCHAIN_ROOT/$VERSION"
TEMP_DIR="$(mktemp -d)"

cleanup() {
    rm -rf "$TEMP_DIR"
}
trap cleanup EXIT

if [ ! -f "$ARCHIVE" ]; then
    echo "missing archive: $ARCHIVE" >&2
    echo "run: bash ./script/package_stage2_release.sh $VERSION" >&2
    exit 1
fi

mkdir -p "$TOOLCHAIN_ROOT" "$BIN_LINK_DIR"
rm -rf "$INSTALL_DIR"
tar -C "$TEMP_DIR" -xJf "$ARCHIVE"
mv "$TEMP_DIR/jiang-$VERSION-$TARGET" "$INSTALL_DIR"

ln -sf "$INSTALL_DIR/bin/jiang" "$BIN_LINK_DIR/jiang"

echo "installed Jiang toolchain to $INSTALL_DIR"
echo "bootstrap compiler path:"
echo "  $INSTALL_DIR/bin/jiang"
echo "convenience symlink:"
echo "  $BIN_LINK_DIR/jiang"
echo "use it with:"
echo "  export STAGE2_BOOTSTRAP_STAGE2=$INSTALL_DIR/bin/jiang"
