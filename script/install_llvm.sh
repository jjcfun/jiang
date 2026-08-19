#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JIANG_HOME="${JIANG_HOME:-$HOME/.jiang}"
JIANG_LLVM_VERSION="${JIANG_LLVM_VERSION:-22}"
JIANG_LLVM_RELEASE_VERSION="${JIANG_LLVM_RELEASE_VERSION:-22.1.8}"
JIANG_LLVM_SOURCE_REVISION="${JIANG_LLVM_SOURCE_REVISION:-ca7933e47d3a3451d81e72ac174dcb5aa28b59d1}"
JIANG_LLVM_SDK_REVISION="${JIANG_LLVM_SDK_REVISION:-1}"
JIANG_LLVM_REPO="${JIANG_LLVM_REPO:-https://github.com/jjcfun/llvm-project.git}"
JIANG_LLVM_REF="${JIANG_LLVM_REF:-llvmorg-22.1.8}"
JIANG_LLVM_SOURCE_DIR="${JIANG_LLVM_SOURCE_DIR:-$ROOT_DIR/build/llvm-source/$JIANG_LLVM_RELEASE_VERSION}"
JIANG_LLVM_BUILD_DIR="${JIANG_LLVM_BUILD_DIR:-}"
JIANG_LLVM_TOOLCHAIN_DIR="${JIANG_LLVM_TOOLCHAIN_DIR:-$JIANG_HOME/toolchains/llvm}"
JIANG_LLVM_DOWNLOAD_DIR="${JIANG_LLVM_DOWNLOAD_DIR:-$ROOT_DIR/build/downloads}"
JIANG_LLVM_SDK_BASE_URL="${JIANG_LLVM_SDK_BASE_URL:-https://github.com/jjcfun/llvm-project/releases/download}"
JIANG_LLVM_INSTALL_SCOPE="${JIANG_LLVM_INSTALL_SCOPE:-local}"
JIANG_LLVM_INSTALL_MODE="${JIANG_LLVM_INSTALL_MODE:-sdk}"
JIANG_LLVM_PROJECTS="${JIANG_LLVM_PROJECTS:-clang;lld}"
JIANG_LLVM_TARGETS="${JIANG_LLVM_TARGETS:-X86;AArch64;WebAssembly}"
JIANG_LLVM_BUILD_TYPE="${JIANG_LLVM_BUILD_TYPE:-Release}"
JIANG_LLVM_PARALLEL="${JIANG_LLVM_PARALLEL:-}"
JIANG_LLVM_FORCE_BUILD="${JIANG_LLVM_FORCE_BUILD:-0}"
JIANG_MACOS_DEPLOYMENT_TARGET="${JIANG_MACOS_DEPLOYMENT_TARGET:-${MACOSX_DEPLOYMENT_TARGET:-11.0}}"
JIANG_LLVM_BOOTSTRAP_ROOT="${JIANG_LLVM_BOOTSTRAP_ROOT:-}"

host_tag() {
  local os
  local arch
  os="$(uname -s | tr '[:upper:]' '[:lower:]')"
  arch="$(uname -m)"
  printf '%s-%s\n' "$os" "$arch"
}

sdk_host_tag() {
  case "$(uname -s):$(uname -m)" in
    Darwin:arm64|Darwin:aarch64)
      printf '%s\n' "macos-arm64"
      ;;
    Linux:x86_64|Linux:amd64)
      printf '%s\n' "linux-x86_64"
      ;;
    *)
      echo "no prebuilt Jiang LLVM SDK for $(uname -s) $(uname -m); use --from-source" >&2
      return 2
      ;;
  esac
}

sdk_version() {
  printf '%s-%s\n' "$JIANG_LLVM_RELEASE_VERSION" "$JIANG_LLVM_SDK_REVISION"
}

sdk_archive_name() {
  printf 'jiang-llvm-%s-%s.tar.gz\n' "$(sdk_version)" "$(sdk_host_tag)"
}

sdk_archive_sha256() {
  case "$(sdk_host_tag)" in
    linux-x86_64)
      printf '%s\n' "42596f306bfa7af4c95f5f919ff0e32915146f1ea952620d035ef913451421de"
      ;;
    macos-arm64)
      printf '%s\n' "15cf20d4e314879e4760840a5ad1f33b84bc19becbd5ca8796957eca93d43541"
      ;;
  esac
}

sdk_release_tag() {
  printf 'jiang-sdk-llvm-%s\n' "$(sdk_version)"
}

usage() {
  cat <<'EOF'
usage: bash ./script/install_llvm.sh [--local|--user] [--from-source]

  --local        install into build/llvm/<host>/install (default)
  --user         install into $JIANG_HOME/toolchains/llvm/<version>/<host>
  --from-source  build the locked LLVM source instead of downloading the SDK
EOF
}

