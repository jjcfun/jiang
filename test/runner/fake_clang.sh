#!/usr/bin/env bash
set -euo pipefail

# 为统一测试 runner 自测模拟 clang，把 fake LLVM 文件转换成可执行脚本。

input=""
output=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    -o)
      output="$2"
      shift 2
      ;;
    *.ll)
      input="$1"
      shift
      ;;
    *)
      shift
      ;;
  esac
done

if [ -z "$input" ] || [ -z "$output" ]; then
  echo "fake clang: missing input or output" >&2
  exit 2
fi

expected_exit="$(sed -n 's/^exit=//p' "$input" | head -n 1)"
expected_exit="${expected_exit:-0}"
printf '#!/usr/bin/env bash\nexit %s\n' "$expected_exit" >"$output"
chmod +x "$output"
