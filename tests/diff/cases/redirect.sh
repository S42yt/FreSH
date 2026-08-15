printf 'written\n' > out.txt
printf '%s\n' "$(cat out.txt)"

printf 'appended\n' >> out.txt
while IFS= read -r line; do printf '[%s]' "$line"; done < out.txt
printf '\n'

printf 'replaced\n' > out.txt
printf '%s\n' "$(cat out.txt)"

cat <<EOF
plain heredoc
EOF

name=value
cat <<EOF
expanded $name
EOF

cat <<'EOF'
literal $name
EOF

cat <<-EOF
	indented heredoc
	EOF

cat <<< "here string"

read -r first rest <<< "alpha beta gamma"
printf '%s|%s\n' "$first" "$rest"

printf 'a\nb\nc\n' | while IFS= read -r line; do printf '<%s>' "$line"; done
printf '\n'

printf 'one\ntwo\n' > lines.txt
count=0
while IFS= read -r line; do count=$((count + 1)); done < lines.txt
printf '%s\n' "$count"

printf 'to stdout\n' 2> /dev/null
printf 'quiet\n' > /dev/null
printf '%s\n' "$?"

{ printf 'grouped\n'; printf 'output\n'; } > grouped.txt
printf '%s\n' "$(cat grouped.txt)"

exec 3> fd.txt
printf 'through fd three\n' >&3
exec 3>&-
printf '%s\n' "$(cat fd.txt)"

printf 'first\n' | cat | cat
printf '%s\n' "$(printf 'piped\n' | cat)"

rm -f out.txt lines.txt grouped.txt fd.txt