parse_args() {
  while [ "$#" -gt 0 ]; do
    case "$1" in
      --local)
        JIANG_LLVM_INSTALL_SCOPE="local"
        ;;
      --user)
        JIANG_LLVM_INSTALL_SCOPE="user"
        ;;
      --from-source)
        JIANG_LLVM_INSTALL_MODE="source"
        ;;
      -h|--help)
        usage
        exit 0
        ;;
      *)
        echo "unknown option: $1" >&2
        usage >&2
        exit 2
        ;;
    esac
    shift
  done
}

llvm_install_prefix() {
  local host
  host="$(host_tag)"
  case "$JIANG_LLVM_INSTALL_SCOPE" in
    local)
      printf '%s/llvm/%s/install\n' "$ROOT_DIR/build" "$host"
      ;;
    user)
      printf '%s/%s/%s\n' "$JIANG_LLVM_TOOLCHAIN_DIR" "$JIANG_LLVM_VERSION" "$host"
      ;;
    *)
      echo "unknown LLVM install scope: $JIANG_LLVM_INSTALL_SCOPE" >&2
      exit 2
      ;;
  esac
}

llvm_build_dir() {
  local host
  host="$(host_tag)"
  if [ -n "$JIANG_LLVM_BUILD_DIR" ]; then
    printf '%s/%s/%s/build\n' "$JIANG_LLVM_BUILD_DIR" "$JIANG_LLVM_VERSION" "$host"
    return
  fi
  case "$JIANG_LLVM_INSTALL_SCOPE" in
    local)
      printf '%s/llvm/%s/build\n' "$ROOT_DIR/build" "$host"
      ;;
    user)
      printf '%s/toolchains/llvm/%s/%s/build\n' "$ROOT_DIR/build" "$JIANG_LLVM_VERSION" "$host"
      ;;
    *)
      echo "unknown LLVM install scope: $JIANG_LLVM_INSTALL_SCOPE" >&2
      exit 2
      ;;
  esac
}

