#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist}"
PACKAGE_VERSION="$(sed -n 's/^[[:space:]]*version[[:space:]]*=[[:space:]]*//p' "$ROOT_DIR/package.ini" | head -n 1)"
VERSION="${VERSION:-$PACKAGE_VERSION}"
TARGET="${TARGET:-macos-arm64}"
JIANGC_BIN="${JIANGC_BIN:-$BUILD_DIR/jiangc}"
LLVM_CONFIG="${LLVM_CONFIG:-/opt/homebrew/opt/llvm@21/bin/llvm-config}"

PACKAGE_NAME="jiang-$VERSION-$TARGET"
PACKAGE_DIR="$DIST_DIR/$PACKAGE_NAME"
PACKAGE_ZIP="$DIST_DIR/$PACKAGE_NAME.zip"

cd "$ROOT_DIR"

case "$VERSION" in
  (*[!A-Za-z0-9._+-]*|'')
    echo "invalid VERSION=$VERSION; expected [A-Za-z0-9._+-]+" >&2
    exit 2
    ;;
esac

if [ ! -x "$JIANGC_BIN" ]; then
  echo "missing compiler: $JIANGC_BIN" >&2
  echo "run: VERIFY=full bash ./script/build_next.sh" >&2
  exit 2
fi

if [ ! -x "$LLVM_CONFIG" ]; then
  echo "missing llvm-config: $LLVM_CONFIG" >&2
  echo "run: bash ./script/install_llvm_macos.sh" >&2
  exit 2
fi

if ! command -v zip >/dev/null 2>&1; then
  echo "missing zip command" >&2
  exit 2
fi

llvm_version="$("$LLVM_CONFIG" --version)"
llvm_lib_dir="$("$LLVM_CONFIG" --libdir)"
llvm_dylib="$llvm_lib_dir/libLLVM.dylib"
if [ ! -f "$llvm_dylib" ]; then
  echo "missing LLVM dylib: $llvm_dylib" >&2
  exit 2
fi

actual_version="$("$JIANGC_BIN" --version | sed -n '1p')"
expected_version="jiang $VERSION"
if [ "$actual_version" != "$expected_version" ]; then
  echo "compiler version mismatch: expected '$expected_version', got '$actual_version'" >&2
  echo "run: JIANG_VERSION=$VERSION VERIFY=full bash ./script/build_next.sh" >&2
  exit 2
fi

rm -rf "$PACKAGE_DIR" "$PACKAGE_ZIP"
mkdir -p "$PACKAGE_DIR/bin" "$PACKAGE_DIR/script"

cp "$JIANGC_BIN" "$PACKAGE_DIR/bin/jiangc"
cp "$ROOT_DIR/script/install_llvm_macos.sh" "$PACKAGE_DIR/script/install_llvm_macos.sh"
chmod +x "$PACKAGE_DIR/bin/jiangc" "$PACKAGE_DIR/script/install_llvm_macos.sh"

cat >"$PACKAGE_DIR/install.sh" <<'INSTALL_SH'
#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
PACKAGE_NAME="$(basename "$ROOT_DIR")"
VERSION="${PACKAGE_NAME#jiang-}"
VERSION="${VERSION%-macos-arm64}"
PREFIX="${PREFIX:-$HOME/.jiang}"
VERSION_DIR="$PREFIX/versions/$VERSION"

if ! command -v otool >/dev/null 2>&1; then
  echo "missing otool; install Xcode Command Line Tools." >&2
  exit 2
fi

llvm_dylib="$(otool -L "$ROOT_DIR/bin/jiangc" | awk '/libLLVM\.dylib/ { print $1; exit }')"
if [ -z "$llvm_dylib" ] || [ ! -f "$llvm_dylib" ]; then
  echo "missing llvm@21 runtime dependency." >&2
  if [ -n "$llvm_dylib" ]; then
    echo "Expected dylib: $llvm_dylib" >&2
  fi
  echo "Install it with:" >&2
  echo "  bash \"$ROOT_DIR/script/install_llvm_macos.sh\"" >&2
  exit 2
fi

mkdir -p "$VERSION_DIR" "$PREFIX/bin"
rm -rf "$VERSION_DIR/bin"
cp -R "$ROOT_DIR/bin" "$VERSION_DIR/bin"
chmod +x "$VERSION_DIR/bin/jiangc"

ln -sfn "../versions/$VERSION/bin/jiangc" "$PREFIX/bin/jiangc"

echo "Installed Jiang $VERSION to $VERSION_DIR"
echo
echo "LLVM dependency:"
echo "  $llvm_dylib"
echo
echo "Add this to your shell profile if needed:"
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
echo
echo "Test:"
echo "  jiangc --version"
INSTALL_SH
chmod +x "$PACKAGE_DIR/install.sh"

cat >"$PACKAGE_DIR/README.md" <<README
# Jiang $VERSION ($TARGET)

This package depends on macOS arm64 LLVM 21 at runtime.

Install LLVM first:

\`\`\`bash
bash ./script/install_llvm_macos.sh
\`\`\`

Then install Jiang:

\`\`\`bash
./install.sh
\`\`\`

By default this installs to:

\`\`\`text
~/.jiang/versions/$VERSION
~/.jiang/bin/jiangc
\`\`\`

Use a custom prefix with:

\`\`\`bash
PREFIX=/usr/local ./install.sh
\`\`\`

Build-time LLVM detected by the release script:

\`\`\`text
LLVM $llvm_version
$llvm_dylib
\`\`\`
README

"$PACKAGE_DIR/bin/jiangc" --version >/dev/null

(
  cd "$DIST_DIR"
  zip -qry "$PACKAGE_NAME.zip" "$PACKAGE_NAME"
)

printf 'OK package: %s\n' "$PACKAGE_ZIP"
otool -L "$PACKAGE_DIR/bin/jiangc"
