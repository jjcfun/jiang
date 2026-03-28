#!/bin/bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
COMPILER="$BUILD_DIR/jiangc"
TESTS_DIR="$PROJECT_ROOT/tests"

mkdir -p "$BUILD_DIR"
cd "$PROJECT_ROOT"

run_single_test() {
    local test_name="$1"
    local source="$TESTS_DIR/$test_name.jiang"
    local binary="$BUILD_DIR/$test_name"

    echo "llvm backend: $test_name"
    rm -f "$binary"
    "$COMPILER" --backend llvm "$source"
    if [[ "$test_name" == "assert_fail" ]]; then
        set +e
        "$binary" >/dev/null 2>&1
        local status=$?
        set -e
        if [[ $status -ne 0 ]]; then
            return 0
        fi
        echo "assert_fail should abort under llvm backend" >&2
        return 1
    fi
    "$binary" >/dev/null
}

for test_file in "$TESTS_DIR"/*.jiang; do
    test_name="$(basename "$test_file" .jiang)"
    case "$test_name" in
        fail_*|b|enum_ns_defs|secret|utils)
            continue
            ;;
    esac
    run_single_test "$test_name"
done

"$COMPILER" build --backend llvm --manifest "$PROJECT_ROOT/examples/build_demo/jiang.build"
"$PROJECT_ROOT/examples/build_demo/build/build_demo" >/dev/null

"$COMPILER" run --backend llvm --manifest "$PROJECT_ROOT/examples/build_demo/jiang.build" >/dev/null
"$COMPILER" run --backend llvm --manifest "$PROJECT_ROOT/examples/build_demo/jiang.build" --target alt >/dev/null

"$COMPILER" test --backend llvm --manifest "$PROJECT_ROOT/examples/build_demo/jiang.build" >/dev/null
"$COMPILER" test --backend llvm --manifest "$PROJECT_ROOT/examples/build_demo/jiang.build" --target alt >/dev/null

echo "llvm backend regression passed"
