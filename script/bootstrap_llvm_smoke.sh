#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
COMPILER="$BUILD_DIR/jiangc"
INPUT="$PROJECT_ROOT/bootstrap/entries/lexer.jiang"
OUTPUT_LL="$BUILD_DIR/lexer.ll"
OUTPUT_BIN="$BUILD_DIR/lexer"
PARSER_INPUT="$PROJECT_ROOT/bootstrap/entries/parser.jiang"
PARSER_BIN="$BUILD_DIR/parser"
PARSER_EXPECTED="$PROJECT_ROOT/bootstrap/parser.golden"
HIR_INPUT="$PROJECT_ROOT/bootstrap/entries/hir.jiang"
HIR_BIN="$BUILD_DIR/hir"
HIR_EXPECTED="$PROJECT_ROOT/bootstrap/hir.golden"
JIR_INPUT="$PROJECT_ROOT/bootstrap/entries/jir.jiang"
JIR_BIN="$BUILD_DIR/jir"
JIR_EXPECTED="$PROJECT_ROOT/bootstrap/jir.golden"
COMPILER_SOURCE_LOADER_INPUT="$PROJECT_ROOT/bootstrap/entries/compiler_source_loader.jiang"
COMPILER_SOURCE_LOADER_BIN="$BUILD_DIR/compiler_source_loader"
HIR_COMPILER_CORE_INPUT="$PROJECT_ROOT/bootstrap/entries/hir_compiler_core.jiang"
HIR_COMPILER_CORE_BIN="$BUILD_DIR/hir_compiler_core"
COMPILER_COMPILER_CORE_INPUT="$PROJECT_ROOT/bootstrap/entries/compiler_compiler_core.jiang"
COMPILER_COMPILER_CORE_BIN="$BUILD_DIR/compiler_compiler_core"
HIER_MODULE_LOADER_INPUT="$PROJECT_ROOT/bootstrap/entries/hir_module_loader.jiang"
HIER_MODULE_LOADER_BIN="$BUILD_DIR/hir_module_loader"
COMPILER_JIR_LOWER_INPUT="$PROJECT_ROOT/bootstrap/entries/compiler_jir_lower.jiang"
COMPILER_JIR_LOWER_BIN="$BUILD_DIR/compiler_jir_lower"
COMPILER_PARSER_STORE_INPUT="$PROJECT_ROOT/bootstrap/entries/compiler_parser_store.jiang"
COMPILER_PARSER_STORE_BIN="$BUILD_DIR/compiler_parser_store"
COMPILER_BUFFER_INT_INPUT="$PROJECT_ROOT/bootstrap/entries/compiler_buffer_int.jiang"
COMPILER_BUFFER_INT_BIN="$BUILD_DIR/compiler_buffer_int"
COMPILER_BUFFER_BYTES_INPUT="$PROJECT_ROOT/bootstrap/entries/compiler_buffer_bytes.jiang"
COMPILER_BUFFER_BYTES_BIN="$BUILD_DIR/compiler_buffer_bytes"
COMPILER_INTERN_POOL_INPUT="$PROJECT_ROOT/bootstrap/entries/compiler_intern_pool.jiang"
COMPILER_INTERN_POOL_BIN="$BUILD_DIR/compiler_intern_pool"
COMPILER_MODULE_PATHS_INPUT="$PROJECT_ROOT/bootstrap/entries/compiler_module_paths.jiang"
COMPILER_MODULE_PATHS_BIN="$BUILD_DIR/compiler_module_paths"
COMPILER_TOKEN_STORE_INPUT="$PROJECT_ROOT/bootstrap/entries/compiler_token_store.jiang"
COMPILER_TOKEN_STORE_BIN="$BUILD_DIR/compiler_token_store"
COMPILER_HIR_STORE_INPUT="$PROJECT_ROOT/bootstrap/entries/compiler_hir_store.jiang"
COMPILER_HIR_STORE_BIN="$BUILD_DIR/compiler_hir_store"
COMPILER_JIR_STORE_INPUT="$PROJECT_ROOT/bootstrap/entries/compiler_jir_store.jiang"
COMPILER_JIR_STORE_BIN="$BUILD_DIR/compiler_jir_store"
COMPILER_SYMBOL_STORE_INPUT="$PROJECT_ROOT/bootstrap/entries/compiler_symbol_store.jiang"
COMPILER_SYMBOL_STORE_BIN="$BUILD_DIR/compiler_symbol_store"
COMPILER_TYPE_STORE_INPUT="$PROJECT_ROOT/bootstrap/entries/compiler_type_store.jiang"
COMPILER_TYPE_STORE_BIN="$BUILD_DIR/compiler_type_store"
HIR_PARSER_CORE_INPUT="$PROJECT_ROOT/bootstrap/entries/hir_parser_core.jiang"
HIR_PARSER_CORE_BIN="$BUILD_DIR/hir_parser_core"
JIR_PARSER_CORE_INPUT="$PROJECT_ROOT/bootstrap/entries/jir_parser_core.jiang"
JIR_PARSER_CORE_BIN="$BUILD_DIR/jir_parser_core"
HIR_LEXER_CORE_INPUT="$PROJECT_ROOT/bootstrap/entries/hir_lexer_core.jiang"
HIR_LEXER_CORE_BIN="$BUILD_DIR/hir_lexer_core"
HIR_HIR_CORE_INPUT="$PROJECT_ROOT/bootstrap/entries/hir_hir_core.jiang"
HIR_HIR_CORE_BIN="$BUILD_DIR/hir_hir_core"
MANIFEST="$PROJECT_ROOT/bootstrap/jiang.build"
MANIFEST_BIN="$PROJECT_ROOT/bootstrap/build/lexer"

