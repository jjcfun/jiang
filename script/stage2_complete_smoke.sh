#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

bash "$PROJECT_ROOT/script/build_stage2.sh"
bash "$PROJECT_ROOT/script/stage2_cli_smoke.sh"
bash "$PROJECT_ROOT/script/stage2_emit_c_smoke.sh"
bash "$PROJECT_ROOT/script/stage2_run_smoke.sh"
bash "$PROJECT_ROOT/script/stage2_selfhost_smoke.sh"
bash "$PROJECT_ROOT/script/stage2_error_smoke.sh"
bash "$PROJECT_ROOT/script/stage2_llvm_smoke.sh"
bash "$PROJECT_ROOT/script/stage2_llvm_error_smoke.sh"

echo "stage2 complete smoke passed"
