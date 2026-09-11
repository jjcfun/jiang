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

artifact_stat_snapshot() {
  local log="$1"
  local output="$2"
  local key
  : >"$output"
  for key in \
    artifact_interface_hit artifact_interface_miss artifact_interface_stale \
    artifact_section_reads artifact_parsed_sources artifact_source_dirty \
    artifact_dependency_candidates artifact_object_hit \
    artifact_object_miss artifact_object_stale artifact_emitted_units \
    artifact_reused_units artifact_linked_objects artifact_no_op_hits
  do
    printf '%s=%s\n' "$key" "$(stat_value "$log" "$key")" >>"$output"
  done
}

compare_object_caches() {
  local first="$1"
  local second="$2"
  local first_files="$WORK_DIR/first-object-files"
  local second_files="$WORK_DIR/second-object-files"
  find "$first" -type f -name '*.o' -print | sed "s#^${first}/##" | sort >"$first_files"
  find "$second" -type f -name '*.o' -print | sed "s#^${second}/##" | sort >"$second_files"
  cmp "$first_files" "$second_files"
  while IFS= read -r relative; do
    cmp "$first/$relative" "$second/$relative"
  done <"$first_files"
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

require_unique_weak_monomorphs() {
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
  [ -z "$(sort "$symbols" | uniq -d | head -n 1)" ] \
    || fail "a monomorph definition was emitted by multiple codegen units"
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
  require_stat_ge "$WORK_DIR/hot.log" query_def_signature_l2 1
  require_stat_ge "$WORK_DIR/hot.log" query_def_body_l2 1
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
  perl -0pi -e \
    's/public Int answer\(\) \{\n    40\n\}/public Int answer(Int offset = 0) {\n    40 + offset\n}/' \
    "$dependency"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/dependency-cold.log" \
    "$WORK_DIR/dependency-cold" "$input"
  perl -0pi -e 's/Int hidden\(\) \{\n    99\n\}/Int hidden() {\n    98\n}/' "$dependency"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/private-change.log" \
    "$WORK_DIR/private-change" "$input"
  require_stat_ge "$WORK_DIR/private-change.log" artifact_parsed_sources 1
  require_stat_ge "$WORK_DIR/private-change.log" artifact_interface_hit 1
  require_stat_ge "$WORK_DIR/private-change.log" artifact_source_dirty 1
  require_stat_eq "$WORK_DIR/private-change.log" artifact_dependency_candidates 0
  require_stat_eq "$WORK_DIR/private-change.log" artifact_emitted_units 1
  require_stat_ge "$WORK_DIR/private-change.log" artifact_reused_units 1
  expect_exit "$WORK_DIR/private-change" 52

  printf '%s\n' 'public Int unused_public() { 1 }' >>"$dependency"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/unused-public.log" \
    "$WORK_DIR/unused-public" "$input"
  require_stat_ge "$WORK_DIR/unused-public.log" artifact_parsed_sources 1
  require_stat_ge "$WORK_DIR/unused-public.log" artifact_interface_hit 1
  require_stat_ge "$WORK_DIR/unused-public.log" artifact_source_dirty 1
  require_stat_eq "$WORK_DIR/unused-public.log" artifact_dependency_candidates 0
  require_stat_ge "$WORK_DIR/unused-public.log" artifact_emitted_units 2
  require_stat_ge "$WORK_DIR/unused-public.log" artifact_reused_units 1
  expect_exit "$WORK_DIR/unused-public" 52

  perl -0pi -e 's/answer\(Int offset = 0\)/answer(Int offset = 1)/' "$dependency"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/public-change.log" \
    "$WORK_DIR/public-change" "$input"
  require_stat_ge "$WORK_DIR/public-change.log" artifact_parsed_sources 1
  require_stat_ge "$WORK_DIR/public-change.log" artifact_source_dirty 2
  require_stat_eq "$WORK_DIR/public-change.log" artifact_dependency_candidates 0
  require_stat_eq "$WORK_DIR/public-change.log" artifact_emitted_units 2
  require_stat_ge "$WORK_DIR/public-change.log" artifact_reused_units 1
  expect_exit "$WORK_DIR/public-change" 53
}

check_build_context_lookup() {
  local fixture="$WORK_DIR/context-lookup"
  local cache="$WORK_DIR/context-lookup-cache"
  local output="$WORK_DIR/context-lookup-output"
  local lookup
  mkdir -p "$fixture"
  printf '%s\n' \
    '#doc(module) 目标索引回归的配置；版本变化属于构建输入。' \
    '#package { name = "lookup"; root = "main.jiang"; version = "1"; }' >"$fixture/package.jiang"
  printf '%s\n' \
    '#doc(module) 通过普通导入把配置版本用于运行结果。' \
    'import "package.jiang";' \
    'Int main() { package.info.version.length - 1 }' >"$fixture/main.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/lookup-cold.log" "$output" "$fixture"
  expect_exit "$output" 0
  lookup="$(find "$cache/targets" -type f -name '*.context' -print | head -n 1)"
  [ -n "$lookup" ] || fail "build context lookup was not published"
  printf 'broken' >"$lookup"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/lookup-recover.log" "$output" "$fixture"
  require_stat_ge "$WORK_DIR/lookup-recover.log" artifact_parsed_sources 1
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/lookup-hot.log" "$output" "$fixture"
  require_stat_eq "$WORK_DIR/lookup-hot.log" artifact_parsed_sources 0
  require_stat_eq "$WORK_DIR/lookup-hot.log" artifact_no_op_hits 1
  rm "$lookup"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/lookup-missing.log" "$output" "$fixture"
  [ -f "$lookup" ] || fail "missing build context lookup was not restored"
  perl -0pi -e 's/version = "1"/version = "20"/' "$fixture/package.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/lookup-version.log" "$output" "$fixture"
  require_stat_eq "$WORK_DIR/lookup-version.log" artifact_no_op_hits 0
  expect_exit "$output" 1
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/lookup-version-hot.log" "$output" "$fixture"
  require_stat_eq "$WORK_DIR/lookup-version-hot.log" artifact_parsed_sources 0
  expect_exit "$output" 1
}

check_hidden_caller_coverage() {
  local fixture="$WORK_DIR/hidden-caller-coverage"
  local cache="$WORK_DIR/hidden-caller-cache"
  local output="$WORK_DIR/hidden-caller"
  mkdir -p "$fixture"
  printf '%s\n' \
    '#doc(module) 声明缓存回归的包入口。' \
    '#package { name = "hidden_caller_coverage"; root = "main.jiang"; }' >"$fixture/package.jiang"
  printf '%s\n' \
    'import api = "./api.jiang";' \
    'Int main() { api.value() }' >"$fixture/main.jiang"
  printf '%s\n' 'public Int value() { 7 }' >"$fixture/api.jiang"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/hidden-caller-cold.log" \
    "$output" "$fixture"
  expect_exit "$output" 7

  printf '%s\n' 'public Int value() { 8 }' >"$fixture/api.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/hidden-caller-hot.log" \
    "$output" "$fixture"
  require_stat_ge "$WORK_DIR/hidden-caller-hot.log" artifact_source_dirty 2
  require_stat_ge "$WORK_DIR/hidden-caller-hot.log" artifact_emitted_units 1
  expect_exit "$output" 8
}

# 从接口读取到实现签名时，函数值和构造调用必须按需恢复 body，不能当成 extern。
check_cached_callable_values() {
  local fixture="$WORK_DIR/callable-values"
  local cache="$WORK_DIR/callable-values-cache"
  local output="$WORK_DIR/callable-values-app"
  mkdir -p "$fixture"
  cat >"$fixture/dep.jiang" <<'JIANG'
public Int first() { return 1; }
public Int second() { return 2; }
public struct Number {
    public Int value;
    public init(self, Int value) { self.value = value; }
}
JIANG
  printf '%s\n' 'alias dep = import "./dep.jiang";' \
    'Int main() { return dep.first(); }' >"$fixture/main.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/callable-cold.log" "$output" "$fixture/main.jiang"
  expect_exit "$output" 1
  printf '%s\n' 'alias dep = import "./dep.jiang";' \
    'Int main() { RawFn<Int> callback = dep.second; return callback(); }' >"$fixture/main.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/callable-changed.log" "$output" "$fixture/main.jiang"
  require_stat_ge "$WORK_DIR/callable-changed.log" artifact_interface_hit 1
  expect_exit "$output" 2
  printf '%s\n' 'alias dep = import "./dep.jiang";' \
    'Int main() { RawFn<Int> callback = dep.second; return callback() + dep.Number(3).value; }' \
    >"$fixture/main.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/callable-init.log" "$output" "$fixture/main.jiang"
  require_stat_ge "$WORK_DIR/callable-init.log" artifact_interface_hit 1
  expect_exit "$output" 5
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/callable-hot.log" "$output" "$fixture/main.jiang"
  require_stat_eq "$WORK_DIR/callable-hot.log" artifact_emitted_units 0
  expect_exit "$output" 5
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
    '#doc(module) 声明缓存回归的包入口。' \
    '#package { name = "import_graph"; root = "main.jiang"; }' >"$fixture/package.jiang"
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
  context_count="$(find "$cache" -mindepth 1 -maxdepth 1 -type d ! -name targets | wc -l | tr -d ' ')"
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
    '#doc(module) 声明缓存回归的包入口。' \
    '#package { name = "shared_generic_callers"; root = "main.jiang"; }' >"$fixture/package.jiang"
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
  require_unique_weak_monomorphs "$cache"
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
  local added_mono_units
  added_mono_units="$(($(stat_value "$WORK_DIR/shared-generic-add.log" artifact_linked_objects) - cold_linked))"
  [ "$added_mono_units" -ge 0 ] && [ "$added_mono_units" -le 1 ] \
    || fail "adding one generic call changed link unit count by ${added_mono_units}"

  perl -0pi -e \
    's/common\.identity<Int>\(4\) \+ common\.second<Int>\(1\) - 1/common.identity<Int>(4)/' \
    "$fixture/right.jiang"
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/shared-generic-remove.log" \
    "$WORK_DIR/shared-generic-remove" "$fixture"
  # 共享 identity 的首次访问者决定 mono 归属，不能依赖临时路径影响的模块遍历顺序。
  # 若 second 独占新增 unit，移除时该 unit 退出链接；否则重建仍含 identity 的原 mono unit。
  require_stat_eq "$WORK_DIR/shared-generic-remove.log" artifact_emitted_units "$((2 - added_mono_units))"
  require_stat_eq "$WORK_DIR/shared-generic-remove.log" artifact_linked_objects "$cold_linked"

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
  require_stat_ge "$WORK_DIR/shared-generic-template.log" artifact_object_stale 1

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
    '#doc(module) 显式加载只包含全局变量的依赖包。' \
    '#package {' \
    '    name = "app"; root = "main.jiang";' \
    '    dependencies { globals = "../globals"; }' \
    '}' >"$app/package.jiang"
  printf '%s\n' \
    'import globals;' \
    '' \
    'Int main() {' \
    '    globals.value - 7' \
    '}' >"$app/main.jiang"
  printf '%s\n' \
    '#doc(module) 声明缓存回归的包入口。' \
    '#package { name = "globals"; root = "globals.jiang"; }' >"$dependency/package.jiang"
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
    '#doc(module) 声明缓存回归的包入口。' \
    '#package { name = "public_alias"; root = "main.jiang"; }' >"$fixture/package.jiang"
  printf '%s\n' \
    'import dep = "./dep.jiang";' \
    '' \
    'Int main() {' \
    '    dep.Number(value = 0).value' \
    '}' >"$fixture/main.jiang"
  printf '%s\n' \
    'import api = "./api.jiang";' \
    '' \
    'public alias Number = api.middle.Number;' >"$fixture/dep.jiang"
  printf '%s\n' \
    'public import middle = "./middle.jiang";' >"$fixture/api.jiang"
  printf '%s\n' \
    'public alias * = import "./leaf.jiang";' >"$fixture/middle.jiang"
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

check_deferred_const_instances() {
  local fixture="$WORK_DIR/deferred-const"
  local cache="$WORK_DIR/deferred-const-cache"
  local prefix="$WORK_DIR/deferred-const"
  cp -R "$ROOT_DIR/test/compiler/fixture/deferred_const_cache" "$fixture"
  check_only "$cache" "$prefix-check-cold.log" "$fixture/main.jiang"
  check_only "$cache" "$prefix-check-hot.log" "$fixture/main.jiang"
  require_stat_ge "$prefix-check-hot.log" artifact_interface_hit 1
  compile_executable "$JIANGC" "$cache" "$prefix-cold.log" "$prefix-cold" "$fixture/main.jiang"
  expect_exit "$prefix-cold" 68
  compile_executable "$JIANGC" "$cache" "$prefix-hot.log" "$prefix-hot" "$fixture/main.jiang"
  expect_exit "$prefix-hot" 68
  require_stat_eq "$prefix-hot.log" artifact_emitted_units 0
  cp "$fixture/library_changed.jiang" "$fixture/library.jiang"
  compile_executable "$JIANGC" "$cache" "$prefix-changed.log" "$prefix-changed" "$fixture/main.jiang"
  expect_exit "$prefix-changed" 80
  require_stat_ge "$prefix-changed.log" artifact_emitted_units 1
  compile_executable "$JIANGC" "$cache" "$prefix-changed-hot.log" "$prefix-changed" "$fixture/main.jiang"
  expect_exit "$prefix-changed" 80
  require_stat_eq "$prefix-changed-hot.log" artifact_no_op_hits 1
  cp "$ROOT_DIR/test/compiler/fixture/deferred_const_cache/library.jiang" "$fixture/library.jiang"
  compile_executable "$JIANGC" "$cache" "$prefix-restored.log" "$prefix-restored" "$fixture/main.jiang"
  expect_exit "$prefix-restored" 68
  cp "$fixture/library_helper_changed.jiang" "$fixture/library.jiang"
  compile_executable "$JIANGC" "$cache" "$prefix-helper.log" "$prefix-helper" "$fixture/main.jiang"
  expect_exit "$prefix-helper" 74
  cp "$fixture/library_failed.jiang" "$fixture/library.jiang"
  local attempt
  for attempt in 1 2; do
    if compile_executable "$JIANGC" "$cache" "$prefix-failed-$attempt.log" \
      "$prefix-failed" "$fixture/main.jiang"; then
      fail "failed const instance unexpectedly compiled"
    fi
    grep -q 'comptime_assertion_failed' "$prefix-failed-$attempt.log" \
      || fail "const instance failure diagnostic missing"
  done
  cp "$ROOT_DIR/test/compiler/fixture/deferred_const_cache/library.jiang" "$fixture/library.jiang"
  compile_executable "$JIANGC" "$cache" "$prefix-retry.log" "$prefix-retry" "$fixture/main.jiang"
  expect_exit "$prefix-retry" 68
}

check_reflection_documentation() {
  local fixture="$WORK_DIR/reflection-doc"
  local cache="$WORK_DIR/reflection-doc-cache"
  local prefix="$WORK_DIR/reflection-doc"
  cp -R "$ROOT_DIR/test/compiler/fixture/reflection_doc_cache" "$fixture"
  check_only "$cache" "$prefix-check-cold.log" "$fixture/app"
  compile_executable "$JIANGC" "$cache" "$prefix-cold.log" "$prefix-cold" "$fixture/app"
  expect_exit "$prefix-cold" 1
  check_only "$cache" "$prefix-check-hot.log" "$fixture/app"
  compile_executable "$JIANGC" "$cache" "$prefix-hot.log" "$prefix-hot" "$fixture/app"
  expect_exit "$prefix-hot" 1
  require_stat_eq "$prefix-hot.log" artifact_emitted_units 0

  cp "$fixture/library_changed.jiang" "$fixture/library/main.jiang"
  check_only "$cache" "$prefix-check-changed.log" "$fixture/app"
  compile_executable "$JIANGC" "$cache" "$prefix-changed.log" "$prefix-changed" "$fixture/app"
  expect_exit "$prefix-changed" 4
  require_stat_ge "$prefix-changed.log" artifact_emitted_units 1
  compile_executable "$JIANGC" "$cache" "$prefix-changed-hot.log" "$prefix-changed" "$fixture/app"
  expect_exit "$prefix-changed" 4
  require_stat_eq "$prefix-changed-hot.log" artifact_no_op_hits 1

  cp "$fixture/library_unread.jiang" "$fixture/library/main.jiang"
  compile_executable "$JIANGC" "$cache" "$prefix-unread.log" "$prefix-unread" "$fixture/app"
  expect_exit "$prefix-unread" 4
  require_stat_eq "$prefix-unread.log" artifact_emitted_units 0
  cp "$fixture/library_removed.jiang" "$fixture/library/main.jiang"
  compile_executable "$JIANGC" "$cache" "$prefix-removed.log" "$prefix-removed" "$fixture/app"
  expect_exit "$prefix-removed" 0
  require_stat_ge "$prefix-removed.log" artifact_emitted_units 1
  cp "$ROOT_DIR/test/compiler/fixture/reflection_doc_cache/library/main.jiang" "$fixture/library/main.jiang"
  compile_executable "$JIANGC" "$cache" "$prefix-restored.log" "$prefix-restored" "$fixture/app"
  expect_exit "$prefix-restored" 1

  compile_executable "$JIANGC" "$cache" "$prefix-local-cold.log" "$prefix-local" "$fixture/local"
  expect_exit "$prefix-local" 1
  compile_executable "$JIANGC" "$cache" "$prefix-local-hot.log" "$prefix-local-hot" "$fixture/local"
  expect_exit "$prefix-local-hot" 1
  require_stat_eq "$prefix-local-hot.log" artifact_emitted_units 0
  cp "$fixture/local_changed.jiang" "$fixture/local/main.jiang"
  check_only "$cache" "$prefix-local-check.log" "$fixture/local"
  compile_executable "$JIANGC" "$cache" "$prefix-local-changed.log" "$prefix-local" "$fixture/local"
  expect_exit "$prefix-local" 4
  require_stat_ge "$prefix-local-changed.log" artifact_emitted_units 1
}

write_reflection_location_input() {
  printf '#doc(module) 位置输入。\npublic Int unused() {%sreturn 1; }\npublic struct Box {\npublic Int field;\n}\n' "$2" >"$1"
}

write_reflection_location_local() {
  write_reflection_location_input "$1" "$2"
  cat >>"$1" <<'EOF'
Int width<T>() { return reflect.type_of<T>().members().get(0).location().line; }
const Int line = width<Box>();
Int main() { return line; }
EOF
}

check_reflection_location() {
  local fixture="$WORK_DIR/reflection-location"
  local cache="$WORK_DIR/reflection-location-cache"
  local prefix="$WORK_DIR/reflection-location"
  cp -R "$ROOT_DIR/test/compiler/fixture/reflection_doc_cache" "$fixture"
  cat >"$fixture/reader/main.jiang" <<'EOF'
#doc(module) 跨包读取字段位置。
public Int width<T>() { return reflect.type_of<T>().members().get(0).location().line; }
EOF
  write_reflection_location_input "$fixture/library/main.jiang" '   '
  check_only "$cache" "$prefix-check-cold.log" "$fixture/app"
  compile_executable "$JIANGC" "$cache" "$prefix-cold.log" "$prefix-cold" "$fixture/app"
  expect_exit "$prefix-cold" 4
  compile_executable "$JIANGC" "$cache" "$prefix-hot.log" "$prefix-hot" "$fixture/app"
  expect_exit "$prefix-hot" 4
  require_stat_eq "$prefix-hot.log" artifact_emitted_units 0

  # 字节 offset 不变，仅将一个空格替换为换行。
  write_reflection_location_input "$fixture/library/main.jiang" $'\n  '
  check_only "$cache" "$prefix-check-changed.log" "$fixture/app"
  compile_executable "$JIANGC" "$cache" "$prefix-changed.log" "$prefix-changed" "$fixture/app"
  expect_exit "$prefix-changed" 5
  require_stat_ge "$prefix-changed.log" artifact_emitted_units 1
  compile_executable "$JIANGC" "$cache" "$prefix-changed-hot.log" "$prefix-changed" "$fixture/app"
  expect_exit "$prefix-changed" 5
  require_stat_eq "$prefix-changed-hot.log" artifact_no_op_hits 1

  # 只改变字段之后的空白；被读取的位置保持不变，不应额外发射 object。
  printf '\n\n' >>"$fixture/library/main.jiang"
  compile_executable "$JIANGC" "$cache" "$prefix-unread.log" "$prefix-unread" "$fixture/app"
  expect_exit "$prefix-unread" 5
  require_stat_eq "$prefix-unread.log" artifact_emitted_units 0
  write_reflection_location_input "$fixture/library/main.jiang" '   '
  compile_executable "$JIANGC" "$cache" "$prefix-reverted.log" "$prefix-reverted" "$fixture/app"
  expect_exit "$prefix-reverted" 4

  write_reflection_location_local "$fixture/local/main.jiang" '   '
  compile_executable "$JIANGC" "$cache" "$prefix-local-cold.log" "$prefix-local" "$fixture/local"
  expect_exit "$prefix-local" 4
  compile_executable "$JIANGC" "$cache" "$prefix-local-hot.log" "$prefix-local-hot" "$fixture/local"
  expect_exit "$prefix-local-hot" 4
  require_stat_eq "$prefix-local-hot.log" artifact_emitted_units 0
  write_reflection_location_local "$fixture/local/main.jiang" $'\n  '
  check_only "$cache" "$prefix-local-check.log" "$fixture/local"
  compile_executable "$JIANGC" "$cache" "$prefix-local-changed.log" "$prefix-local" "$fixture/local"
  expect_exit "$prefix-local" 5
  require_stat_ge "$prefix-local-changed.log" artifact_emitted_units 1
}

write_reflection_scalar_input() {
  printf '#doc(module) 标量反射缓存输入。\npublic struct Box { public Int work(self, Int %s%s) { return %s; } }\n' \
    "$2" "$3" "$4" >"$1"
}

check_reflection_scalar() {
  local fixture="$WORK_DIR/reflection-scalar"
  local cache="$WORK_DIR/reflection-scalar-cache"
  local prefix="$WORK_DIR/reflection-scalar"
  cp -R "$ROOT_DIR/test/compiler/fixture/reflection_doc_cache" "$fixture"
  cat >"$fixture/reader/main.jiang" <<'EOF'
#doc(module) 跨包读取函数与参数标量。
public Int width<T>() {
    _ decl = reflect.type_of<T>().members().get(0);
    guard decl is .function(fn) else { return 0; }
    _ param = fn.signature().parameters.get(0);
    Int extra = if param.has_default() { 1 } else { 0 };
    return decl.name().length + param.name().length + extra;
}
EOF
  write_reflection_scalar_input "$fixture/library/main.jiang" value ' = 1' value
  check_only "$cache" "$prefix-check-cold.log" "$fixture/app"
  compile_executable "$JIANGC" "$cache" "$prefix-cold.log" "$prefix-cold" "$fixture/app"
  expect_exit "$prefix-cold" 10
  compile_executable "$JIANGC" "$cache" "$prefix-hot.log" "$prefix-hot" "$fixture/app"
  expect_exit "$prefix-hot" 10
  require_stat_eq "$prefix-hot.log" artifact_emitted_units 0

  write_reflection_scalar_input "$fixture/library/main.jiang" value ' = 9' 'value + 1'
  compile_executable "$JIANGC" "$cache" "$prefix-body.log" "$prefix-body" "$fixture/app"
  expect_exit "$prefix-body" 10
  write_reflection_scalar_input "$fixture/library/main.jiang" id ' = 9' id
  check_only "$cache" "$prefix-check-name.log" "$fixture/app"
  compile_executable "$JIANGC" "$cache" "$prefix-name.log" "$prefix-name" "$fixture/app"
  expect_exit "$prefix-name" 7
  require_stat_ge "$prefix-name.log" artifact_emitted_units 1
  write_reflection_scalar_input "$fixture/library/main.jiang" id '' id
  check_only "$cache" "$prefix-check-default.log" "$fixture/app"
  compile_executable "$JIANGC" "$cache" "$prefix-default.log" "$prefix-default" "$fixture/app"
  expect_exit "$prefix-default" 6
  compile_executable "$JIANGC" "$cache" "$prefix-default-hot.log" "$prefix-default-hot" "$fixture/app"
  expect_exit "$prefix-default-hot" 6
  require_stat_eq "$prefix-default-hot.log" artifact_emitted_units 0
  write_reflection_scalar_input "$fixture/library/main.jiang" value ' = 1' value
  compile_executable "$JIANGC" "$cache" "$prefix-restored.log" "$prefix-restored" "$fixture/app"
  expect_exit "$prefix-restored" 10

  write_reflection_scalar_input "$fixture/local/main.jiang" value ' = 1' value
  tail -n +2 "$fixture/reader/main.jiang" >>"$fixture/local/main.jiang"
  printf '\nconst Int result = width<Box>(); Int main() { return result; }\n' >>"$fixture/local/main.jiang"
  compile_executable "$JIANGC" "$cache" "$prefix-local.log" "$prefix-local" "$fixture/local"
  expect_exit "$prefix-local" 10
  compile_executable "$JIANGC" "$cache" "$prefix-local-hot.log" "$prefix-local-hot" "$fixture/local"
  expect_exit "$prefix-local-hot" 10
  require_stat_eq "$prefix-local-hot.log" artifact_emitted_units 0
  write_reflection_scalar_input "$fixture/local/main.jiang" id '' id
  tail -n +2 "$fixture/reader/main.jiang" >>"$fixture/local/main.jiang"
  printf '\nconst Int result = width<Box>(); Int main() { return result; }\n' >>"$fixture/local/main.jiang"
  check_only "$cache" "$prefix-local-check.log" "$fixture/local"
  compile_executable "$JIANGC" "$cache" "$prefix-local-changed.log" "$prefix-local-changed" "$fixture/local"
  expect_exit "$prefix-local-changed" 6
}

check_member_alias_interface() {
  local cache="$WORK_DIR/member-alias-cache"
  local input="$ROOT_DIR/test/lang/import/run/alias_member_public.jiang"
  check_only "$cache" "$WORK_DIR/member-alias-cold.log" "$input"
  check_only "$cache" "$WORK_DIR/member-alias-hot.log" "$input"
  require_stat_ge "$WORK_DIR/member-alias-hot.log" artifact_interface_hit 1
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/member-alias-run.log" \
    "$WORK_DIR/member-alias-run" "$input"
  expect_exit "$WORK_DIR/member-alias-run" 0
}

check_namespace_wildcard() {
  local form="${1:-wildcard}"
  local fixture="$WORK_DIR/namespace-$form"
  local cache="$WORK_DIR/namespace-$form-cache"
  local prefix="$WORK_DIR/namespace-$form"
  cp -R "$ROOT_DIR/test/compiler/fixture/namespace_wildcard" "$fixture"
  if [ "$form" = named ]; then
    cp "$fixture/api_named.jiang" "$fixture/api.jiang"
  fi
  if [ "$form" = async ] || [ "$form" = global ] || [ "$form" = lambda ] \
    || [ "$form" = trait ] || [ "$form" = trait_extension ] \
    || [ "$form" = trait_extension_concrete ]; then
    cp "$fixture/right_$form.jiang" "$fixture/right.jiang"
  fi
  if [ "$form" = async_unused ]; then
    cp "$fixture/right_async.jiang" "$fixture/right.jiang"
    cp "$fixture/main_async_unused.jiang" "$fixture/main.jiang"
  fi
  cp "$fixture/settings.jiang" "$fixture/settings_left.jiang"
  local input="$fixture/main.jiang"
  check_only "$cache" "$prefix-cold.log" "$input"
  check_only "$cache" "$prefix-hot.log" "$input"
  require_stat_ge "$prefix-hot.log" artifact_interface_hit 1
  compile_executable "$JIANGC" "$cache" "$prefix-left.log" "$prefix-left" "$input"
  expect_exit "$prefix-left" 41
  cp "$fixture/settings_right.jiang" "$fixture/settings.jiang"
  compile_executable "$JIANGC" "$cache" "$prefix-right.log" "$prefix-right" "$input"
  expect_exit "$prefix-right" 42
  compile_executable "$JIANGC" "$cache" "$prefix-right-hot.log" "$prefix-right-hot" "$input"
  expect_exit "$prefix-right-hot" 42
  require_stat_eq "$prefix-right-hot.log" artifact_emitted_units 0
  cp "$fixture/settings_left.jiang" "$fixture/settings.jiang"
  compile_executable "$JIANGC" "$cache" "$prefix-left-again.log" "$prefix-left-again" "$input"
  expect_exit "$prefix-left-again" 41
}

check_namespace_named() {
  check_namespace_wildcard named
}

check_namespace_async() {
  check_namespace_wildcard async
}

check_namespace_async_unused() {
  check_namespace_wildcard async_unused
}

check_namespace_lambda() {
  check_namespace_wildcard lambda
}

check_namespace_global() {
  check_namespace_wildcard global
}

check_namespace_trait() {
  check_namespace_wildcard trait
}

check_namespace_trait_extension() {
  check_namespace_wildcard trait_extension
}

check_namespace_trait_extension_concrete() {
  check_namespace_wildcard trait_extension_concrete
}

check_attribute_alias_interface() {
  local cache="$WORK_DIR/attribute-alias-cache"
  local input="$ROOT_DIR/test/lang/generic/run/alias_attribute_import.jiang"
  check_only "$cache" "$WORK_DIR/attribute-alias-cold.log" "$input"
  check_only "$cache" "$WORK_DIR/attribute-alias-hot.log" "$input"
  require_stat_ge "$WORK_DIR/attribute-alias-hot.log" artifact_interface_hit 1
  compile_executable "$JIANGC" "$cache" "$WORK_DIR/attribute-alias-run.log" \
    "$WORK_DIR/attribute-alias-run" "$input"
  expect_exit "$WORK_DIR/attribute-alias-run" 0
  local invalid="$ROOT_DIR/test/lang/generic/fail/alias_attribute_import_no_leak.jiang"
  if check_only "$cache" "$WORK_DIR/attribute-alias-private.log" "$invalid"; then
    fail "function attribute alias leaked into its module after interface restore"
  fi
  grep -q 'unresolved_type' "$WORK_DIR/attribute-alias-private.log" \
    || fail "expected unresolved_type for function-local alias"
}

check_trait_interface() {
  local fixture="$WORK_DIR/trait-interface"
  local cache="$WORK_DIR/trait-interface-cache"
  mkdir -p "$fixture"
  printf '%s\n' \
    '#doc(module) 声明缓存回归的包入口。' \
    '#package { name = "trait_interface"; root = "main.jiang"; }' >"$fixture/package.jiang"
  printf '%s\n' \
    'import dep = "./dep.jiang";' \
    '' \
    'Int main() {' \
    '    dep.Id id! = dep.Id(index = 0);' \
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
    '        Id(index = index)' \
    '    }' \
    '' \
    '    public Int normalize(self, UInt8 value) {' \
    '        Int(value)' \
    '    }' \
    '' \
    '    public Void write(Self&! self, UInt8[]& bytes) {}' \
    '    public Void write(Self&! self, UInt8 value) {}' \
    '    public UInt64 finish(self) { 0 }' \
    '}' \
    '' \
    'public Void write_u64<H: Hasher>(H&! value) {' \
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

check_backend_emission() {
  local input="$ROOT_DIR/test/lang/package/run/source_dependency_app"
  local cache="$WORK_DIR/backend-emission-cache"
  local failed_cache="$WORK_DIR/backend-failed-unit-cache"
  local repeat_cache="$WORK_DIR/backend-failed-unit-repeat-cache"
  local output="$WORK_DIR/backend-emission"
  local failed_output="$WORK_DIR/backend-failed-unit"

  "$JIANGC" --artifact-cache-dir "$cache" --artifact-stats \
    -o "$output" "$input" >"$WORK_DIR/backend-emission.log" 2>&1
  expect_exit "$output" 52
  require_stat_ge "$WORK_DIR/backend-emission.log" artifact_emitted_units 2

  "$JIANGC" --artifact-cache-dir "$cache" --artifact-stats \
    -o "$output" "$input" >"$WORK_DIR/backend-emission-hot.log" 2>&1
  require_stat_eq "$WORK_DIR/backend-emission-hot.log" artifact_no_op_hits 1
  require_stat_eq "$WORK_DIR/backend-emission-hot.log" artifact_emitted_units 0
  require_no_temporary_files "$cache"

  if env JIANG_INTERNAL_BACKEND_FAIL_UNIT=0 \
    "$JIANGC" --artifact-cache-dir "$failed_cache" --artifact-stats \
      -o "$failed_output" "$input" >"$WORK_DIR/backend-failed-unit.log" 2>&1
  then
    fail "forced backend unit failure unexpectedly succeeded"
  fi
  [ ! -e "$failed_output" ] || fail "failed backend emission published an executable"
  grep 'llvm_object_unit_emit_failed' "$WORK_DIR/backend-failed-unit.log" \
    >"$WORK_DIR/backend-failed-unit.diagnostic"
  require_stat_ge "$WORK_DIR/backend-failed-unit.log" artifact_emitted_units 1
  require_stat_eq "$WORK_DIR/backend-failed-unit.log" artifact_linked_objects 0
  require_stat_eq "$WORK_DIR/backend-failed-unit.log" artifact_no_op_hits 0
  [ -z "$(find "$failed_cache" -type f -name '*.jbuild' -print -quit)" ] \
    || fail "failed backend emission published build state"
  require_no_temporary_files "$failed_cache"

  if env JIANG_INTERNAL_BACKEND_FAIL_UNIT=0 \
    "$JIANGC" --artifact-cache-dir "$repeat_cache" --artifact-stats \
      -o "$failed_output.repeat" "$input" >"$WORK_DIR/backend-failed-unit-repeat.log" 2>&1
  then
    fail "repeated forced backend unit failure unexpectedly succeeded"
  fi
  grep 'llvm_object_unit_emit_failed' "$WORK_DIR/backend-failed-unit-repeat.log" \
    >"$WORK_DIR/backend-failed-unit-repeat.diagnostic"
  cmp "$WORK_DIR/backend-failed-unit.diagnostic" \
    "$WORK_DIR/backend-failed-unit-repeat.diagnostic"
  require_no_temporary_files "$repeat_cache"

  "$JIANGC" --artifact-cache-dir "$failed_cache" --artifact-stats \
    -o "$failed_output" "$input" >"$WORK_DIR/backend-failed-unit-retry.log" 2>&1
  require_stat_ge "$WORK_DIR/backend-failed-unit-retry.log" artifact_emitted_units 2
  require_stat_eq "$WORK_DIR/backend-failed-unit-retry.log" artifact_object_hit 0
  expect_exit "$failed_output" 52
  require_no_temporary_files "$failed_cache"
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

check_failed_link_discards_manifest() {
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
  [ -z "$(find "$cache" -type f -name '*.jbuild' -print -quit)" ] \
    || fail "failed link published build state"

  compile_executable "$JIANGC" "$cache" "$WORK_DIR/failed-link-retry.log" \
    "$output" "$source"
  require_stat_ge "$WORK_DIR/failed-link-retry.log" artifact_emitted_units 1
  require_stat_eq "$WORK_DIR/failed-link-retry.log" artifact_object_hit 0
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
run_check context_lookup "build context lookup recovery and configuration change" check_build_context_lookup
run_check invalidation "dependency invalidation" check_dependency_invalidation
run_check coverage "hidden caller object coverage" check_hidden_caller_coverage
run_check callable_values "cached function values and constructors" check_cached_callable_values
run_check metadata "mtime-only source change" check_metadata_only_change
run_check check_codegen "--check and codegen transition" check_check_then_codegen
run_check import_graph "import graph changes" check_import_graph_changes
run_check diagnostics "clean/cache diagnostic equivalence" check_diagnostic_equivalence
run_check recovery "corrupt cache recovery" check_corrupt_cache_recovery
run_check compiler_build "compiler build identity" check_compiler_build_invalidation
run_check object_contract "object contract and target" check_emit_object_contract_and_target
run_check const_generic "const generic closure" check_cross_package_const_generic
run_check deferred_const "deferred const instance restore and invalidation" check_deferred_const_instances
run_check reflection_documentation "reflection documentation and object invalidation" check_reflection_documentation
run_check reflection_location "reflection locations and object invalidation" check_reflection_location
run_check reflection_scalar "reflection declaration scalars and object invalidation" check_reflection_scalar
run_check shared_generic "shared generic callers" check_shared_generic_callers
run_check release_units "release whole-package state" check_release_whole_package_state
run_check global_only "global-only dependency" check_global_only_dependency
run_check public_alias "public alias dependency" check_public_alias_dependency
run_check member_alias "member alias interface ownership" check_member_alias_interface
run_check namespace_wildcard "namespace wildcard selection and invalidation" check_namespace_wildcard
run_check namespace_named "named namespace selection and invalidation" check_namespace_named
run_check namespace_async "cached async body selection and invalidation" check_namespace_async
run_check namespace_async_unused "cached non-root async template dependency" check_namespace_async_unused
run_check namespace_lambda "cached nested lambda body selection and invalidation" check_namespace_lambda
run_check namespace_global "cached global storage selection and invalidation" check_namespace_global
run_check namespace_trait "cached trait eval and drop dependencies" check_namespace_trait
run_check namespace_trait_extension "cached extension trait default dispatch" check_namespace_trait_extension
run_check namespace_trait_extension_concrete "cached concrete extension trait dispatch" check_namespace_trait_extension_concrete
run_check attribute_alias "function attribute alias interface" check_attribute_alias_interface
run_check trait_interface "trait interface" check_trait_interface
run_check backend_emission "serial backend emission" check_backend_emission
run_check concurrent "concurrent publication" check_concurrent_publish
run_check clean "explicit cache clean" check_explicit_cache_clean
run_check partial "failed link does not publish manifest" check_failed_link_discards_manifest
run_check source_race "source change before link" check_source_change_before_link
run_check dylib "lang provider dylib" check_lang_provider_dylib

SUCCESS=1
printf 'artifact cache smoke passed\n'
