#!/bin/sh

set -eu

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
CACHE_FILE="$BUILD_DIR/CMakeCache.txt"

if [ -f "$CACHE_FILE" ]; then
  CACHE_SOURCE="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$CACHE_FILE" | tail -n 1)"
  if [ -n "$CACHE_SOURCE" ] && [ "$CACHE_SOURCE" != "$PROJECT_ROOT" ]; then
    echo "warning: clearing stale CMake cache for $CACHE_SOURCE" >&2
    rm -f "$CACHE_FILE"
    rm -rf "$BUILD_DIR/CMakeFiles"
  fi
fi

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --target stage0c

echo "$BUILD_DIR/stage0c"
