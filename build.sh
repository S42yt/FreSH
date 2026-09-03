#!/bin/bash
set -e

CC=${CC:-gcc}
WINDRES=${WINDRES:-windres}
CFLAGS="-std=c11 -Wall -Wextra -Wno-unused-parameter"
CFLAGS="$CFLAGS -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables $FRESH_EXTRA_CFLAGS"
BUILD=build
HOT_FILES="exec expand vars util parser cmd_text commands regex"

green() { printf '\033[32m%s\033[0m\n' "$1"; }
info() { printf '\033[36m%s\033[0m\n' "$1"; }

windows=0
case "${OS:-}$(uname -s 2> /dev/null)" in
    Windows_NT*|*MINGW*|*MSYS*|*CYGWIN*) windows=1 ;;
esac

if [ "$windows" = 1 ]; then
    CFLAGS="$CFLAGS -D_WIN32_WINNT=0x0601"
    LDFLAGS="-s -Wl,--gc-sections"
    RUST_TARGET=${FRESH_RUST_TARGET:-x86_64-pc-windows-gnu}
    OUTPUT="$BUILD/FreSH.exe"
    if ! command -v "$WINDRES" > /dev/null 2>&1 && command -v llvm-windres > /dev/null 2>&1; then
        WINDRES=llvm-windres
    fi
else
    CC=${FRESH_CC:-cc}
    CFLAGS="$CFLAGS -D_GNU_SOURCE"
    host=$(uname -s)
    case "$host" in
        Darwin) LDFLAGS="-Wl,-dead_strip" ;;
        *) LDFLAGS="-s -Wl,--gc-sections" ;;
    esac
    if [ -n "$FRESH_TARGET_ARCH" ] && [ "$host" = Darwin ]; then
        CFLAGS="$CFLAGS -arch $FRESH_TARGET_ARCH"
        LDFLAGS="$LDFLAGS -arch $FRESH_TARGET_ARCH"
        case "$FRESH_TARGET_ARCH" in
            arm64) rust_arch=aarch64 ;;
            *) rust_arch=$FRESH_TARGET_ARCH ;;
        esac
        RUST_TARGET=${FRESH_RUST_TARGET:-$rust_arch-apple-darwin}
    else
        RUST_TARGET=${FRESH_RUST_TARGET:-$(rustc -vV 2> /dev/null | sed -n 's/^host: //p')}
    fi
    if [ "$FRESH_STATIC" = 1 ] && [ "$host" != Darwin ]; then
        LDFLAGS="$LDFLAGS -static"
    fi
    OUTPUT="$BUILD/fresh"
fi

mkdir -p "$BUILD"

if [ "$windows" = 1 ]; then
    info "Generating the application icon..."
    $CC -O2 -o "$BUILD/makeicon.exe" tools/makeicon.c -lm
    "./$BUILD/makeicon.exe" "$BUILD/fresh.ico"
    cp "$BUILD/fresh.ico" src/fresh.ico
    cp "$BUILD/fresh.ico" installation/fresh.ico
fi

RUST_LIB=rust/core/target/$RUST_TARGET/release/libfresh_core.a

if [ ! -f "$RUST_LIB" ] && command -v cargo > /dev/null 2>&1; then
    info "Building the Rust core..."
    (cd rust/core && cargo build --release --target "$RUST_TARGET")
fi
if [ ! -f "$RUST_LIB" ]; then
    printf '\033[31m%s\033[0m\n' "FreSH needs its Rust core. Install rust, then: cargo build --release --target $RUST_TARGET, in rust/core" >&2
    exit 1
fi
green "  $RUST_LIB"

info "Building FreSH..."
RESOURCES=""
if [ "$windows" = 1 ]; then
    $WINDRES src/fresh.rc -O coff -o "$BUILD/fresh.res"
    RESOURCES="$BUILD/fresh.res"
fi

rm -rf "$BUILD/obj"
mkdir -p "$BUILD/obj"
OBJECTS=""
for source in src/*.c; do
    name=$(basename "$source" .c)
    level=-Os
    case " $HOT_FILES " in
        *" $name "*) level=-O2 ;;
    esac
    $CC $CFLAGS $level -c "$source" -o "$BUILD/obj/$name.o"
    OBJECTS="$OBJECTS $BUILD/obj/$name.o"
done

if [ "$windows" = 1 ]; then
    $CC $OBJECTS $RESOURCES $RUST_LIB -o "$OUTPUT" $LDFLAGS
else
    $CC $OBJECTS $RUST_LIB -lpthread -lm -o "$OUTPUT" $LDFLAGS
    if [ "$host" = Darwin ] && command -v strip > /dev/null 2>&1; then strip "$OUTPUT"; fi
fi
green "  $OUTPUT"

if [ "$windows" != 1 ]; then
    info "Building payload generator..."
    $CC -O2 -o "$BUILD/bin2c" tools/bin2c.c

    info "Embedding fresh into the installer..."
    "./$BUILD/bin2c" "$OUTPUT" installation/posix/payload.h FRESH_PAYLOAD

    info "Building installer..."
    version=$(sed -n 's/#define FRESH_VERSION "\(.*\)"/\1/p' src/shell.h)
    $CC $CFLAGS -Os -DFRESH_VERSION="\"$version\"" -Iinstallation/posix installation/posix/setup.c \
        -o "$BUILD/fresh-setup" $LDFLAGS
    if [ "$host" = Darwin ] && command -v strip > /dev/null 2>&1; then strip "$BUILD/fresh-setup"; fi
    green "  $BUILD/fresh-setup"
    green "Build complete."
    exit 0
fi

info "Building payload generator..."
$CC -O2 -o "$BUILD/bin2c.exe" tools/bin2c.c

info "Embedding FreSH.exe into the installer..."
"./$BUILD/bin2c.exe" "$BUILD/FreSH.exe" installation/payload.h FRESH_PAYLOAD

info "Building installer..."
$WINDRES installation/setup.rc -O coff -o "$BUILD/setup.res"
$CC $CFLAGS -Os installation/*.c "$BUILD/setup.res" -o "$BUILD/FreSH-Setup.exe" $LDFLAGS \
    -lole32 -luuid -lshell32 -ladvapi32 -luser32
green "  $BUILD/FreSH-Setup.exe"

green "Build complete."
