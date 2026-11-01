printf 'one\ntwo\nthree\n' > lines

mapfile -t rows < lines
printf '%s [%s] [%s]\n' "${#rows[@]}" "${rows[0]}" "${rows[2]}"
mapfile rows < lines
printf '%s [%s]\n' "${#rows[@]}" "${rows[0]}"
mapfile -t < lines
printf 'default %s %s\n' "${#MAPFILE[@]}" "${MAPFILE[1]}"
readarray -t again < lines
printf 'readarray %s\n' "${again[*]}"
mapfile -t -n 2 rows < lines
printf 'count %s\n' "${rows[*]}"
mapfile -t -s 1 rows < lines
printf 'skip %s\n' "${rows[*]}"
mapfile -t -s 1 -n 1 rows < lines
printf 'skip and count %s\n' "${rows[*]}"
rows=(x y z w)
mapfile -t -O 2 rows < lines
printf 'origin %s %s\n' "${#rows[@]}" "${rows[*]}"
mapfile -t rows < lines
printf 'replaced %s\n' "${#rows[@]}"
mapfile -t -d , rows <<< 'a,b,c'
printf 'delim %s [%s]\n' "${#rows[@]}" "${rows[2]}"
mapfile -t -u 3 rows 3< lines
printf 'fd %s\n' "${rows[*]}"
mapfile -t rows < /dev/null
printf 'empty %s\n' "${#rows[@]}"
mapfile -t rows <<< ''
printf 'blank %s [%s]\n' "${#rows[@]}" "${rows[0]}"
mapfile -t first <<< 'alpha'
mapfile -t second <<< 'beta'
printf 'twice %s %s\n' "${first[0]}" "${second[0]}"
printf 'no newline' > tail
mapfile -t rows < tail
printf 'tail %s [%s]\n' "${#rows[@]}" "${rows[0]}"

long=$(printf '%05000d' 0)
printf '%s\nshort\n' "$long" > wide
mapfile -t rows < wide
printf 'wide %s %s %s\n' "${#rows[@]}" "${#rows[0]}" "${rows[1]}"

printf 'a\nb\n' | { mapfile -t piped; printf 'piped %s\n' "${piped[*]}"; }

mapfile -Z rows < lines 2> /dev/null
printf 'bad option %s\n' "$?"
mapfile -t 1bad < lines 2> /dev/null
printf 'bad name %s\n' "$?"
mapfile -n x rows < lines 2> /dev/null
printf 'bad count %s\n' "$?"
