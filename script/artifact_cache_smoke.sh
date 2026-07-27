#!/usr/bin/env bash
set -euo pipefail

# 持久 object cache 的聚焦回归矩阵。
#
# 这里只验证 P3 的跨进程恢复、失效、原子发布和链接闭包，不代替发布阶段的
# 完整 bootstrap、slow smoke 或 full language test。

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
JIANGC="${JIANGC:-$ROOT_DIR/build/bin/jiangc.next}"
CC_BIN="${CC_BIN:-cc}"
KEEP_ARTIFACT_CACHE_SMOKE="${KEEP_ARTIFACT_CACHE_SMOKE:-0}"
ARTIFACT_CACHE_SMOKE_FILTER="${ARTIFACT_CACHE_SMOKE_FILTER:-}"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/jiang-artifact-cache.XXXXXX")"
SUCCESS=0

cleanup() {
  if [ "$SUCCESS" = "1" ] && [ "$KEEP_ARTIFACT_CACHE_SMOKE" != "1" ]; then
    rm -rf "$WORK_DIR"
    return
  fi
  printf 'artifact cache smoke files: %s\n' "$WORK_DIR" >&2
}

trap cleanup EXIT

fail() {
  printf 'artifact cache smoke failed: %s\n' "$*" >&2
  exit 1
}

stat_value() {
  local log="$1"
  local key="$2"
  local value
  value="$(sed -n "s/.*${key}=\\([0-9][0-9]*\\).*/\\1/p" "$log" | tail -n 1)"
  [ -n "$value" ] || fail "missing ${key} in $log"
  printf '%s\n' "$value"
}

require_stat_eq() {
  local log="$1"
  local key="$2"
  local expected="$3"
  local actual
  actual="$(stat_value "$log" "$key")"
  [ "$actual" = "$expected" ] || fail "${key}: expected ${expected}, got ${actual} in $log"
}

require_stat_ge() {
  local log="$1"
  local key="$2"
  local expected="$3"
  local actual
  actual="$(stat_value "$log" "$key")"
  [ "$actual" -ge "$expected" ] || fail "${key}: expected >= ${expected}, got ${actual} in $log"
}

compile_executable() {
  local compiler="$1"
  local cache="$2"
  local log="$3"
  local output="$4"
  local input="$5"
  local mode="${6:-debug}"
  local args=(--artifact-cache-dir "$cache" --artifact-stats)
  if [ "$mode" = "release" ]; then
    args+=(--mode release)
  fi
  "$compiler" "${args[@]}" -o "$output" "$input" >"$log" 2>&1
}

compile_object() {
  local cache="$1"
  local log="$2"
  local output="$3"
  local input="$4"
  shift 4
  "$JIANGC" --artifact-cache-dir "$cache" --artifact-stats "$@" \
    --emit-obj -o "$output" "$input" >"$log" 2>&1
}

expect_exit() {
  local executable="$1"
  local expected="$2"
  local actual
  set +e
  "$executable"
  actual=$?
  set -e
  [ "$actual" = "$expected" ] || fail "$executable exited ${actual}, expected ${expected}"
}

normalized_symbols() {
  local artifact="$1"
  local output="$2"
  nm -g "$artifact" | awk '{ print $(NF - 1), $NF }' | sort >"$output"
}

require_no_temporary_files() {
  local cache="$1"
  local temporary
  temporary="$(find "$cache" -type f \
    \( -name '*.object.tmp.*' -o -name '*.index.tmp.*' \) -print -quit)"
  [ -z "$temporary" ] || fail "temporary cache file remains: $temporary"
}

write_object_fixture() {
  local path="$1"
  printf '%s\n' \
    'T identity<T>(T value) {' \
    '    value' \
    '}' \
    '' \
    'Int main() {' \
    '    identity<Int>(3) - 3' \
    '}' >"$path"
}

