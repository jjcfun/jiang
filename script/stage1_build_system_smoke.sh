#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
MANIFEST="$PROJECT_ROOT/bootstrap/jiang.build"
OUTPUT_BIN="$PROJECT_ROOT/bootstrap/build/lexer"
PARSER_BIN="$PROJECT_ROOT/bootstrap/build/lexer_parser"
HIR_BIN="$PROJECT_ROOT/bootstrap/build/lexer_hir"
JIR_BIN="$PROJECT_ROOT/bootstrap/build/lexer_jir"
COMPILER_BIN="$PROJECT_ROOT/bootstrap/build/lexer_compiler"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make

cd "$PROJECT_ROOT"

"$BUILD_DIR/jiangc" build --manifest "$MANIFEST"

if [[ ! -f "$OUTPUT_BIN" ]]; then
    echo "stage1 build system smoke missing bootstrap output binary" >&2
    exit 1
fi

LEXER_OUT="$("$OUTPUT_BIN")"
printf '%s\n' "$LEXER_OUT"

if [[ "$LEXER_OUT" != *"bootstrap lexer token dump:"* ]]; then
    echo "stage1 build system smoke missing lexer dump header" >&2
    exit 1
fi

if [[ "$LEXER_OUT" != *"eof"* ]]; then
    echo "stage1 build system smoke missing lexer eof token" >&2
    exit 1
fi

RUN_LEXER_OUT="$("$BUILD_DIR/jiangc" run --manifest "$MANIFEST")"
if [[ "$RUN_LEXER_OUT" != *"bootstrap lexer token dump:"* ]]; then
    echo "stage1 build system smoke missing lexer run output" >&2
    exit 1
fi

echo "--- Stage1 Build System Smoke: parser target ---"
"$BUILD_DIR/jiangc" build --manifest "$MANIFEST" --target parser
test -f "$PARSER_BIN"
PARSER_OUT="$("$PARSER_BIN")"
if [[ "$PARSER_OUT" != *"bootstrap parser dump:"* ]]; then
    echo "stage1 build system smoke missing parser dump header" >&2
    exit 1
fi
RUN_PARSER_OUT="$("$BUILD_DIR/jiangc" run --manifest "$MANIFEST" --target parser)"
if [[ "$RUN_PARSER_OUT" != *"bootstrap parser dump:"* ]]; then
    echo "stage1 build system smoke missing parser run output" >&2
    exit 1
fi

echo "--- Stage1 Build System Smoke: hir target ---"
"$BUILD_DIR/jiangc" build --manifest "$MANIFEST" --target hir
test -f "$HIR_BIN"
HIR_OUT="$("$HIR_BIN")"
if [[ "$HIR_OUT" != *"bootstrap hir dump:"* ]]; then
    echo "stage1 build system smoke missing hir dump header" >&2
    exit 1
fi
RUN_HIR_OUT="$("$BUILD_DIR/jiangc" run --manifest "$MANIFEST" --target hir)"
if [[ "$RUN_HIR_OUT" != *"bootstrap hir dump:"* ]]; then
    echo "stage1 build system smoke missing hir run output" >&2
    exit 1
fi

echo "--- Stage1 Build System Smoke: jir target ---"
"$BUILD_DIR/jiangc" build --manifest "$MANIFEST" --target jir
test -f "$JIR_BIN"
JIR_OUT="$("$JIR_BIN")"
if [[ "$JIR_OUT" != *"bootstrap jir dump:"* ]]; then
    echo "stage1 build system smoke missing jir dump header" >&2
    exit 1
fi
RUN_JIR_OUT="$("$BUILD_DIR/jiangc" run --manifest "$MANIFEST" --target jir)"
if [[ "$RUN_JIR_OUT" != *"bootstrap jir dump:"* ]]; then
    echo "stage1 build system smoke missing jir run output" >&2
    exit 1
fi

echo "--- Stage1 Build System Smoke: compiler target ---"
"$BUILD_DIR/jiangc" build --manifest "$MANIFEST" --target compiler
test -f "$COMPILER_BIN"
COMPILER_OUT="$("$COMPILER_BIN")"
if [[ "$COMPILER_OUT" != *"#include <stdio.h>"* ]]; then
    echo "stage1 build system smoke missing compiler c header" >&2
    exit 1
fi
if [[ "$COMPILER_OUT" != *"int main(void)"* ]]; then
    echo "stage1 build system smoke missing compiler output" >&2
    exit 1
fi
RUN_COMPILER_OUT="$("$BUILD_DIR/jiangc" run --manifest "$MANIFEST" --target compiler)"
if [[ "$RUN_COMPILER_OUT" != *"#include <stdio.h>"* ]]; then
    echo "stage1 build system smoke missing compiler run output" >&2
    exit 1
fi
if [[ "$RUN_COMPILER_OUT" != *"int main(void)"* ]]; then
    echo "stage1 build system smoke missing compiler run main" >&2
    exit 1
fi

echo "stage1 build system target matrix passed"
