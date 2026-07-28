#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
BUILD_BIN_DIR="${BUILD_BIN_DIR:-$BUILD_DIR/bin}"
NEXT_BIN="${NEXT_BIN:-$BUILD_BIN_DIR/jiangc.next}"
JIANGC_BIN="${JIANGC_BIN:-$BUILD_BIN_DIR/jiangc}"
VERIFY="${VERIFY:-full}"
BOOTSTRAP_DEPTH="${BOOTSTRAP_DEPTH:-next}"
PACKAGE_VERSION="$(sed -n 's/^[[:space:]]*version[[:space:]]*=[[:space:]]*//p' "$ROOT_DIR/package.ini" | head -n 1)"
JIANG_VERSION="$PACKAGE_VERSION"
BOOTSTRAP_RELEASE_VERSION="${BOOTSTRAP_RELEASE_VERSION:-0.4.9}"
JIANG_HOME="${JIANG_HOME:-$HOME/.jiang}"
DEFAULT_BOOTSTRAP_BIN="$JIANG_HOME/versions/$BOOTSTRAP_RELEASE_VERSION/bin/jiangc"
BOOTSTRAP_BIN="${BOOTSTRAP_BIN:-$DEFAULT_BOOTSTRAP_BIN}"
NEXT_ARTIFACT_CACHE_DIR="${NEXT_ARTIFACT_CACHE_DIR:-$BUILD_DIR/cache/next}"

source "$ROOT_DIR/script/llvm_env.sh"

mkdir -p \
  "$BUILD_DIR" \
  "$BUILD_BIN_DIR" \
  "$NEXT_ARTIFACT_CACHE_DIR"
cp "$ROOT_DIR/package.ini" "$BUILD_DIR/package.ini"
cd "$ROOT_DIR"

if [ -z "$BOOTSTRAP_BIN" ] || [ ! -x "$BOOTSTRAP_BIN" ]; then
  echo "missing Jiang $BOOTSTRAP_RELEASE_VERSION stable compiler: $BOOTSTRAP_BIN" >&2
  echo "install Jiang $BOOTSTRAP_RELEASE_VERSION to $JIANG_HOME/versions/$BOOTSTRAP_RELEASE_VERSION," >&2
  echo "or set BOOTSTRAP_BIN=/path/to/jiangc" >&2
  exit 2
fi
BOOTSTRAP_VERSION="$("$BOOTSTRAP_BIN" --version | sed -n '1p')"
case "$BOOTSTRAP_VERSION" in
  "jiang $BOOTSTRAP_RELEASE_VERSION") ;;
  *)
    echo "unsupported bootstrap compiler: $BOOTSTRAP_VERSION" >&2
    echo "build_next requires Jiang $BOOTSTRAP_RELEASE_VERSION stable; expected $DEFAULT_BOOTSTRAP_BIN" >&2
    echo "or set BOOTSTRAP_BIN" >&2
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

collect_llvm_link_args() {
  local arg
  for arg in \
    $("$LLVM_CONFIG" --link-static --ldflags) \
    $("$LLVM_CONFIG" --link-static --libs all) \
    $("$LLVM_CONFIG" --link-static --system-libs) \
    $(jiang_macos_sdkroot_link_args) \
    $(jiang_llvm_cxx_runtime_link_args)
  do
    LLVM_LINK_ARGS+=(--link-arg "$arg")
  done
}

write_compiler_build_id() {
  local compiler_bin="$1"
  local build_id
  if command -v shasum >/dev/null 2>&1; then
    build_id="$(shasum -a 256 "$compiler_bin" | awk '{print $1}')"
  elif command -v sha256sum >/dev/null 2>&1; then
    build_id="$(sha256sum "$compiler_bin" | awk '{print $1}')"
  else
    echo "missing shasum or sha256sum for compiler build id" >&2
    exit 2
  fi
  printf '%s\n' "$build_id" >"$compiler_bin.build-id"
}

clear_bootstrap_artifact_cache() {
  # 0.4.9 不支持显式 artifact cache 路径，只能使用默认 build/cache。
  # stable 编译前后清理该目录，避免不同 schema 的 source artifact 相互污染。
  rm -rf "$ROOT_DIR/build/cache"
}

emit_next_from_bootstrap() {
  local output_bin="$1"
  printf '== build next: compile executable with %s (%s) ==\n' "$BOOTSTRAP_BIN" "$BOOTSTRAP_VERSION"
  "$BOOTSTRAP_BIN" \
    --target "$JIANG_HOST_TARGET" \
    --linker "$CLANG_BIN" \
    "${LLVM_LINK_ARGS[@]}" \
    -o "$output_bin" \
    src/jiangc.jiang
  test -x "$output_bin"
  write_compiler_build_id "$output_bin"
  printf 'OK %s\n' "$output_bin"
}

emit_compiler_with_compiler() {
  local source_bin="$1"
  local output_bin="$2"
  test -x "$source_bin"
  printf '== build next: compile executable with %s ==\n' "$source_bin"
  "$source_bin" \
    --artifact-cache-dir "$NEXT_ARTIFACT_CACHE_DIR" \
    --target "$JIANG_HOST_TARGET" \
    --linker "$CLANG_BIN" \
    "${LLVM_LINK_ARGS[@]}" \
    -o "$output_bin" \
    src/jiangc.jiang
  test -x "$output_bin"
  write_compiler_build_id "$output_bin"
  printf 'OK %s\n' "$output_bin"
}

collect_llvm_link_args

printf '== build next: %s -> next ==\n' "$BOOTSTRAP_VERSION"
clear_bootstrap_artifact_cache
emit_next_from_bootstrap "$NEXT_BIN"
clear_bootstrap_artifact_cache
mkdir -p "$NEXT_ARTIFACT_CACHE_DIR"

VERIFY_BIN="$NEXT_BIN"
if [ "$BOOTSTRAP_DEPTH" = "stable" ]; then
  printf '\n== build stable: next -> jiangc ==\n'
  emit_compiler_with_compiler "$NEXT_BIN" "$JIANGC_BIN"
  VERIFY_BIN="$JIANGC_BIN"
fi

if [ "$VERIFY" != "none" ]; then
  printf '\n== next verify: smoke with %s ==\n' "$VERIFY_BIN"
  slow_smoke=""
  if [ "$VERIFY" = "full" ]; then
    slow_smoke=1
  fi
  BUILD_DIR="$BUILD_DIR" \
  JIANGC="$VERIFY_BIN" \
  JIANG_SLOW_SMOKE="$slow_smoke" \
  bash "$ROOT_DIR/script/smoke.sh"

  printf '\n== next verify: backend cli smoke ==\n'
  BUILD_DIR="$BUILD_DIR" \
  JIANGC="$VERIFY_BIN" \
  bash "$ROOT_DIR/script/backend_cli_smoke.sh"
fi

if [ "$VERIFY" = "full" ]; then
  printf '\n== next verify: language tests with %s ==\n' "$VERIFY_BIN"
  JIANGC="$VERIFY_BIN" \
  bash "$ROOT_DIR/script/test.sh"
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
