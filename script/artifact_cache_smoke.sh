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

check_only() {
  local cache="$1"
  local log="$2"
  local input="$3"
  "$JIANGC" --artifact-cache-dir "$cache" --artifact-stats --check "$input" >"$log" 2>&1
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

weak_symbols() {
  local object="$1"
  if [ "$(uname -s)" = "Darwin" ]; then
    nm -m "$object" | sed -n 's/.*weak external //p'
    return
  fi
  nm -g "$object" | awk '$2 == "W" || $2 == "V" { print $3 }'
}

require_shared_weak_monomorphs() {
  local cache="$1"
  local symbols="$WORK_DIR/shared-generic-weak.symbols"
  : >"$symbols"
  while IFS= read -r object; do
    local before
    before="$(wc -l <"$symbols" | tr -d ' ')"
    weak_symbols "$object" >>"$symbols"
    local after
    after="$(wc -l <"$symbols" | tr -d ' ')"
    [ "$after" -gt "$before" ] || fail "monomorph object has no weak definition: $object"
  done < <(find "$cache" -type f -name '*.mono.o' | sort)
  [ -n "$(sort "$symbols" | uniq -d | head -n 1)" ] \
    || fail "shared generic callers did not emit duplicate weak definitions"
}

require_no_temporary_files() {
  local cache="$1"
  local temporary
  temporary="$(find "$cache" -type f \
    \( -name '*.object.tmp.*' -o -name '*.ji.tmp.*' -o -name '*.link.tmp.*' \) \
    -print -quit)"
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

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/cold.log" "$cold" "$input"
  require_stat_ge "$WORK_DIR/cold.log" artifact_object_miss 1
  require_stat_ge "$WORK_DIR/cold.log" artifact_emitted_units 1
  expect_exit "$cold" 52

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/hot.log" "$hot" "$input"
  require_stat_eq "$WORK_DIR/hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/hot.log" artifact_object_stale 0
  require_stat_eq "$WORK_DIR/hot.log" artifact_emitted_units 0
  require_stat_ge "$WORK_DIR/hot.log" artifact_object_hit 1
  require_stat_ge "$WORK_DIR/hot.log" artifact_parsed_sources 1
  expect_exit "$hot" 52

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/hot-noop.log" "$hot" "$input"
  require_stat_eq "$WORK_DIR/hot-noop.log" artifact_no_op_hits 1
  require_stat_eq "$WORK_DIR/hot-noop.log" artifact_parsed_sources 0

  normalized_symbols "$cold" "$WORK_DIR/cold.symbols"
  normalized_symbols "$hot" "$WORK_DIR/hot.symbols"
  cmp "$WORK_DIR/cold.symbols" "$WORK_DIR/hot.symbols"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/release.log" "$release" "$input" release
  require_stat_eq "$WORK_DIR/release.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/release.log" artifact_emitted_units 0
  expect_exit "$release" 52

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/release-hot.log" "$release" "$input" release
  require_stat_eq "$WORK_DIR/release-hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/release-hot.log" artifact_emitted_units 0
  require_stat_eq "$WORK_DIR/release-hot.log" artifact_no_op_hits 1
  expect_exit "$release" 52
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

check_metadata_only_change() {
  local fixture="$WORK_DIR/metadata-fixture"
  local input="$fixture/package/run/source_dependency_app"
  local dependency="$fixture/package/check/source_dependency_util/util.jiang"
  local cache="$WORK_DIR/metadata-cache"
  mkdir -p "$fixture"
  cp -R "$ROOT_DIR/test/lang/package" "$fixture/package"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/metadata-cold.log" \
    "$WORK_DIR/metadata-cold" "$input"
  touch -m -t 203001010000.00 "$dependency"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/metadata-hot.log" \
    "$WORK_DIR/metadata-hot" "$input"
  require_stat_eq "$WORK_DIR/metadata-hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/metadata-hot.log" artifact_emitted_units 0
  expect_exit "$WORK_DIR/metadata-hot" 52
}

check_check_then_codegen() {
  local input="$ROOT_DIR/test/lang/package/run/source_dependency_app"
  local cache="$WORK_DIR/check-codegen-cache"

  check_only "$cache" "$WORK_DIR/check-cold.log" "$input"
  local object_count
  object_count="$(find "$cache" -type f -name '*.o' | wc -l | tr -d ' ')"
  [ "$object_count" = "0" ] || fail "--check generated ${object_count} objects"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/check-codegen.log" \
    "$WORK_DIR/check-codegen" "$input"
  require_stat_ge "$WORK_DIR/check-codegen.log" artifact_emitted_units 1
  check_only "$cache" "$WORK_DIR/check-after-codegen.log" "$input"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/check-codegen-hot.log" \
    "$WORK_DIR/check-codegen-hot" "$input"
  require_stat_eq "$WORK_DIR/check-codegen-hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/check-codegen-hot.log" artifact_emitted_units 0
  expect_exit "$WORK_DIR/check-codegen-hot" 52
}

write_import_graph_fixture() {
  local fixture="$1"
  mkdir -p "$fixture"
  printf '%s\n' \
    '[package]' \
    'name = import_graph' \
    'root = main.jiang' >"$fixture/package.ini"
  printf '%s\n' \
    'import api = "./api.jiang";' \
    'Int main() { api.value() - 7 }' >"$fixture/main.jiang"
  printf '%s\n' 'public Int value() { 7 }' >"$fixture/api.jiang"
}

check_import_graph_changes() {
  local fixture="$WORK_DIR/import-graph"
  local cache="$WORK_DIR/import-graph-cache"
  write_import_graph_fixture "$fixture"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/import-cold.log" \
    "$WORK_DIR/import-cold" "$fixture"

  printf '%s\n' \
    'import extra = "./extra.jiang";' \
    'public Int value() { extra.value() }' >"$fixture/api.jiang"
  printf '%s\n' 'public Int value() { 7 }' >"$fixture/extra.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/import-add.log" \
    "$WORK_DIR/import-add" "$fixture"

  printf '%s\n' \
    'import api = "./api.jiang";' \
    'public Int value() { 7 }' >"$fixture/extra.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/import-cycle.log" \
    "$WORK_DIR/import-cycle" "$fixture"

  printf '%s\n' 'public Int value() { 7 }' >"$fixture/api.jiang"
  rm -f "$fixture/extra.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/import-delete.log" \
    "$WORK_DIR/import-delete" "$fixture"

  mv "$fixture/api.jiang" "$fixture/renamed.jiang"
  perl -0pi -e 's/api[.]jiang/renamed.jiang/' "$fixture/main.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/import-rename.log" \
    "$WORK_DIR/import-rename" "$fixture"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/import-hot.log" \
    "$WORK_DIR/import-hot" "$fixture"
  require_stat_eq "$WORK_DIR/import-hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/import-hot.log" artifact_emitted_units 0
  expect_exit "$WORK_DIR/import-hot" 0
}

check_diagnostic_equivalence() {
  local fixture="$WORK_DIR/diagnostic-equivalence"
  local cached="$WORK_DIR/diagnostic-cache"
  local clean="$WORK_DIR/diagnostic-clean-cache"
  write_import_graph_fixture "$fixture"
  compile_executable "$JIANGC" "$cached" "$WORK_DIR/diagnostic-seed.log" \
    "$WORK_DIR/diagnostic-seed" "$fixture"
  printf '%s\n' 'public Int value( {' >"$fixture/api.jiang"

  if "$JIANGC" --artifact-cache-dir "$cached" --check "$fixture" \
    >"$WORK_DIR/diagnostic-cached.log" 2>&1; then
    fail "cached invalid source unexpectedly compiled"
  fi
  if "$JIANGC" --artifact-cache-dir "$clean" --check "$fixture" \
    >"$WORK_DIR/diagnostic-clean.log" 2>&1; then
    fail "clean invalid source unexpectedly compiled"
  fi
  cmp "$WORK_DIR/diagnostic-clean.log" "$WORK_DIR/diagnostic-cached.log"
}

check_corrupt_cache_recovery() {
  local input="$ROOT_DIR/test/lang/package/run/source_dependency_app"
  local cache="$WORK_DIR/recovery-cache"
  local object_file
  local object_name
  local interface_file
  local build_file

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/recovery-cold.log" \
    "$WORK_DIR/recovery-cold" "$input"
  object_file="$(
    find "$cache" -type f -path '*/objects/[0-9]*.o' ! -name '*.mono.o' -print |
      sort |
      head -n 1
  )"
  [ -n "$object_file" ] || fail "no cached object found"
  # 模拟进程在写完临时 object、尚未原子替换时退出。孤立临时文件不能
  # 影响最后一次成功的 object/record。
  cp "$object_file" "$object_file.object.tmp.interrupted"
  : >"$object_file"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/truncated-object.log" \
    "$WORK_DIR/truncated-object" "$input"
  require_stat_ge "$WORK_DIR/truncated-object.log" artifact_object_stale 1
  require_stat_ge "$WORK_DIR/truncated-object.log" artifact_emitted_units 1
  expect_exit "$WORK_DIR/truncated-object" 52
  rm -f "$object_file.object.tmp.interrupted"

  object_name="$(basename "$object_file" .o)"
  interface_file="$(find "$cache" -type f -path "*/sources/$object_name.ji" -print | head -n 1)"
  [ -n "$interface_file" ] || fail "no source interface found"
  # 模拟进程在写完临时 `.ji`、尚未发布 section table 时退出。
  cp "$interface_file" "$interface_file.ji.tmp.interrupted"
  printf 'broken-interface\n' >"$interface_file"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/corrupt-interface.log" \
    "$WORK_DIR/corrupt-interface" "$input"
  require_stat_eq "$WORK_DIR/corrupt-interface.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/corrupt-interface.log" artifact_emitted_units 0
  rm -f "$interface_file.ji.tmp.interrupted"

  printf '\377\000\000\000\000\000\000\000' |
    dd of="$interface_file" bs=1 seek=8 conv=notrunc status=none
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/interface-version.log" \
    "$WORK_DIR/interface-version" "$input"
  require_stat_eq "$WORK_DIR/interface-version.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/interface-version.log" artifact_emitted_units 0
  expect_exit "$WORK_DIR/interface-version" 52

  build_file="$(find "$cache" -type f -name '*.jbuild' -print | head -n 1)"
  [ -n "$build_file" ] || fail "no build state found"
  : >"$build_file"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/truncated-jbuild.log" \
    "$WORK_DIR/truncated-jbuild" "$input"
  require_stat_ge "$WORK_DIR/truncated-jbuild.log" artifact_emitted_units 1
  expect_exit "$WORK_DIR/truncated-jbuild" 52

  build_file="$(find "$cache" -type f -name '*.jbuild' -print | head -n 1)"
  [ -n "$build_file" ] || fail "build state was not republished"
  rm -f "$build_file"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/missing-jbuild.log" \
    "$WORK_DIR/missing-jbuild" "$input"
  require_stat_ge "$WORK_DIR/missing-jbuild.log" artifact_emitted_units 1
  expect_exit "$WORK_DIR/missing-jbuild" 52
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
  require_stat_ge "$WORK_DIR/compiler-b.log" artifact_object_miss 1
  require_stat_ge "$WORK_DIR/compiler-b.log" artifact_emitted_units 1
  local context_count
  context_count="$(find "$cache" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')"
  [ "$context_count" = "2" ] || fail "compiler build identity created ${context_count} contexts"
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
  compile_object "$cache" "$WORK_DIR/object-hot.log" "$hot" "$source"
  cmp "$cold" "$hot"
  normalized_symbols "$cold" "$WORK_DIR/object-cold.symbols"
  normalized_symbols "$hot" "$WORK_DIR/object-hot.symbols"
  cmp "$WORK_DIR/object-cold.symbols" "$WORK_DIR/object-hot.symbols"
  "$CC_BIN" "$hot" -o "$WORK_DIR/object-hot-bin"
  expect_exit "$WORK_DIR/object-hot-bin" 0

  compile_object "$cache" "$WORK_DIR/object-wasm.log" "$wasm" "$source" \
    --target wasm32-unknown-unknown --no-link-libc
  compile_object "$cache" "$WORK_DIR/object-wasm-hot.log" "$WORK_DIR/object-wasm-hot.o" "$source" \
    --target wasm32-unknown-unknown --no-link-libc
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

check_shared_generic_callers() {
  local fixture="$WORK_DIR/shared-generic-callers"
  local cache="$WORK_DIR/shared-generic-cache"
  mkdir -p "$fixture"
  printf '%s\n' \
    '[package]' \
    'name = shared_generic_callers' \
    'root = main.jiang' >"$fixture/package.ini"
  printf '%s\n' \
    'public T identity<T>(T value) {' \
    '    value' \
    '}' \
    '' \
    'public T second<T>(T value) {' \
    '    value' \
    '}' \
    '' \
    'Int hidden() {' \
    '    1' \
    '}' >"$fixture/common.jiang"
  printf '%s\n' \
    'import common = "./common.jiang";' \
    '' \
    'public Int left() {' \
    '    common.identity<Int>(2)' \
    '}' >"$fixture/left.jiang"
  printf '%s\n' \
    'import common = "./common.jiang";' \
    '' \
    'public Int right() {' \
    '    common.identity<Int>(4)' \
    '}' >"$fixture/right.jiang"
  printf '%s\n' \
    'import left = "./left.jiang";' \
    'import right = "./right.jiang";' \
    '' \
    'Int main() {' \
    '    left.left() + right.right() - 6' \
    '}' >"$fixture/main.jiang"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/shared-generic-cold.log" \
    "$WORK_DIR/shared-generic-cold" "$fixture"
  local cold_linked
  cold_linked="$(stat_value "$WORK_DIR/shared-generic-cold.log" artifact_linked_objects)"
  local mono_count
  mono_count="$(find "$cache" -type f -name '*.mono.o' | wc -l | tr -d ' ')"
  [ "$mono_count" -ge 2 ] \
    || fail "shared generic callers produced only ${mono_count} monomorph objects"
  require_shared_weak_monomorphs "$cache"
  perl -0pi -e 's/Int hidden\(\) \{\n    1\n\}/Int hidden() {\n    2\n}/' \
    "$fixture/common.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/shared-generic-private.log" \
    "$WORK_DIR/shared-generic-private" "$fixture"
  require_stat_eq "$WORK_DIR/shared-generic-private.log" artifact_emitted_units 1

  perl -0pi -e \
    's/common\.identity<Int>\(4\)/common.identity<Int>(4) + common.second<Int>(1) - 1/' \
    "$fixture/right.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/shared-generic-add.log" \
    "$WORK_DIR/shared-generic-add" "$fixture"
  require_stat_eq "$WORK_DIR/shared-generic-add.log" artifact_emitted_units 2

  perl -0pi -e \
    's/common\.identity<Int>\(4\) \+ common\.second<Int>\(1\) - 1/common.identity<Int>(4)/' \
    "$fixture/right.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/shared-generic-remove.log" \
    "$WORK_DIR/shared-generic-remove" "$fixture"
  require_stat_eq "$WORK_DIR/shared-generic-remove.log" artifact_emitted_units 2

  printf '%s\n' \
    'public T identity<T>(T value) {' \
    '    T result = value;' \
    '    result' \
    '}' \
    '' \
    'public T second<T>(T value) {' \
    '    value' \
    '}' \
    '' \
    'Int hidden() {' \
    '    2' \
    '}' >"$fixture/common.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/shared-generic-template.log" \
    "$WORK_DIR/shared-generic-template" "$fixture"
  require_stat_ge "$WORK_DIR/shared-generic-template.log" artifact_object_stale 2

  printf '%s\n' \
    'import left = "./left.jiang";' \
    '' \
    'Int main() {' \
    '    left.left() - 2' \
    '}' >"$fixture/main.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/shared-generic-unreachable.log" \
    "$WORK_DIR/shared-generic-unreachable" "$fixture"
  local current_linked
  current_linked="$(stat_value "$WORK_DIR/shared-generic-unreachable.log" artifact_linked_objects)"
  [ "$current_linked" -lt "$cold_linked" ] \
    || fail "unreachable source kept historical objects in current link plan"

  expect_exit "$WORK_DIR/shared-generic-cold" 0
  expect_exit "$WORK_DIR/shared-generic-private" 0
  expect_exit "$WORK_DIR/shared-generic-add" 0
  expect_exit "$WORK_DIR/shared-generic-remove" 0
  expect_exit "$WORK_DIR/shared-generic-template" 0
  expect_exit "$WORK_DIR/shared-generic-unreachable" 0
}

check_release_whole_package_state() {
  local fixture="$WORK_DIR/release-whole-package.jiang"
  local cache="$WORK_DIR/release-whole-package-cache"
  local output="$WORK_DIR/release-whole-package"
  printf '%s\n' \
    'Int hidden() { 1 }' \
    'Int main() { hidden() - 1 }' >"$fixture"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/release-whole-cold.log" \
    "$output" "$fixture" release
  require_stat_eq "$WORK_DIR/release-whole-cold.log" artifact_object_hit 0
  require_stat_eq "$WORK_DIR/release-whole-cold.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/release-whole-cold.log" artifact_emitted_units 0
  [ -z "$(find "$cache" -type f -name '*.o' -print -quit)" ] \
    || fail "release wrote fine-grained work-product objects"

  perl -0pi -e 's/Int hidden\(\) \{ 1 \}/Int hidden() { 2 }/' "$fixture"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/release-whole-stale.log" \
    "$output" "$fixture" release
  require_stat_eq "$WORK_DIR/release-whole-stale.log" artifact_object_hit 0
  require_stat_eq "$WORK_DIR/release-whole-stale.log" artifact_object_stale 0
  require_stat_eq "$WORK_DIR/release-whole-stale.log" artifact_emitted_units 0
  expect_exit "$output" 1

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/release-whole-hot.log" \
    "$output" "$fixture" release
  require_stat_eq "$WORK_DIR/release-whole-hot.log" artifact_no_op_hits 1
  require_stat_eq "$WORK_DIR/release-whole-hot.log" artifact_parsed_sources 0
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

check_public_alias_dependency() {
  local fixture="$WORK_DIR/public-alias"
  local cache="$WORK_DIR/public-alias-cache"
  mkdir -p "$fixture"
  printf '%s\n' \
    '[package]' \
    'name = public_alias' \
    'root = main.jiang' >"$fixture/package.ini"
  printf '%s\n' \
    'import dep = "./dep.jiang";' \
    '' \
    'Int main() {' \
    '    dep.Number(value: 0).value' \
    '}' >"$fixture/main.jiang"
  printf '%s\n' \
    'import api = "./api.jiang";' \
    '' \
    'public alias Number = api.middle.Number;' >"$fixture/dep.jiang"
  printf '%s\n' \
    'public import middle = "./middle.jiang";' >"$fixture/api.jiang"
  printf '%s\n' \
    'public import * = "./leaf.jiang";' >"$fixture/middle.jiang"
  printf '%s\n' \
    'public struct Number {' \
    '    public Int value;' \
    '}' >"$fixture/leaf.jiang"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/public-alias-cold.log" \
    "$WORK_DIR/public-alias-cold" "$fixture"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/public-alias-hot.log" \
    "$WORK_DIR/public-alias-hot" "$fixture"
  require_stat_eq "$WORK_DIR/public-alias-hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/public-alias-hot.log" artifact_emitted_units 0
  expect_exit "$WORK_DIR/public-alias-cold" 0
  expect_exit "$WORK_DIR/public-alias-hot" 0
}

check_trait_interface() {
  local fixture="$WORK_DIR/trait-interface"
  local cache="$WORK_DIR/trait-interface-cache"
  mkdir -p "$fixture"
  printf '%s\n' \
    '[package]' \
    'name = trait_interface' \
    'root = main.jiang' >"$fixture/package.ini"
  printf '%s\n' \
    'import dep = "./dep.jiang";' \
    '' \
    'Int main() {' \
    '    dep.Id id! = dep.Id(index: 0);' \
    '    dep.write_u64<dep.Id>(id$.mut_ref());' \
    '    id.normalize(0)' \
    '}' >"$fixture/main.jiang"
  printf '%s\n' \
    'public trait Indexable {' \
    '    Int to_index(self);' \
    '    Self from_index(Int index);' \
    '    Int doubled(self) {' \
    '        self.to_index() + self.to_index()' \
    '    }' \
    '    Int normalize(self, UInt8 value);' \
    '    Int normalize(self, Int value) {' \
    '        self.normalize(UInt8(value))' \
    '    }' \
    '}' >"$fixture/trait.jiang"
  printf '%s\n' \
    'import contract = "./trait.jiang";' \
    '' \
    'public struct Id: contract.Indexable, Hasher {' \
    '    public Int index;' \
    '' \
    '    public Int to_index(self) {' \
    '        self.index' \
    '    }' \
    '' \
    '    public Id from_index(Int index) {' \
    '        Id(index: index)' \
    '    }' \
    '' \
    '    public Int normalize(self, UInt8 value) {' \
    '        Int(value)' \
    '    }' \
    '' \
    '    public () write(Self&! self, UInt8[]& bytes) {}' \
    '    public () write(Self&! self, UInt8 value) {}' \
    '    public UInt64 finish(self) { 0 }' \
    '}' \
    '' \
    'public () write_u64<H: Hasher>(H&! value) {' \
    '    value.write(UInt64(0));' \
    '}' \
    '' \
    'Int private_helper() {' \
    '    1' \
    '}' >"$fixture/dep.jiang"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/trait-interface-cold.log" \
    "$WORK_DIR/trait-interface-cold" "$fixture"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/trait-interface-hot.log" \
    "$WORK_DIR/trait-interface-hot" "$fixture"
  require_stat_eq "$WORK_DIR/trait-interface-hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/trait-interface-hot.log" artifact_emitted_units 0
  expect_exit "$WORK_DIR/trait-interface-cold" 0
  expect_exit "$WORK_DIR/trait-interface-hot" 0
  perl -0pi -e 's/Int private_helper\(\) \{\n    1\n\}/Int private_helper() {\n    2\n}/' \
    "$fixture/dep.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/trait-interface-private.log" \
    "$WORK_DIR/trait-interface-private" "$fixture"
  require_stat_eq "$WORK_DIR/trait-interface-private.log" artifact_emitted_units 1
  expect_exit "$WORK_DIR/trait-interface-private" 0
  perl -0pi -e 's/Int private_helper\(\)/public Int private_helper()/' "$fixture/dep.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/trait-interface-public.log" \
    "$WORK_DIR/trait-interface-public" "$fixture"
  require_stat_ge "$WORK_DIR/trait-interface-public.log" artifact_emitted_units 1
  expect_exit "$WORK_DIR/trait-interface-public" 0
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
  local emitted_total
  local object_count
  emitted_total="$((
    $(stat_value "$WORK_DIR/concurrent-a.log" artifact_emitted_units)
    + $(stat_value "$WORK_DIR/concurrent-b.log" artifact_emitted_units)
  ))"
  object_count="$(find "$cache" -type f -name '*.o' | wc -l | tr -d ' ')"
  [ "$emitted_total" = "$object_count" ] \
    || fail "concurrent compilers emitted ${emitted_total} units for ${object_count} objects"
  require_no_temporary_files "$cache"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/concurrent-hot.log" \
    "$WORK_DIR/concurrent-hot" "$input"
  require_stat_eq "$WORK_DIR/concurrent-hot.log" artifact_object_miss 0
  require_stat_eq "$WORK_DIR/concurrent-hot.log" artifact_object_stale 0
  require_stat_eq "$WORK_DIR/concurrent-hot.log" artifact_emitted_units 0
}

check_explicit_cache_clean() {
  local input="$WORK_DIR/clean.jiang"
  local cache="$WORK_DIR/clean-cache"
  write_object_fixture "$input"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/clean-cold.log" \
    "$WORK_DIR/clean-cold" "$input"
  [ -d "$cache" ] || fail "cold build did not create cache root"
  "$JIANGC" --artifact-cache-dir "$cache" --clean-artifact-cache
  [ ! -e "$cache" ] || fail "explicit cache clean left cache root behind"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/clean-rebuild.log" \
    "$WORK_DIR/clean-rebuild" "$input"
  require_stat_ge "$WORK_DIR/clean-rebuild.log" artifact_emitted_units 1
}

check_failed_link_preserves_work_products() {
  local source="$WORK_DIR/failed-link.jiang"
  local cache="$WORK_DIR/failed-link-cache"
  local output="$WORK_DIR/failed-link"
  write_object_fixture "$source"

  if "$JIANGC" --artifact-cache-dir "$cache" --artifact-stats \
    --linker "$WORK_DIR/missing-linker" -o "$output" "$source" \
    >"$WORK_DIR/failed-link.log" 2>&1
  then
    fail "missing linker unexpectedly succeeded"
  fi
  [ -n "$(find "$cache" -type f -name '*.jbuild' -print -quit)" ] \
    || fail "failed link did not preserve build state"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/failed-link-retry.log" \
    "$output" "$source"
  require_stat_eq "$WORK_DIR/failed-link-retry.log" artifact_emitted_units 0
  require_stat_ge "$WORK_DIR/failed-link-retry.log" artifact_object_hit 1
  expect_exit "$output" 0
}

check_source_change_before_link() {
  local fixture="$WORK_DIR/source-change-fixture"
  local input="$fixture/package/run/source_dependency_app"
  local dependency="$fixture/package/check/source_dependency_util/util.jiang"
  local cache="$WORK_DIR/source-change-cache"
  local output="$WORK_DIR/source-change"
  local marker="$WORK_DIR/source-change.ready"
  local log="$WORK_DIR/source-change.log"
  local pid
  local attempts

  mkdir -p "$fixture"
  cp -R "$ROOT_DIR/test/lang/package" "$fixture/package"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/source-change-seed.log" \
    "$output" "$input"
  expect_exit "$output" 52
  perl -0pi -e 's/Int hidden\(\) \{\n    99\n\}/Int hidden() {\n    98\n}/' "$dependency"

  env JIANG_INTERNAL_BACKEND_PAUSE_BEFORE_LINK="$marker" \
    "$JIANGC" --artifact-cache-dir "$cache" --artifact-stats \
      -o "$output" "$input" >"$log" 2>&1 &
  pid=$!
  attempts=0
  while [ ! -f "$marker" ] && [ "$attempts" -lt 500 ]; do
    sleep 0.01
    attempts=$((attempts + 1))
  done
  [ -f "$marker" ] || fail "compiler did not reach before-link marker"
  perl -0pi -e 's/Int hidden\(\) \{\n    98\n\}/Int hidden() {\n    97\n}/' "$dependency"
  if wait "$pid"; then
    fail "source change before link unexpectedly succeeded"
  fi
  grep -q 'source_changed_during_build' "$log" \
    || fail "source-change diagnostic missing"
  expect_exit "$output" 52
  require_no_temporary_files "$cache"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/source-change-retry.log" \
    "$output" "$input"
  require_stat_ge "$WORK_DIR/source-change-retry.log" artifact_emitted_units 1
  expect_exit "$output" 52
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
run_check metadata "mtime-only source change" check_metadata_only_change
run_check check_codegen "--check and codegen transition" check_check_then_codegen
run_check import_graph "import graph changes" check_import_graph_changes
run_check diagnostics "clean/cache diagnostic equivalence" check_diagnostic_equivalence
run_check recovery "corrupt cache recovery" check_corrupt_cache_recovery
run_check compiler_build "compiler build identity" check_compiler_build_invalidation
run_check object_contract "object contract and target" check_emit_object_contract_and_target
run_check const_generic "const generic closure" check_cross_package_const_generic
run_check shared_generic "shared generic callers" check_shared_generic_callers
run_check release_units "release whole-package state" check_release_whole_package_state
run_check global_only "global-only dependency" check_global_only_dependency
run_check public_alias "public alias dependency" check_public_alias_dependency
run_check trait_interface "trait interface" check_trait_interface
run_check concurrent "concurrent publication" check_concurrent_publish
run_check clean "explicit cache clean" check_explicit_cache_clean
run_check partial "failed link preserves work products" check_failed_link_preserves_work_products
run_check source_race "source change before link" check_source_change_before_link
run_check dylib "lang provider dylib" check_lang_provider_dylib

SUCCESS=1
printf 'artifact cache smoke passed\n'
