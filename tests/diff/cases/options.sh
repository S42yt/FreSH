parse() {
    local OPTIND=1 opt
    while getopts 'ab:c' opt; do
        printf '[%s:%s]' "$opt" "${OPTARG-unset}"
    done
    printf ' end=%s optind=%s' "$opt" "$OPTIND"
    shift $((OPTIND - 1))
    printf ' rest=%s\n' "$*"
}
parse -a -b value -c rest of it
parse -ab value
parse -abvalue -c
parse -ca -- -b x
parse plain -a
parse -a - -c
parse
parse -x 2> /dev/null
parse -b 2> /dev/null

silent() {
    local OPTIND=1 opt
    while getopts ':ab:' opt; do
        printf '[%s:%s]' "$opt" "${OPTARG-unset}"
    done
    printf ' optind=%s\n' "$OPTIND"
}
silent -a -x -b
silent -ba

quiet() {
    local OPTIND=1 opt OPTERR=0
    while getopts 'a' opt; do
        printf '[%s:%s]' "$opt" "${OPTARG-unset}"
    done
    printf '\n'
}
quiet -z -a

getopts 'xy:' opt -x -y given
printf '%s %s %s\n' "$opt" "$OPTIND" "$OPTARG"
getopts 'xy:' opt -x -y given
printf '%s %s %s\n' "$opt" "$OPTIND" "$OPTARG"
getopts 'xy:' opt -x -y given
printf 'done %s %s %s\n' "$?" "$opt" "$OPTIND"
OPTIND=1
set -- -y again
getopts 'xy:' opt
printf '%s %s %s\n' "$opt" "$OPTIND" "$OPTARG"
getopts 2> /dev/null
printf 'no spec %s\n' "$?"
getopts ab 2> /dev/null
printf 'no name %s\n' "$?"

shopt -s nullglob
printf '[%s]\n' nothing*here
shopt -u nullglob
printf '[%s]\n' nothing*here
shopt nullglob
printf 'off %s\n' "$?"
shopt -s nullglob
shopt nullglob
printf 'on %s\n' "$?"
shopt -p nullglob
shopt -u nullglob
shopt -p nullglob
shopt -q nullglob
printf 'quiet %s\n' "$?"
shopt -q extglob
printf 'extglob default %s\n' "$?"
shopt -s extglob
shopt -q extglob
printf 'extglob on %s\n' "$?"
shopt -s expand_aliases
printf 'expand_aliases %s\n' "$?"
shopt -s nosuchoption 2> /dev/null
printf 'unknown %s\n' "$?"
shopt nosuchoption 2> /dev/null
printf 'unknown query %s\n' "$?"
shopt -s dotglob nullglob
shopt dotglob nullglob
shopt -u dotglob nullglob
shopt -o pipefail
shopt -so pipefail
shopt -o pipefail
shopt -uo pipefail
shopt -Z 2> /dev/null
printf 'bad option %s\n' "$?"
