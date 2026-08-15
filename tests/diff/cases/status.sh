true
printf '%s\n' "$?"
false
printf '%s\n' "$?"

(exit 7)
printf '%s\n' "$?"

if false; then printf 'no\n'; fi
printf '%s\n' "$?"

true && printf 'and ran\n'
false || printf 'or ran\n'
printf '%s\n' "$?"

! false
printf '%s\n' "$?"

false | true
printf '%s\n' "$?"

set -o pipefail
false | true
printf '%s\n' "$?"
set +o pipefail

true | false | true
printf '%s\n' "${PIPESTATUS[0]}-${PIPESTATUS[1]}-${PIPESTATUS[2]}"

value=$(exit 4)
printf '%s\n' "$?"

fails() {
    return 3
}
fails
printf '%s\n' "$?"

if fails; then printf 'yes\n'; else printf 'no %s\n' "$?"; fi

subshell_status() {
    (exit 5)
    return $?
}
subshell_status
printf '%s\n' "$?"

printf 'before\n'
(
    set -e
    false
    printf 'unreachable\n'
)
printf 'after %s\n' "$?"

set -e
run_guarded() {
    false || true
    printf 'guarded ok\n'
}
run_guarded
set +e

exit 0
