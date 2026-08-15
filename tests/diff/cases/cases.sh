check() {
    case "$1" in
        start*) printf 'prefix\n' ;;
        *end) printf 'suffix\n' ;;
        *middle*) printf 'contains\n' ;;
        one|two|three) printf 'listed\n' ;;
        [0-9]) printf 'digit\n' ;;
        [0-9][0-9]) printf 'two digits\n' ;;
        [!0-9]*) printf 'not a digit first\n' ;;
        *) printf 'other\n' ;;
    esac
}

check starting
check theend
check inmiddleof
check two
check 7
check 42
check zebra
check ''

fallthrough() {
    case "$1" in
        a) printf 'was a\n' ;;
        b) printf 'was b\n' ;;
        *) printf 'was other\n' ;;
    esac
}
fallthrough a
fallthrough b
fallthrough c

quoted=' spaced '
case "$quoted" in
    ' spaced ') printf 'exact with spaces\n' ;;
    *) printf 'no match\n' ;;
esac

star='*'
case "$star" in
    '*') printf 'literal star\n' ;;
esac

case abc in
    a?c) printf 'question mark\n' ;;
esac

path=/usr/local/bin/tool
case "$path" in
    */bin/*) printf 'in a bin\n' ;;
esac

for word in alpha beta gamma delta; do
    case "$word" in
        a*|b*) printf '%s: early\n' "$word" ;;
        *) printf '%s: late\n' "$word" ;;
    esac
done

nested() {
    case "$1" in
        outer)
            case "$2" in
                inner) printf 'both\n' ;;
                *) printf 'outer only\n' ;;
            esac
            ;;
        *) printf 'neither\n' ;;
    esac
}
nested outer inner
nested outer other
nested x y

value=$(case hit in hit) printf 'from a substitution' ;; esac)
printf '[%s]\n' "$value"

case x in
    x) printf 'status\n' ;;
esac
printf '%s\n' "$?"

case nomatch in
    y) printf 'no\n' ;;
esac
printf '%s\n' "$?"
