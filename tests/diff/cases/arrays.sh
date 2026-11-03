list=(one two three)
printf '%s\n' "${list[0]}"
printf '%s\n' "${list[2]}"
printf '%s\n' "${#list[@]}"
printf '[%s]' "${list[@]}"
printf '\n'
printf '%s\n' "${list[*]}"

list+=(four)
printf '%s\n' "${#list[@]}"
printf '%s\n' "${list[3]}"

list[6]=seven
printf '%s\n' "${#list[@]}"
printf '[%s]' "${!list[@]}"
printf '\n'

for item in "${list[@]}"; do printf '<%s>' "$item"; done
printf '\n'

sliced=("${list[@]:1:2}")
printf '[%s]' "${sliced[@]}"
printf '\n'

spaced=("first item" "second item")
printf '%s\n' "${#spaced[@]}"
for item in "${spaced[@]}"; do printf '<%s>' "$item"; done
printf '\n'
for item in ${spaced[*]}; do printf '<%s>' "$item"; done
printf '\n'

unset 'list[0]'
printf '%s\n' "${#list[@]}"

empty=()
printf '%s\n' "${#empty[@]}"

declare -A table
table[alpha]=1
table[beta]=2
printf '%s\n' "${table[alpha]}"
printf '%s\n' "${table[beta]}"
printf '%s\n' "${#table[@]}"

words=(alpha beta gamma)
printf '%s\n' "${words[@]#a}"
printf '%s\n' "${words[@]/a/A}"

joined="${words[*]}"
printf '%s\n' "$joined"
IFS=-
printf '%s\n' "${words[*]}"
unset IFS

set -- one two three
argv=("$@")
printf '%s\n' "${#argv[@]}"
printf '%s\n' "${argv[1]}"

letters=(a b c d)
i=2
printf '%s\n' "${letters[i]}" "${letters[i+1]}" "${letters[i-1]}" "${#letters[i]}"
letters[i]=C
printf '%s\n' "${letters[*]}"
unset 'letters[i-2]'
printf '%s\n' "${letters[*]}"

tab=$(printf '\t')
items=("list${tab}desc" two)
printf '[%s]\n' "${items[0]%%"$tab"*}" "${items[1]#t}" "${items[0]:0:3}" "${items[1]/w/W}" "${items[1]^^}"
printf '[%s]\n' "${items[1]:-x}" "${items[5]:-fallback}" "${items[7]-unset}" "${items[1]:+set}" "${items[i-1]%o}"
