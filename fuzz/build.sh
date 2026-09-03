#!/usr/bin/env bash
set -e

CC=${CC:-clang}
OUT=${OUT:-fuzz/build}

FLAGS="-g -O1 -std=c11 -Wall -Wextra -Wno-unused-parameter"
FLAGS="$FLAGS -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer"
FLAGS="$FLAGS -Ifuzz -Isrc -D_GNU_SOURCE -DFRESH_BORROWS"

mkdir -p "$OUT"

RUST_LIB=rust/core/target/release/libfresh_core.a
if [ ! -f "$RUST_LIB" ]; then
    (cd rust/core && cargo build --release)
fi

COMMON="src/util.c src/platform_posix.c $RUST_LIB"

$CC $FLAGS fuzz/fuzz_parser.c src/parser.c $COMMON -o "$OUT/fuzz_parser"
$CC $FLAGS fuzz/fuzz_regex.c src/regex.c $COMMON -o "$OUT/fuzz_regex"
$CC $FLAGS fuzz/fuzz_awk.c fuzz/stubs.c src/awk.c src/regex.c $COMMON -o "$OUT/fuzz_awk"

echo "built $OUT/fuzz_parser $OUT/fuzz_regex $OUT/fuzz_awk"
