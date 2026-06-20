#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
SMOKE_BUILD_DIR="$BUILD_DIR/smoke/stage2_backend_cli"

source "$ROOT_DIR/script/llvm_env.sh"

mkdir -p "$SMOKE_BUILD_DIR"
cd "$ROOT_DIR"

COMPILER_UNDER_TEST="${JIANGC:-}"
if [ -z "$COMPILER_UNDER_TEST" ]; then
  COMPILER_UNDER_TEST="$(command -v jiangc || true)"
  if [ -z "$COMPILER_UNDER_TEST" ] || [ ! -x "$COMPILER_UNDER_TEST" ]; then
    echo "missing compiler: set JIANGC or put jiangc on PATH" >&2
    echo "install Jiang 0.2 so jiangc is on PATH" >&2
    exit 2
  fi
  COMPILER_VERSION="$("$COMPILER_UNDER_TEST" --version | sed -n '1p')"
  case "$COMPILER_VERSION" in
    "jiang 0.2"|"jiang 0.2."*) ;;
    *)
      echo "unsupported bootstrap compiler: $COMPILER_VERSION" >&2
      echo "install Jiang 0.2 so jiangc is on PATH, or pass JIANGC=<compiler>" >&2
      exit 2
      ;;
  esac
elif [ ! -x "$COMPILER_UNDER_TEST" ]; then
  echo "missing compiler: $COMPILER_UNDER_TEST" >&2
  exit 2
fi
COMPILER_VERSION="$("$COMPILER_UNDER_TEST" --version | sed -n '1p')"

clang_bin="$LLVM_CLANG"
compiler_ll="$SMOKE_BUILD_DIR/jiangc.ll"
compiler_bin="$SMOKE_BUILD_DIR/jiangc"
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
system_fs_sample="$SMOKE_BUILD_DIR/system_fs_target.jiang"
system_fs_linux_ll="$SMOKE_BUILD_DIR/system_fs_linux.ll"
system_fs_linux_obj="$SMOKE_BUILD_DIR/system_fs_linux.o"
system_fs_linux_no_libc_ll="$SMOKE_BUILD_DIR/system_fs_linux_no_libc.ll"
system_fs_linux_no_libc_obj="$SMOKE_BUILD_DIR/system_fs_linux_no_libc.o"
alloc_sample="$SMOKE_BUILD_DIR/no_libc_alloc.jiang"
alloc_no_libc_ll="$SMOKE_BUILD_DIR/no_libc_alloc.ll"
alloc_no_libc_obj="$SMOKE_BUILD_DIR/no_libc_alloc.o"
alloc_linux_no_libc_ll="$SMOKE_BUILD_DIR/no_libc_alloc_linux.ll"
alloc_linux_no_libc_obj="$SMOKE_BUILD_DIR/no_libc_alloc_linux.o"
alloc_wasm_ll="$SMOKE_BUILD_DIR/no_libc_alloc_wasm.ll"
macos_target_ll="$SMOKE_BUILD_DIR/minimal_macos.ll"
macos_target_obj="$SMOKE_BUILD_DIR/minimal_macos.o"
macos_target_bin="$SMOKE_BUILD_DIR/minimal_macos"
linux_target_ll="$SMOKE_BUILD_DIR/minimal_linux.ll"
linux_target_obj="$SMOKE_BUILD_DIR/minimal_linux.o"
windows_target_ll="$SMOKE_BUILD_DIR/minimal_windows.ll"
windows_target_obj="$SMOKE_BUILD_DIR/minimal_windows.obj"
wasm_target_ll="$SMOKE_BUILD_DIR/minimal_wasm.ll"
wasm_target_obj="$SMOKE_BUILD_DIR/minimal_wasm.o"
linux_no_libc_exe_log="$SMOKE_BUILD_DIR/linux_no_libc_executable.log"
windows_exe_log="$SMOKE_BUILD_DIR/windows_executable.log"
wasm_exe_log="$SMOKE_BUILD_DIR/wasm_executable.log"
unsupported_target_log="$SMOKE_BUILD_DIR/unsupported_target.log"

printf 'Int main() { 0 }\n' >"$sample"
printf 'struct Pair { Int left; Int right; }\nInt get_left(Pair p) { p.left }\nInt main() { 0 }\n' >"$field_sample"
printf 'import fs = "../../../src/system/fs.jiang";\nInt main() { if (fs.exists("/tmp")) { 0 } else { 1 } }\n' >"$system_fs_sample"
printf 'Int main() { Int![*] values = Int!$.alloc_many(2); values[0] = 1; values$.free(); 0 }\n' >"$alloc_sample"

printf '== backend cli smoke: build compiler with %s (%s) ==\n' "$COMPILER_UNDER_TEST" "$COMPILER_VERSION"
"$COMPILER_UNDER_TEST" --emit-llvm src/jiangc.jiang >"$compiler_ll"
"$clang_bin" "$compiler_ll" -o "$compiler_bin" \
  $("$LLVM_CONFIG" --ldflags) \
  $("$LLVM_CONFIG" --libs all) \
  $("$LLVM_CONFIG" --system-libs)

