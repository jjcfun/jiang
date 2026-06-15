#!/usr/bin/env bash

# Resolve the local LLVM toolchain used to build and test jiangc.
#
# This file is meant to be sourced by other scripts. It keeps LLVM discovery in
# one place while the project still relies on the host LLVM installation.

JIANG_LLVM_VERSION="${JIANG_LLVM_VERSION:-21}"

jiang_find_llvm_config() {
  if [ -n "${LLVM_CONFIG:-}" ]; then
    printf '%s\n' "$LLVM_CONFIG"
    return
  fi
  if [ -n "${LLVM_ROOT:-}" ] && [ -x "$LLVM_ROOT/bin/llvm-config" ]; then
    printf '%s\n' "$LLVM_ROOT/bin/llvm-config"
    return
  fi
  if command -v "llvm-config-$JIANG_LLVM_VERSION" >/dev/null 2>&1; then
    command -v "llvm-config-$JIANG_LLVM_VERSION"
    return
  fi
  if [ -x "/opt/homebrew/opt/llvm@$JIANG_LLVM_VERSION/bin/llvm-config" ]; then
    printf '%s\n' "/opt/homebrew/opt/llvm@$JIANG_LLVM_VERSION/bin/llvm-config"
    return
  fi
  if [ -x "/usr/local/opt/llvm@$JIANG_LLVM_VERSION/bin/llvm-config" ]; then
    printf '%s\n' "/usr/local/opt/llvm@$JIANG_LLVM_VERSION/bin/llvm-config"
    return
  fi
  if command -v llvm-config >/dev/null 2>&1; then
    command -v llvm-config
    return
  fi
  return 1
}

jiang_resolve_llvm_env() {
  LLVM_CONFIG="$(jiang_find_llvm_config || true)"
  if [ -z "$LLVM_CONFIG" ] || [ ! -x "$LLVM_CONFIG" ]; then
    echo "missing llvm-config; install LLVM $JIANG_LLVM_VERSION or set LLVM_CONFIG" >&2
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
  LLVM_ROOT="${LLVM_ROOT:-$(cd "$LLVM_BINDIR/.." && pwd)}"
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

  export LLVM_CONFIG LLVM_VERSION LLVM_BINDIR LLVM_ROOT LLVM_CLANG LLVM_LIB_DIR
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  set -euo pipefail
  jiang_resolve_llvm_env
  printf 'LLVM_CONFIG=%s\n' "$LLVM_CONFIG"
  printf 'LLVM_VERSION=%s\n' "$LLVM_VERSION"
  printf 'LLVM_ROOT=%s\n' "$LLVM_ROOT"
  printf 'LLVM_CLANG=%s\n' "$LLVM_CLANG"
  printf 'LLVM_LIB_DIR=%s\n' "$LLVM_LIB_DIR"
else
  jiang_resolve_llvm_env
fi
