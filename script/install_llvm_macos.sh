#!/usr/bin/env bash
set -euo pipefail

if [ "$(uname -s)" != "Darwin" ]; then
  echo "This script only supports macOS." >&2
  exit 2
fi

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew is required to install llvm@21." >&2
  echo >&2
  echo "Install Homebrew first:" >&2
  echo '  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"' >&2
  echo >&2
  echo "Then run this script again:" >&2
  echo "  bash ./script/install_llvm_macos.sh" >&2
  exit 2
fi

if brew list llvm@21 >/dev/null 2>&1; then
  echo "llvm@21 already installed."
else
  brew install llvm@21
fi

LLVM_ROOT="$(brew --prefix llvm@21)"
LLVM_CONFIG="$LLVM_ROOT/bin/llvm-config"

if [ ! -x "$LLVM_CONFIG" ]; then
  echo "llvm-config not found: $LLVM_CONFIG" >&2
  exit 2
fi

echo "LLVM_ROOT=$LLVM_ROOT"
echo "LLVM_CONFIG=$LLVM_CONFIG"
"$LLVM_CONFIG" --version
