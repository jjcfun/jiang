#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
STAGE1_BIN="${STAGE1_BIN:-$HOME/.jiang/stage1/bin/jiangc}"
STAGE2_BIN="${STAGE2_BIN:-$BUILD_DIR/jiangc}"
NEXT_BIN="${NEXT_BIN:-$BUILD_DIR/jiangc.next}"
NEXT2_BIN="${NEXT2_BIN:-$BUILD_DIR/jiangc.next2}"
STABLE_BIN="${STABLE_BIN:-$BUILD_DIR/jiangc.stable}"
VERIFY="${VERIFY:-full}"

mkdir -p "$BUILD_DIR"
cd "$ROOT_DIR"

if [ ! -x "$STAGE1_BIN" ]; then
  echo "missing bootstrap compiler: $STAGE1_BIN" >&2
  echo "set STAGE1_BIN=/path/to/jiangc or install the stage1 bootstrap compiler" >&2
  exit 2
fi

case "$VERIFY" in
  none|smoke|full) ;;
  *)
    echo "invalid VERIFY=$VERIFY; expected none, smoke, or full" >&2
    exit 2
    ;;
esac

printf '== stable bootstrap: stage1 -> stage2 ==\n'
STAGE1_BIN="$STAGE1_BIN" \
BUILD_DIR="$BUILD_DIR" \
STAGE2_BIN="$STAGE2_BIN" \
bash "$ROOT_DIR/script/build_stage2.sh"

printf '\n== stable bootstrap: stage2 -> next ==\n'
BUILD_DIR="$BUILD_DIR" \
STAGE2_BIN="$STAGE2_BIN" \
NEXT_BIN="$NEXT_BIN" \
bash "$ROOT_DIR/script/build_next.sh"

printf '\n== stable bootstrap: next -> next2 ==\n'
BUILD_DIR="$BUILD_DIR" \
STAGE2_BIN="$NEXT_BIN" \
NEXT_BIN="$NEXT2_BIN" \
bash "$ROOT_DIR/script/build_next.sh"

if [ "$VERIFY" != "none" ]; then
  printf '\n== stable verify: smoke with %s ==\n' "$NEXT2_BIN"
  BUILD_DIR="$BUILD_DIR" \
  JIANGC="$NEXT2_BIN" \
  bash "$ROOT_DIR/script/smoke.sh"

  printf '\n== stable verify: backend cli smoke ==\n'
  STAGE1_BIN="$STAGE2_BIN" \
  BUILD_DIR="$BUILD_DIR" \
  bash "$ROOT_DIR/script/backend_cli_smoke.sh"
fi

if [ "$VERIFY" = "full" ]; then
  printf '\n== stable verify: lang check with %s ==\n' "$NEXT2_BIN"
  JIANGC="$NEXT2_BIN" \
  bash "$ROOT_DIR/script/lang_check.sh"
fi

cp "$NEXT2_BIN" "$STABLE_BIN"
chmod +x "$STABLE_BIN"

printf '\nOK stable compiler: %s\n' "$STABLE_BIN"
printf 'candidate source: %s\n' "$NEXT2_BIN"
