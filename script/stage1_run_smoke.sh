#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_DIR="$BUILD_DIR/stage1_run_smoke"

mkdir -p "$OUT_DIR"
cd "$PROJECT_ROOT"

bash "$PROJECT_ROOT/script/build_stage1.sh"

run_stage1_entry() {
    local source_path="$1"
    local stem="$2"
    local expected_text="$3"
    local out_c="$OUT_DIR/${stem}.c"
    local out_bin="$OUT_DIR/${stem}"
    local out_log="$OUT_DIR/${stem}.log"

    "$BUILD_DIR/stage1c" "$source_path" > "$out_c"
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
