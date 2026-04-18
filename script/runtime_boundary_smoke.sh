#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SEMANTIC_FILE="$PROJECT_ROOT/src/semantic.c"
README_FILE="$PROJECT_ROOT/README.md"
STD_ROOT="$PROJECT_ROOT/std/std.jiang"
RUNTIME_HEADER="$PROJECT_ROOT/include/runtime.h"
HOST_RUNTIME="$PROJECT_ROOT/runtime/host_runtime.c"

RUNTIME_INTRINSICS=(
  "__intrinsic_print"
  "__intrinsic_assert"
  "__intrinsic_read_file"
  "__intrinsic_file_exists"
  "__intrinsic_malloc"
  "__intrinsic_free"
  "__intrinsic_memmove"
  "__intrinsic_alloc_ints"
  "__intrinsic_alloc_bytes"
)

STD_MODULES=(
  "io"
  "debug"
  "fs"
  "mem"
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
if [ "$abi_count" -ne 9 ]; then
  echo "runtime boundary smoke failed: expected 9 runtime ABI registrations, got $abi_count"
  exit 1
fi

std_export_count=$(rg '^public import ' "$STD_ROOT" | wc -l | tr -d ' ')
if [ "$std_export_count" -ne 4 ]; then
  echo "runtime boundary smoke failed: expected std root to export 4 modules, got $std_export_count"
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

RUNTIME_SMOKE_C="$PROJECT_ROOT/build/runtime_boundary_smoke.c"
RUNTIME_SMOKE_BIN="$PROJECT_ROOT/build/runtime_boundary_smoke"

mkdir -p "$PROJECT_ROOT/build"
cat > "$RUNTIME_SMOKE_C" <<'EOF'
#include "runtime.h"
#include <stdint.h>

int main(void) {
    uint8_t* data = __intrinsic_malloc(4);
    if (!data) return 10;
    data[0] = 1;
    data[1] = 2;
    data[2] = 3;
    data[3] = 4;
    __intrinsic_memmove(data + 1, data, 3);
    if (data[0] != 1) return 11;
    if (data[1] != 1) return 12;
    if (data[2] != 2) return 13;
    if (data[3] != 3) return 14;
    __intrinsic_free(data);
    return 0;
}
EOF

cc -std=c99 -I "$PROJECT_ROOT/include" "$RUNTIME_SMOKE_C" "$HOST_RUNTIME" -o "$RUNTIME_SMOKE_BIN"
"$RUNTIME_SMOKE_BIN"

echo "runtime boundary smoke passed"