ensure_llvm_source() {
  if [ -f "$JIANG_LLVM_SOURCE_DIR/llvm/CMakeLists.txt" ]; then
    return
  fi

  mkdir -p "$(dirname "$JIANG_LLVM_SOURCE_DIR")"
  git clone --depth 1 --branch "$JIANG_LLVM_REF" "$JIANG_LLVM_REPO" "$JIANG_LLVM_SOURCE_DIR"

  if [ ! -f "$JIANG_LLVM_SOURCE_DIR/llvm/CMakeLists.txt" ]; then
    echo "missing LLVM source after clone: $JIANG_LLVM_SOURCE_DIR" >&2
    exit 2
  fi
  if [ "$(git -C "$JIANG_LLVM_SOURCE_DIR" rev-parse HEAD)" != "$JIANG_LLVM_SOURCE_REVISION" ]; then
    echo "unexpected LLVM source revision in $JIANG_LLVM_SOURCE_DIR" >&2
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

verify_sha256() {
  local expected="$1"
  local path="$2"
  local actual
  if command -v sha256sum >/dev/null 2>&1; then
    actual="$(sha256sum "$path" | awk '{print $1}')"
  elif command -v shasum >/dev/null 2>&1; then
    actual="$(shasum -a 256 "$path" | awk '{print $1}')"
  else
    echo "missing sha256sum or shasum" >&2
    return 2
  fi
  if [ "$actual" != "$expected" ]; then
    echo "SHA-256 mismatch for $path: expected $expected, got $actual" >&2
    return 1
  fi
  printf '%s: OK\n' "$path"
}

download_llvm_sdk() {
  local archive
  local archive_name
  local expected
  local partial
  local url
  archive_name="$(sdk_archive_name)"
  archive="$JIANG_LLVM_DOWNLOAD_DIR/$archive_name"
  expected="$(sdk_archive_sha256)"
  if [ -f "$archive" ] && verify_sha256 "$expected" "$archive" >/dev/null 2>&1; then
    printf '%s\n' "$archive"
    return
  fi

  require_command curl
  mkdir -p "$JIANG_LLVM_DOWNLOAD_DIR"
  partial="$archive.partial"
  url="$JIANG_LLVM_SDK_BASE_URL/$(sdk_release_tag)/$archive_name"
  rm -f "$partial"
  if ! curl --fail --location --retry 3 --output "$partial" "$url"; then
    rm -f "$partial"
    return 1
  fi
  if ! verify_sha256 "$expected" "$partial" >&2; then
    rm -f "$partial"
    return 1
  fi
  if ! mv "$partial" "$archive"; then
    rm -f "$partial"
    return 1
  fi
  printf '%s\n' "$archive"
}

llvm_sdk_install_valid() {
  local install_dir="$1"
  local manifest="$install_dir/share/jiang/llvm-sdk.json"
  if [ ! -x "$install_dir/bin/llvm-config" ] || [ ! -r "$manifest" ]; then
    return 1
  fi
  if [ "$("$install_dir/bin/llvm-config" --version)" != "$JIANG_LLVM_RELEASE_VERSION" ]; then
    return 1
  fi
  grep -Fq "\"sdk_version\": \"$(sdk_version)\"" "$manifest" && \
    grep -Fq "\"host\": \"$(sdk_host_tag)\"" "$manifest" && \
    grep -Fq "\"llvm_revision\": \"$JIANG_LLVM_SOURCE_REVISION\"" "$manifest"
}

install_prebuilt_llvm() {
  local archive
  local archive_root
  local install_dir
  local stage_dir
  install_dir="$(llvm_install_prefix)"
  if llvm_sdk_install_valid "$install_dir"; then
    "$ROOT_DIR/script/llvm_env.sh"
    return
  fi

  require_command tar
  if ! archive="$(download_llvm_sdk)"; then
    return 1
  fi
  mkdir -p "$(dirname "$install_dir")"
  stage_dir="$(mktemp -d "$(dirname "$install_dir")/.llvm-sdk.XXXXXX")"
  archive_root="${archive##*/}"
  archive_root="${archive_root%.tar.gz}"
  if ! tar -xzf "$archive" -C "$stage_dir"; then
    rm -rf "$stage_dir"
    return 2
  fi
  if ! llvm_sdk_install_valid "$stage_dir/$archive_root"; then
    echo "invalid Jiang LLVM SDK archive: $archive" >&2
    rm -rf "$stage_dir"
    return 2
  fi

  rm -rf "$install_dir"
  mv "$stage_dir/$archive_root" "$install_dir"
  rmdir "$stage_dir"
  "$ROOT_DIR/script/llvm_env.sh"
}

llvm_source_install_valid() {
  local install_dir="$1"
  [ -x "$install_dir/bin/llvm-config" ] && \
    [ "$("$install_dir/bin/llvm-config" --version)" = "$JIANG_LLVM_RELEASE_VERSION" ]
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

bootstrap_clang() {
  if [ -n "$JIANG_LLVM_BOOTSTRAP_ROOT" ] && [ -x "$JIANG_LLVM_BOOTSTRAP_ROOT/bin/clang" ]; then
    printf '%s\n' "$JIANG_LLVM_BOOTSTRAP_ROOT/bin/clang"
    return 0
  fi
  if command -v clang >/dev/null 2>&1; then
    command -v clang
    return 0
  fi
  return 1
}

bootstrap_clangxx() {
  if [ -n "$JIANG_LLVM_BOOTSTRAP_ROOT" ] && [ -x "$JIANG_LLVM_BOOTSTRAP_ROOT/bin/clang++" ]; then
    printf '%s\n' "$JIANG_LLVM_BOOTSTRAP_ROOT/bin/clang++"
    return 0
  fi
  if command -v clang++ >/dev/null 2>&1; then
    command -v clang++
    return 0
  fi
  return 1
}

cmake_bootstrap_compiler_args() {
  local clang
  local clangxx
  clang="$(bootstrap_clang || true)"
  clangxx="$(bootstrap_clangxx || true)"
  if [ -n "$clang" ] && [ -n "$clangxx" ]; then
    printf '%s\n' "-DCMAKE_C_COMPILER=$clang"
    printf '%s\n' "-DCMAKE_CXX_COMPILER=$clangxx"
  fi
}

cmake_macos_deployment_args() {
  if [ "$(uname -s)" = "Darwin" ]; then
    printf '%s\n' "-DCMAKE_OSX_DEPLOYMENT_TARGET=$JIANG_MACOS_DEPLOYMENT_TARGET"
  fi
}

install_source_llvm() {
  local source_dir
  local build_dir
  local install_dir
  source_dir="$JIANG_LLVM_SOURCE_DIR/llvm"
  build_dir="$(llvm_build_dir)"
  install_dir="$(llvm_install_prefix)"

  if [ "$JIANG_LLVM_FORCE_BUILD" != "1" ] && llvm_source_install_valid "$install_dir"; then
    "$ROOT_DIR/script/llvm_env.sh"
    return
  fi

  require_command cmake
  require_command git
  ensure_llvm_source
  if [ "$(uname -s)" = "Darwin" ]; then
    export MACOSX_DEPLOYMENT_TARGET="$JIANG_MACOS_DEPLOYMENT_TARGET"
  fi

  cmake \
    $(cmake_generator_args) \
    $(cmake_bootstrap_compiler_args) \
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

  "$ROOT_DIR/script/llvm_env.sh"
}

main() {
  parse_args "$@"
  if [ "$JIANG_LLVM_FORCE_BUILD" = "1" ]; then
    JIANG_LLVM_INSTALL_MODE="source"
  fi

  case "$(uname -s)" in
    Darwin|Linux) ;;
    *)
      echo "unsupported host OS: $(uname -s)" >&2
      return 2
      ;;
  esac

  case "$JIANG_LLVM_INSTALL_MODE" in
    sdk)
      install_prebuilt_llvm
      ;;
    source)
      install_source_llvm
      ;;
    *)
      echo "unknown LLVM install mode: $JIANG_LLVM_INSTALL_MODE" >&2
      return 2
      ;;
  esac
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  main "$@"
fi
