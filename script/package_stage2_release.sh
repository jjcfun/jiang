#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
DIST_ROOT="$PROJECT_ROOT/dist"

VERSION="${1:-v0.2.0}"
TAG="${VERSION}"
VERSION_NO_V="${VERSION#v}"

OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"
TARGET="${OS}-${ARCH}"

RELEASE_DIR="$DIST_ROOT/$VERSION"
STAGE2_BIN="$BUILD_DIR/stage2c"
SOURCE_TARBALL="$RELEASE_DIR/jiang-$VERSION-src.tar.xz"
BINARY_STAGING="$RELEASE_DIR/jiang-$VERSION-$TARGET"
BINARY_TARBALL="$RELEASE_DIR/jiang-$VERSION-$TARGET.tar.xz"
SHA_FILE="$RELEASE_DIR/SHA256SUMS"
RELEASE_NOTES_SRC="$PROJECT_ROOT/releases/$VERSION.md"
RELEASE_NOTES_DST="$RELEASE_DIR/RELEASES-$VERSION.md"

rm -rf "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR"

bash "$PROJECT_ROOT/script/build_stage2.sh"
bash "$PROJECT_ROOT/script/stage2_selfhost_smoke.sh"
bash "$PROJECT_ROOT/script/stage2_complete_smoke.sh"

git archive --format=tar --prefix="jiang-$VERSION_NO_V/" "$TAG" | xz > "$SOURCE_TARBALL"

mkdir -p "$BINARY_STAGING/bin"
cp "$STAGE2_BIN" "$BINARY_STAGING/bin/jiang"
cp "$PROJECT_ROOT/compiler/BOOTSTRAP.md" "$BINARY_STAGING/BOOTSTRAP.md"
cp "$PROJECT_ROOT/compiler/README.md" "$BINARY_STAGING/README-stage2.md"
tar -C "$RELEASE_DIR" -cJf "$BINARY_TARBALL" "jiang-$VERSION-$TARGET"
rm -rf "$BINARY_STAGING"

cp "$RELEASE_NOTES_SRC" "$RELEASE_NOTES_DST"

(
    cd "$RELEASE_DIR"
    shasum -a 256 \
        "$(basename "$SOURCE_TARBALL")" \
        "$(basename "$BINARY_TARBALL")" \
        "$(basename "$RELEASE_NOTES_DST")" \
        > "$SHA_FILE"
)

echo "packaged Stage2 release in $RELEASE_DIR"