require_output_contains() {
    local label="$1"
    local output="$2"
    local needle="$3"
    local description="$4"
    if [[ "$output" != *"$needle"* ]]; then
        echo "bootstrap llvm smoke $label missing $description" >&2
        exit 1
    fi
}

run_llvm_wrapper() {
    local label="$1"
    local input="$2"
    local bin="$3"
    "$COMPILER" --backend llvm "$input" >/dev/null
    if [[ ! -f "$bin" ]]; then
        echo "bootstrap llvm smoke missing $label output: $bin" >&2
        exit 1
    fi
    "$bin"
}

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

COMPILER_SOURCE_LOADER_OUT="$(run_llvm_wrapper "compiler_source_loader" "$COMPILER_SOURCE_LOADER_INPUT" "$COMPILER_SOURCE_LOADER_BIN")"
require_output_contains "compiler_source_loader" "$COMPILER_SOURCE_LOADER_OUT" "is_supported_module_import" "source_loader symbol"
require_output_contains "compiler_source_loader" "$COMPILER_SOURCE_LOADER_OUT" "read_source" "read_source symbol"

HIR_COMPILER_CORE_OUT="$(run_llvm_wrapper "hir_compiler_core" "$HIR_COMPILER_CORE_INPUT" "$HIR_COMPILER_CORE_BIN")"
require_output_contains "hir_compiler_core" "$HIR_COMPILER_CORE_OUT" "bootstrap hir dump:" "hir dump"
require_output_contains "hir_compiler_core" "$HIR_COMPILER_CORE_OUT" "func_decl public compile_entry" "compile_entry symbol"

COMPILER_COMPILER_CORE_OUT="$(run_llvm_wrapper "compiler_compiler_core" "$COMPILER_COMPILER_CORE_INPUT" "$COMPILER_COMPILER_CORE_BIN")"
require_output_contains "compiler_compiler_core" "$COMPILER_COMPILER_CORE_OUT" "compile_entry" "compile_entry symbol"
require_output_contains "compiler_compiler_core" "$COMPILER_COMPILER_CORE_OUT" "lower_program_to_jir" "lower_program_to_jir symbol"

