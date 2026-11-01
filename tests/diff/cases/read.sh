show() {
    printf '%s=[%s] ' "$@"
    printf '\n'
}

read a b c <<< 'one two three four'
show a "$a" b "$b" c "$c"
read a b c <<< 'only'
show a "$a" b "$b" c "$c"
read a <<< '   lead  and trail   '
show a "$a"
read <<< '  kept as is  '
show REPLY "$REPLY"
read -r a <<< 'back\slash'
show a "$a"
read a <<< 'back\slash \\ end'
show a "$a"
IFS= read -r a <<< '  spaced  '
show a "$a"
IFS=: read a b <<< 'x:y:z'
show a "$a" b "$b"
IFS=: read a b c <<< 'x::z'
show a "$a" b "$b" c "$c"
IFS=, read -a parts <<< 'a,b,,c'
show count "${#parts[@]}" third "${parts[2]}" fourth "${parts[3]}"
read -a parts <<< '  p   q  r '
show count "${#parts[@]}" all "${parts[*]}"
read -d : a <<< 'ab:cd'
printf 'delim %s ' "$?"
show a "$a"
read -n 3 a <<< 'abcdef'
show a "$a"
read -n 10 a <<< 'ab'
show a "$a"
read -N 4 a <<< 'ab cd'
show a "$a"
read -p 'prompt> ' a <<< 'answer'
show a "$a"
read -s a <<< 'secret'
show a "$a"
read -t 5 a <<< 'timely'
printf 'timeout %s ' "$?"
show a "$a"
printf 'from three\n' > three
read -u 3 a 3< three
show a "$a"

printf 'line one\\\nline two\n' > joined
read a < joined
show a "$a"
read -r a < joined
show a "$a"

a=stale
read a < /dev/null
printf 'eof %s ' "$?"
show a "$a"
printf 'no newline' > tail
read a < tail
printf 'partial %s ' "$?"
show a "$a"

count=0
while read -r line; do
    count=$((count + 1))
    printf '<%s>' "$line"
done << 'EOF'
first
  second
third\
EOF
printf ' %s\n' "$count"

read -Z a <<< 'x' 2> /dev/null
printf 'bad option %s\n' "$?"
read 1bad <<< 'x' 2> /dev/null
printf 'bad name %s\n' "$?"
read -r -d '' a <<< 'multi
line'
printf 'nul %s ' "$?"
show a "$a"
read -a 2> /dev/null <<< 'x'
printf 'array needs a name %s\n' "$?"
read -n 2> /dev/null <<< 'x'
printf 'count needs a value %s\n' "$?"
read a b <<< 'only-one'
show a "$a" b "${b-unset}"
