#!/usr/bin/env bash
set -e

version=$1
setup_hash=$2
plain_hash=$3
setup_arm64_hash=$4
plain_arm64_hash=$5

if [ -z "$version" ] || [ -z "$setup_hash" ] || [ -z "$plain_hash" ] ||
   [ -z "$setup_arm64_hash" ] || [ -z "$plain_arm64_hash" ]; then
    echo "usage: tools/manifests.sh <version> <setup sha256> <exe sha256> <arm64 setup sha256> <arm64 exe sha256>" >&2
    exit 2
fi

upper() { printf '%s' "$1" | tr 'a-f' 'A-F'; }
today=$(date -u +%Y-%m-%d)
download="https://github.com/S42yt/FreSH/releases/download/v$version"

stamp() {
    sed -i \
        -e "s/^\(PackageVersion:\) .*/\1 $version/" \
        -e "s/^\(ReleaseDate:\) .*/\1 $today/" \
        -e "s|/releases/tag/v.*|/releases/tag/v$version|" \
        "$1"
}

promote() {
    local package=$1

    local latest
    latest=$(ls -d "manifests/$package"/*/ | sort -V | tail -n 1)
    local target="manifests/$package/$version"

    if [ "$latest" != "$target/" ]; then
        mkdir -p "$target"
        cp "$latest"*.yaml "$target/"
    fi

    for file in "$target"/*.yaml; do
        stamp "$file"
    done
}

promote S42yt.FreSH
promote S42yt.FreSH.Portable

cat > "manifests/S42yt.FreSH/$version/S42yt.FreSH.installer.yaml" <<EOF
PackageIdentifier: S42yt.FreSH
PackageVersion: $version
InstallerType: exe
Scope: user
InstallModes:
  - interactive
  - silent
  - silentWithProgress
InstallerSwitches:
  Silent: /silent /user
  SilentWithProgress: /silent /user
UpgradeBehavior: install
ReleaseDate: $today
Installers:
  - Architecture: x64
    InstallerUrl: $download/FreSH-Setup.exe
    InstallerSha256: $(upper "$setup_hash")
  - Architecture: arm64
    InstallerUrl: $download/FreSH-Setup-arm64.exe
    InstallerSha256: $(upper "$setup_arm64_hash")
ManifestType: installer
ManifestVersion: 1.6.0
EOF

cat > "manifests/S42yt.FreSH.Portable/$version/S42yt.FreSH.Portable.installer.yaml" <<EOF
PackageIdentifier: S42yt.FreSH.Portable
PackageVersion: $version
InstallerType: portable
Commands:
  - FreSH
UpgradeBehavior: install
ReleaseDate: $today
Installers:
  - Architecture: x64
    InstallerUrl: $download/FreSH.exe
    InstallerSha256: $(upper "$plain_hash")
  - Architecture: arm64
    InstallerUrl: $download/FreSH-arm64.exe
    InstallerSha256: $(upper "$plain_arm64_hash")
ManifestType: installer
ManifestVersion: 1.6.0
EOF

cat > bucket/fresh.json <<EOF
{
    "version": "$version",
    "description": "A fast, zsh flavoured shell for Windows, written in C with no runtime dependencies.",
    "homepage": "https://github.com/S42yt/FreSH",
    "license": "GPL-3.0-only",
    "architecture": {
        "64bit": {
            "url": "$download/FreSH-Setup.exe#/setup.exe",
            "hash": "$setup_hash"
        },
        "arm64": {
            "url": "$download/FreSH-Setup-arm64.exe#/setup.exe",
            "hash": "$setup_arm64_hash"
        }
    },
    "installer": {
        "script": [
            "Start-Process -FilePath \\"\$dir\\\\setup.exe\\" -ArgumentList '/silent','/user' -Wait"
        ]
    },
    "uninstaller": {
        "script": [
            "\$u = \\"\$env:LOCALAPPDATA\\\\FreSH\\\\Uninstall-FreSH.exe\\"",
            "if (Test-Path \$u) { Start-Process -FilePath \$u -ArgumentList '/silent' -Wait }"
        ]
    },
    "checkver": {
        "github": "https://github.com/S42yt/FreSH"
    },
    "autoupdate": {
        "architecture": {
            "64bit": {
                "url": "https://github.com/S42yt/FreSH/releases/download/v\$version/FreSH-Setup.exe#/setup.exe"
            },
            "arm64": {
                "url": "https://github.com/S42yt/FreSH/releases/download/v\$version/FreSH-Setup-arm64.exe#/setup.exe"
            }
        },
        "hash": {
            "url": "https://github.com/S42yt/FreSH/releases/download/v\$version/SHA256SUMS.txt"
        }
    }
}
EOF

cat > bucket/fresh-portable.json <<EOF
{
    "version": "$version",
    "description": "FreSH portable - a fast, zsh flavoured shell for Windows. Single executable, no installer.",
    "homepage": "https://github.com/S42yt/FreSH",
    "license": "GPL-3.0-only",
    "architecture": {
        "64bit": {
            "url": "$download/FreSH.exe",
            "hash": "$plain_hash"
        },
        "arm64": {
            "url": "$download/FreSH-arm64.exe#/FreSH.exe",
            "hash": "$plain_arm64_hash"
        }
    },
    "bin": "FreSH.exe",
    "shortcuts": [
        ["FreSH.exe", "FreSH"]
    ],
    "checkver": {
        "github": "https://github.com/S42yt/FreSH"
    },
    "autoupdate": {
        "architecture": {
            "64bit": {
                "url": "https://github.com/S42yt/FreSH/releases/download/v\$version/FreSH.exe"
            },
            "arm64": {
                "url": "https://github.com/S42yt/FreSH/releases/download/v\$version/FreSH-arm64.exe#/FreSH.exe"
            }
        },
        "hash": {
            "url": "https://github.com/S42yt/FreSH/releases/download/v\$version/SHA256SUMS.txt"
        }
    }
}
EOF

echo "manifests stamped at $version"