HIER_MODULE_LOADER_OUT="$(run_llvm_wrapper "hir_module_loader" "$HIER_MODULE_LOADER_INPUT" "$HIER_MODULE_LOADER_BIN")"
require_output_contains "hir_module_loader" "$HIER_MODULE_LOADER_OUT" "bootstrap hir dump:" "hir dump"
require_output_contains "hir_module_loader" "$HIER_MODULE_LOADER_OUT" "func_decl public load_entry_graph" "load_entry_graph symbol"

COMPILER_JIR_LOWER_OUT="$(run_llvm_wrapper "compiler_jir_lower" "$COMPILER_JIR_LOWER_INPUT" "$COMPILER_JIR_LOWER_BIN")"
require_output_contains "compiler_jir_lower" "$COMPILER_JIR_LOWER_OUT" "lower_program_to_jir" "lower_program_to_jir symbol"
require_output_contains "compiler_jir_lower" "$COMPILER_JIR_LOWER_OUT" "emit_c_program" "emit_c_program symbol"

COMPILER_PARSER_STORE_OUT="$(run_llvm_wrapper "compiler_parser_store" "$COMPILER_PARSER_STORE_INPUT" "$COMPILER_PARSER_STORE_BIN")"
require_output_contains "compiler_parser_store" "$COMPILER_PARSER_STORE_OUT" "parser_store_new_node" "parser_store_new_node symbol"
require_output_contains "compiler_parser_store" "$COMPILER_PARSER_STORE_OUT" "node_kinds" "parser_store globals"

COMPILER_BUFFER_INT_OUT="$(run_llvm_wrapper "compiler_buffer_int" "$COMPILER_BUFFER_INT_INPUT" "$COMPILER_BUFFER_INT_BIN")"
require_output_contains "compiler_buffer_int" "$COMPILER_BUFFER_INT_OUT" "buffer_int_new" "buffer_int_new symbol"
require_output_contains "compiler_buffer_int" "$COMPILER_BUFFER_INT_OUT" "data_pool_used" "buffer_int globals"

COMPILER_BUFFER_BYTES_OUT="$(run_llvm_wrapper "compiler_buffer_bytes" "$COMPILER_BUFFER_BYTES_INPUT" "$COMPILER_BUFFER_BYTES_BIN")"
require_output_contains "compiler_buffer_bytes" "$COMPILER_BUFFER_BYTES_OUT" "buffer_bytes_new" "buffer_bytes_new symbol"
require_output_contains "compiler_buffer_bytes" "$COMPILER_BUFFER_BYTES_OUT" "buffer_bytes_slice" "buffer_bytes_slice symbol"

COMPILER_INTERN_POOL_OUT="$(run_llvm_wrapper "compiler_intern_pool" "$COMPILER_INTERN_POOL_INPUT" "$COMPILER_INTERN_POOL_BIN")"
require_output_contains "compiler_intern_pool" "$COMPILER_INTERN_POOL_OUT" "intern_pool_intern" "intern_pool_intern symbol"
require_output_contains "compiler_intern_pool" "$COMPILER_INTERN_POOL_OUT" "intern_pool_value" "intern_pool_value symbol"

COMPILER_MODULE_PATHS_OUT="$(run_llvm_wrapper "compiler_module_paths" "$COMPILER_MODULE_PATHS_INPUT" "$COMPILER_MODULE_PATHS_BIN")"
require_output_contains "compiler_module_paths" "$COMPILER_MODULE_PATHS_OUT" "module_id_for_path" "module_id_for_path symbol"
require_output_contains "compiler_module_paths" "$COMPILER_MODULE_PATHS_OUT" "root_module_count" "root_module_count symbol"

COMPILER_TOKEN_STORE_OUT="$(run_llvm_wrapper "compiler_token_store" "$COMPILER_TOKEN_STORE_INPUT" "$COMPILER_TOKEN_STORE_BIN")"
require_output_contains "compiler_token_store" "$COMPILER_TOKEN_STORE_OUT" "push_token" "push_token symbol"
require_output_contains "compiler_token_store" "$COMPILER_TOKEN_STORE_OUT" "token_kinds" "token_store globals"

