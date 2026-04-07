#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

HOST_OUTPUT="$("$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/compiler/entries/compiler.jiang")"
STAGE2_C_PATHS="$(printf '%s\n' "$HOST_OUTPUT" | sed -n 's/^Successfully generated C\/H code from JIR at: \(.*\.c\)$/\1/p')"

if [[ -z "$STAGE2_C_PATHS" ]]; then
    echo "failed to locate generated Stage2 compiler C output" >&2
    exit 1
fi

STAGE2_SOURCES=()
ENTRY_INDEX=0
while IFS= read -r stage2_c_path; do
    if [[ -z "$stage2_c_path" ]]; then
        continue
    fi
    if [[ "$stage2_c_path" != /* ]]; then
        stage2_c_path="$PROJECT_ROOT/$stage2_c_path"
    fi
    if [[ $ENTRY_INDEX -eq 0 ]]; then
        perl -0pe 's/\nint main\(\) \{\n    return 0;\n\}\n/\n/s' "$stage2_c_path" > "$BUILD_DIR/stage2_compiler.c"
        STAGE2_SOURCES+=("$BUILD_DIR/stage2_compiler.c")
    else
        STAGE2_SOURCES+=("$stage2_c_path")
    fi
    ENTRY_INDEX=$((ENTRY_INDEX + 1))
done <<< "$STAGE2_C_PATHS"

cc -std=c99 -I "$PROJECT_ROOT/include" \
    "${STAGE2_SOURCES[@]}" \
    "$PROJECT_ROOT/runtime/stage1_host.c" \
    "$PROJECT_ROOT/compiler/stage2_driver.c" \
    -o "$BUILD_DIR/stage2c"

echo "built $BUILD_DIR/stage2c"
