printf '%s\n' 'single $notexpanded'
name=value
printf '%s\n' "double $name"
printf '%s\n' "escaped \$name"
printf '%s\n' "a\\b"
printf '%s\n' 'a\b'
printf '%s\n' "quote \" inside"
printf '%s\n' 'quote '"'"' inside'

printf '%s\n' "tab$(printf '\t')end"
printf 'percent %% literal\n'
printf '[%5s]\n' ab
printf '[%-5s]\n' ab
printf '[%05d]\n' 42
printf '[%.2f]\n' 3.14159
printf '%s-%s\n' a b c d

printf '%s\n' "$(printf 'nested "quotes" here')"

word=hello
printf '%s\n' "${word}world"
printf '%s\n' "$word"world

joined="a
b"
printf '%s\n' "$joined"
printf '[%s]\n' $joined

back=$(printf 'a b')
printf '[%s]\n' "$back"

printf '%s\n' "*"
printf '%s\n' '*'

star=*
printf '%s\n' "$star"

printf '%s\n' "semi ; colon"
printf '%s\n' "pipe | bar"
printf '%s\n' "amp & sand"
printf '%s\n' "paren ( close )"
printf '%s\n' "brace { close }"
printf '%s\n' "angle < close >"

printf '%s\n' "dollar \$ sign"
printf '%s\n' "back \` tick"
printf '%s\n' "bang ! mark"

empty=""
printf '[%s]\n' "$empty"
printf '%s\n' "[${empty}]"
