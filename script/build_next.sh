#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
NEXT_BIN="${NEXT_BIN:-$BUILD_DIR/jiangc.next}"
JIANGC_BIN="${JIANGC_BIN:-$BUILD_DIR/jiangc}"
VERIFY="${VERIFY:-full}"
BOOTSTRAP_DEPTH="${BOOTSTRAP_DEPTH:-next}"
PACKAGE_VERSION="$(sed -n 's/^[[:space:]]*version[[:space:]]*=[[:space:]]*//p' "$ROOT_DIR/package.ini" | head -n 1)"
JIANG_VERSION="${JIANG_VERSION:-$PACKAGE_VERSION}"
OPTIONS_FILE="$ROOT_DIR/src/driver/options.jiang"

source "$ROOT_DIR/script/llvm_env.sh"

mkdir -p "$BUILD_DIR"
cd "$ROOT_DIR"

BOOTSTRAP_BIN="${BOOTSTRAP_BIN:-$(command -v jiangc || true)}"
if [ -z "$BOOTSTRAP_BIN" ] || [ ! -x "$BOOTSTRAP_BIN" ]; then
  echo "missing bootstrap compiler: jiangc" >&2
  echo "install a supported Jiang bootstrap compiler so jiangc is on PATH, or set BOOTSTRAP_BIN" >&2
  exit 2
fi
BOOTSTRAP_VERSION="$("$BOOTSTRAP_BIN" --version | sed -n '1p')"
case "$BOOTSTRAP_VERSION" in
  "jiang 0.3.1"|"jiang 0.3.2") ;;
  *)
    echo "unsupported bootstrap compiler: $BOOTSTRAP_VERSION" >&2
    echo "install Jiang 0.3.1 or 0.3.2 so jiangc is on PATH, or set BOOTSTRAP_BIN" >&2
    exit 2
    ;;
esac

CLANG_BIN="$LLVM_CLANG"

case "$VERIFY" in
  none|smoke|full) ;;
  *)
    echo "invalid VERIFY=$VERIFY; expected none, smoke, or full" >&2
    exit 2
    ;;
esac

case "$BOOTSTRAP_DEPTH" in
  next|stable) ;;
  *)
    echo "invalid BOOTSTRAP_DEPTH=$BOOTSTRAP_DEPTH; expected next or stable" >&2
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

perl -0pi -e 's/public UInt8\[\]&? default_compiler_version\(\) \{\n    return "[^"]*";\n\}/public UInt8[]& default_compiler_version() {\n    return "'"$JIANG_VERSION"'";\n}/' "$OPTIONS_FILE"

link_llvm() {
  local input_ll="$1"
  local output_bin="$2"
  "$CLANG_BIN" "$input_ll" -o "$output_bin" \
    $("$LLVM_CONFIG" --ldflags) \
    $("$LLVM_CONFIG" --libs all) \
    $("$LLVM_CONFIG" --system-libs)
  test -x "$output_bin"
}

emit_next_from_bootstrap() {
  local output_bin="$1"
  local output_ll="$BUILD_DIR/jiangc.next.ll"
  printf '== build next: emit next llvm with %s (%s) ==\n' "$BOOTSTRAP_BIN" "$BOOTSTRAP_VERSION"
  "$BOOTSTRAP_BIN" --emit-llvm src/jiangc.jiang >"$output_ll"
  printf '== build next: link %s ==\n' "$output_bin"
  link_llvm "$output_ll" "$output_bin"
  printf 'OK %s\n' "$output_bin"
}

emit_compiler_with_compiler() {
  local source_bin="$1"
  local output_bin="$2"
  local output_ll="$3"
  test -x "$source_bin"
  printf '== build next: emit llvm with %s ==\n' "$source_bin"
  "$source_bin" --emit-llvm -o "$output_ll" src/jiangc.jiang
  printf '== build next: link %s ==\n' "$output_bin"
  link_llvm "$output_ll" "$output_bin"
  printf 'OK %s\n' "$output_bin"
}

printf '== build next: %s -> next ==\n' "$BOOTSTRAP_VERSION"
emit_next_from_bootstrap "$NEXT_BIN"

VERIFY_BIN="$NEXT_BIN"
if [ "$BOOTSTRAP_DEPTH" = "stable" ]; then
  printf '\n== build stable: next -> jiangc ==\n'
  emit_compiler_with_compiler "$NEXT_BIN" "$JIANGC_BIN" "$BUILD_DIR/jiangc.ll"
  VERIFY_BIN="$JIANGC_BIN"
fi

if [ "$VERIFY" != "none" ]; then
  printf '\n== next verify: smoke with %s ==\n' "$VERIFY_BIN"
  BUILD_DIR="$BUILD_DIR" \
  JIANGC="$VERIFY_BIN" \
  bash "$ROOT_DIR/script/smoke.sh"

  printf '\n== next verify: backend cli smoke ==\n'
  BUILD_DIR="$BUILD_DIR" \
  JIANGC="$VERIFY_BIN" \
  bash "$ROOT_DIR/script/backend_cli_smoke.sh"
fi

if [ "$VERIFY" = "full" ]; then
  printf '\n== next verify: lang check with %s ==\n' "$VERIFY_BIN"
  JIANGC="$VERIFY_BIN" \
  bash "$ROOT_DIR/script/lang_check.sh"
fi

chmod +x "$VERIFY_BIN"

actual_version="$("$VERIFY_BIN" --version | sed -n '1p')"
expected_version="jiang $JIANG_VERSION"
if [ "$actual_version" != "$expected_version" ]; then
  echo "compiler version mismatch: expected '$expected_version', got '$actual_version'" >&2
  exit 1
fi

printf '\nOK compiler: %s\n' "$VERIFY_BIN"
if [ "$BOOTSTRAP_DEPTH" = "next" ]; then
  printf 'stable compiler not rebuilt; run BOOTSTRAP_DEPTH=stable for release verification.\n'
else
  printf 'bootstrap candidate: %s\n' "$NEXT_BIN"
fi
