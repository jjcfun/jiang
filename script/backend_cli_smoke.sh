#!/usr/bin/env bash
set -euo pipefail
trap 'case $- in *e*) echo "backend CLI smoke failed at line $LINENO" >&2 ;; esac' ERR

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
SMOKE_BUILD_DIR="$BUILD_DIR/smoke/stage2_backend_cli"
PACKAGE_VERSION="$(sed -n 's/^[[:space:]]*version[[:space:]]*=[[:space:]]*//p' "$ROOT_DIR/package.ini" | head -n 1)"
EXPECTED_COMPILER_VERSION="${EXPECTED_COMPILER_VERSION:-$PACKAGE_VERSION}"
HOST_OS="$(uname -s)"
HOST_ARCH="$(uname -m)"

source "$ROOT_DIR/script/llvm_env.sh"

assert_file_matches() {
  local path="$1"
  local pattern="$2"
  local output="$path.file"
  file "$path" >"$output"
  if grep -Eiq "$pattern" "$output"; then
    return
  fi
  cat "$output" >&2
  return 1
}

mkdir -p "$SMOKE_BUILD_DIR"
cd "$ROOT_DIR"

COMPILER_UNDER_TEST="${JIANGC:-}"
if [ -z "$COMPILER_UNDER_TEST" ]; then
  COMPILER_UNDER_TEST="$(command -v jiangc || true)"
  if [ -z "$COMPILER_UNDER_TEST" ] || [ ! -x "$COMPILER_UNDER_TEST" ]; then
    echo "missing compiler: set JIANGC or put jiangc on PATH" >&2
    echo "install Jiang $EXPECTED_COMPILER_VERSION so jiangc is on PATH" >&2
    exit 2
  fi
  COMPILER_VERSION="$("$COMPILER_UNDER_TEST" --version | sed -n '1p')"
  case "$COMPILER_VERSION" in
    "jiang $EXPECTED_COMPILER_VERSION") ;;
    *)
      echo "unsupported bootstrap compiler: $COMPILER_VERSION" >&2
      echo "install Jiang $EXPECTED_COMPILER_VERSION so jiangc is on PATH, or pass JIANGC=<compiler>" >&2
      exit 2
      ;;
  esac
elif [ ! -x "$COMPILER_UNDER_TEST" ]; then
  echo "missing compiler: $COMPILER_UNDER_TEST" >&2
  exit 2
fi
COMPILER_VERSION="$("$COMPILER_UNDER_TEST" --version | sed -n '1p')"

