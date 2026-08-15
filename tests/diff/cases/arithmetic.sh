printf '%s\n' "$((1 + 2 * 3))"
printf '%s\n' "$(((1 + 2) * 3))"
printf '%s\n' "$((7 / 2))"
printf '%s\n' "$((-7 / 2))"
printf '%s\n' "$((7 % 3))"
printf '%s\n' "$((-7 % 3))"
printf '%s\n' "$((2 ** 10))"
printf '%s\n' "$((1 << 8))"
printf '%s\n' "$((255 >> 4))"
printf '%s\n' "$((6 & 3))"
printf '%s\n' "$((6 | 3))"
printf '%s\n' "$((6 ^ 3))"
printf '%s\n' "$((~0))"
printf '%s\n' "$((!0))"
printf '%s\n' "$((!5))"
printf '%s\n' "$((1 && 0))"
printf '%s\n' "$((1 || 0))"
printf '%s\n' "$((3 > 2 ? 10 : 20))"
printf '%s\n' "$((016))"
printf '%s\n' "$((0x1f))"
printf '%s\n' "$((2#1011))"
printf '%s\n' "$((16#ff))"

n=5
printf '%s\n' "$((n + 1))"
printf '%s\n' "$((n++))"
printf '%s\n' "$n"
printf '%s\n' "$((++n))"
printf '%s\n' "$((n--))"
printf '%s\n' "$((--n))"

total=0
for i in 1 2 3 4 5; do
    total=$((total + i))
done
printf '%s\n' "$total"

(( total > 10 )) && printf 'greater\n'
(( total > 100 )) || printf 'not greater\n'

x=2
(( x *= 4 ))
printf '%s\n' "$x"
(( x /= 2 ))
printf '%s\n' "$x"
(( x %= 3 ))
printf '%s\n' "$x"

let y=3+4
printf '%s\n' "$y"

unset undefined
printf '%s\n' "$((undefined + 7))"

printf '%s\n' "$(( 1 + 1 ))"
i=0
while [ "$i" -lt 3 ]; do
    printf 'i=%s\n' "$i"
    i=$((i + 1))
done
for (( j = 0; j < 3; j++ )); do
    printf 'j=%s\n' "$j"
done
