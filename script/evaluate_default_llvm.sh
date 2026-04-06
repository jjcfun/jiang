#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
COMPILER="$BUILD_DIR/jiangc"

LEXER_INPUT="$PROJECT_ROOT/bootstrap/entries/lexer.jiang"
PARSER_INPUT="$PROJECT_ROOT/bootstrap/entries/parser.jiang"
HIR_INPUT="$PROJECT_ROOT/bootstrap/entries/hir.jiang"

DEMO_MANIFEST="$PROJECT_ROOT/examples/build_demo/jiang.build"
DEMO_BIN="$PROJECT_ROOT/examples/build_demo/build/build_demo"
DEMO_ALT_BIN="$PROJECT_ROOT/examples/build_demo/build/build_demo_alt"

BOOTSTRAP_MANIFEST="$PROJECT_ROOT/bootstrap/jiang.build"
BOOTSTRAP_BIN="$PROJECT_ROOT/bootstrap/build/lexer"

run_backend_binary() {
    local backend="$1"
    local input="$2"
    local bin="$3"

    rm -f "$bin"
    if [[ "$backend" == "c" ]]; then
        "$COMPILER" "$input" >/dev/null
    else
        "$COMPILER" --backend llvm "$input" >/dev/null
    fi

    if [[ ! -f "$bin" ]]; then
        echo "missing output binary for backend=$backend input=$input" >&2
        exit 1
    fi

    "$bin"
}

compare_backend_output() {
    local label="$1"
    local input="$2"
    local bin="$3"

    echo "compare backend output: $label"
    local c_out
    local llvm_out
    c_out="$(run_backend_binary "c" "$input" "$bin")"
    llvm_out="$(run_backend_binary "llvm" "$input" "$bin")"

    if [[ "$c_out" != "$llvm_out" ]]; then
        echo "backend output mismatch for $label" >&2
        diff -u <(printf '%s\n' "$c_out") <(printf '%s\n' "$llvm_out") || true
        exit 1
    fi
}

compare_manifest_output() {
    local label="$1"
    local manifest="$2"
    local bin="$3"

    echo "compare manifest output: $label"

    rm -f "$bin"
    "$COMPILER" build --manifest "$manifest" >/dev/null
    [[ -f "$bin" ]] || { echo "missing c manifest output for $label" >&2; exit 1; }
    local c_out
    c_out="$("$bin")"

    rm -f "$bin"
    "$COMPILER" build --backend llvm --manifest "$manifest" >/dev/null
    [[ -f "$bin" ]] || { echo "missing llvm manifest output for $label" >&2; exit 1; }
    local llvm_out
    llvm_out="$("$bin")"

    if [[ "$c_out" != "$llvm_out" ]]; then
        echo "manifest output mismatch for $label" >&2
        diff -u <(printf '%s\n' "$c_out") <(printf '%s\n' "$llvm_out") || true
        exit 1
    fi
}

run_manifest_matrix() {
    local label="$1"
    local manifest="$2"
    local default_bin="$3"
    local alt_bin="${4:-}"
    local run_tests="${5:-yes}"

    echo "manifest matrix: $label (c)"
    "$COMPILER" build --manifest "$manifest" >/dev/null
    [[ -f "$default_bin" ]] || { echo "missing c build output for $label" >&2; exit 1; }
    "$default_bin" >/dev/null
    "$COMPILER" run --manifest "$manifest" >/dev/null
    if [[ "$run_tests" == "yes" ]]; then
        "$COMPILER" test --manifest "$manifest" >/dev/null
    fi

    if [[ -n "$alt_bin" ]]; then
        "$COMPILER" build --manifest "$manifest" --target alt >/dev/null
        [[ -f "$alt_bin" ]] || { echo "missing c alt build output for $label" >&2; exit 1; }
        "$alt_bin" >/dev/null
        "$COMPILER" run --manifest "$manifest" --target alt >/dev/null
        if [[ "$run_tests" == "yes" ]]; then
            "$COMPILER" test --manifest "$manifest" --target alt >/dev/null
        fi
    fi

    echo "manifest matrix: $label (llvm)"
    "$COMPILER" build --backend llvm --manifest "$manifest" >/dev/null
    [[ -f "$default_bin" ]] || { echo "missing llvm build output for $label" >&2; exit 1; }
    "$default_bin" >/dev/null
    "$COMPILER" run --backend llvm --manifest "$manifest" >/dev/null
    if [[ "$run_tests" == "yes" ]]; then
        "$COMPILER" test --backend llvm --manifest "$manifest" >/dev/null
    fi

    if [[ -n "$alt_bin" ]]; then
        "$COMPILER" build --backend llvm --manifest "$manifest" --target alt >/dev/null
        [[ -f "$alt_bin" ]] || { echo "missing llvm alt build output for $label" >&2; exit 1; }
        "$alt_bin" >/dev/null
        "$COMPILER" run --backend llvm --manifest "$manifest" --target alt >/dev/null
        if [[ "$run_tests" == "yes" ]]; then
            "$COMPILER" test --backend llvm --manifest "$manifest" --target alt >/dev/null
        fi
    fi
}

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ..
make

cd "$PROJECT_ROOT"

bash "$PROJECT_ROOT/script/test_llvm_backend.sh"
bash "$PROJECT_ROOT/script/bootstrap_llvm_smoke.sh"

compare_backend_output "bootstrap lexer" "$LEXER_INPUT" "$BUILD_DIR/lexer"
compare_backend_output "bootstrap parser" "$PARSER_INPUT" "$BUILD_DIR/parser"
compare_backend_output "bootstrap hir" "$HIR_INPUT" "$BUILD_DIR/hir"

run_manifest_matrix "build demo" "$DEMO_MANIFEST" "$DEMO_BIN" "$DEMO_ALT_BIN"
run_manifest_matrix "bootstrap lexer manifest" "$BOOTSTRAP_MANIFEST" "$BOOTSTRAP_BIN" "" "no"
compare_manifest_output "bootstrap lexer manifest" "$BOOTSTRAP_MANIFEST" "$BOOTSTRAP_BIN"

echo "LLVM default candidate evaluation passed"
