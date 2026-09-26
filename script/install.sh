#!/usr/bin/env bash
# 下载并校验官方发行包，调用包内安装器并为当前用户配置 PATH。
set -euo pipefail

fail() { printf 'Jiang 安装失败：%s\n' "$*" >&2; exit 1; }
require() { command -v "$1" >/dev/null 2>&1 || fail "缺少 $1，请先安装该工具。"; }

require curl
require awk
case "$(uname -s):$(uname -m)" in
  Darwin:arm64|Darwin:aarch64) platform=macos-arm64; extension=zip; require unzip ;;
  Linux:x86_64|Linux:amd64) platform=linux-x86_64; extension=tar.gz; require tar ;;
  *) fail "目前提供 macOS Apple Silicon 和 Linux x86_64 安装包。" ;;
esac
if command -v sha256sum >/dev/null 2>&1; then
  checksum=(sha256sum)
elif command -v shasum >/dev/null 2>&1; then
  checksum=(shasum -a 256)
else
  fail "缺少 SHA-256 工具（sha256sum 或 shasum）。"
fi

repository=https://github.com/jjcfun/jiang
version="${1:-}"
[ "$#" -le 1 ] || fail "用法：bash install.sh [版本号]"
if [ -z "$version" ]; then
  latest_url="$(curl --fail --silent --show-error --location --retry 3 \
    --output /dev/null --write-out '%{url_effective}' "$repository/releases/latest")"
  case "$latest_url" in
    "$repository/releases/tag/"*) version="${latest_url##*/}" ;;
    *) fail "无法确定最新发行版。" ;;
  esac
fi
case "$version" in
  ''|*[!A-Za-z0-9._+-]*|.*) fail "无效版本号：$version" ;;
esac
install_prefix="${PREFIX:-$HOME/.jiang}"
case "$install_prefix" in
  /*) ;;
  *) fail "PREFIX 必须是绝对路径。" ;;
esac

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/jiang-install.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
archive_name="jiang-$version-$platform.$extension"
release_url="$repository/releases/download/$version"
printf '安装 Jiang %s（%s）…\n' "$version" "$platform"
curl --fail --silent --show-error --location --retry 3 \
  --output "$work_dir/$archive_name" "$release_url/$archive_name"
curl --fail --silent --show-error --location --retry 3 \
  --output "$work_dir/SHA256SUMS" "$release_url/jiang-$version-SHA256SUMS"
expected="$(awk -v name="$archive_name" '$2 == name || $2 == "*" name { print $1 }' "$work_dir/SHA256SUMS")"
[[ "$expected" =~ ^[0-9a-fA-F]{64}$ ]] || fail "校验文件中没有唯一有效的 $archive_name 记录。"
actual="$("${checksum[@]}" "$work_dir/$archive_name")"
[ "${actual%% *}" = "$expected" ] || fail "发行包 SHA-256 校验不通过。"

if [ "$extension" = zip ]; then
  unzip -q "$work_dir/$archive_name" -d "$work_dir"
else
  tar -xzf "$work_dir/$archive_name" -C "$work_dir"
fi
package_dir="$work_dir/jiang-$version-$platform"
[ -f "$package_dir/install.sh" ] || fail "发行包缺少 install.sh。"
PREFIX="$install_prefix" bash "$package_dir/install.sh"

# env 文件同时供当前终端和 shell 启动文件读取；重复安装不重复添加 PATH。
printf 'case ":$PATH:" in\n  *:%q:*) ;;\n  *) export PATH=%q:"$PATH" ;;\nesac\n' \
  "$install_prefix/bin" "$install_prefix/bin" >"$install_prefix/env"
printf -v source_line '. %q' "$install_prefix/env"
profiles=()
installer_shell="${SHELL:-}"
case "${installer_shell##*/}" in
  zsh) profiles=("${ZDOTDIR:-$HOME}/.zprofile" "${ZDOTDIR:-$HOME}/.zshrc") ;;
  bash)
    login_profile="$HOME/.profile"
    if [ -f "$HOME/.bash_profile" ]; then login_profile="$HOME/.bash_profile"
    elif [ -f "$HOME/.bash_login" ]; then login_profile="$HOME/.bash_login"; fi
    profiles=("$login_profile" "$HOME/.bashrc")
    ;;
esac
for profile in ${profiles[@]+"${profiles[@]}"}; do
  if [ ! -f "$profile" ] || ! grep -Fqx "$source_line" "$profile"; then
    mkdir -p "$(dirname "$profile")"
    printf '\n# Jiang\n%s\n' "$source_line" >>"$profile"
  fi
done
"$install_prefix/bin/jiang" --version
printf '\n安装完成。当前终端执行：\n  %s\n' "$source_line"
if [ "${#profiles[@]}" -gt 0 ]; then
  printf '新终端可直接使用 jiang。\n'
fi
