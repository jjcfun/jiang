#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SEMANTIC_FILE="$PROJECT_ROOT/src/semantic.c"
README_FILE="$PROJECT_ROOT/README.md"
STD_ROOT="$PROJECT_ROOT/std/std.jiang"

RUNTIME_INTRINSICS=(
  "__intrinsic_print"
  "__intrinsic_assert"
  "__intrinsic_read_file"
  "__intrinsic_file_exists"
  "__intrinsic_alloc_ints"
  "__intrinsic_alloc_bytes"
)

STD_MODULES=(
  "io"
  "debug"
  "fs"
)

for intrinsic in "${RUNTIME_INTRINSICS[@]}"; do
  if ! rg -q "symbol_define\\(\"${intrinsic}\"" "$SEMANTIC_FILE"; then
    echo "runtime boundary smoke failed: missing runtime ABI symbol ${intrinsic} in semantic.c"
    exit 1
  fi

  if ! rg -q "${intrinsic}" "$README_FILE"; then
    echo "runtime boundary smoke failed: README.md missing runtime ABI doc for ${intrinsic}"
    exit 1
  fi
done

abi_count=$(rg 'symbol_define\("__intrinsic_' "$SEMANTIC_FILE" | wc -l | tr -d ' ')
if [ "$abi_count" -ne 6 ]; then
  echo "runtime boundary smoke failed: expected 6 runtime ABI registrations, got $abi_count"
  exit 1
fi

std_export_count=$(rg '^public import ' "$STD_ROOT" | wc -l | tr -d ' ')
if [ "$std_export_count" -ne 3 ]; then
  echo "runtime boundary smoke failed: expected std root to export 3 modules, got $std_export_count"
  exit 1
fi

for module_name in "${STD_MODULES[@]}"; do
  if ! rg -q "^public import ${module_name} = " "$STD_ROOT"; then
    echo "runtime boundary smoke failed: std root missing module export ${module_name}"
    exit 1
  fi

  if ! rg -q "std\\.${module_name}" "$README_FILE"; then
    echo "runtime boundary smoke failed: README.md missing std module doc for std.${module_name}"
    exit 1
  fi
done

echo "runtime boundary smoke passed"