check_cold_hot_and_profiles() {
  local input="$ROOT_DIR/test/lang/package/run/source_dependency_app"
  local cache="$WORK_DIR/profile-cache"
  local cold="$WORK_DIR/cold"
  local hot="$WORK_DIR/hot"
  local release="$WORK_DIR/release"
  local release_hot="$WORK_DIR/release-hot"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/cold.log" "$cold" "$input"
  require_stat_ge "$WORK_DIR/cold.log" artifact_object_miss 1
  require_stat_ge "$WORK_DIR/cold.log" artifact_emitted_units 1
  expect_exit "$cold" 52

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/hot.log" "$hot" "$input"
  require_stat_eq "$WORK_DIR/hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/hot.log" artifact_object_stale 0
  require_stat_eq "$WORK_DIR/hot.log" artifact_emitted_units 0
  require_stat_ge "$WORK_DIR/hot.log" artifact_object_hit 1
  require_stat_ge "$WORK_DIR/hot.log" artifact_interface_hit 1
  expect_exit "$hot" 52

  normalized_symbols "$cold" "$WORK_DIR/cold.symbols"
  normalized_symbols "$hot" "$WORK_DIR/hot.symbols"
  cmp "$WORK_DIR/cold.symbols" "$WORK_DIR/hot.symbols"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/release.log" "$release" "$input" release
  require_stat_ge "$WORK_DIR/release.log" artifact_object_miss 1
  require_stat_ge "$WORK_DIR/release.log" artifact_emitted_units 1
  expect_exit "$release" 52

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/release-hot.log" "$release_hot" "$input" release
  require_stat_eq "$WORK_DIR/release-hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/release-hot.log" artifact_emitted_units 0
  expect_exit "$release_hot" 52
}

check_dependency_invalidation() {
  local fixture="$WORK_DIR/dependency-fixture"
  local input="$fixture/package/run/source_dependency_app"
  local dependency="$fixture/package/check/source_dependency_util/util.jiang"
  local cache="$WORK_DIR/dependency-cache"
  mkdir -p "$fixture"
  cp -R "$ROOT_DIR/test/lang/package" "$fixture/package"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/dependency-cold.log" \
    "$WORK_DIR/dependency-cold" "$input"
  perl -0pi -e 's/Int hidden\(\) \{\n    99\n\}/Int hidden() {\n    98\n}/' "$dependency"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/private-change.log" \
    "$WORK_DIR/private-change" "$input"
  require_stat_eq "$WORK_DIR/private-change.log" artifact_emitted_units 1
  expect_exit "$WORK_DIR/private-change" 52

  perl -0pi -e 's/public Int cstring_length/public Int64 cstring_length/' "$dependency"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/public-change.log" \
    "$WORK_DIR/public-change" "$input"
  require_stat_ge "$WORK_DIR/public-change.log" artifact_emitted_units 2
  expect_exit "$WORK_DIR/public-change" 52
}

check_corrupt_cache_recovery() {
  local input="$ROOT_DIR/test/lang/package/run/source_dependency_app"
  local cache="$WORK_DIR/recovery-cache"
  local object_file
  local index_file

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/recovery-cold.log" \
    "$WORK_DIR/recovery-cold" "$input"
  object_file="$(find "$cache/objects" -type f -name '*.o' -print | sort | head -n 1)"
  [ -n "$object_file" ] || fail "no cached object found"
  : >"$object_file"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/truncated-object.log" \
    "$WORK_DIR/truncated-object" "$input"
  require_stat_ge "$WORK_DIR/truncated-object.log" artifact_object_stale 1
  require_stat_ge "$WORK_DIR/truncated-object.log" artifact_emitted_units 1
  expect_exit "$WORK_DIR/truncated-object" 52

  index_file="$(find "$cache/index/objects" -type f -name '*.jai' -print | sort | head -n 1)"
  [ -n "$index_file" ] || fail "no object index record found"
  printf 'broken-index\n' >"$index_file"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/corrupt-index.log" \
    "$WORK_DIR/corrupt-index" "$input"
  require_stat_ge "$WORK_DIR/corrupt-index.log" artifact_object_stale 1
  require_stat_ge "$WORK_DIR/corrupt-index.log" artifact_emitted_units 1

  printf '\002\000\000\000\000\000\000\000' |
    dd of="$index_file" bs=1 seek=8 conv=notrunc status=none
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/index-version.log" \
    "$WORK_DIR/index-version" "$input"
  require_stat_ge "$WORK_DIR/index-version.log" artifact_object_stale 1
  require_stat_ge "$WORK_DIR/index-version.log" artifact_emitted_units 1
  expect_exit "$WORK_DIR/index-version" 52
  require_no_temporary_files "$cache"
}

