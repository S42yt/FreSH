name="FreSH"
empty=""
unset missing

printf '%s\n' "${name:-fallback}"
printf '%s\n' "${empty:-fallback}"
printf '%s\n' "${empty-fallback}"
printf '%s\n' "${missing:-fallback}"
printf '%s\n' "${name:+present}"
printf '%s\n' "${empty:+present}"
printf '%s\n' "${#name}"

path="/usr/local/share/file.tar.gz"
printf '%s\n' "${path#*/}"
printf '%s\n' "${path##*/}"
printf '%s\n' "${path%.*}"
printf '%s\n' "${path%%.*}"

text="one two one two"
printf '%s\n' "${text/one/1}"
printf '%s\n' "${text//one/1}"
printf '%s\n' "${text/#one/start}"
printf '%s\n' "${text/%two/end}"

printf '%s\n' "${name:1}"
printf '%s\n' "${name:1:2}"
printf '%s\n' "${name: -2}"

mixed="MiXeD"
printf '%s\n' "${mixed^^}"
printf '%s\n' "${mixed,,}"

value=set
holder=value
printf '%s\n' "${!holder}"

printf '%s\n' "$(printf 'from a substitution')"
printf '%s\n' "`printf 'from a backquote'`"

printf '%s\n' "$((1 + 2))"
printf '%s\n' "prefix$((3 * 4))suffix"

home_like="~"
printf '%s\n' "$home_like"

printf '%s\n' $'tab\there'
printf '%s\n' $'newline\nhere'

braced=$(printf '%s ' a{1,2,3})
printf '%s\n' "$braced"
printf '%s\n' "$(printf '%s ' {1..4})"
printf '%s\n' "$(printf '%s ' {a..d})"

nested_default=${missing:-${name}}
printf '%s\n' "$nested_default"

count=0
count=$((count + 1))
printf '%s\n' "${count}"
