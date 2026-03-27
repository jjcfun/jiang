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

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/hir_lexer_core.jiang"
HIR_LEXER_CORE_OUT="$("$BUILD_DIR/hir_lexer_core")"
printf '%s\n' "$HIR_LEXER_CORE_OUT"
if [[ "$HIR_LEXER_CORE_OUT" != *"func_decl public lex_source"* ]]; then
    echo "stage1 real-entry failed to run lexer_core hir wrapper" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/hir_hir_core.jiang"
HIR_HIR_CORE_OUT="$("$BUILD_DIR/hir_hir_core")"
printf '%s\n' "$HIR_HIR_CORE_OUT"
if [[ "$HIR_HIR_CORE_OUT" != *"func_decl public hir_core_lower_module"* ]]; then
    echo "stage1 real-entry failed to run hir_core hir wrapper" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/hir_module_loader.jiang"
HIR_MODULE_LOADER_OUT="$("$BUILD_DIR/hir_module_loader")"
printf '%s\n' "$HIR_MODULE_LOADER_OUT"
if [[ "$HIR_MODULE_LOADER_OUT" != *"func_decl public load_entry_graph"* ]]; then
    echo "stage1 real-entry failed to run module_loader hir wrapper" >&2
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

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_jir_lower.jiang"
JIR_LOWER_C="$("$BUILD_DIR/compiler_jir_lower")"
printf '%s\n' "$JIR_LOWER_C"
if [[ "$JIR_LOWER_C" != *"lower_program_to_jir"* ]]; then
    echo "stage1 real-entry codegen missing jir_lower lowering symbol" >&2
    exit 1
fi
if [[ "$JIR_LOWER_C" != *"emit_c_program"* ]]; then
    echo "stage1 real-entry codegen missing jir_lower emitter symbol" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_parser_store.jiang"
PARSER_STORE_C="$("$BUILD_DIR/compiler_parser_store")"
printf '%s\n' "$PARSER_STORE_C"
if [[ "$PARSER_STORE_C" != *"parser_store_new_node"* ]]; then
    echo "stage1 real-entry codegen missing parser_store_new_node symbol" >&2
    exit 1
fi
if [[ "$PARSER_STORE_C" != *"node_kinds"* ]]; then
    echo "stage1 real-entry codegen missing parser_store globals" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_buffer_int.jiang"
BUFFER_INT_C="$("$BUILD_DIR/compiler_buffer_int")"
printf '%s\n' "$BUFFER_INT_C"
if [[ "$BUFFER_INT_C" != *"buffer_int_new"* ]]; then
    echo "stage1 real-entry codegen missing buffer_int_new symbol" >&2
    exit 1
fi
if [[ "$BUFFER_INT_C" != *"data_pool_used"* ]]; then
    echo "stage1 real-entry codegen missing buffer_int globals" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_buffer_bytes.jiang"
BUFFER_BYTES_C="$("$BUILD_DIR/compiler_buffer_bytes")"
printf '%s\n' "$BUFFER_BYTES_C"
if [[ "$BUFFER_BYTES_C" != *"buffer_bytes_new"* ]]; then
    echo "stage1 real-entry codegen missing buffer_bytes_new symbol" >&2
    exit 1
fi
if [[ "$BUFFER_BYTES_C" != *"buffer_bytes_slice"* ]]; then
    echo "stage1 real-entry codegen missing buffer_bytes_slice symbol" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_intern_pool.jiang"
INTERN_POOL_C="$("$BUILD_DIR/compiler_intern_pool")"
printf '%s\n' "$INTERN_POOL_C"
if [[ "$INTERN_POOL_C" != *"intern_pool_intern"* ]]; then
    echo "stage1 real-entry codegen missing intern_pool_intern symbol" >&2
    exit 1
fi
if [[ "$INTERN_POOL_C" != *"intern_pool_value"* ]]; then
    echo "stage1 real-entry codegen missing intern_pool_value symbol" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_module_paths.jiang"
MODULE_PATHS_C="$("$BUILD_DIR/compiler_module_paths")"
printf '%s\n' "$MODULE_PATHS_C"
if [[ "$MODULE_PATHS_C" != *"module_id_for_path"* ]]; then
    echo "stage1 real-entry codegen missing module_id_for_path symbol" >&2
    exit 1
fi
if [[ "$MODULE_PATHS_C" != *"root_module_count"* ]]; then
    echo "stage1 real-entry codegen missing root_module_count symbol" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_token_store.jiang"
TOKEN_STORE_C="$("$BUILD_DIR/compiler_token_store")"
printf '%s\n' "$TOKEN_STORE_C"
if [[ "$TOKEN_STORE_C" != *"push_token"* ]]; then
    echo "stage1 real-entry codegen missing token_store push_token symbol" >&2
    exit 1
fi
if [[ "$TOKEN_STORE_C" != *"token_kinds"* ]]; then
    echo "stage1 real-entry codegen missing token_store globals" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_hir_store.jiang"
HIR_STORE_C="$("$BUILD_DIR/compiler_hir_store")"
printf '%s\n' "$HIR_STORE_C"
if [[ "$HIR_STORE_C" != *"hir_store_new_node"* ]]; then
    echo "stage1 real-entry codegen missing hir_store_new_node symbol" >&2
    exit 1
fi
if [[ "$HIR_STORE_C" != *"hir_node_kinds"* ]]; then
    echo "stage1 real-entry codegen missing hir_store globals" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_jir_store.jiang"
JIR_STORE_C="$("$BUILD_DIR/compiler_jir_store")"
printf '%s\n' "$JIR_STORE_C"
if [[ "$JIR_STORE_C" != *"jir_store_new_node"* ]]; then
    echo "stage1 real-entry codegen missing jir_store_new_node symbol" >&2
    exit 1
fi
if [[ "$JIR_STORE_C" != *"jir_node_kinds"* ]]; then
    echo "stage1 real-entry codegen missing jir_store globals" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_symbol_store.jiang"
SYMBOL_STORE_C="$("$BUILD_DIR/compiler_symbol_store")"
printf '%s\n' "$SYMBOL_STORE_C"
if [[ "$SYMBOL_STORE_C" != *"symbol_store_new_symbol"* ]]; then
    echo "stage1 real-entry codegen missing symbol_store_new_symbol symbol" >&2
    exit 1
fi
if [[ "$SYMBOL_STORE_C" != *"symbol_kinds"* ]]; then
    echo "stage1 real-entry codegen missing symbol_store globals" >&2
    exit 1
fi

"$BUILD_DIR/jiangc" --stdlib-dir "$PROJECT_ROOT/std" "$PROJECT_ROOT/bootstrap/compiler_type_store.jiang"
TYPE_STORE_C="$("$BUILD_DIR/compiler_type_store")"
printf '%s\n' "$TYPE_STORE_C"
if [[ "$TYPE_STORE_C" != *"type_store_reset"* ]]; then
    echo "stage1 real-entry codegen missing type_store_reset symbol" >&2
    exit 1
fi
if [[ "$TYPE_STORE_C" != *"type_kinds"* ]]; then
    echo "stage1 real-entry codegen missing type_store globals" >&2
    exit 1
fi

echo "stage1 real-entry smoke passed"
