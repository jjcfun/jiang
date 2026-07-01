#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JIANG_LLVM_VERSION="${JIANG_LLVM_VERSION:-22}"
JIANG_LLVM_REPO="${JIANG_LLVM_REPO:-https://github.com/jjcfun/llvm-project.git}"
JIANG_LLVM_REF="${JIANG_LLVM_REF:-jiang/22.1.8}"
JIANG_LLVM_SOURCE_DIR="${JIANG_LLVM_SOURCE_DIR:-$ROOT_DIR/vendor/llvm-project}"
JIANG_LLVM_BUILD_DIR="${JIANG_LLVM_BUILD_DIR:-$ROOT_DIR/build/llvm}"
JIANG_LLVM_PROJECTS="${JIANG_LLVM_PROJECTS:-clang;lld}"
JIANG_LLVM_TARGETS="${JIANG_LLVM_TARGETS:-X86;AArch64;WebAssembly}"
JIANG_LLVM_BUILD_TYPE="${JIANG_LLVM_BUILD_TYPE:-Release}"
JIANG_LLVM_PARALLEL="${JIANG_LLVM_PARALLEL:-}"
JIANG_LLVM_FORCE_BUILD="${JIANG_LLVM_FORCE_BUILD:-0}"
JIANG_MACOS_DEPLOYMENT_TARGET="${JIANG_MACOS_DEPLOYMENT_TARGET:-${MACOSX_DEPLOYMENT_TARGET:-11.0}}"

host_tag() {
  local os
  local arch
  os="$(uname -s | tr '[:upper:]' '[:lower:]')"
  arch="$(uname -m)"
  printf '%s-%s\n' "$os" "$arch"
}

llvm_install_prefix() {
  printf '%s/%s/install\n' "$JIANG_LLVM_BUILD_DIR" "$(host_tag)"
}

ensure_llvm_source() {
  if [ -f "$JIANG_LLVM_SOURCE_DIR/llvm/CMakeLists.txt" ]; then
    return
  fi

  if [ ! -f "$ROOT_DIR/.gitmodules" ]; then
    mkdir -p "$(dirname "$JIANG_LLVM_SOURCE_DIR")"
    git clone --depth 1 --branch "$JIANG_LLVM_REF" "$JIANG_LLVM_REPO" "$JIANG_LLVM_SOURCE_DIR"
    return
  fi

  git -C "$ROOT_DIR" submodule update --init --depth 1 vendor/llvm-project

  if [ ! -f "$JIANG_LLVM_SOURCE_DIR/llvm/CMakeLists.txt" ]; then
    echo "missing LLVM source after submodule init: $JIANG_LLVM_SOURCE_DIR" >&2
    exit 2
  fi
}

require_command() {
  local name="$1"
  if ! command -v "$name" >/dev/null 2>&1; then
    echo "missing required command: $name" >&2
    exit 2
  fi
}

cmake_generator_args() {
  if command -v ninja >/dev/null 2>&1; then
    printf '%s\n' -G Ninja
  fi
}

build_parallel_args() {
  if [ -n "$JIANG_LLVM_PARALLEL" ]; then
    printf '%s\n' --parallel "$JIANG_LLVM_PARALLEL"
  fi
}

cmake_macos_deployment_args() {
  if [ "$(uname -s)" = "Darwin" ]; then
    printf '%s\n' "-DCMAKE_OSX_DEPLOYMENT_TARGET=$JIANG_MACOS_DEPLOYMENT_TARGET"
  fi
}

install_managed_llvm() {
  local host
  local source_dir
  local build_dir
  local install_dir
  host="$(host_tag)"
  source_dir="$JIANG_LLVM_SOURCE_DIR/llvm"
  build_dir="$JIANG_LLVM_BUILD_DIR/$host/build"
  install_dir="$(llvm_install_prefix)"

  if [ "$JIANG_LLVM_FORCE_BUILD" != "1" ] && [ -x "$install_dir/bin/llvm-config" ]; then
    JIANG_LLVM_ROOT="$install_dir" "$ROOT_DIR/script/llvm_env.sh"
    exit 0
  fi

  require_command cmake
  require_command git
  ensure_llvm_source
  if [ "$(uname -s)" = "Darwin" ]; then
    export MACOSX_DEPLOYMENT_TARGET="$JIANG_MACOS_DEPLOYMENT_TARGET"
  fi

  cmake \
    $(cmake_generator_args) \
    $(cmake_macos_deployment_args) \
    -S "$source_dir" \
    -B "$build_dir" \
    -DCMAKE_BUILD_TYPE="$JIANG_LLVM_BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$install_dir" \
    -DLLVM_ENABLE_PROJECTS="$JIANG_LLVM_PROJECTS" \
    -DLLVM_TARGETS_TO_BUILD="$JIANG_LLVM_TARGETS" \
    -DBUILD_SHARED_LIBS=OFF \
    -DLLVM_BUILD_LLVM_DYLIB=OFF \
    -DLLVM_LINK_LLVM_DYLIB=OFF \
    -DLLVM_ENABLE_ZLIB=OFF \
    -DLLVM_ENABLE_ZSTD=OFF \
    -DLLVM_ENABLE_LIBXML2=OFF \
    -DLLVM_INCLUDE_BENCHMARKS=OFF \
    -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_INCLUDE_TESTS=OFF

  cmake --build "$build_dir" $(build_parallel_args)
  cmake --install "$build_dir"

  JIANG_LLVM_ROOT="$install_dir" "$ROOT_DIR/script/llvm_env.sh"
}

case "$(uname -s)" in
  Darwin|Linux)
    install_managed_llvm
    ;;
  *)
    echo "unsupported host OS: $(uname -s)" >&2
    exit 2
    ;;
esac
