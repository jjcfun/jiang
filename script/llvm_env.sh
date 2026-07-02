#!/usr/bin/env bash

# Resolve the local LLVM toolchain used to build and test jiangc.
#
# This file is meant to be sourced by other scripts. It is the only place that
# discovers LLVM. Callers should consume the exported LLVM_* variables instead
# of probing llvm-config or clang themselves.

JIANG_LLVM_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JIANG_ROOT_DIR="$(cd "$JIANG_LLVM_SCRIPT_DIR/.." && pwd)"
JIANG_HOME="${JIANG_HOME:-$HOME/.jiang}"
JIANG_LLVM_TOOLCHAIN_DIR="${JIANG_LLVM_TOOLCHAIN_DIR:-$JIANG_HOME/toolchains/llvm}"
JIANG_LLVM_VERSION="${JIANG_LLVM_VERSION:-22}"
JIANG_MACOS_DEPLOYMENT_TARGET="${JIANG_MACOS_DEPLOYMENT_TARGET:-${MACOSX_DEPLOYMENT_TARGET:-11.0}}"
BUILD_DIR="${BUILD_DIR:-$JIANG_ROOT_DIR/build}"
JIANG_LLVM_CONFIG_DIR="$BUILD_DIR/config"
JIANG_LLVM_ENV_FILE="$JIANG_LLVM_CONFIG_DIR/llvm.env"
JIANG_BUILD_HELPER_BIN="${JIANG_BUILD_HELPER_BIN:-$BUILD_DIR/bin/jiang-build}"

jiang_find_llvm_config() {
  if jiang_cached_llvm_config; then
    return
  fi
  if jiang_managed_llvm_config; then
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

jiang_host_target_triple() {
  local os
  local arch
  os="$(uname -s)"
  arch="$(uname -m)"
  case "$os:$arch" in
    Darwin:arm64|Darwin:aarch64)
      printf '%s\n' "arm64-apple-macosx"
      ;;
    Linux:x86_64|Linux:amd64)
      printf '%s\n' "x86_64-unknown-linux-gnu"
      ;;
    Linux:aarch64|Linux:arm64)
      printf '%s\n' "aarch64-unknown-linux-gnu"
      ;;
    *)
      echo "unsupported host target: $os $arch" >&2
      return 2
      ;;
  esac
}

jiang_managed_llvm_config() {
  local host
  host="$(jiang_host_tag)"
  for root in \
    "$JIANG_LLVM_TOOLCHAIN_DIR/$JIANG_LLVM_VERSION/$host"
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
  case "$cached_config" in
    "$JIANG_LLVM_TOOLCHAIN_DIR/"*) ;;
    *) return 1 ;;
  esac
  if [ -x "$cached_config" ]; then
    printf '%s\n' "$cached_config"
    return 0
  fi
  return 1
}

jiang_find_lld() {
  if [ -n "${JIANG_LLD:-}" ] && [ -x "$JIANG_LLD" ]; then
    printf '%s\n' "$JIANG_LLD"
    return 0
  fi
  if [ -n "${LLVM_BINDIR:-}" ] && [ -x "$LLVM_BINDIR/ld.lld" ]; then
    printf '%s\n' "$LLVM_BINDIR/ld.lld"
    return 0
  fi
  if command -v ld.lld >/dev/null 2>&1; then
    command -v ld.lld
    return 0
  fi
  return 1
}

jiang_llvm_cxx_runtime_link_args() {
  case "$(uname -s)" in
    Darwin)
      printf '%s\n' "-lc++"
      ;;
    Linux)
      printf '%s\n' "-lstdc++"
      ;;
  esac
}

jiang_macos_sdkroot_link_args() {
  if [ "$(uname -s)" != "Darwin" ]; then
    return 0
  fi
  jiang_build_helper macos-sdkroot-link-args
}