clang_bin="$LLVM_CLANG"
compiler_bin="$COMPILER_UNDER_TEST"
host_clang_link_args=()
sample="$SMOKE_BUILD_DIR/minimal.jiang"
sample_ll="$SMOKE_BUILD_DIR/minimal.ll"
sample_obj="$SMOKE_BUILD_DIR/minimal.o"
sample_from_ll="$SMOKE_BUILD_DIR/minimal_from_ll"
sample_from_obj="$SMOKE_BUILD_DIR/minimal_from_obj"
sample_with_link_arg="$SMOKE_BUILD_DIR/minimal_with_link_arg"
sample_release_obj="$SMOKE_BUILD_DIR/minimal_release.o"
sample_from_release_obj="$SMOKE_BUILD_DIR/minimal_from_release_obj"
sample_release_bin="$SMOKE_BUILD_DIR/minimal_release"
system_env_bin="$SMOKE_BUILD_DIR/system_env"
package_release_bin="$SMOKE_BUILD_DIR/package_release"
field_sample="$SMOKE_BUILD_DIR/field_projection.jiang"
field_ll="$SMOKE_BUILD_DIR/field_projection.ll"
field_bin="$SMOKE_BUILD_DIR/field_projection"
system_fs_sample="$ROOT_DIR/test/compiler/fixture/system_fs_target.jiang"
system_fs_linux_ll="$SMOKE_BUILD_DIR/system_fs_linux.ll"
system_fs_linux_no_libc_ll="$SMOKE_BUILD_DIR/system_fs_linux_no_libc.ll"
alloc_sample="$SMOKE_BUILD_DIR/no_libc_alloc.jiang"
alloc_no_libc_ll="$SMOKE_BUILD_DIR/no_libc_alloc.ll"
alloc_no_libc_obj="$SMOKE_BUILD_DIR/no_libc_alloc.o"
alloc_no_libc_nm="$SMOKE_BUILD_DIR/no_libc_alloc.nm"
alloc_no_libc_undefined_nm="$SMOKE_BUILD_DIR/no_libc_alloc.undefined.nm"
alloc_linux_no_libc_ll="$SMOKE_BUILD_DIR/no_libc_alloc_linux.ll"
alloc_linux_no_libc_obj="$SMOKE_BUILD_DIR/no_libc_alloc_linux.o"
alloc_linux_no_libc_nm="$SMOKE_BUILD_DIR/no_libc_alloc_linux.nm"
alloc_linux_no_libc_undefined_nm="$SMOKE_BUILD_DIR/no_libc_alloc_linux.undefined.nm"
alloc_wasm_ll="$SMOKE_BUILD_DIR/no_libc_alloc_wasm.ll"
macos_target_ll="$SMOKE_BUILD_DIR/minimal_macos.ll"
macos_target_obj="$SMOKE_BUILD_DIR/minimal_macos.o"
macos_target_bin="$SMOKE_BUILD_DIR/minimal_macos"
linux_target_ll="$SMOKE_BUILD_DIR/minimal_linux.ll"
linux_target_obj="$SMOKE_BUILD_DIR/minimal_linux.o"
linux_aarch64_target_ll="$SMOKE_BUILD_DIR/minimal_linux_aarch64.ll"
linux_aarch64_target_obj="$SMOKE_BUILD_DIR/minimal_linux_aarch64.o"
linux_aarch64_no_libc_exe_log="$SMOKE_BUILD_DIR/linux_aarch64_no_libc_executable.log"
windows_target_ll="$SMOKE_BUILD_DIR/minimal_windows.ll"
windows_target_obj="$SMOKE_BUILD_DIR/minimal_windows.obj"
wasm_target_ll="$SMOKE_BUILD_DIR/minimal_wasm.ll"
wasm_target_obj="$SMOKE_BUILD_DIR/minimal_wasm.o"
wasi_target_ll="$SMOKE_BUILD_DIR/minimal_wasi.ll"
wasi_target_obj="$SMOKE_BUILD_DIR/minimal_wasi.o"
wasi_target_bin="$SMOKE_BUILD_DIR/minimal_wasi.wasm"
wasi_provider_sample="$ROOT_DIR/test/compiler/fixture/wasi_provider.jiang"
wasi_provider_bin="$SMOKE_BUILD_DIR/wasi_provider.wasm"
wasi_provider_stdout="$SMOKE_BUILD_DIR/wasi_provider.stdout"
wasi_provider_stderr="$SMOKE_BUILD_DIR/wasi_provider.stderr"
wasi_provider_sandbox="$SMOKE_BUILD_DIR/wasi_sandbox"
linux_no_libc_exe_log="$SMOKE_BUILD_DIR/linux_no_libc_executable.log"
windows_exe_log="$SMOKE_BUILD_DIR/windows_executable.log"
wasm_exe_log="$SMOKE_BUILD_DIR/wasm_executable.log"
wasi_exe_log="$SMOKE_BUILD_DIR/wasi_executable.log"
unsupported_target_log="$SMOKE_BUILD_DIR/unsupported_target.log"

case "$HOST_OS" in
  Darwin) host_clang_link_args+=(-Wl,-dead_strip) ;;
  Linux) host_clang_link_args+=(-no-pie) ;;
esac

