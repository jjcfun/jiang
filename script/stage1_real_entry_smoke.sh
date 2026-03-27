#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make

cd "$PROJECT_ROOT"

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_source_loader.jiang"
SOURCE_LOADER_C="$("$BUILD_DIR/compiler_source_loader")"
printf '%s\n' "$SOURCE_LOADER_C"

if [[ "$SOURCE_LOADER_C" != *"is_supported_module_import"* ]]; then
    echo "stage1 real-entry codegen missing source_loader symbol" >&2
    exit 1
fi
if [[ "$SOURCE_LOADER_C" != *"read_source"* ]]; then
    echo "stage1 real-entry codegen missing read_source symbol" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/hir_parser_core.jiang"
HIR_PARSER_CORE_OUT="$("$BUILD_DIR/hir_parser_core")"
printf '%s\n' "$HIR_PARSER_CORE_OUT"
if [[ "$HIR_PARSER_CORE_OUT" != *"bootstrap hir dump:"* ]]; then
    echo "stage1 real-entry failed to run parser_core hir wrapper" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/jir_parser_core.jiang"
JIR_PARSER_CORE_OUT="$("$BUILD_DIR/jir_parser_core")"
printf '%s\n' "$JIR_PARSER_CORE_OUT"
if [[ "$JIR_PARSER_CORE_OUT" != *"bootstrap jir dump:"* ]]; then
    echo "stage1 real-entry failed to run parser_core jir wrapper" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/hir_compiler_core.jiang"
HIR_COMPILER_CORE_OUT="$("$BUILD_DIR/hir_compiler_core")"
printf '%s\n' "$HIR_COMPILER_CORE_OUT"
if [[ "$HIR_COMPILER_CORE_OUT" != *"bootstrap hir dump:"* ]]; then
    echo "stage1 real-entry failed to run compiler_core hir wrapper" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_compiler_core.jiang"
COMPILER_CORE_C="$("$BUILD_DIR/compiler_compiler_core")"
printf '%s\n' "$COMPILER_CORE_C"
if [[ "$COMPILER_CORE_C" != *"compile_entry"* ]]; then
    echo "stage1 real-entry codegen missing compile_entry symbol" >&2
    exit 1
fi
if [[ "$COMPILER_CORE_C" != *"lower_program_to_jir"* ]]; then
    echo "stage1 real-entry codegen missing lower_program_to_jir symbol" >&2
    exit 1
fi

echo "stage1 real-entry smoke passed"
