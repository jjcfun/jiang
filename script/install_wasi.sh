#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JIANG_HOME="${JIANG_HOME:-$HOME/.jiang}"
WASI_SDK_VERSION="${WASI_SDK_VERSION:-33.0}"
WASI_SDK_RELEASE="${WASI_SDK_RELEASE:-wasi-sdk-33}"
WASI_SDK_BASE_DIR="${WASI_SDK_BASE_DIR:-$JIANG_HOME/toolchains/wasi-sdk}"
WASI_SDK_FORCE_INSTALL="${WASI_SDK_FORCE_INSTALL:-0}"

host_tag() {
  local os
  local arch
  os="$(uname -s)"
  arch="$(uname -m)"
  case "$os:$arch" in
    Darwin:arm64|Darwin:aarch64)
      printf '%s\n' "darwin-arm64"
      ;;
    Darwin:x86_64)
      printf '%s\n' "darwin-x86_64"
      ;;
    Linux:x86_64|Linux:amd64)
      printf '%s\n' "linux-x86_64"
      ;;
    Linux:aarch64|Linux:arm64)
      printf '%s\n' "linux-arm64"
      ;;
    *)
      echo "unsupported host for wasi-sdk: $os $arch" >&2
      exit 2
      ;;
  esac
}

wasi_asset_host() {
  case "$(host_tag)" in
    darwin-arm64) printf '%s\n' "arm64-macos" ;;
    darwin-x86_64) printf '%s\n' "x86_64-macos" ;;
    linux-x86_64) printf '%s\n' "x86_64-linux" ;;
    linux-arm64) printf '%s\n' "arm64-linux" ;;
  esac
}

require_command() {
  local name="$1"
  if ! command -v "$name" >/dev/null 2>&1; then
    echo "missing required command: $name" >&2
    exit 2
  fi
}

install_wasi() {
  local host
  local asset_host
  local install_dir
  local archive
  local url
  local tmp_dir
  host="$(host_tag)"
  asset_host="$(wasi_asset_host)"
  install_dir="$WASI_SDK_BASE_DIR/$WASI_SDK_VERSION/$host"

  if [ "$WASI_SDK_FORCE_INSTALL" != "1" ] && [ -x "$install_dir/bin/clang" ]; then
    printf 'wasi-sdk already installed: %s\n' "$install_dir"
    exit 0
  fi

  require_command curl
  require_command tar

  archive="wasi-sdk-$WASI_SDK_VERSION-$asset_host.tar.gz"
  url="https://github.com/WebAssembly/wasi-sdk/releases/download/$WASI_SDK_RELEASE/$archive"
  tmp_dir="$ROOT_DIR/build/download/wasi-sdk"
  mkdir -p "$tmp_dir" "$WASI_SDK_BASE_DIR/$WASI_SDK_VERSION"

  printf 'download wasi-sdk: %s\n' "$url"
  curl -L "$url" -o "$tmp_dir/$archive"

  rm -rf "$install_dir" "$tmp_dir/extract"
  mkdir -p "$tmp_dir/extract"
  tar -xzf "$tmp_dir/$archive" -C "$tmp_dir/extract"

  local extracted
  extracted="$(find "$tmp_dir/extract" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
  if [ -z "$extracted" ] || [ ! -x "$extracted/bin/clang" ]; then
    echo "invalid wasi-sdk archive: missing bin/clang" >&2
    exit 2
  fi

  mv "$extracted" "$install_dir"
  printf 'installed wasi-sdk: %s\n' "$install_dir"
}

install_wasi