"$compiler_bin" --emit-llvm -o "$sample_ll" "$sample"
test -s "$sample_ll"
"$clang_bin" "$sample_ll" -o "$sample_from_ll" \
  $("$LLVM_CONFIG" --ldflags) \
  $("$LLVM_CONFIG" --libs all) \
  $("$LLVM_CONFIG" --system-libs)
"$sample_from_ll"

"$compiler_bin" --emit-obj -o "$sample_obj" "$sample"
test -s "$sample_obj"
"$clang_bin" "$sample_obj" -o "$sample_from_obj" \
  $("$LLVM_CONFIG" --ldflags) \
  $("$LLVM_CONFIG" --libs all) \
  $("$LLVM_CONFIG" --system-libs)
"$sample_from_obj"

"$compiler_bin" --no-link-libc --emit-llvm -o "$alloc_no_libc_ll" "$alloc_sample"
test -s "$alloc_no_libc_ll"
grep -q "__jiang_malloc" "$alloc_no_libc_ll"
grep -q "__jiang_free" "$alloc_no_libc_ll"
if grep -q "@malloc" "$alloc_no_libc_ll"; then
  echo "unexpected malloc reference in --no-link-libc LLVM output" >&2
  exit 1
fi
if grep -q "@free" "$alloc_no_libc_ll"; then
  echo "unexpected free reference in --no-link-libc LLVM output" >&2
  exit 1
fi
"$compiler_bin" --no-link-libc --emit-obj -o "$alloc_no_libc_obj" "$alloc_sample"
test -s "$alloc_no_libc_obj"
nm -u "$alloc_no_libc_obj" | grep -q "___jiang_malloc"
nm -u "$alloc_no_libc_obj" | grep -q "___jiang_free"
if nm -u "$alloc_no_libc_obj" | grep -q "^_malloc$"; then
  echo "unexpected malloc reference in --no-link-libc object output" >&2
  exit 1
fi
if nm -u "$alloc_no_libc_obj" | grep -q "^_free$"; then
  echo "unexpected free reference in --no-link-libc object output" >&2
  exit 1
fi

"$compiler_bin" --target x86_64-unknown-linux-gnu --no-link-libc --emit-llvm -o "$alloc_linux_no_libc_ll" "$alloc_sample"
test -s "$alloc_linux_no_libc_ll"
grep -q "__jiang_malloc" "$alloc_linux_no_libc_ll"
grep -q "__jiang_free" "$alloc_linux_no_libc_ll"
if grep -q "@malloc" "$alloc_linux_no_libc_ll"; then
  echo "unexpected malloc reference in linux --no-link-libc LLVM output" >&2
  exit 1
fi
if grep -q "@free" "$alloc_linux_no_libc_ll"; then
  echo "unexpected free reference in linux --no-link-libc LLVM output" >&2
  exit 1
fi
"$compiler_bin" --target x86_64-unknown-linux-gnu --no-link-libc --emit-obj -o "$alloc_linux_no_libc_obj" "$alloc_sample"
test -s "$alloc_linux_no_libc_obj"
nm -u "$alloc_linux_no_libc_obj" | grep -q "__jiang_malloc"
nm -u "$alloc_linux_no_libc_obj" | grep -q "__jiang_free"
if nm -u "$alloc_linux_no_libc_obj" | grep -q " malloc$"; then
  echo "unexpected malloc reference in linux --no-link-libc object output" >&2
  exit 1
fi
if nm -u "$alloc_linux_no_libc_obj" | grep -q " free$"; then
  echo "unexpected free reference in linux --no-link-libc object output" >&2
  exit 1
fi

"$compiler_bin" --target wasm32-unknown-unknown --emit-llvm -o "$alloc_wasm_ll" "$alloc_sample"
test -s "$alloc_wasm_ll"
grep -q "__jiang_malloc" "$alloc_wasm_ll"
grep -q "__jiang_free" "$alloc_wasm_ll"
if grep -q "@malloc" "$alloc_wasm_ll"; then
  echo "unexpected malloc reference in wasm LLVM output" >&2
  exit 1
fi
if grep -q "@free" "$alloc_wasm_ll"; then
  echo "unexpected free reference in wasm LLVM output" >&2
  exit 1
fi

"$compiler_bin" --target arm64-apple-macosx --emit-llvm -o "$macos_target_ll" "$sample"
test -s "$macos_target_ll"
grep -q 'target triple = "arm64-apple-macosx11.0.0"' "$macos_target_ll"
grep -q 'target datalayout = ' "$macos_target_ll"
"$compiler_bin" --target arm64-apple-macosx --emit-obj -o "$macos_target_obj" "$sample"
test -s "$macos_target_obj"
file "$macos_target_obj" | grep -q "Mach-O 64-bit object arm64"
"$compiler_bin" --target arm64-apple-macosx -o "$macos_target_bin" "$sample"
"$macos_target_bin"

