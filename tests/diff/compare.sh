#!/usr/bin/env bash
set -u

expected=$1
actual=$2

here=$(cd "$(dirname "$0")" && pwd)
known=${KNOWN:-$here/known-differences.txt}
failed=0
skipped=0
passed=0

for reference in "$expected"/*.out; do
    name=$(basename "$reference" .out)

    if [ -f "$known" ] && grep -q "^$name[[:space:]]" "$known"; then
        reason=$(grep "^$name[[:space:]]" "$known" | head -n 1 | cut -f2-)
        printf '  skip  %-14s %s\n' "$name" "$reason"
        skipped=$((skipped + 1))
        continue
    fi

    if [ ! -f "$actual/$name.out" ]; then
        printf '  FAIL  %-14s FreSH produced no output file\n' "$name"
        failed=$((failed + 1))
        continue
    fi

    trouble=""
    if ! diff -u "$reference" "$actual/$name.out" > "$actual/$name.diff"; then
        trouble="stdout"
    fi
    if [ "$(cat "$expected/$name.status")" != "$(cat "$actual/$name.status")" ]; then
        trouble="${trouble:+$trouble and }exit status"
    fi
    if [ "$(cat "$expected/$name.stderr")" != "$(cat "$actual/$name.stderr")" ]; then
        trouble="${trouble:+$trouble and }stderr"
    fi

    if [ -z "$trouble" ]; then
        printf '  ok    %-14s\n' "$name"
        passed=$((passed + 1))
        continue
    fi

    printf '  FAIL  %-14s differs from the reference in %s\n' "$name" "$trouble"
    printf '        reference exited %s, FreSH exited %s, reference stderr %s, FreSH stderr %s\n' \
        "$(cat "$expected/$name.status")" "$(cat "$actual/$name.status")" \
        "$(cat "$expected/$name.stderr")" "$(cat "$actual/$name.stderr")"
    sed 's/^/        /' "$actual/$name.diff" | head -n ${DIFF_LINES:-40}
    if [ -s "$actual/$name.err" ]; then
        printf '        FreSH said:\n'
        sed 's/^/          /' "$actual/$name.err" | head -n 10
    fi
    failed=$((failed + 1))
done

printf '\n  %d matched, %d differed, %d known differences\n' "$passed" "$failed" "$skipped"
[ "$failed" -eq 0 ]
