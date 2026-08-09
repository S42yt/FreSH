#!/bin/bash
set -e

CC=${CC:-gcc}
CFLAGS="-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -D_WIN32_WINNT=0x0601"
BUILD=build

green() { printf '\033[32m%s\033[0m\n' "$1"; }
info() { printf '\033[36m%s\033[0m\n' "$1"; }

mkdir -p "$BUILD"

info "Building FreSH..."
$CC $CFLAGS src/*.c -o "$BUILD/FreSH.exe" -ladvapi32
green "  $BUILD/FreSH.exe"

info "Building payload generator..."
$CC -O2 -o "$BUILD/bin2c.exe" tools/bin2c.c

info "Embedding FreSH.exe into the installer..."
"./$BUILD/bin2c.exe" "$BUILD/FreSH.exe" installation/payload.h FRESH_PAYLOAD

info "Building installer..."
$CC $CFLAGS installation/*.c -o "$BUILD/FreSH-Setup.exe" -lole32 -luuid -lshell32 -ladvapi32 -luser32
green "  $BUILD/FreSH-Setup.exe"

green "Build complete."
