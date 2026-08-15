[ -n "text" ] && printf 'n ok\n'
[ -z "" ] && printf 'z ok\n'
[ "a" = "a" ] && printf 'equal ok\n'
[ "a" != "b" ] && printf 'unequal ok\n'
[ 3 -eq 3 ] && printf 'eq ok\n'
[ 3 -ne 4 ] && printf 'ne ok\n'
[ 3 -lt 4 ] && printf 'lt ok\n'
[ 4 -gt 3 ] && printf 'gt ok\n'
[ 3 -le 3 ] && printf 'le ok\n'
[ 3 -ge 3 ] && printf 'ge ok\n'

[[ -n "text" ]] && printf 'double n ok\n'
[[ "abc" == a* ]] && printf 'pattern ok\n'
[[ "abc" != x* ]] && printf 'pattern negated ok\n'
[[ "abc" =~ ^a.c$ ]] && printf 'regex ok\n'
[[ "one two" =~ ([a-z]+)\ ([a-z]+) ]] && printf 'groups %s %s\n' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}"

[[ 3 -lt 4 && 4 -lt 5 ]] && printf 'double and ok\n'
[[ 3 -gt 4 || 4 -lt 5 ]] && printf 'double or ok\n'
[[ ! 3 -gt 4 ]] && printf 'double not ok\n'

: > present.txt
[ -f present.txt ] && printf 'file ok\n'
[ -e present.txt ] && printf 'exists ok\n'
[ ! -f absent.txt ] && printf 'absent ok\n'
mkdir -p adir
[ -d adir ] && printf 'dir ok\n'
[ -s present.txt ] || printf 'empty file ok\n'
printf 'x' > present.txt
[ -s present.txt ] && printf 'non empty ok\n'

if test "a" = "a"; then printf 'test builtin ok\n'; fi

value=""
if [ -z "$value" ]; then printf 'empty variable ok\n'; fi

word="spaced word"
if [ "$word" = "spaced word" ]; then printf 'quoted compare ok\n'; fi

if [[ $word == "spaced word" ]]; then printf 'double quoted compare ok\n'; fi

printf '%s\n' "$( [ 1 -eq 1 ]; echo $? )"
printf '%s\n' "$( [ 1 -eq 2 ]; echo $? )"
printf '%s\n' "$( [[ 1 -eq 2 ]]; echo $? )"

rm -f present.txt
rm -r adir
