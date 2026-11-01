(
    trap 'printf "leaving subshell\n"' EXIT
    printf 'body\n'
)
(
    trap 'printf "in trap %s\n" "$?"' EXIT
    exit 3
)
printf 'status %s\n' "$?"

trap 'printf "on exit\n"' EXIT
trap -p EXIT
trap 'printf "%s\n" "it'\''s"' INT
trap -p INT
trap '' TERM
trap -p TERM
trap 'printf "hup\n"' 1
trap -p HUP
trap -p EXIT HUP INT QUIT TERM
trap - INT
trap -p INT
trap TERM
trap -p TERM
trap -- - HUP
trap -p EXIT HUP INT QUIT TERM
printf 'after resets\n'

trap 'printf "err trapped\n"' ERR
false
printf 'after false %s\n' "$?"
if false; then :; fi
false || true
trap - ERR
false

trap 'printf "x\n"' BOGUS 2> /dev/null
printf 'bad signal %s\n' "$?"
trap -Z 2> /dev/null
printf 'bad option %s\n' "$?"
trap 'printf "sigint\n"' SIGINT
trap -p INT
trap 'printf "lower\n"' int
trap -p 2
trap - 2
trap 'printf "zero\n"' 0
trap -p EXIT HUP INT QUIT TERM
