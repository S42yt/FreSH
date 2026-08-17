#!/bin/bash
set -e

CC=${CC:-gcc}
WINDRES=${WINDRES:-windres}
CFLAGS="-std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -D_WIN32_WINNT=0x0601"
CFLAGS="$CFLAGS -ffunction-sections -fdata-sections"
LDFLAGS="-s -Wl,--gc-sections"
BUILD=build

green() { printf '\033[32m%s\033[0m\n' "$1"; }
info() { printf '\033[36m%s\033[0m\n' "$1"; }

mkdir -p "$BUILD"

info "Generating the application icon..."
$CC -O2 -o "$BUILD/makeicon.exe" tools/makeicon.c -lm
"./$BUILD/makeicon.exe" "$BUILD/fresh.ico"
cp "$BUILD/fresh.ico" src/fresh.ico
cp "$BUILD/fresh.ico" installation/fresh.ico

RUST_LIB=""
RUST_TARGET=x86_64-pc-windows-gnu
if [ -z "$FRESH_NO_RUST" ] && command -v cargo > /dev/null 2>&1; then
    info "Building the Rust compute core..."
    (cd rust/core && cargo build --release --target "$RUST_TARGET")
    RUST_LIB="rust/core/target/$RUST_TARGET/release/libfresh_core.a"
    if [ -f "$RUST_LIB" ]; then
        CFLAGS="$CFLAGS -DFRESH_RUST"
        green "  $RUST_LIB"
    else
        RUST_LIB=""
        info "The Rust core did not build, carrying on with C only."
    fi
fi

info "Building FreSH..."
$WINDRES src/fresh.rc -O coff -o "$BUILD/fresh.res"
$CC $CFLAGS src/*.c "$BUILD/fresh.res" $RUST_LIB -o "$BUILD/FreSH.exe" $LDFLAGS -ladvapi32
green "  $BUILD/FreSH.exe"

info "Building payload generator..."
$CC -O2 -o "$BUILD/bin2c.exe" tools/bin2c.c

info "Embedding FreSH.exe into the installer..."
"./$BUILD/bin2c.exe" "$BUILD/FreSH.exe" installation/payload.h FRESH_PAYLOAD

info "Building installer..."
$WINDRES installation/setup.rc -O coff -o "$BUILD/setup.res"
$CC $CFLAGS installation/*.c "$BUILD/setup.res" -o "$BUILD/FreSH-Setup.exe" $LDFLAGS \
    -lole32 -luuid -lshell32 -ladvapi32 -luser32
green "  $BUILD/FreSH-Setup.exe"

green "Build complete."
