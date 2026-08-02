#!/usr/bin/env bash
set -euo pipefail

BINARY="${1:-}"

if [ -z "$BINARY" ] || [ ! -x "$BINARY" ]; then
  echo "usage: bash ./script/linux_release_abi_audit.sh /path/to/executable" >&2
  exit 2
fi
if [ "$(uname -s)" != "Linux" ]; then
  echo "Linux release ABI audit requires a Linux host" >&2
  exit 2
fi
for command_name in readelf ldd sha256sum sort; do
  if ! command -v "$command_name" >/dev/null 2>&1; then
    echo "missing required command: $command_name" >&2
    exit 2
  fi
done

version_info="$(readelf --version-info "$BINARY")"
glibc_versions="$(sed -n 's/.*Name: GLIBC_\([0-9][0-9.]*\).*/\1/p' <<<"$version_info" | sort -Vu)"
minimum_glibc="$(tail -n 1 <<<"$glibc_versions")"
if [ -z "$minimum_glibc" ]; then
  echo "missing GLIBC symbol version requirements: $BINARY" >&2
  exit 1
fi

dynamic_section="$(readelf -d "$BINARY")"
dependencies="$(ldd "$BINARY")"
if grep -E 'lib(LLVM|lld)' <<<"$dependencies" >/dev/null; then
  echo "release compiler must not dynamically depend on LLVM/lld" >&2
  exit 1
fi

printf 'format=jiang-linux-abi-v1\n'
printf 'artifact=%s\n' "$(basename "$BINARY")"
printf 'machine=%s\n' "$(readelf -h "$BINARY" | sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p')"
printf 'interpreter=%s\n' "$(readelf -l "$BINARY" | sed -n 's/.*interpreter: \([^]]*\).*/\1/p')"
printf 'minimum_glibc=%s\n' "$minimum_glibc"
sed -n 's/.*Shared library: \[\([^]]*\)\].*/needed=\1/p' <<<"$dynamic_section"
printf 'sha256=%s\n' "$(sha256sum "$BINARY" | awk '{print $1}')"
