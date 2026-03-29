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