COMPILER_HIR_STORE_OUT="$(run_llvm_wrapper "compiler_hir_store" "$COMPILER_HIR_STORE_INPUT" "$COMPILER_HIR_STORE_BIN")"
require_output_contains "compiler_hir_store" "$COMPILER_HIR_STORE_OUT" "hir_store_new_node" "hir_store_new_node symbol"
require_output_contains "compiler_hir_store" "$COMPILER_HIR_STORE_OUT" "hir_node_kinds" "hir_store globals"

COMPILER_JIR_STORE_OUT="$(run_llvm_wrapper "compiler_jir_store" "$COMPILER_JIR_STORE_INPUT" "$COMPILER_JIR_STORE_BIN")"
require_output_contains "compiler_jir_store" "$COMPILER_JIR_STORE_OUT" "jir_store_new_node" "jir_store_new_node symbol"
require_output_contains "compiler_jir_store" "$COMPILER_JIR_STORE_OUT" "jir_node_kinds" "jir_store globals"

COMPILER_SYMBOL_STORE_OUT="$(run_llvm_wrapper "compiler_symbol_store" "$COMPILER_SYMBOL_STORE_INPUT" "$COMPILER_SYMBOL_STORE_BIN")"
require_output_contains "compiler_symbol_store" "$COMPILER_SYMBOL_STORE_OUT" "symbol_store_new_symbol" "symbol_store_new_symbol symbol"
require_output_contains "compiler_symbol_store" "$COMPILER_SYMBOL_STORE_OUT" "symbol_kinds" "symbol_store globals"

COMPILER_TYPE_STORE_OUT="$(run_llvm_wrapper "compiler_type_store" "$COMPILER_TYPE_STORE_INPUT" "$COMPILER_TYPE_STORE_BIN")"
require_output_contains "compiler_type_store" "$COMPILER_TYPE_STORE_OUT" "type_store_reset" "type_store_reset symbol"
require_output_contains "compiler_type_store" "$COMPILER_TYPE_STORE_OUT" "type_kinds" "type_store globals"

HIR_PARSER_CORE_OUT="$(run_llvm_wrapper "hir_parser_core" "$HIR_PARSER_CORE_INPUT" "$HIR_PARSER_CORE_BIN")"
require_output_contains "hir_parser_core" "$HIR_PARSER_CORE_OUT" "bootstrap hir dump:" "hir dump"
require_output_contains "hir_parser_core" "$HIR_PARSER_CORE_OUT" "func_decl public parser_core_parse_source" "parser_core_parse_source symbol"

JIR_PARSER_CORE_OUT="$(run_llvm_wrapper "jir_parser_core" "$JIR_PARSER_CORE_INPUT" "$JIR_PARSER_CORE_BIN")"
require_output_contains "jir_parser_core" "$JIR_PARSER_CORE_OUT" "bootstrap jir dump:" "jir dump"
require_output_contains "jir_parser_core" "$JIR_PARSER_CORE_OUT" "func_decl sym=" "jir function dump"

HIR_LEXER_CORE_OUT="$(run_llvm_wrapper "hir_lexer_core" "$HIR_LEXER_CORE_INPUT" "$HIR_LEXER_CORE_BIN")"
require_output_contains "hir_lexer_core" "$HIR_LEXER_CORE_OUT" "bootstrap hir dump:" "hir dump"
require_output_contains "hir_lexer_core" "$HIR_LEXER_CORE_OUT" "func_decl public lex_source" "lex_source symbol"

HIR_HIR_CORE_OUT="$(run_llvm_wrapper "hir_hir_core" "$HIR_HIR_CORE_INPUT" "$HIR_HIR_CORE_BIN")"
require_output_contains "hir_hir_core" "$HIR_HIR_CORE_OUT" "bootstrap hir dump:" "hir dump"
require_output_contains "hir_hir_core" "$HIR_HIR_CORE_OUT" "func_decl public hir_core_lower_module" "hir_core_lower_module symbol"

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
