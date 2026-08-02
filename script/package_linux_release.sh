#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export TARGET="${TARGET:-linux-x86_64}"
exec bash "$ROOT_DIR/script/package_release.sh"
