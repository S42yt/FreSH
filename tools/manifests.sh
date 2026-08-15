#!/usr/bin/env bash
set -e

version=$1
setup_hash=$2
plain_hash=$3

if [ -z "$version" ] || [ -z "$setup_hash" ] || [ -z "$plain_hash" ]; then
    echo "usage: tools/manifests.sh <version> <setup sha256> <exe sha256>" >&2
    exit 2
fi

setup_upper=$(printf '%s' "$setup_hash" | tr 'a-f' 'A-F')
plain_upper=$(printf '%s' "$plain_hash" | tr 'a-f' 'A-F')
today=$(date -u +%Y-%m-%d)

winget() {
    sed -i \
        -e "s/^\(PackageVersion:\) .*/\1 $version/" \
        -e "s/^\(ReleaseDate:\) .*/\1 $today/" \
        -e "s|/download/v[^/]*/|/download/v$version/|" \
        -e "s/^\( *InstallerSha256:\) .*/\1 $2/" \
        -e "s|/releases/tag/v.*|/releases/tag/v$version|" \
        "$1"
}

scoop() {
    sed -i \
        -e "s/^\( *\"version\": \).*/\1\"$version\",/" \
        -e "s|/download/v[^/]*/|/download/v$version/|" \
        -e "s/^\( *\"hash\": \)\".*\"/\1\"$2\"/" \
        "$1"
}

winget manifests/S42yt.FreSH.yaml "$setup_upper"
winget manifests/S42yt.FreSH.locale.en-US.yaml "$setup_upper"
winget manifests/S42yt.FreSH.installer.yaml "$setup_upper"
winget manifests/S42yt.FreSH.Portable.yaml "$plain_upper"
winget manifests/S42yt.FreSH.Portable.locale.en-US.yaml "$plain_upper"
winget manifests/S42yt.FreSH.Portable.installer.yaml "$plain_upper"

scoop bucket/fresh.json "$setup_hash"
scoop bucket/fresh-portable.json "$plain_hash"

echo "manifests stamped at $version"
