#!/usr/bin/env bash
set -euo pipefail

# 为统一测试 runner 自测模拟 jiangc，只实现 runner 会使用的命令行契约。

artifact_cache_dir=""
output=""
source_path=""
emit_llvm=0
check_only=0
release_mode=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --artifact-cache-dir)
      artifact_cache_dir="$2"
      shift 2
      ;;
    --emit-llvm)
      emit_llvm=1
      shift
      ;;
    --check)
      check_only=1
      shift
      ;;
    --mode)
      if [ "$2" = "release" ]; then
        release_mode=1
      fi
      shift 2
      ;;
    --link-arg|--linker)
      shift 2
      ;;
    -o)
      output="$2"
      shift 2
      ;;
    *.jiang)
      source_path="$1"
      shift
      ;;
    *)
      shift
      ;;
  esac
done

if [ -z "$artifact_cache_dir" ] || [ -z "$source_path" ]; then
  echo "fake jiangc: missing cache or source" >&2
  exit 2
fi

mkdir -p "$artifact_cache_dir"
printf '%s|%s\n' "$source_path" "$artifact_cache_dir" >>"${FAKE_JIANGC_LOG:?}"

case "$source_path" in
  *slow_success.jiang)
    sleep 1
    ;;
  *hang.jiang)
    echo "compiler output before timeout" >&2
    sleep "${FAKE_JIANGC_HANG_SECONDS:-3}"
    ;;
esac

case "$source_path" in
  *compile_error.jiang)
    echo "E_COMPILE: requested compiler failure" >&2
    exit 9
    ;;
esac

if [ "$check_only" = "1" ]; then
  case "$source_path" in
    *expected_failure.jiang)
      echo "E_EXPECTED: requested diagnostic" >&2
      exit 1
      ;;
  esac
  exit 0
fi

expected_exit="$(sed -n 's/^.*expected-exit:[[:space:]]*//p' "$source_path" | head -n 1)"
expected_exit="${expected_exit:-0}"

if [ "$emit_llvm" = "1" ]; then
  printf 'exit=%s\n' "$expected_exit" >"$output"
  exit 0
fi

if [ "$release_mode" = "1" ]; then
  printf '#!/usr/bin/env bash\nexit %s\n' "$expected_exit" >"$output"
  chmod +x "$output"
  exit 0
fi

echo "fake jiangc: unsupported invocation" >&2
exit 2
