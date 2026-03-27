#!/bin/bash

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

run_expect_fail() {
    local name="$1"
    local expected="$2"
    shift 2

    local output
    set +e
    output="$("$@" 2>&1)"
    local status=$?
    set -e

    if [ "$status" -eq 0 ]; then
        echo "build system error smoke '$name' unexpectedly succeeded" >&2
        exit 1
    fi

    if [[ "$output" != *"$expected"* ]]; then
        echo "build system error smoke '$name' missing expected text: $expected" >&2
        printf '%s\n' "$output" >&2
        exit 1
    fi
}

cat > "$TMP_DIR/missing_root.build" <<'EOF'
name = missing_root
stdlib_dir = ./std
EOF

cat > "$TMP_DIR/unknown_key.build" <<'EOF'
name = broken
root = main.jiang
bad_key = value
EOF

cat > "$TMP_DIR/missing_module.build" <<'EOF'
name = missing_module
root = main.jiang
stdlib_dir = ../../std
EOF

cat > "$TMP_DIR/main.jiang" <<'EOF'
import greet;

print(greet.message());
EOF

cat > "$TMP_DIR/missing_target_test.build" <<'EOF'
name = broken_target
root = main.jiang
stdlib_dir = ../../std
target.alt.root = alt.jiang
EOF

cat > "$TMP_DIR/alt.jiang" <<'EOF'
print("alt\n");
EOF

cat > "$TMP_DIR/missing_test_dir.build" <<'EOF'
name = missing_tests
root = main.jiang
stdlib_dir = ../../std
target.alt.root = alt.jiang
target.alt.test_dir = nope
EOF

"$BUILD_DIR/jiangc" build --manifest "$PROJECT_ROOT/examples/build_demo/jiang.build" >/dev/null

run_expect_fail "unknown key" "unknown key 'bad_key'" \
    "$BUILD_DIR/jiangc" build --manifest "$TMP_DIR/unknown_key.build"

run_expect_fail "missing root" "missing required key 'root'" \
    "$BUILD_DIR/jiangc" build --manifest "$TMP_DIR/missing_root.build"

run_expect_fail "unknown target" "Unknown target 'missing'" \
    "$BUILD_DIR/jiangc" build --manifest "$PROJECT_ROOT/examples/build_demo/jiang.build" --target missing

run_expect_fail "missing module map" "Could not resolve import 'module:greet'" \
    "$BUILD_DIR/jiangc" build --manifest "$TMP_DIR/missing_module.build"

run_expect_fail "missing target test dir" "Unknown test target 'alt'" \
    "$BUILD_DIR/jiangc" test --manifest "$TMP_DIR/missing_target_test.build" --target alt

run_expect_fail "missing test directory" "Could not open test directory" \
    "$BUILD_DIR/jiangc" test --manifest "$TMP_DIR/missing_test_dir.build" --target alt

echo "build system error smoke passed"