printf 'Int main() { 0 }\n' >"$sample"
printf 'struct Pair { Int left; Int right; }\nInt get_left(Pair p) { p.left }\nInt main() { 0 }\n' >"$field_sample"
cat >"$alloc_sample" <<'EOF'
Int main() {
    unsafe {
        Int*! values = Int$.alloc(2);
        values[0] = 1;
        values$.dealloc();
    }
    0
}
EOF
printf '== backend cli smoke: test %s (%s) ==\n' "$compiler_bin" "$COMPILER_VERSION"

# smoke 必须从当前源码重新生成目标产物，不能复用上一次运行留下的 source artifact。
rm -rf "$BUILD_DIR/cache"

"$compiler_bin" --emit-llvm -o "$sample_ll" "$sample"
test -s "$sample_ll"
"$clang_bin" "${host_clang_link_args[@]}" "$sample_ll" -o "$sample_from_ll" \
  $("$LLVM_CONFIG" --link-static --ldflags) \
  $("$LLVM_CONFIG" --link-static --libs all) \
  $("$LLVM_CONFIG" --link-static --system-libs) \
  $(jiang_macos_sdkroot_link_args) \
  $(jiang_llvm_cxx_runtime_link_args)
"$sample_from_ll"

"$compiler_bin" --emit-obj -o "$sample_obj" "$sample"
test -s "$sample_obj"
"$clang_bin" "${host_clang_link_args[@]}" "$sample_obj" -o "$sample_from_obj" \
  $("$LLVM_CONFIG" --link-static --ldflags) \
  $("$LLVM_CONFIG" --link-static --libs all) \
  $("$LLVM_CONFIG" --link-static --system-libs) \
  $(jiang_macos_sdkroot_link_args) \
  $(jiang_llvm_cxx_runtime_link_args)
"$sample_from_obj"

"$compiler_bin" --no-link-libc --emit-llvm -o "$alloc_no_libc_ll" "$alloc_sample"
test -s "$alloc_no_libc_ll"
grep -q "__jiang_malloc" "$alloc_no_libc_ll"
grep -q "__jiang_free" "$alloc_no_libc_ll"
if grep -q "@malloc(" "$alloc_no_libc_ll"; then
  echo "unexpected malloc reference in --no-link-libc LLVM output" >&2
  exit 1
fi
if grep -q "@free(" "$alloc_no_libc_ll"; then
  echo "unexpected free reference in --no-link-libc LLVM output" >&2
  exit 1
fi
"$compiler_bin" --no-link-libc --emit-obj -o "$alloc_no_libc_obj" "$alloc_sample"
test -s "$alloc_no_libc_obj"
nm "$alloc_no_libc_obj" >"$alloc_no_libc_nm"
nm -u "$alloc_no_libc_obj" >"$alloc_no_libc_undefined_nm"
case "$HOST_OS" in
  Darwin)
    grep -q "___jiang_malloc" "$alloc_no_libc_undefined_nm"
    grep -q "___jiang_free" "$alloc_no_libc_undefined_nm"
    if grep -Eq "^_(malloc|free)$" "$alloc_no_libc_undefined_nm"; then
      echo "unexpected libc allocation reference in --no-link-libc object output" >&2
      exit 1
    fi
    ;;
  Linux)
    grep -q " T __jiang_malloc" "$alloc_no_libc_nm"
    grep -q " T __jiang_free" "$alloc_no_libc_nm"
    if grep -Eq " (malloc|free)$" "$alloc_no_libc_undefined_nm"; then
      echo "unexpected libc allocation reference in --no-link-libc object output" >&2
      exit 1
    fi
    ;;
esac

"$compiler_bin" --target x86_64-unknown-linux-gnu --no-link-libc \
  --emit-llvm -o "$alloc_linux_no_libc_ll" "$alloc_sample"
test -s "$alloc_linux_no_libc_ll"
grep -q "__jiang_malloc" "$alloc_linux_no_libc_ll"
grep -q "__jiang_free" "$alloc_linux_no_libc_ll"
if grep -q "@malloc(" "$alloc_linux_no_libc_ll"; then
  echo "unexpected malloc reference in linux --no-link-libc LLVM output" >&2
  exit 1
