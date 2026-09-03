#!/bin/sh
set -e

repo="S42yt/FreSH"
version=${FRESH_VERSION:-}

os=$(uname -s)
arch=$(uname -m)
case "$os" in
    Darwin) os=macos ;;
    Linux) os=linux ;;
    *) echo "install.sh knows macOS and Linux; on Windows download FreSH-Setup.exe from https://github.com/$repo/releases" >&2; exit 1 ;;
esac
case "$arch" in
    x86_64|amd64) arch=x86_64 ;;
    arm64|aarch64) arch=arm64 ;;
    *) echo "no FreSH build for $arch yet, see https://github.com/$repo/releases" >&2; exit 1 ;;
esac

asset="fresh-setup-$os-$arch"
if [ -n "$version" ]; then
    url="https://github.com/$repo/releases/download/v$version/$asset"
else
    url="https://github.com/$repo/releases/latest/download/$asset"
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/fresh-setup.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

echo "downloading $url"
curl -fsSL -o "$tmp/$asset" "$url"
chmod +x "$tmp/$asset"

if [ $# -gt 0 ]; then
    exec "$tmp/$asset" "$@"
elif [ -t 0 ]; then
    exec "$tmp/$asset"
else
    exec "$tmp/$asset" --silent --user
fi
