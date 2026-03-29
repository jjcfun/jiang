#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
COMPILER="$BUILD_DIR/jiangc"
INPUT="$PROJECT_ROOT/bootstrap/lexer.jiang"
OUTPUT_LL="$BUILD_DIR/lexer.ll"
OUTPUT_BIN="$BUILD_DIR/lexer"
PARSER_INPUT="$PROJECT_ROOT/bootstrap/parser.jiang"
PARSER_BIN="$BUILD_DIR/parser"
PARSER_EXPECTED="$PROJECT_ROOT/bootstrap/parser.golden"
HIR_INPUT="$PROJECT_ROOT/bootstrap/hir.jiang"
HIR_BIN="$BUILD_DIR/hir"
HIR_EXPECTED="$PROJECT_ROOT/bootstrap/hir.golden"
JIR_INPUT="$PROJECT_ROOT/bootstrap/jir.jiang"
JIR_BIN="$BUILD_DIR/jir"
JIR_EXPECTED="$PROJECT_ROOT/bootstrap/jir.golden"
COMPILER_SOURCE_LOADER_INPUT="$PROJECT_ROOT/bootstrap/compiler_source_loader.jiang"
COMPILER_SOURCE_LOADER_BIN="$BUILD_DIR/compiler_source_loader"
HIR_COMPILER_CORE_INPUT="$PROJECT_ROOT/bootstrap/hir_compiler_core.jiang"
HIR_COMPILER_CORE_BIN="$BUILD_DIR/hir_compiler_core"
COMPILER_COMPILER_CORE_INPUT="$PROJECT_ROOT/bootstrap/compiler_compiler_core.jiang"
COMPILER_COMPILER_CORE_BIN="$BUILD_DIR/compiler_compiler_core"
MANIFEST="$PROJECT_ROOT/bootstrap/jiang.build"
MANIFEST_BIN="$PROJECT_ROOT/bootstrap/build/lexer"

mkdir -p "$BUILD_DIR"
cd "$PROJECT_ROOT"

"$COMPILER" --emit-llvm "$INPUT" >/dev/null

if [[ ! -f "$OUTPUT_LL" ]]; then
    echo "bootstrap llvm smoke missing IR output: $OUTPUT_LL" >&2
    exit 1
fi

opt -passes=verify -disable-output "$OUTPUT_LL"

if ! rg -q "define i32 @main" "$OUTPUT_LL"; then
    echo "bootstrap llvm smoke missing main definition" >&2
    exit 1
fi

"$COMPILER" --backend llvm "$INPUT" >/dev/null

if [[ ! -f "$OUTPUT_BIN" ]]; then
    echo "bootstrap llvm smoke missing single-file output: $OUTPUT_BIN" >&2
    exit 1
fi

SINGLE_OUT="$("$OUTPUT_BIN")"
if [[ "$SINGLE_OUT" != *"bootstrap lexer token dump:"* || "$SINGLE_OUT" != *"eof"* ]]; then
    echo "bootstrap llvm smoke single-file run output mismatch" >&2
    exit 1
fi

"$COMPILER" --backend llvm "$PARSER_INPUT" >/dev/null

if [[ ! -f "$PARSER_BIN" ]]; then
    echo "bootstrap llvm smoke missing parser output: $PARSER_BIN" >&2
    exit 1
fi

PARSER_OUT="$("$PARSER_BIN")"
if [[ "$PARSER_OUT" != "$(cat "$PARSER_EXPECTED")" ]]; then
    echo "bootstrap llvm smoke parser output mismatch" >&2
    diff -u "$PARSER_EXPECTED" <(printf '%s\n' "$PARSER_OUT") || true
    exit 1
fi

"$COMPILER" --backend llvm "$HIR_INPUT" >/dev/null

if [[ ! -f "$HIR_BIN" ]]; then
    echo "bootstrap llvm smoke missing hir output: $HIR_BIN" >&2
    exit 1
fi

HIR_OUT="$("$HIR_BIN")"
if [[ "$HIR_OUT" != "$(cat "$HIR_EXPECTED")" ]]; then
    echo "bootstrap llvm smoke hir output mismatch" >&2
    diff -u "$HIR_EXPECTED" <(printf '%s\n' "$HIR_OUT") || true
    exit 1