fi
if grep -q "@free(" "$alloc_linux_no_libc_ll"; then
  echo "unexpected free reference in linux --no-link-libc LLVM output" >&2
  exit 1
fi
"$compiler_bin" --target x86_64-unknown-linux-gnu --no-link-libc \
  --emit-obj -o "$alloc_linux_no_libc_obj" "$alloc_sample"
test -s "$alloc_linux_no_libc_obj"
nm "$alloc_linux_no_libc_obj" >"$alloc_linux_no_libc_nm"
nm -u "$alloc_linux_no_libc_obj" >"$alloc_linux_no_libc_undefined_nm"
grep -q " T __jiang_malloc" "$alloc_linux_no_libc_nm"
grep -q " T __jiang_free" "$alloc_linux_no_libc_nm"
if grep -q " malloc$" "$alloc_linux_no_libc_undefined_nm"; then
  echo "unexpected malloc reference in linux --no-link-libc object output" >&2
  exit 1
fi
if grep -q " free$" "$alloc_linux_no_libc_undefined_nm"; then
  echo "unexpected free reference in linux --no-link-libc object output" >&2
  exit 1
fi

"$compiler_bin" --target wasm32-unknown-unknown --emit-llvm -o "$alloc_wasm_ll" "$alloc_sample"
test -s "$alloc_wasm_ll"
grep -q "__jiang_malloc" "$alloc_wasm_ll"
grep -q "__jiang_free" "$alloc_wasm_ll"
if grep -q "@malloc(" "$alloc_wasm_ll"; then
  echo "unexpected malloc reference in wasm LLVM output" >&2
  exit 1
fi
if grep -q "@free(" "$alloc_wasm_ll"; then
  echo "unexpected free reference in wasm LLVM output" >&2
  exit 1
fi

"$compiler_bin" --target arm64-apple-macosx --emit-llvm -o "$macos_target_ll" "$sample"
test -s "$macos_target_ll"
grep -q 'target triple = "arm64-apple-macosx11.0.0"' "$macos_target_ll"
grep -q 'target datalayout = ' "$macos_target_ll"
"$compiler_bin" --target arm64-apple-macosx --emit-obj -o "$macos_target_obj" "$sample"
test -s "$macos_target_obj"
assert_file_matches "$macos_target_obj" "Mach-O 64-bit (object arm64|arm64 object)"
if [ "$HOST_OS:$HOST_ARCH" = "Darwin:arm64" ]; then
  "$compiler_bin" --target arm64-apple-macosx -o "$macos_target_bin" "$sample"
  "$macos_target_bin"
fi

"$compiler_bin" --target x86_64-unknown-linux-gnu --emit-llvm -o "$linux_target_ll" "$sample"
test -s "$linux_target_ll"
grep -q 'target triple = "x86_64-unknown-linux-gnu"' "$linux_target_ll"
grep -q 'target datalayout = ' "$linux_target_ll"
"$compiler_bin" --target x86_64-unknown-linux-gnu --emit-obj -o "$linux_target_obj" "$sample"
test -s "$linux_target_obj"
assert_file_matches "$linux_target_obj" "ELF 64-bit.*x86-64"

"$compiler_bin" --target aarch64-unknown-linux-gnu --emit-llvm -o "$linux_aarch64_target_ll" "$sample"
test -s "$linux_aarch64_target_ll"
grep -q 'target triple = "aarch64-unknown-linux-gnu"' "$linux_aarch64_target_ll"
grep -q 'target datalayout = ' "$linux_aarch64_target_ll"
"$compiler_bin" --target aarch64-unknown-linux-gnu --emit-obj -o "$linux_aarch64_target_obj" "$sample"
test -s "$linux_aarch64_target_obj"
assert_file_matches "$linux_aarch64_target_obj" "ELF 64-bit.*ARM aarch64"

