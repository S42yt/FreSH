v="a b   c"
for w in $v; do printf '[%s]' "$w"; done
printf '\n'
for w in "$v"; do printf '[%s]' "$w"; done
printf '\n'

IFS=:
p="one:two::three"
for w in $p; do printf '[%s]' "$w"; done
printf '\n'
unset IFS

set -- alpha "beta gamma" delta
printf '%s\n' "$#"
printf '[%s]' "$@"
printf '\n'
printf '[%s]' "$*"
printf '\n'

IFS=-
printf '[%s]' "$*"
printf '\n'
unset IFS

empty=""
set -- $empty
printf '%s\n' "$#"

set -- one two three four
shift 2
printf '%s\n' "$*"
printf '%s\n' "${@:1:2}"

spaced="  padded  "
printf '[%s]\n' "$spaced"
printf '[%s]\n' $spaced

nested="a'b"
printf '[%s]\n' "$nested"

quoted='it is "quoted"'
printf '%s\n' "$quoted"

joined=$(printf 'x\ny\n')
printf '[%s]\n' "$joined"
for part in $joined; do printf '<%s>' "$part"; done
printf '\n'
