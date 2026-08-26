#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist}"
PACKAGE_VERSION="$(sed -n 's/^[[:space:]]*version[[:space:]]*=[[:space:]]*//p' "$ROOT_DIR/package.ini" | head -n 1)"
VERSION="${VERSION:-$PACKAGE_VERSION}"
TARGET="${TARGET:-}"
JIANGC_BIN="${JIANGC_BIN:-$BUILD_DIR/bin/jiangc}"

source "$ROOT_DIR/script/llvm_env.sh"

PACKAGE_NAME="jiang-$VERSION-$TARGET"
PACKAGE_DIR="$DIST_DIR/$PACKAGE_NAME"
ARCHIVE_FORMAT=""
PACKAGE_ARCHIVE=""

require_command() {
  local name="$1"
  if ! command -v "$name" >/dev/null 2>&1; then
    echo "missing required command: $name" >&2
    exit 2
  fi
}

configure_target() {
  case "$TARGET:$(uname -s):$(uname -m)" in
    macos-arm64:Darwin:arm64|macos-arm64:Darwin:aarch64)
      ARCHIVE_FORMAT="zip"
      PACKAGE_ARCHIVE="$DIST_DIR/$PACKAGE_NAME.zip"
      require_command zip
      require_command otool
      ;;
    linux-x86_64:Linux:x86_64|linux-x86_64:Linux:amd64)
      ARCHIVE_FORMAT="tar.gz"
      PACKAGE_ARCHIVE="$DIST_DIR/$PACKAGE_NAME.tar.gz"
      require_command tar
      require_command readelf
      require_command ldd
      ;;
    macos-arm64:*|linux-x86_64:*)
      echo "package target $TARGET does not match host $(uname -s) $(uname -m)" >&2
      exit 2
      ;;
    *)
      echo "unsupported release target: $TARGET" >&2
      exit 2
      ;;
  esac
}

check_dynamic_dependencies() {
  local binary="$1"
  local dependencies
  if [ "$TARGET" = "macos-arm64" ]; then
    dependencies="$(otool -L "$binary")"
  else
    readelf -d "$binary" >/dev/null
    dependencies="$(ldd "$binary")"
  fi
  printf '%s\n' "$dependencies"
  if grep -E 'lib(LLVM|lld)' <<<"$dependencies" >/dev/null; then
    echo "release compiler must not dynamically depend on LLVM/lld" >&2
    exit 1
  fi
}

write_install_script() {
  cat >"$PACKAGE_DIR/install.sh" <<'INSTALL_SH'
#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(sed -n 's/^[[:space:]]*version[[:space:]]*=[[:space:]]*//p' "$ROOT_DIR/package.ini" | head -n 1)"
PREFIX="${PREFIX:-$HOME/.jiang}"
VERSION_DIR="$PREFIX/versions/$VERSION"

case "$VERSION" in
  (*[!A-Za-z0-9._+-]*|'')
    echo "invalid package version: $VERSION" >&2
    exit 2
    ;;
esac

mkdir -p "$VERSION_DIR" "$PREFIX/bin"
rm -rf "$VERSION_DIR/bin"
cp -R "$ROOT_DIR/bin" "$VERSION_DIR/bin"
cp "$ROOT_DIR/package.ini" "$VERSION_DIR/package.ini"
cp "$ROOT_DIR/package.ini" "$PREFIX/package.ini"
chmod +x "$VERSION_DIR/bin/jiangc"
ln -sfn "../versions/$VERSION/bin/jiangc" "$PREFIX/bin/jiangc"

echo "Installed Jiang $VERSION to $VERSION_DIR"
echo
echo "Add this to your shell profile if needed:"
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
echo
echo "Test:"
echo "  jiangc --version"
INSTALL_SH
  chmod +x "$PACKAGE_DIR/install.sh"
}

write_package_readme() {
  local llvm_version="$1"
  cat >"$PACKAGE_DIR/README.md" <<README
# Jiang $VERSION ($TARGET)

This package statically links LLVM into \`jiangc\`; users do not need an LLVM runtime.
A hosted C linker driver named \`cc\` must be available when building executables.

Install Jiang:

\`\`\`bash
./install.sh
\`\`\`

The default prefix is \`~/.jiang\`. Use a custom prefix with:

\`\`\`bash
PREFIX=/usr/local ./install.sh
\`\`\`

Build-time LLVM: $llvm_version
README
}

create_archive() {
  if [ "$ARCHIVE_FORMAT" = "zip" ]; then
    (
      cd "$DIST_DIR"
      zip -qry "$PACKAGE_NAME.zip" "$PACKAGE_NAME"
    )
    return
  fi
  tar -czf "$PACKAGE_ARCHIVE" -C "$DIST_DIR" "$PACKAGE_NAME"
}

cd "$ROOT_DIR"

case "$VERSION" in
  (*[!A-Za-z0-9._+-]*|'')
    echo "invalid VERSION=$VERSION; expected [A-Za-z0-9._+-]+" >&2
    exit 2
    ;;
esac

configure_target

if [ ! -x "$JIANGC_BIN" ]; then
  echo "missing compiler: $JIANGC_BIN" >&2
  echo "run: BOOTSTRAP_CHECK_MODE=audit BOOTSTRAP_DEPTH=stable VERIFY=full \\" >&2
  echo "  bash ./script/build_next.sh" >&2
  exit 2
fi
if [ ! -f "$JIANGC_BIN.build-id" ]; then
  echo "missing compiler build id: $JIANGC_BIN.build-id" >&2
  exit 2
fi

actual_version="$("$JIANGC_BIN" --version | sed -n '1p')"
if [ "$actual_version" != "jiang $VERSION" ]; then
  echo "compiler version mismatch: expected 'jiang $VERSION', got '$actual_version'" >&2
  exit 2
fi

rm -rf "$PACKAGE_DIR" "$PACKAGE_ARCHIVE"
mkdir -p "$PACKAGE_DIR/bin" "$PACKAGE_DIR/script"
cp "$JIANGC_BIN" "$PACKAGE_DIR/bin/jiangc"
cp "$JIANGC_BIN.build-id" "$PACKAGE_DIR/bin/jiangc.build-id"
cp "$ROOT_DIR/package.ini" "$PACKAGE_DIR/package.ini"
cp "$ROOT_DIR/script/install_llvm.sh" "$PACKAGE_DIR/script/install_llvm.sh"
chmod +x "$PACKAGE_DIR/bin/jiangc" "$PACKAGE_DIR/script/install_llvm.sh"

write_install_script
write_package_readme "$("$LLVM_CONFIG" --version)"
"$PACKAGE_DIR/bin/jiangc" --version >/dev/null
check_dynamic_dependencies "$PACKAGE_DIR/bin/jiangc"
if [ "$TARGET" = "linux-x86_64" ]; then
  bash "$ROOT_DIR/script/linux_release_abi_audit.sh" \
    "$PACKAGE_DIR/bin/jiangc" >"$PACKAGE_DIR/ABI.txt"
fi
create_archive

printf 'OK package: %s\n' "$PACKAGE_ARCHIVE"
