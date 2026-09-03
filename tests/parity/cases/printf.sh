printf 'plain\n'
printf '%s-%s\n' a b
printf '%d %i\n' 42 -7
printf '%5d|%-5d|%05d\n' 42 42 42
printf '%x %X %o\n' 255 255 8
printf '%f %.2f %e\n' 3.14159 3.14159 31415.9
printf '%g %G\n' 0.00001 1e10
printf '%c%c\n' hello world
printf '%s\n' one two three
printf '%d\n' "'A"
printf '%b\n' 'tab\there'
printf '%q\n' 'a b'
printf '%q\n' plain
printf '%%\n'
printf '\x41\101\n'
printf '%5s|%-5s|%.2s\n' ab ab abcdef
printf '%*d\n' 6 42
printf '%.*f\n' 1 2.55
printf 'abc\cdef\n'; echo
printf '%d\n' abc; echo "status $?"
printf '%d\n' 12abc; echo "status $?"
printf '%s'; echo "status $?"
printf '%d %d\n' 1
printf '%s\n'
printf '%u\n' -1
printf '%z\n' x; echo "status $?"
