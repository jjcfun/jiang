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
BOOTSTRAP_RELEASE_VERSION="0.4.2"
DEFAULT_BOOTSTRAP_BIN="$HOME/.jiang/versions/$BOOTSTRAP_RELEASE_VERSION/bin/jiangc"

source "$ROOT_DIR/script/llvm_env.sh"

mkdir -p "$BUILD_DIR"
cd "$ROOT_DIR"

BOOTSTRAP_BIN="${BOOTSTRAP_BIN:-$DEFAULT_BOOTSTRAP_BIN}"
if [ -z "$BOOTSTRAP_BIN" ] || [ ! -x "$BOOTSTRAP_BIN" ]; then
  echo "missing Jiang $BOOTSTRAP_RELEASE_VERSION release compiler: $BOOTSTRAP_BIN" >&2
  echo "install Jiang $BOOTSTRAP_RELEASE_VERSION release to ~/.jiang/versions/$BOOTSTRAP_RELEASE_VERSION, or set BOOTSTRAP_BIN=/path/to/jiangc" >&2
  exit 2
fi
BOOTSTRAP_VERSION="$("$BOOTSTRAP_BIN" --version | sed -n '1p')"
case "$BOOTSTRAP_VERSION" in
  "jiang $BOOTSTRAP_RELEASE_VERSION") ;;
  *)
    echo "unsupported bootstrap compiler: $BOOTSTRAP_VERSION" >&2
    echo "build_next requires Jiang $BOOTSTRAP_RELEASE_VERSION release; expected $DEFAULT_BOOTSTRAP_BIN or set BOOTSTRAP_BIN" >&2
    exit 2
    ;;
esac

CLANG_BIN="$LLVM_CLANG"
LLVM_LINK_ARGS=()

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

collect_llvm_link_args() {
  local arg
  for arg in $("$LLVM_CONFIG" --ldflags) $("$LLVM_CONFIG" --libs all) $("$LLVM_CONFIG" --system-libs); do
    LLVM_LINK_ARGS+=(--link-arg "$arg")
  done
}

clear_bootstrap_artifact_cache() {
  # bootstrap 编译器可能把旧 schema 的 source artifact 写到默认 build/cache。
  # next/stable 阶段必须用当前编译器重新生成，避免 smoke 读到旧接口。
  rm -rf "$ROOT_DIR/build/cache"
}

emit_next_from_bootstrap() {
  local output_bin="$1"
  printf '== build next: compile executable with %s (%s) ==\n' "$BOOTSTRAP_BIN" "$BOOTSTRAP_VERSION"
  "$BOOTSTRAP_BIN" --linker "$CLANG_BIN" "${LLVM_LINK_ARGS[@]}" -o "$output_bin" src/jiangc.jiang
  test -x "$output_bin"
  printf 'OK %s\n' "$output_bin"
}

emit_compiler_with_compiler() {
  local source_bin="$1"
  local output_bin="$2"
  test -x "$source_bin"
  printf '== build next: compile executable with %s ==\n' "$source_bin"
  "$source_bin" --linker "$CLANG_BIN" "${LLVM_LINK_ARGS[@]}" -o "$output_bin" src/jiangc.jiang
  test -x "$output_bin"
  printf 'OK %s\n' "$output_bin"
}

collect_llvm_link_args

printf '== build next: %s -> next ==\n' "$BOOTSTRAP_VERSION"
clear_bootstrap_artifact_cache
emit_next_from_bootstrap "$NEXT_BIN"
clear_bootstrap_artifact_cache

VERIFY_BIN="$NEXT_BIN"
if [ "$BOOTSTRAP_DEPTH" = "stable" ]; then
  printf '\n== build stable: next -> jiangc ==\n'
  emit_compiler_with_compiler "$NEXT_BIN" "$JIANGC_BIN"
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