"$compiler_bin" --target x86_64-unknown-linux-gnu --emit-llvm -o "$linux_target_ll" "$sample"
test -s "$linux_target_ll"
grep -q 'target triple = "x86_64-unknown-linux-gnu"' "$linux_target_ll"
grep -q 'target datalayout = ' "$linux_target_ll"
"$compiler_bin" --target x86_64-unknown-linux-gnu --emit-obj -o "$linux_target_obj" "$sample"
test -s "$linux_target_obj"
file "$linux_target_obj" | grep -q "ELF 64-bit.*x86-64"

"$compiler_bin" --target x86_64-unknown-linux-gnu --emit-llvm -o "$system_fs_linux_ll" "$system_fs_sample"
test -s "$system_fs_linux_ll"
grep -q 'target triple = "x86_64-unknown-linux-gnu"' "$system_fs_linux_ll"
"$compiler_bin" --target x86_64-unknown-linux-gnu --emit-obj -o "$system_fs_linux_obj" "$system_fs_sample"
test -s "$system_fs_linux_obj"
file "$system_fs_linux_obj" | grep -q "ELF 64-bit.*x86-64"

"$compiler_bin" --target x86_64-unknown-linux-gnu --no-link-libc --emit-llvm -o "$system_fs_linux_no_libc_ll" "$system_fs_sample"
test -s "$system_fs_linux_no_libc_ll"
grep -q 'target triple = "x86_64-unknown-linux-gnu"' "$system_fs_linux_no_libc_ll"
if grep -Eq "@(getenv|posix_spawn|opendir|memcpy|memset)\\b" "$system_fs_linux_no_libc_ll"; then
  echo "unexpected hosted/libc symbol in linux no-libc system provider LLVM output" >&2
  exit 1
fi
"$compiler_bin" --target x86_64-unknown-linux-gnu --no-link-libc --emit-obj -o "$system_fs_linux_no_libc_obj" "$system_fs_sample"
test -s "$system_fs_linux_no_libc_obj"
file "$system_fs_linux_no_libc_obj" | grep -q "ELF 64-bit.*x86-64"

"$compiler_bin" --target x86_64-pc-windows-msvc --emit-llvm -o "$windows_target_ll" "$sample"
test -s "$windows_target_ll"
grep -q 'target triple = "x86_64-pc-windows-msvc"' "$windows_target_ll"
grep -q 'target datalayout = ' "$windows_target_ll"
"$compiler_bin" --target x86_64-pc-windows-msvc --emit-obj -o "$windows_target_obj" "$sample"
test -s "$windows_target_obj"
file "$windows_target_obj" | grep -q "COFF"

"$compiler_bin" --target wasm32-unknown-unknown --emit-llvm -o "$wasm_target_ll" "$sample"
test -s "$wasm_target_ll"
grep -q 'target triple = "wasm32-unknown-unknown"' "$wasm_target_ll"
grep -q 'target datalayout = ' "$wasm_target_ll"
"$compiler_bin" --target wasm32-unknown-unknown --emit-obj -o "$wasm_target_obj" "$sample"
test -s "$wasm_target_obj"
file "$wasm_target_obj" | grep -qi "WebAssembly"

set +e
"$compiler_bin" --target x86_64-unknown-linux-gnu --no-link-libc -o "$SMOKE_BUILD_DIR/minimal_linux_no_libc_exe" "$sample" >"$linux_no_libc_exe_log" 2>&1
linux_no_libc_exe_status=$?
set -e
test "$linux_no_libc_exe_status" -ne 0
grep -q "target_executable_requires_runtime" "$linux_no_libc_exe_log"

set +e
"$compiler_bin" --target x86_64-pc-windows-msvc -o "$SMOKE_BUILD_DIR/minimal_windows_exe" "$sample" >"$windows_exe_log" 2>&1
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
"$compiler_bin" --target x86_64-unknown-freebsd --emit-llvm -o "$SMOKE_BUILD_DIR/minimal_freebsd.ll" "$sample" >"$unsupported_target_log" 2>&1
unsupported_target_status=$?
set -e
test "$unsupported_target_status" -ne 0
grep -q "unsupported target triple: x86_64-unknown-freebsd" "$unsupported_target_log"

"$compiler_bin" --link-arg -Wl,-dead_strip -o "$sample_with_link_arg" "$sample"
"$sample_with_link_arg"

"$compiler_bin" -o "$system_env_bin" test/lang/system/run/env_get.jiang
"$system_env_bin"

"$compiler_bin" --mode release --emit-obj -o "$sample_release_obj" "$sample"
test -s "$sample_release_obj"
"$clang_bin" "$sample_release_obj" -o "$sample_from_release_obj" \
  $("$LLVM_CONFIG" --ldflags) \
  $("$LLVM_CONFIG" --libs all) \
  $("$LLVM_CONFIG" --system-libs)
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
"$clang_bin" "$field_ll" -o "$field_bin" \
  $("$LLVM_CONFIG" --ldflags) \
  $("$LLVM_CONFIG" --libs all) \
  $("$LLVM_CONFIG" --system-libs)
"$field_bin"

echo "OK"
