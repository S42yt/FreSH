for i in one two three; do
    printf 'for %s\n' "$i"
done

i=0
while [ "$i" -lt 3 ]; do
    printf 'while %s\n' "$i"
    i=$((i + 1))
done

i=0
until [ "$i" -ge 2 ]; do
    printf 'until %s\n' "$i"
    i=$((i + 1))
done

for i in 1 2 3 4 5; do
    if [ "$i" -eq 2 ]; then continue; fi
    if [ "$i" -eq 4 ]; then break; fi
    printf 'loop %s\n' "$i"
done

for outer in a b; do
    for inner in 1 2; do
        if [ "$inner" = 2 ]; then continue 2; fi
        printf '%s%s\n' "$outer" "$inner"
    done
done

value=beta
case "$value" in
    alpha) printf 'was alpha\n' ;;
    beta) printf 'was beta\n' ;;
    *) printf 'was other\n' ;;
esac

case abc in
    a*) printf 'starts with a\n' ;;
esac

case 5 in
    [0-9]) printf 'single digit\n' ;;
esac

greet() {
    printf 'hello %s\n' "$1"
}
greet world

adder() {
    printf '%s\n' "$(( $1 + $2 ))"
}
adder 3 4

scoped() {
    local inner=local-value
    printf '%s\n' "$inner"
}
inner=outer-value
scoped
printf '%s\n' "$inner"

recurse() {
    if [ "$1" -le 0 ]; then return 0; fi
    printf 'depth %s\n' "$1"
    recurse $(( $1 - 1 ))
}
recurse 3

counter=0
step() {
    counter=$((counter + 1))
}
step
step
printf '%s\n' "$counter"

{
    printf 'grouped one\n'
    printf 'grouped two\n'
}

if [ -n "unset-check" ] && [ 1 -eq 1 ]; then
    printf 'compound condition\n'
fi
