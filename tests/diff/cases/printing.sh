echo plain words
echo
echo -n 'no newline'
echo '|'
echo -e 'tab\there'
echo -E 'raw\there'
echo 'raw\there'
echo -ne 'both\n'
echo -n -e 'split\n'
echo -en 'cluster\n'
echo -x
echo -nx
echo --
echo -
echo -e 'hex \x41\x4a end'
echo -e 'octal \0101\0102 end'
echo -e 'unicode A end'
echo -e 'unknown \z \q end'
echo -e 'bell \a| back \b| form \f| vtab \v| escape \E| end'
echo -e 'stop \c here'
echo '|'
echo -e 'backslash \\ end'
echo -e "trailing \\"
echo a '' b
echo "-n"
echo -e

printf '%s|' a b c
printf '\n'
printf '%d\n' 42 -3 0x1F 010 "'A" '"B'
printf '%5s|%-5s|\n' ab cd
printf '%05d|%+d|% d\n' 42 7 7
printf '%x %X %o %#x %#o\n' 255 255 8 255 8
printf '%.2f %8.3f %-8.1f|\n' 3.14159 2.5 1.25
printf '%e\n' 1234.5
printf '%g %g\n' 0.0001 1000000
printf '%c%c\n' hello world
printf '%b\n' 'tab\tend' 'oct\0101' 'stop\cnever'
printf '%q\n' 'a b' "it's" plain ''
printf '%%\n'
printf '%s-%d-%s\n'
printf '%s %s\n' one two three
printf '%*d|%-*d|\n' 5 42 4 7
printf '%.*f\n' 2 3.14159
printf '%i %u\n' 5 6
printf '%5.2s|\n' abcdef
printf '\x41\101\n'
printf 'a\tb\\c\n'
printf -- '%s\n' dashes
printf '%s\n'
printf 'literal only\n' extra args
printf '%d\n' abc 2> /dev/null
printf 'bad number %s\n' "$?"
printf '%d\n' 12abc 2> /dev/null
printf 'trailing junk %s\n' "$?"
printf 2> /dev/null
printf 'no format %s\n' "$?"
printf '%z\n' 2> /dev/null
printf 'bad directive %s\n' "$?"

printf -v saved '%s=%d' answer 42
printf '%s\n' "$saved"
printf -v saved 'second'
printf '%s\n' "$saved"
declare -a slots=(a b c)
printf -v 'slots[1]' '%s' middle
printf '%s\n' "${slots[*]}"
printf -v 1bad x 2> /dev/null
printf 'bad name %s\n' "$?"
printf -v 2> /dev/null
printf 'no name %s\n' "$?"
printf '%(%Y-%m-%d)T\n' 0
