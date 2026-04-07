#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_DIR="$BUILD_DIR/stage1_run_smoke"

mkdir -p "$OUT_DIR"
cd "$PROJECT_ROOT"

bash "$PROJECT_ROOT/script/build_stage1.sh"

HELP_OUT="$OUT_DIR/stage1c_help.txt"
DEFAULT_C="$OUT_DIR/lexer_entry_default.c"
EXPLICIT_C="$OUT_DIR/lexer_entry_explicit.c"
DUMP_HIR_OUT="$OUT_DIR/hir_dump.txt"

if ! "$BUILD_DIR/stage1c" --help > "$HELP_OUT"; then
    echo "stage1c --help failed" >&2
    exit 1
fi
if [[ "$(<"$HELP_OUT")" != *"emit-c"* ]]; then
    echo "stage1c --help missing emit-c mode" >&2
    exit 1
fi
if [[ "$(<"$HELP_OUT")" != *"dump-ast"* ]]; then
    echo "stage1c --help missing dump-ast mode" >&2
    exit 1
fi
if [[ "$(<"$HELP_OUT")" != *"dump-hir"* ]]; then
    echo "stage1c --help missing dump-hir mode" >&2
    exit 1
fi
if [[ "$(<"$HELP_OUT")" != *"dump-jir"* ]]; then
    echo "stage1c --help missing dump-jir mode" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/entries/lexer.jiang" > "$DEFAULT_C"
"$BUILD_DIR/stage1c" --mode emit-c "bootstrap/entries/lexer.jiang" > "$EXPLICIT_C"
if ! cmp -s "$DEFAULT_C" "$EXPLICIT_C"; then
    echo "stage1c default emit-c output differs from explicit --mode emit-c" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" --mode dump-hir "bootstrap/entries/hir.jiang" > "$DUMP_HIR_OUT"
if [[ "$(<"$DUMP_HIR_OUT")" != *"bootstrap hir dump:"* ]]; then
    echo "stage1c --mode dump-hir missing hir dump header" >&2
    exit 1
fi

if "$BUILD_DIR/stage1c" --mode bad "bootstrap/entries/lexer.jiang" > /dev/null 2>&1; then
    echo "stage1c accepted invalid mode" >&2
    exit 1
fi

run_stage1_entry() {
    local source_path="$1"
    local stem="$2"
    local expected_text="$3"
    local out_c="$OUT_DIR/${stem}.c"
    local out_bin="$OUT_DIR/${stem}"
    local out_log="$OUT_DIR/${stem}.log"

    "$BUILD_DIR/stage1c" --mode emit-c "$source_path" > "$out_c"
    cc -std=c99 -I "$PROJECT_ROOT/include" "$out_c" "$PROJECT_ROOT/runtime/stage1_host.c" -o "$out_bin"
    "$out_bin" > "$out_log"
    if [[ "$(<"$out_log")" != *"$expected_text"* ]]; then
        echo "stage1 run smoke missing expected text '$expected_text' for $stem" >&2
        exit 1
    fi
}

run_stage1_entry "bootstrap/entries/lexer.jiang" "lexer_entry" "bootstrap lexer smoke passed"
run_stage1_entry "bootstrap/entries/parser.jiang" "parser_entry" "bootstrap parser smoke passed"
run_stage1_entry "bootstrap/entries/hir.jiang" "hir_entry" "bootstrap hir smoke passed"
run_stage1_entry "bootstrap/entries/jir.jiang" "jir_entry" "bootstrap jir smoke passed"

echo "stage1 run smoke passed"
