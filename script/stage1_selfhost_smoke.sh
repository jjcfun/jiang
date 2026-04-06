#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
OUT_DIR="$BUILD_DIR/stage1_selfhost_smoke"

mkdir -p "$OUT_DIR"
cd "$PROJECT_ROOT"

bash "$PROJECT_ROOT/script/build_stage1.sh"

"$BUILD_DIR/stage1c" "bootstrap/compiler_core.jiang" > "$OUT_DIR/compiler_core_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/compiler_core_from_stage1.c" -o "$OUT_DIR/compiler_core_from_stage1.o"
if [[ "$(<"$OUT_DIR/compiler_core_from_stage1.c")" != *"compile_entry"* ]]; then
    echo "stage1 selfhost output missing compile_entry symbol" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/source_loader.jiang" > "$OUT_DIR/source_loader_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/source_loader_from_stage1.c" -o "$OUT_DIR/source_loader_from_stage1.o"
if [[ "$(<"$OUT_DIR/source_loader_from_stage1.c")" != *"read_source"* ]]; then
    echo "stage1 selfhost output missing read_source symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/source_loader_from_stage1.c")" != *"is_supported_module_import"* ]]; then
    echo "stage1 selfhost output missing is_supported_module_import symbol" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/jir_lower.jiang" > "$OUT_DIR/jir_lower_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/jir_lower_from_stage1.c" -o "$OUT_DIR/jir_lower_from_stage1.o"
if [[ "$(<"$OUT_DIR/jir_lower_from_stage1.c")" != *"lower_program_to_jir"* ]]; then
    echo "stage1 selfhost output missing lower_program_to_jir symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/jir_lower_from_stage1.c")" != *"emit_c_program"* ]]; then
    echo "stage1 selfhost output missing emit_c_program symbol" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/hir_core.jiang" > "$OUT_DIR/hir_core_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/hir_core_from_stage1.c" -o "$OUT_DIR/hir_core_from_stage1.o"
if [[ "$(<"$OUT_DIR/hir_core_from_stage1.c")" != *"hir_core_lower_module"* ]]; then
    echo "stage1 selfhost output missing hir_core_lower_module symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/hir_core_from_stage1.c")" != *"predeclare_types"* ]]; then
    echo "stage1 selfhost output missing predeclare_types symbol" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/module_loader.jiang" > "$OUT_DIR/module_loader_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/module_loader_from_stage1.c" -o "$OUT_DIR/module_loader_from_stage1.o"
if [[ "$(<"$OUT_DIR/module_loader_from_stage1.c")" != *"load_entry_graph"* ]]; then
    echo "stage1 selfhost output missing load_entry_graph symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/module_loader_from_stage1.c")" != *"module_states"* ]]; then
    echo "stage1 selfhost output missing module_states global" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/parser_core.jiang" > "$OUT_DIR/parser_core_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/parser_core_from_stage1.c" -o "$OUT_DIR/parser_core_from_stage1.o"
if [[ "$(<"$OUT_DIR/parser_core_from_stage1.c")" != *"parser_core_parse_source"* ]]; then
    echo "stage1 selfhost output missing parser_core_parse_source symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/parser_core_from_stage1.c")" != *"lex_source"* ]]; then
    echo "stage1 selfhost output missing lex_source symbol" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/token_store.jiang" > "$OUT_DIR/token_store_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/token_store_from_stage1.c" -o "$OUT_DIR/token_store_from_stage1.o"
if [[ "$(<"$OUT_DIR/token_store_from_stage1.c")" != *"push_token"* ]]; then
    echo "stage1 selfhost output missing push_token symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/token_store_from_stage1.c")" != *"token_kinds"* ]]; then
    echo "stage1 selfhost output missing token_kinds global" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/symbol_store.jiang" > "$OUT_DIR/symbol_store_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/symbol_store_from_stage1.c" -o "$OUT_DIR/symbol_store_from_stage1.o"
if [[ "$(<"$OUT_DIR/symbol_store_from_stage1.c")" != *"symbol_store_new_symbol"* ]]; then
    echo "stage1 selfhost output missing symbol_store_new_symbol symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/symbol_store_from_stage1.c")" != *"symbol_kinds"* ]]; then
    echo "stage1 selfhost output missing symbol_kinds global" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/type_store.jiang" > "$OUT_DIR/type_store_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/type_store_from_stage1.c" -o "$OUT_DIR/type_store_from_stage1.o"
if [[ "$(<"$OUT_DIR/type_store_from_stage1.c")" != *"type_store_reset"* ]]; then
    echo "stage1 selfhost output missing type_store_reset symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/type_store_from_stage1.c")" != *"type_kinds"* ]]; then
    echo "stage1 selfhost output missing type_kinds global" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/hir_store.jiang" > "$OUT_DIR/hir_store_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/hir_store_from_stage1.c" -o "$OUT_DIR/hir_store_from_stage1.o"
if [[ "$(<"$OUT_DIR/hir_store_from_stage1.c")" != *"hir_store_new_node"* ]]; then
    echo "stage1 selfhost output missing hir_store_new_node symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/hir_store_from_stage1.c")" != *"hir_node_kinds"* ]]; then
    echo "stage1 selfhost output missing hir_node_kinds global" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/jir_store.jiang" > "$OUT_DIR/jir_store_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/jir_store_from_stage1.c" -o "$OUT_DIR/jir_store_from_stage1.o"