fi

"$COMPILER" --backend llvm "$JIR_INPUT" >/dev/null

if [[ ! -f "$JIR_BIN" ]]; then
    echo "bootstrap llvm smoke missing jir output: $JIR_BIN" >&2
    exit 1
fi

JIR_OUT="$("$JIR_BIN")"
if [[ "$JIR_OUT" != "$(cat "$JIR_EXPECTED")" ]]; then
    echo "bootstrap llvm smoke jir output mismatch" >&2
    diff -u "$JIR_EXPECTED" <(printf '%s\n' "$JIR_OUT") || true
    exit 1
fi

"$COMPILER" --backend llvm "$COMPILER_SOURCE_LOADER_INPUT" >/dev/null

if [[ ! -f "$COMPILER_SOURCE_LOADER_BIN" ]]; then
    echo "bootstrap llvm smoke missing compiler_source_loader output: $COMPILER_SOURCE_LOADER_BIN" >&2
    exit 1
fi

COMPILER_SOURCE_LOADER_OUT="$("$COMPILER_SOURCE_LOADER_BIN")"
if [[ "$COMPILER_SOURCE_LOADER_OUT" != *"is_supported_module_import"* ]]; then
    echo "bootstrap llvm smoke compiler_source_loader missing source_loader symbol" >&2
    exit 1
fi
if [[ "$COMPILER_SOURCE_LOADER_OUT" != *"read_source"* ]]; then
    echo "bootstrap llvm smoke compiler_source_loader missing read_source symbol" >&2
    exit 1
fi

"$COMPILER" --backend llvm "$HIR_COMPILER_CORE_INPUT" >/dev/null

if [[ ! -f "$HIR_COMPILER_CORE_BIN" ]]; then
    echo "bootstrap llvm smoke missing hir_compiler_core output: $HIR_COMPILER_CORE_BIN" >&2
    exit 1
fi

HIR_COMPILER_CORE_OUT="$("$HIR_COMPILER_CORE_BIN")"
if [[ "$HIR_COMPILER_CORE_OUT" != *"bootstrap hir dump:"* ]]; then
    echo "bootstrap llvm smoke hir_compiler_core missing hir dump" >&2
    exit 1
fi
if [[ "$HIR_COMPILER_CORE_OUT" != *"func_decl public compile_entry"* ]]; then
    echo "bootstrap llvm smoke hir_compiler_core missing compile_entry symbol" >&2
    exit 1
fi

"$COMPILER" --backend llvm "$COMPILER_COMPILER_CORE_INPUT" >/dev/null

if [[ ! -f "$COMPILER_COMPILER_CORE_BIN" ]]; then
    echo "bootstrap llvm smoke missing compiler_compiler_core output: $COMPILER_COMPILER_CORE_BIN" >&2
    exit 1
fi

COMPILER_COMPILER_CORE_OUT="$("$COMPILER_COMPILER_CORE_BIN")"
if [[ "$COMPILER_COMPILER_CORE_OUT" != *"compile_entry"* ]]; then
    echo "bootstrap llvm smoke compiler_compiler_core missing compile_entry symbol" >&2
    exit 1
fi
if [[ "$COMPILER_COMPILER_CORE_OUT" != *"lower_program_to_jir"* ]]; then
    echo "bootstrap llvm smoke compiler_compiler_core missing lower_program_to_jir symbol" >&2
    exit 1
fi

"$COMPILER" build --backend llvm --manifest "$MANIFEST" >/dev/null

if [[ ! -f "$MANIFEST_BIN" ]]; then
    echo "bootstrap llvm smoke missing manifest output: $MANIFEST_BIN" >&2
    exit 1
fi

MANIFEST_OUT="$("$MANIFEST_BIN")"
if [[ "$MANIFEST_OUT" != *"bootstrap lexer token dump:"* || "$MANIFEST_OUT" != *"eof"* ]]; then
    echo "bootstrap llvm smoke manifest run output mismatch" >&2
    exit 1
fi

echo "bootstrap llvm smoke passed"
