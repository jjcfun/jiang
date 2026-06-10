#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
STAGE1_BIN="${STAGE1_BIN:-$HOME/.jiang/stage1/bin/jiangc}"
STAGE2_BIN="${STAGE2_BIN:-$BUILD_DIR/jiangc}"
NEXT_BIN="${NEXT_BIN:-$BUILD_DIR/jiangc.next}"
NEXT2_BIN="${NEXT2_BIN:-$BUILD_DIR/jiangc.next2}"
STABLE_BIN="${STABLE_BIN:-$BUILD_DIR/jiangc.stable}"
VERIFY="${VERIFY:-full}"
PACKAGE_VERSION="$(sed -n 's/^[[:space:]]*version[[:space:]]*=[[:space:]]*//p' "$ROOT_DIR/package.ini" | head -n 1)"
JIANG_VERSION="${JIANG_VERSION:-$PACKAGE_VERSION}"
OPTIONS_FILE="$ROOT_DIR/src/driver/options.jiang"

mkdir -p "$BUILD_DIR"
cd "$ROOT_DIR"

if [ ! -x "$STAGE1_BIN" ]; then
  echo "missing bootstrap compiler: $STAGE1_BIN" >&2
  echo "set STAGE1_BIN=/path/to/jiangc or install the stage1 bootstrap compiler" >&2
  exit 2
fi

case "$VERIFY" in
  none|smoke|full) ;;
  *)
    echo "invalid VERIFY=$VERIFY; expected none, smoke, or full" >&2
    exit 2
    ;;
esac

case "$JIANG_VERSION" in
  (*[!A-Za-z0-9._+-]*|'')
    echo "invalid JIANG_VERSION=$JIANG_VERSION; expected [A-Za-z0-9._+-]+" >&2
    exit 2
    ;;
esac

OPTIONS_FILE_ORIGINAL="$(cat "$OPTIONS_FILE")"
restore_options_file() {
  printf '%s' "$OPTIONS_FILE_ORIGINAL" >"$OPTIONS_FILE"
}
trap restore_options_file EXIT

perl -0pi -e 's/public UInt8\[\] default_compiler_version\(\) \{\n    return "[^"]*";\n\}/public UInt8[] default_compiler_version() {\n    return "'"$JIANG_VERSION"'";\n}/' "$OPTIONS_FILE"

printf '== stable bootstrap: stage1 -> stage2 ==\n'
STAGE1_BIN="$STAGE1_BIN" \
BUILD_DIR="$BUILD_DIR" \
STAGE2_BIN="$STAGE2_BIN" \
bash "$ROOT_DIR/script/build_stage2.sh"

printf '\n== stable bootstrap: stage2 -> next ==\n'
BUILD_DIR="$BUILD_DIR" \
STAGE2_BIN="$STAGE2_BIN" \
NEXT_BIN="$NEXT_BIN" \
bash "$ROOT_DIR/script/build_next.sh"

printf '\n== stable bootstrap: next -> next2 ==\n'
BUILD_DIR="$BUILD_DIR" \
STAGE2_BIN="$NEXT_BIN" \
NEXT_BIN="$NEXT2_BIN" \
bash "$ROOT_DIR/script/build_next.sh"

if [ "$VERIFY" != "none" ]; then
  printf '\n== stable verify: smoke with %s ==\n' "$NEXT2_BIN"
  BUILD_DIR="$BUILD_DIR" \
  JIANGC="$NEXT2_BIN" \
  bash "$ROOT_DIR/script/smoke.sh"

  printf '\n== stable verify: backend cli smoke ==\n'
  STAGE1_BIN="$STAGE2_BIN" \
  BUILD_DIR="$BUILD_DIR" \
  bash "$ROOT_DIR/script/backend_cli_smoke.sh"
fi

if [ "$VERIFY" = "full" ]; then
  printf '\n== stable verify: lang check with %s ==\n' "$NEXT2_BIN"
  JIANGC="$NEXT2_BIN" \
  bash "$ROOT_DIR/script/lang_check.sh"
fi

cp "$NEXT2_BIN" "$STABLE_BIN"
chmod +x "$STABLE_BIN"

actual_version="$("$STABLE_BIN" --version | sed -n '1p')"
expected_version="jiang $JIANG_VERSION"
if [ "$actual_version" != "$expected_version" ]; then
  echo "stable compiler version mismatch: expected '$expected_version', got '$actual_version'" >&2
  exit 1
fi

printf '\nOK stable compiler: %s\n' "$STABLE_BIN"
printf 'candidate source: %s\n' "$NEXT2_BIN"
