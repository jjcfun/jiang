#!/usr/bin/env bash

# Resolve the local LLVM toolchain used to build and test jiangc.
#
# This file is meant to be sourced by other scripts. It is the only place that
# discovers LLVM. Callers should consume the exported LLVM_* variables instead
# of probing llvm-config or clang themselves.

JIANG_LLVM_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JIANG_ROOT_DIR="$(cd "$JIANG_LLVM_SCRIPT_DIR/.." && pwd)"
JIANG_LLVM_VERSION="${JIANG_LLVM_VERSION:-21}"
BUILD_DIR="${BUILD_DIR:-$JIANG_ROOT_DIR/build}"
JIANG_LLVM_CONFIG_DIR="$BUILD_DIR/config"
JIANG_LLVM_ENV_FILE="$JIANG_LLVM_CONFIG_DIR/llvm.env"

jiang_find_llvm_config() {
  if [ -n "${JIANG_LLVM_ROOT:-}" ] && [ -x "$JIANG_LLVM_ROOT/bin/llvm-config" ]; then
    printf '%s\n' "$JIANG_LLVM_ROOT/bin/llvm-config"
    return
  fi
  if [ -n "${LLVM_CONFIG:-}" ]; then
    printf '%s\n' "$LLVM_CONFIG"
    return
  fi
  if [ -n "${LLVM_ROOT:-}" ] && [ -x "$LLVM_ROOT/bin/llvm-config" ]; then
    printf '%s\n' "$LLVM_ROOT/bin/llvm-config"
    return
  fi
  if jiang_cached_llvm_config; then
    return
  fi
  if jiang_managed_llvm_config; then
    return
  fi
  if command -v "llvm-config-$JIANG_LLVM_VERSION" >/dev/null 2>&1; then
    command -v "llvm-config-$JIANG_LLVM_VERSION"
    return
  fi
  for root in \
    "/usr/lib/llvm-$JIANG_LLVM_VERSION" \
    "/usr/local/llvm-$JIANG_LLVM_VERSION" \
    "/opt/llvm-$JIANG_LLVM_VERSION" \
    "/opt/llvm@$JIANG_LLVM_VERSION" \
    "/opt/homebrew/opt/llvm@$JIANG_LLVM_VERSION" \
    "/usr/local/opt/llvm@$JIANG_LLVM_VERSION"
  do
    if [ -x "$root/bin/llvm-config" ]; then
      printf '%s\n' "$root/bin/llvm-config"
      return
    fi
  done
  if command -v llvm-config >/dev/null 2>&1; then
    command -v llvm-config
    return
  fi
  return 1
}

jiang_host_tag() {
  local os
  local arch
  os="$(uname -s | tr '[:upper:]' '[:lower:]')"
  arch="$(uname -m)"
  printf '%s-%s\n' "$os" "$arch"
}

jiang_managed_llvm_config() {
  local host
  host="$(jiang_host_tag)"
  for root in \
    "$JIANG_ROOT_DIR/build/llvm/$host" \
    "$JIANG_ROOT_DIR/build/llvm" \
    "$JIANG_ROOT_DIR/.jiang/llvm/$host" \
    "$JIANG_ROOT_DIR/.jiang/llvm"
  do
    if [ -x "$root/bin/llvm-config" ]; then
      printf '%s\n' "$root/bin/llvm-config"
      return 0
    fi
  done
  return 1
}

jiang_cached_llvm_config() {
  if [ ! -r "$JIANG_LLVM_ENV_FILE" ]; then
    return 1
  fi
  local cached_config
  cached_config="$(sed -n 's/^LLVM_CONFIG=//p' "$JIANG_LLVM_ENV_FILE" | head -n 1)"
  if [ -n "$cached_config" ] && [ -x "$cached_config" ]; then
    printf '%s\n' "$cached_config"
    return 0
  fi
  return 1
}

jiang_write_llvm_env_file() {
  mkdir -p "$JIANG_LLVM_CONFIG_DIR"
  {
    printf 'JIANG_LLVM_VERSION=%s\n' "$JIANG_LLVM_VERSION"
    printf 'LLVM_CONFIG=%s\n' "$LLVM_CONFIG"
    printf 'LLVM_VERSION=%s\n' "$LLVM_VERSION"
    printf 'LLVM_BINDIR=%s\n' "$LLVM_BINDIR"
    printf 'LLVM_ROOT=%s\n' "$LLVM_ROOT"
    printf 'LLVM_CLANG=%s\n' "$LLVM_CLANG"
    printf 'LLVM_LIB_DIR=%s\n' "$LLVM_LIB_DIR"
  } >"$JIANG_LLVM_ENV_FILE"
}

jiang_resolve_llvm_env() {
  LLVM_CONFIG="$(jiang_find_llvm_config || true)"
  if [ -z "$LLVM_CONFIG" ] || [ ! -x "$LLVM_CONFIG" ]; then
    echo "missing llvm-config; install LLVM $JIANG_LLVM_VERSION or set JIANG_LLVM_ROOT/LLVM_CONFIG" >&2
    return 2
  fi

  LLVM_VERSION="$("$LLVM_CONFIG" --version)"
  case "$LLVM_VERSION" in
    "$JIANG_LLVM_VERSION"|"$JIANG_LLVM_VERSION".*) ;;
    *)
      echo "unsupported LLVM version: expected $JIANG_LLVM_VERSION.x, got $LLVM_VERSION from $LLVM_CONFIG" >&2
      return 2
      ;;
  esac

  LLVM_BINDIR="$("$LLVM_CONFIG" --bindir)"
  LLVM_ROOT="$(cd "$LLVM_BINDIR/.." && pwd)"
  LLVM_CLANG="${LLVM_CLANG:-$LLVM_BINDIR/clang}"
  LLVM_LIB_DIR="${LLVM_LIB_DIR:-$("$LLVM_CONFIG" --libdir)}"

  if [ ! -x "$LLVM_CLANG" ]; then
    echo "missing LLVM clang: $LLVM_CLANG" >&2
    return 2
  fi
  if [ ! -d "$LLVM_LIB_DIR" ]; then
    echo "missing LLVM lib dir: $LLVM_LIB_DIR" >&2
    return 2
  fi

  export JIANG_LLVM_VERSION LLVM_CONFIG LLVM_VERSION LLVM_BINDIR LLVM_ROOT LLVM_CLANG LLVM_LIB_DIR
  jiang_write_llvm_env_file
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  set -euo pipefail
  jiang_resolve_llvm_env
  printf 'JIANG_LLVM_VERSION=%s\n' "$JIANG_LLVM_VERSION"
  printf 'LLVM_CONFIG=%s\n' "$LLVM_CONFIG"
  printf 'LLVM_VERSION=%s\n' "$LLVM_VERSION"
  printf 'LLVM_ROOT=%s\n' "$LLVM_ROOT"
  printf 'LLVM_CLANG=%s\n' "$LLVM_CLANG"
  printf 'LLVM_LIB_DIR=%s\n' "$LLVM_LIB_DIR"
  printf 'LLVM_ENV_FILE=%s\n' "$JIANG_LLVM_ENV_FILE"
else
  jiang_resolve_llvm_env
fi