"$compiler_bin" --target x86_64-unknown-linux-gnu --emit-llvm -o "$system_fs_linux_ll" "$system_fs_sample"
test -s "$system_fs_linux_ll"
grep -q 'target triple = "x86_64-unknown-linux-gnu"' "$system_fs_linux_ll"

"$compiler_bin" --target x86_64-unknown-linux-gnu --no-link-libc \
  --emit-llvm -o "$system_fs_linux_no_libc_ll" "$system_fs_sample"
test -s "$system_fs_linux_no_libc_ll"
grep -q 'target triple = "x86_64-unknown-linux-gnu"' "$system_fs_linux_no_libc_ll"
if grep -Eq "^declare .*@(getenv|posix_spawn|opendir|memcpy|memset)\\b" "$system_fs_linux_no_libc_ll"; then
  echo "unexpected hosted/libc symbol in linux no-libc system provider LLVM output" >&2
  exit 1
fi

"$compiler_bin" --target x86_64-pc-windows-msvc --emit-llvm -o "$windows_target_ll" "$sample"
test -s "$windows_target_ll"
grep -q 'target triple = "x86_64-pc-windows-msvc"' "$windows_target_ll"
grep -q 'target datalayout = ' "$windows_target_ll"
"$compiler_bin" --target x86_64-pc-windows-msvc --emit-obj -o "$windows_target_obj" "$sample"
test -s "$windows_target_obj"
assert_file_matches "$windows_target_obj" "COFF"

"$compiler_bin" --target wasm32-unknown-unknown --emit-llvm -o "$wasm_target_ll" "$sample"
test -s "$wasm_target_ll"
grep -q 'target triple = "wasm32-unknown-unknown"' "$wasm_target_ll"
grep -q 'target datalayout = ' "$wasm_target_ll"
"$compiler_bin" --target wasm32-unknown-unknown --emit-obj -o "$wasm_target_obj" "$sample"
test -s "$wasm_target_obj"
assert_file_matches "$wasm_target_obj" "WebAssembly"

"$compiler_bin" --target wasm32-wasi --emit-llvm -o "$wasi_target_ll" "$sample"
test -s "$wasi_target_ll"
grep -q 'target triple = "wasm32-wasip1"' "$wasi_target_ll"
grep -q 'target datalayout = ' "$wasi_target_ll"
"$compiler_bin" --target wasm32-wasi --emit-obj -o "$wasi_target_obj" "$sample"
test -s "$wasi_target_obj"
assert_file_matches "$wasi_target_obj" "WebAssembly"

set +e
"$compiler_bin" --target wasm32-wasi -o "$wasi_target_bin" "$sample" >"$wasi_exe_log" 2>&1
wasi_exe_status=$?
set -e
if [ "$wasi_exe_status" -eq 0 ]; then
  test -s "$wasi_target_bin"
  assert_file_matches "$wasi_target_bin" "WebAssembly"
  if grep -q "function signature mismatch" "$wasi_exe_log"; then
    echo "unexpected WASI linker signature mismatch" >&2
    exit 1
  fi
  if command -v wasmtime >/dev/null 2>&1; then
    wasmtime -C cache=n "$wasi_target_bin"
    mkdir -p "$wasi_provider_sandbox"
    rm -f "$wasi_provider_sandbox/wasi-file.txt"
    "$compiler_bin" --target wasm32-wasi -o "$wasi_provider_bin" "$wasi_provider_sample"
    wasmtime -C cache=n --env JIANG_WASI_ENV=wasiok --dir "$wasi_provider_sandbox::/sandbox" \
      "$wasi_provider_bin" a b >"$wasi_provider_stdout" 2>"$wasi_provider_stderr"
    grep -q "fs-ok" "$wasi_provider_sandbox/wasi-file.txt"
    grep -q "stdout-ok" "$wasi_provider_stdout"
    grep -q "stderr-ok" "$wasi_provider_stderr"
  fi
else
  grep -q "wasi_sdk_missing" "$wasi_exe_log"
fi

