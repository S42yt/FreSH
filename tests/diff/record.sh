#!/usr/bin/env bash
set -u

shell=$1
outdir=$2

here=$(cd "$(dirname "$0")" && pwd)
cases=${CASES:-$here/cases}
mkdir -p "$outdir"
rm -f "$outdir"/*.out "$outdir"/*.status "$outdir"/*.stderr 2> /dev/null

for script in "$cases"/*.sh; do
    name=$(basename "$script" .sh)
    work="$outdir/work-$name"
    rm -rf "$work"
    mkdir -p "$work"

    target=$script
    if command -v cygpath > /dev/null 2>&1; then target=$(cygpath -w "$script"); fi

    (cd "$work" && LC_ALL=C TZ=UTC "$shell" "$target" < /dev/null) > "$outdir/$name.raw" 2> "$outdir/$name.err"
    status=$?

    tr -d '\r' < "$outdir/$name.raw" > "$outdir/$name.out"
    rm -f "$outdir/$name.raw"
    printf '%s\n' "$status" > "$outdir/$name.status"

    if [ -s "$outdir/$name.err" ]; then
        printf 'noisy\n' > "$outdir/$name.stderr"
    else
        printf 'quiet\n' > "$outdir/$name.stderr"
    fi

    rm -rf "$work"
    printf '  recorded %-14s status %s\n' "$name" "$status"
done