if [[ "$(<"$OUT_DIR/jir_store_from_stage1.c")" != *"jir_store_new_node"* ]]; then
    echo "stage1 selfhost output missing jir_store_new_node symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/jir_store_from_stage1.c")" != *"jir_node_kinds"* ]]; then
    echo "stage1 selfhost output missing jir_node_kinds global" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/module_paths.jiang" > "$OUT_DIR/module_paths_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/module_paths_from_stage1.c" -o "$OUT_DIR/module_paths_from_stage1.o"
if [[ "$(<"$OUT_DIR/module_paths_from_stage1.c")" != *"module_id_for_path"* ]]; then
    echo "stage1 selfhost output missing module_id_for_path symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/module_paths_from_stage1.c")" != *"root_module_count"* ]]; then
    echo "stage1 selfhost output missing root_module_count symbol" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/parser_store.jiang" > "$OUT_DIR/parser_store_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/parser_store_from_stage1.c" -o "$OUT_DIR/parser_store_from_stage1.o"
if [[ "$(<"$OUT_DIR/parser_store_from_stage1.c")" != *"parser_store_new_node"* ]]; then
    echo "stage1 selfhost output missing parser_store_new_node symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/parser_store_from_stage1.c")" != *"node_kinds"* ]]; then
    echo "stage1 selfhost output missing node_kinds global" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/buffer_int.jiang" > "$OUT_DIR/buffer_int_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/buffer_int_from_stage1.c" -o "$OUT_DIR/buffer_int_from_stage1.o"
if [[ "$(<"$OUT_DIR/buffer_int_from_stage1.c")" != *"buffer_int_new"* ]]; then
    echo "stage1 selfhost output missing buffer_int_new symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/buffer_int_from_stage1.c")" != *"buffer_int_slice"* ]]; then
    echo "stage1 selfhost output missing buffer_int_slice symbol" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/buffer_bytes.jiang" > "$OUT_DIR/buffer_bytes_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/buffer_bytes_from_stage1.c" -o "$OUT_DIR/buffer_bytes_from_stage1.o"
if [[ "$(<"$OUT_DIR/buffer_bytes_from_stage1.c")" != *"buffer_bytes_new"* ]]; then
    echo "stage1 selfhost output missing buffer_bytes_new symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/buffer_bytes_from_stage1.c")" != *"buffer_bytes_slice"* ]]; then
    echo "stage1 selfhost output missing buffer_bytes_slice symbol" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/intern_pool.jiang" > "$OUT_DIR/intern_pool_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/intern_pool_from_stage1.c" -o "$OUT_DIR/intern_pool_from_stage1.o"
if [[ "$(<"$OUT_DIR/intern_pool_from_stage1.c")" != *"intern_pool_intern"* ]]; then
    echo "stage1 selfhost output missing intern_pool_intern symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/intern_pool_from_stage1.c")" != *"intern_pool_value"* ]]; then
    echo "stage1 selfhost output missing intern_pool_value symbol" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/lexer_core.jiang" > "$OUT_DIR/lexer_core_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/lexer_core_from_stage1.c" -o "$OUT_DIR/lexer_core_from_stage1.o"
if [[ "$(<"$OUT_DIR/lexer_core_from_stage1.c")" != *"lex_source"* ]]; then
    echo "stage1 selfhost output missing lex_source symbol in lexer_core" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/lexer_core_from_stage1.c")" != *"is_keyword"* ]]; then
    echo "stage1 selfhost output missing is_keyword symbol" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/path_utils.jiang" > "$OUT_DIR/path_utils_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/path_utils_from_stage1.c" -o "$OUT_DIR/path_utils_from_stage1.o"
if [[ "$(<"$OUT_DIR/path_utils_from_stage1.c")" != *"is_supported_import_path"* ]]; then
    echo "stage1 selfhost output missing is_supported_import_path symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/path_utils_from_stage1.c")" != *"sample_source_path"* ]]; then
    echo "stage1 selfhost output missing sample_source_path symbol" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/host_runtime.jiang" > "$OUT_DIR/host_runtime_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/host_runtime_from_stage1.c" -o "$OUT_DIR/host_runtime_from_stage1.o"
if [[ "$(<"$OUT_DIR/host_runtime_from_stage1.c")" != *"host_file_exists"* ]]; then
    echo "stage1 selfhost output missing host_file_exists symbol" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/host_runtime_from_stage1.c")" != *"host_alloc_bytes"* ]]; then
    echo "stage1 selfhost output missing host_alloc_bytes symbol" >&2
    exit 1
fi

"$BUILD_DIR/stage1c" "bootstrap/compiler_source_loader.jiang" > "$OUT_DIR/compiler_source_loader_from_stage1.c"
cc -x c -std=c99 -I "$PROJECT_ROOT/include" -Wall -Wextra -Werror -c "$OUT_DIR/compiler_source_loader_from_stage1.c" -o "$OUT_DIR/compiler_source_loader_from_stage1.o"
if [[ "$(<"$OUT_DIR/compiler_source_loader_from_stage1.c")" != *"int main(void)"* ]]; then
    echo "stage1 selfhost output missing entry main for compiler_source_loader" >&2
    exit 1
fi
if [[ "$(<"$OUT_DIR/compiler_source_loader_from_stage1.c")" != *"compile_entry"* ]]; then
    echo "stage1 selfhost output missing compile_entry call path in compiler_source_loader" >&2
    exit 1
fi

echo "stage1 selfhost smoke passed"