"$compiler_bin" --target x86_64-unknown-linux-gnu --no-link-libc \
  -o "$SMOKE_BUILD_DIR/minimal_linux_no_libc_exe" "$sample" \
  >"$linux_no_libc_exe_log" 2>&1
test -s "$SMOKE_BUILD_DIR/minimal_linux_no_libc_exe"
assert_file_matches "$SMOKE_BUILD_DIR/minimal_linux_no_libc_exe" "ELF 64-bit.*x86-64"

set +e
"$compiler_bin" --target aarch64-unknown-linux-gnu --no-link-libc \
  -o "$SMOKE_BUILD_DIR/minimal_linux_aarch64_no_libc_exe" "$sample" \
  >"$linux_aarch64_no_libc_exe_log" 2>&1
linux_aarch64_no_libc_exe_status=$?
set -e
test "$linux_aarch64_no_libc_exe_status" -ne 0
grep -q "target_executable_requires_runtime" "$linux_aarch64_no_libc_exe_log"

set +e
"$compiler_bin" --target x86_64-pc-windows-msvc \
  -o "$SMOKE_BUILD_DIR/minimal_windows_exe" "$sample" >"$windows_exe_log" 2>&1
windows_exe_status=$?
set -e
test "$windows_exe_status" -ne 0
grep -q "target_executable_runtime_unsupported" "$windows_exe_log"

set +e
"$compiler_bin" --target wasm32-unknown-unknown -o "$SMOKE_BUILD_DIR/minimal_wasm_exe" "$sample" >"$wasm_exe_log" 2>&1
wasm_exe_status=$?
set -e
test "$wasm_exe_status" -ne 0
grep -q "target_executable_runtime_unsupported" "$wasm_exe_log"

set +e
"$compiler_bin" --target x86_64-unknown-freebsd --emit-llvm \
  -o "$SMOKE_BUILD_DIR/minimal_freebsd.ll" "$sample" >"$unsupported_target_log" 2>&1
unsupported_target_status=$?
set -e
test "$unsupported_target_status" -ne 0
grep -q "unsupported target triple: x86_64-unknown-freebsd" "$unsupported_target_log"

host_dead_strip_arg="-Wl,--gc-sections"
if [ "$HOST_OS" = "Darwin" ]; then
  host_dead_strip_arg="-Wl,-dead_strip"
fi
"$compiler_bin" --link-arg "$host_dead_strip_arg" -o "$sample_with_link_arg" "$sample"
"$sample_with_link_arg"

"$compiler_bin" -o "$system_env_bin" test/compiler/system/run/env_get.jiang
"$system_env_bin"

"$compiler_bin" --mode release --emit-obj -o "$sample_release_obj" "$sample"
test -s "$sample_release_obj"
"$clang_bin" "${host_clang_link_args[@]}" "$sample_release_obj" -o "$sample_from_release_obj" \
  $("$LLVM_CONFIG" --link-static --ldflags) \
  $("$LLVM_CONFIG" --link-static --libs all) \
  $("$LLVM_CONFIG" --link-static --system-libs) \
  $(jiang_macos_sdkroot_link_args) \
  $(jiang_llvm_cxx_runtime_link_args)
"$sample_from_release_obj"

"$compiler_bin" --mode release -o "$sample_release_bin" "$sample"
"$sample_release_bin"

"$compiler_bin" --mode release -o "$package_release_bin" test/lang/package/run/source_dependency_app
set +e
"$package_release_bin"
package_status=$?
set -e
test "$package_status" -eq 52

"$compiler_bin" --emit-llvm -o "$field_ll" "$field_sample"
test -s "$field_ll"
"$clang_bin" "${host_clang_link_args[@]}" "$field_ll" -o "$field_bin" \
  $("$LLVM_CONFIG" --link-static --ldflags) \
  $("$LLVM_CONFIG" --link-static --libs all) \
  $("$LLVM_CONFIG" --link-static --system-libs) \
  $(jiang_macos_sdkroot_link_args) \
  $(jiang_llvm_cxx_runtime_link_args)
"$field_bin"

echo "OK"
