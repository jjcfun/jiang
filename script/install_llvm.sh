#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JIANG_LLVM_VERSION="${JIANG_LLVM_VERSION:-21}"

install_macos_llvm() {
  local formula="llvm@$JIANG_LLVM_VERSION"
  if ! command -v brew >/dev/null 2>&1; then
    echo "Homebrew is required to install $formula." >&2
    echo >&2
    echo "Install Homebrew first:" >&2
    echo '  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"' >&2
    echo >&2
    echo "Then run this script again:" >&2
    echo "  bash ./script/install_llvm.sh" >&2
    exit 2
  fi

  if brew list "$formula" >/dev/null 2>&1; then
    echo "$formula already installed."
  else
    brew install "$formula"
  fi

  local llvm_root
  llvm_root="$(brew --prefix "$formula")"
  JIANG_LLVM_ROOT="$llvm_root" "$ROOT_DIR/script/llvm_env.sh"
}

install_linux_llvm() {
  if command -v apt-get >/dev/null 2>&1; then
    install_linux_llvm_apt
    return
  fi

  echo "unsupported Linux package manager." >&2
  echo "Install LLVM $JIANG_LLVM_VERSION manually, then run:" >&2
  echo "  JIANG_LLVM_ROOT=/path/to/llvm bash ./script/llvm_env.sh" >&2
  exit 2
}

install_linux_llvm_apt() {
  local packages=(
    "llvm-$JIANG_LLVM_VERSION"
    "llvm-$JIANG_LLVM_VERSION-dev"
    "clang-$JIANG_LLVM_VERSION"
  )
  local sudo_cmd=()
  if [ "$(id -u)" != "0" ]; then
    if ! command -v sudo >/dev/null 2>&1; then
      echo "sudo is required to install LLVM packages with apt-get." >&2
      exit 2
    fi
    sudo_cmd=(sudo)
  fi

  "${sudo_cmd[@]}" apt-get update
  "${sudo_cmd[@]}" apt-get install -y "${packages[@]}"

  if command -v "llvm-config-$JIANG_LLVM_VERSION" >/dev/null 2>&1; then
    LLVM_CONFIG="$(command -v "llvm-config-$JIANG_LLVM_VERSION")" "$ROOT_DIR/script/llvm_env.sh"
    return
  fi
  if [ -x "/usr/lib/llvm-$JIANG_LLVM_VERSION/bin/llvm-config" ]; then
    JIANG_LLVM_ROOT="/usr/lib/llvm-$JIANG_LLVM_VERSION" "$ROOT_DIR/script/llvm_env.sh"
    return
  fi

  echo "LLVM packages installed, but llvm-config-$JIANG_LLVM_VERSION was not found." >&2
  exit 2
}

case "$(uname -s)" in
  Darwin)
    install_macos_llvm
    ;;
  Linux)
    install_linux_llvm
    ;;
  *)
    echo "unsupported host OS: $(uname -s)" >&2
    exit 2
    ;;
esac