check_compiler_build_invalidation() {
  local compiler="$WORK_DIR/jiangc-build-id"
  local cache="$WORK_DIR/compiler-build-cache"
  local input="$ROOT_DIR/test/lang/package/run/source_dependency_app"
  cp "$JIANGC" "$compiler"
  chmod +x "$compiler"
  printf 'artifact-smoke-build-a\n' >"$compiler.build-id"

  compile_executable "$compiler" "$cache" "$WORK_DIR/compiler-a.log" \
    "$WORK_DIR/compiler-a" "$input"
  compile_executable "$compiler" "$cache" "$WORK_DIR/compiler-a-hot.log" \
    "$WORK_DIR/compiler-a-hot" "$input"
  require_stat_eq "$WORK_DIR/compiler-a-hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/compiler-a-hot.log" artifact_emitted_units 0

  printf 'artifact-smoke-build-b\n' >"$compiler.build-id"
  compile_executable "$compiler" "$cache" "$WORK_DIR/compiler-b.log" \
    "$WORK_DIR/compiler-b" "$input"
  require_stat_ge "$WORK_DIR/compiler-b.log" artifact_interface_stale 1
  require_stat_ge "$WORK_DIR/compiler-b.log" artifact_object_miss 1
  require_stat_ge "$WORK_DIR/compiler-b.log" artifact_emitted_units 1
  expect_exit "$WORK_DIR/compiler-b" 52
}

check_emit_object_contract_and_target() {
  local source="$WORK_DIR/object-contract.jiang"
  local cache="$WORK_DIR/object-cache"
  local cold="$WORK_DIR/object-cold.o"
  local hot="$WORK_DIR/object-hot.o"
  local wasm="$WORK_DIR/object-wasm.o"
  write_object_fixture "$source"

  compile_object "$cache" "$WORK_DIR/object-cold.log" "$cold" "$source"
  require_stat_ge "$WORK_DIR/object-cold.log" artifact_object_miss 1
  compile_object "$cache" "$WORK_DIR/object-hot.log" "$hot" "$source"
  require_stat_eq "$WORK_DIR/object-hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/object-hot.log" artifact_emitted_units 0
  cmp "$cold" "$hot"
  normalized_symbols "$cold" "$WORK_DIR/object-cold.symbols"
  normalized_symbols "$hot" "$WORK_DIR/object-hot.symbols"
  cmp "$WORK_DIR/object-cold.symbols" "$WORK_DIR/object-hot.symbols"
  "$CC_BIN" "$hot" -o "$WORK_DIR/object-hot-bin"
  expect_exit "$WORK_DIR/object-hot-bin" 0

  compile_object "$cache" "$WORK_DIR/object-wasm.log" "$wasm" "$source" \
    --target wasm32-unknown-unknown --no-link-libc
  require_stat_ge "$WORK_DIR/object-wasm.log" artifact_object_miss 1
  compile_object "$cache" "$WORK_DIR/object-wasm-hot.log" "$WORK_DIR/object-wasm-hot.o" "$source" \
    --target wasm32-unknown-unknown --no-link-libc
  require_stat_eq "$WORK_DIR/object-wasm-hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/object-wasm-hot.log" artifact_emitted_units 0
}

check_cross_package_const_generic() {
  local input="$ROOT_DIR/test/lang/package/run/source_const_generic_app"
  local cache="$WORK_DIR/const-generic-cache"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/const-generic-cold.log" \
    "$WORK_DIR/const-generic-cold" "$input"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/const-generic-hot.log" \
    "$WORK_DIR/const-generic-hot" "$input"
  require_stat_eq "$WORK_DIR/const-generic-hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/const-generic-hot.log" artifact_emitted_units 0
  require_stat_ge "$WORK_DIR/const-generic-hot.log" artifact_object_hit 1
  expect_exit "$WORK_DIR/const-generic-cold" 0
  expect_exit "$WORK_DIR/const-generic-hot" 0
}

