#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "--- Stage1 Complete Smoke: lexer ---"
bash "$PROJECT_ROOT/script/stage1_smoke.sh"

echo "--- Stage1 Complete Smoke: parser ---"
bash "$PROJECT_ROOT/script/stage1_parser_smoke.sh"

echo "--- Stage1 Complete Smoke: hir ---"
bash "$PROJECT_ROOT/script/stage1_hir_smoke.sh"

echo "--- Stage1 Complete Smoke: jir ---"
bash "$PROJECT_ROOT/script/stage1_jir_smoke.sh"

echo "--- Stage1 Complete Smoke: codegen ---"
bash "$PROJECT_ROOT/script/stage1_codegen_smoke.sh"

echo "--- Stage1 Complete Smoke: real-entry ---"
bash "$PROJECT_ROOT/script/stage1_real_entry_smoke.sh"

echo "--- Stage1 Complete Smoke: build system ---"
bash "$PROJECT_ROOT/script/stage1_build_system_smoke.sh"

echo "--- Stage1 Complete Smoke: build stage1c ---"
bash "$PROJECT_ROOT/script/build_stage1.sh"

echo "--- Stage1 Complete Smoke: link ---"
bash "$PROJECT_ROOT/script/stage1_link_smoke.sh"

echo "--- Stage1 Complete Smoke: run ---"
bash "$PROJECT_ROOT/script/stage1_run_smoke.sh"

echo "--- Stage1 Complete Smoke: selfhost ---"
bash "$PROJECT_ROOT/script/stage1_selfhost_smoke.sh"

echo "stage1 complete smoke passed"
