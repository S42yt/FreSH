set -- a b 'c d'
printf '%s %s [%s]\n' "$#" "$1" "$3"
set -- "$@" e
printf '%s\n' "$#"
set --
printf 'cleared %s\n' "$#"
set -- -x -y
printf '%s %s\n' "$#" "$1"
set -e
case $- in *e*) printf 'errexit in dollar dash\n' ;; esac
set +e
case $- in *e*) printf 'still there\n' ;; *) printf 'errexit gone\n' ;; esac
set -u
case $- in *u*) printf 'nounset in dollar dash\n' ;; esac
set +u
set -o pipefail
set -o | grep '^pipefail'
set +o | grep 'pipefail$'
set +o pipefail
set -o | grep '^pipefail'
set -o errexit
set +o | grep 'errexit$'
set +o errexit
: > globfile
set -f
printf '%s\n' *
set +f
set -o noglob
printf '%s\n' *
set +o noglob
set -o bogus 2> /dev/null
printf 'bad option name %s\n' "$?"
set -Z 2> /dev/null
printf 'bad flag %s\n' "$?"

shift_args() {
    shift
    printf '%s %s ' "$#" "$1"
    shift 2
    printf '%s %s ' "$#" "$1"
    shift 5 2> /dev/null
    printf 'over %s %s %s ' "$?" "$#" "$1"
    shift 0
    printf 'zero %s %s ' "$?" "$#"
    shift -1 2> /dev/null
    printf 'negative %s %s ' "$?" "$#"
    shift x 2> /dev/null
    printf 'word %s %s\n' "$?" "$#"
}
shift_args 1 2 3 4 5

x=1
unset x
printf '%s\n' "${x-unset}"
x=1
unset -v x
printf '%s\n' "${x-unset v}"
fn() { :; }
unset -f fn
type -t fn
printf 'function gone %s\n' "$?"
both=variable
both() { :; }
unset both
printf '%s %s\n' "${both-no variable}" "$(type -t both)"
unset both
type -t both
printf 'second unset %s\n' "$?"
list=(a b c)
unset 'list[1]'
printf '%s %s\n' "${#list[@]}" "${list[*]}"
unset never_set
printf 'unset missing %s\n' "$?"
readonly locked=1
unset locked 2> /dev/null
printf 'unset readonly %s\n' "$?"
unset 1bad 2> /dev/null
printf 'unset bad name %s\n' "$?"
unset -Z x 2> /dev/null
printf 'unset bad option %s\n' "$?"

export SHARED=one
printf '%s\n' "$(printenv SHARED)"
export -p | grep 'SHARED='
export -n SHARED
printf '[%s] %s\n' "$(printenv SHARED)" "$SHARED"
LATER=two
export LATER
printf '%s\n' "$(printenv LATER)"
export EMPTY_EXPORT
export -p | grep 'EMPTY_EXPORT'
export | grep 'LATER='
export 1bad=x 2> /dev/null
printf 'export bad name %s\n' "$?"
export -Z 2> /dev/null
printf 'export bad option %s\n' "$?"
quoted='say "hi" $HOME `x` \ end'
export quoted
export -p | grep ' quoted='

readonly fixed=5
(fixed=6) 2> /dev/null
printf 'assign readonly %s %s\n' "$?" "$fixed"
readonly -p | grep ' fixed='
readonly | grep ' fixed='
readonly marked
readonly -p | grep ' marked'
readonly 1bad=x 2> /dev/null
printf 'readonly bad name %s\n' "$?"
readonly -a frozen=(x y)
printf '%s\n' "${frozen[1]}"

declare -i n=5+3
printf '%s\n' "$n"
n+=2
printf '%s\n' "$n"
n=x
printf '%s\n' "$n"
declare -l lower=MiXeD
declare -u upper=MiXeD
printf '%s %s\n' "$lower" "$upper"
lower=AGAIN
printf '%s\n' "$lower"
declare plain='a "q" $v'
declare -p plain
declare -p n
declare -a arr=(a 'b c' d)
printf '%s [%s]\n' "${#arr[@]}" "${arr[1]}"
declare -p arr
declare -A map=([k]=v)
printf '%s\n' "${map[k]}"
declare -a none=()
declare -p none
declare bare
declare -p bare
declare -x exported_too=1
declare -p exported_too
declare -ri both_flags=3
declare -p both_flags
declare -p not_declared 2> /dev/null
printf 'missing %s\n' "$?"
declare 1bad=x 2> /dev/null
printf 'declare bad name %s\n' "$?"
declare -Z 2> /dev/null
printf 'declare bad option %s\n' "$?"
typeset -i t=2*4
printf '%s\n' "$t"

scoped() {
    declare inner=in
    local other=also
    typeset third=three
    declare -g global=out
    x=changed
    printf '%s %s %s\n' "$inner" "$other" "$third"
}
x=start
scoped
printf '[%s] [%s] [%s] %s %s\n' "${inner-}" "${other-}" "${third-}" "$global" "$x"

shadow() {
    local x=local
    printf '%s ' "$x"
    inner_view
}
inner_view() {
    printf '%s\n' "$x"
}
shadow
printf '%s\n' "$x"
local nope=1 2> /dev/null
printf 'local outside %s\n' "$?"
fn() { :; }
declare -F fn
declare -F missing_fn
printf 'missing function %s\n' "$?"
declare -f fn > /dev/null
printf 'function exists %s\n' "$?"

ref_target=one
declare -n ref=ref_target
ref=two
printf '%s\n' "$ref_target"

let x=2+3
printf '%s\n' "$x"
let 'y = 4 * 2' 'z = y + 1'
printf '%s %s\n' "$y" "$z"
let 0
printf 'let zero %s\n' "$?"
let 1 0
printf 'let last %s\n' "$?"
i=0
let i++
printf 'post increment %s %s\n' "$?" "$i"
let 2> /dev/null
printf 'let empty %s\n' "$?"
let '1/0' 2> /dev/null
printf 'let divide %s\n' "$?"
