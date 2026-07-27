#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

TEST_ROOT="${TEST_ROOT:-${LANG_CHECK_ROOT:-test/lang}}" \
  exec bash "$ROOT_DIR/script/test.sh"