jiang_build_helper_compiler() {
  for compiler in \
    "${BOOTSTRAP_BIN:-}" \
    "${COMPILER_UNDER_TEST:-}" \
    "${JIANGC:-}"
  do
    if [ -n "$compiler" ] && [ -x "$compiler" ]; then
      printf '%s\n' "$compiler"
      return 0
    fi
  done
  if command -v jiangc >/dev/null 2>&1; then
    command -v jiangc
    return 0
  fi
  return 1
}

jiang_ensure_build_helper() {
  if [ -x "$JIANG_BUILD_HELPER_BIN" ] && \
    [ "$JIANG_BUILD_HELPER_BIN" -nt "$JIANG_ROOT_DIR/src/build/main.jiang" ] && \
    [ "$JIANG_BUILD_HELPER_BIN" -nt "$JIANG_ROOT_DIR/src/build/sdk.jiang" ]; then
    return 0
  fi
  local compiler
  compiler="$(jiang_build_helper_compiler || true)"
  if [ -z "$compiler" ]; then
    echo "missing Jiang bootstrap compiler for build helper" >&2
    return 2
  fi
  mkdir -p "$(dirname "$JIANG_BUILD_HELPER_BIN")"
  "$compiler" --target "$JIANG_HOST_TARGET" --linker "$LLVM_CLANG" -o "$JIANG_BUILD_HELPER_BIN" \
    "$JIANG_ROOT_DIR/src/build/main.jiang"
}

jiang_build_helper() {
  jiang_ensure_build_helper || return $?
  "$JIANG_BUILD_HELPER_BIN" "$@"
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
    printf 'JIANG_MACOS_DEPLOYMENT_TARGET=%s\n' "$JIANG_MACOS_DEPLOYMENT_TARGET"
    printf 'MACOSX_DEPLOYMENT_TARGET=%s\n' "${MACOSX_DEPLOYMENT_TARGET:-}"
    printf 'JIANG_LLD=%s\n' "${JIANG_LLD:-}"
  } >"$JIANG_LLVM_ENV_FILE"
}

jiang_resolve_llvm_env() {
  LLVM_CONFIG="$(jiang_find_llvm_config || true)"
  if [ -z "$LLVM_CONFIG" ] || [ ! -x "$LLVM_CONFIG" ]; then
    echo "missing managed llvm-config; run ./script/install_llvm.sh" >&2
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
  JIANG_LLD="${JIANG_LLD:-$(jiang_find_lld || true)}"

  if [ ! -x "$LLVM_CLANG" ]; then
    echo "missing LLVM clang: $LLVM_CLANG" >&2
    return 2
  fi
  if [ ! -d "$LLVM_LIB_DIR" ]; then
    echo "missing LLVM lib dir: $LLVM_LIB_DIR" >&2
    return 2
  fi

  if [ "$(uname -s)" = "Darwin" ]; then
    MACOSX_DEPLOYMENT_TARGET="$JIANG_MACOS_DEPLOYMENT_TARGET"
    export MACOSX_DEPLOYMENT_TARGET
  fi

  export JIANG_LLVM_VERSION JIANG_LLVM_TOOLCHAIN_DIR JIANG_MACOS_DEPLOYMENT_TARGET
  export LLVM_CONFIG LLVM_VERSION LLVM_BINDIR LLVM_ROOT LLVM_CLANG LLVM_LIB_DIR JIANG_LLD
  JIANG_HOST_TARGET="${JIANG_HOST_TARGET:-$(jiang_host_target_triple)}"
  export JIANG_HOST_TARGET
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
  printf 'JIANG_MACOS_DEPLOYMENT_TARGET=%s\n' "$JIANG_MACOS_DEPLOYMENT_TARGET"
  printf 'MACOSX_DEPLOYMENT_TARGET=%s\n' "${MACOSX_DEPLOYMENT_TARGET:-}"
  printf 'JIANG_LLD=%s\n' "${JIANG_LLD:-}"
  printf 'LLVM_ENV_FILE=%s\n' "$JIANG_LLVM_ENV_FILE"
else
  jiang_resolve_llvm_env
fi