check_global_only_dependency() {
  local fixture="$WORK_DIR/global-only"
  local app="$fixture/app"
  local dependency="$fixture/globals"
  local cache="$WORK_DIR/global-only-cache"
  mkdir -p "$app" "$dependency"
  printf '%s\n' \
    '[package]' \
    'name = app' \
    'root = main.jiang' \
    '' \
    '[dependencies]' \
    'globals = ../globals' >"$app/package.ini"
  printf '%s\n' \
    'import globals;' \
    '' \
    'Int main() {' \
    '    globals.value - 7' \
    '}' >"$app/main.jiang"
  printf '%s\n' \
    '[package]' \
    'name = globals' \
    'root = globals.jiang' >"$dependency/package.ini"
  printf '%s\n' 'public Int value! = 7;' >"$dependency/globals.jiang"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/global-only-cold.log" \
    "$WORK_DIR/global-only-cold" "$app"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/global-only-hot.log" \
    "$WORK_DIR/global-only-hot" "$app"
  require_stat_eq "$WORK_DIR/global-only-hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/global-only-hot.log" artifact_emitted_units 0
  expect_exit "$WORK_DIR/global-only-cold" 0
  expect_exit "$WORK_DIR/global-only-hot" 0
}

check_concurrent_publish() {
  local input="$ROOT_DIR/test/lang/package/run/source_dependency_app"
  local cache="$WORK_DIR/concurrent-cache"
  local pid_a
  local pid_b

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/concurrent-a.log" \
    "$WORK_DIR/concurrent-a" "$input" &
  pid_a=$!
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/concurrent-b.log" \
    "$WORK_DIR/concurrent-b" "$input" &
  pid_b=$!
  wait "$pid_a" || fail "first concurrent compiler failed"
  wait "$pid_b" || fail "second concurrent compiler failed"
  expect_exit "$WORK_DIR/concurrent-a" 52
  expect_exit "$WORK_DIR/concurrent-b" 52
  require_no_temporary_files "$cache"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/concurrent-hot.log" \
    "$WORK_DIR/concurrent-hot" "$input"
  require_stat_eq "$WORK_DIR/concurrent-hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/concurrent-hot.log" artifact_object_stale 0
  require_stat_eq "$WORK_DIR/concurrent-hot.log" artifact_emitted_units 0
}

check_lang_provider_dylib() {
  TEST_ROOT="$ROOT_DIR/test/lang" \
    TEST_JOBS=1 \
    TEST_FILTER='lang_provider/run/lang_dylib_pipeline\.jiang' \
    JIANGC="$JIANGC" \
    bash "$ROOT_DIR/script/test.sh"
}

run_check() {
  local name="$1"
  local title="$2"
  local function_name="$3"
  if [ -n "$ARTIFACT_CACHE_SMOKE_FILTER" ] && [[ ! "$name" =~ $ARTIFACT_CACHE_SMOKE_FILTER ]]; then
    return
  fi
  printf '== artifact cache smoke: %s ==\n' "$title"
  "$function_name"
}

[ -x "$JIANGC" ] || fail "missing compiler: $JIANGC"
command -v "$CC_BIN" >/dev/null 2>&1 || fail "missing C linker: $CC_BIN"
command -v nm >/dev/null 2>&1 || fail "missing nm"

run_check cold_hot "cold/hot and profiles" check_cold_hot_and_profiles
run_check invalidation "dependency invalidation" check_dependency_invalidation
run_check recovery "corrupt cache recovery" check_corrupt_cache_recovery
run_check compiler_build "compiler build identity" check_compiler_build_invalidation
run_check object_contract "object contract and target" check_emit_object_contract_and_target
run_check const_generic "const generic closure" check_cross_package_const_generic
run_check global_only "global-only dependency" check_global_only_dependency
run_check concurrent "concurrent publication" check_concurrent_publish
run_check dylib "lang provider dylib" check_lang_provider_dylib

SUCCESS=1
printf 'artifact cache smoke passed\n'
