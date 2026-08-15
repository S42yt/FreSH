greet() {
    printf 'hello %s\n' "${1:-world}"
}
greet
greet there

count() {
    printf '%s\n' "$#"
    printf '[%s]' "$@"
    printf '\n'
}
count
count one "two three" four

nested_outer() {
    nested_inner "$@"
}
nested_inner() {
    printf 'inner got %s\n' "$*"
}
nested_outer a b

returns() {
    return 3
}
returns
printf '%s\n' "$?"

echoes() {
    printf 'value\n'
}
captured=$(echoes)
printf '[%s]\n' "$captured"

scoped() {
    local v=inner
    deeper
    printf 'after %s\n' "$v"
}
deeper() {
    printf 'sees %s\n' "${v:-nothing}"
}
v=outer
scoped
printf 'outer %s\n' "$v"

shadow() {
    local shared=local
    printf '%s\n' "$shared"
}
shared=global
shadow
printf '%s\n' "$shared"

factorial() {
    if [ "$1" -le 1 ]; then
        printf '1\n'
        return
    fi
    local previous=$(factorial $(( $1 - 1 )))
    printf '%s\n' "$(( $1 * previous ))"
}
factorial 6

set -- keep these
uses_params() {
    printf 'inside %s\n' "$#"
}
uses_params a
printf 'outside %s\n' "$#"

with_default() {
    local name=${1:-anonymous}
    local count=${2:-0}
    printf '%s:%s\n' "$name" "$count"
}
with_default
with_default given 4

table() {
    printf '%-6s|%4s|\n' "$1" "$2"
}
table ab 12
table abcdefgh 123456

unset -f greet
if command -v greet > /dev/null 2>&1; then printf 'still here\n'; else printf 'gone\n'; fi
