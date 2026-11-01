t() {
    test "$@"
    printf '%s ' "$?"
}

bad() {
    test "$@" 2> /dev/null
    printf '%s ' "$?"
}

: > empty
printf 'data\n' > full
mkdir -p dir

t
t x
t ''
t -n
t -z
t !
t =
t '('
printf '| none and one\n'

t ! x
t ! ''
t -n x
t -n ''
t -z ''
t -z x
t -e full
t -e missing
t -f full
t -f dir
t -d dir
t -d full
t -s full
t -s empty
t -r full
t -w full
t -a full
t -a missing
t -t 1
bad -q x
bad x y
printf '| two\n'

t x = x
t x = y
t x == x
t x != y
t abc '<' abd
t b '>' a
t ! -z x
t ! -n x
t ! = x
t = = =
t '(' x ')'
t '(' '' ')'
t x -a y
t x -a ''
t '' -o x
t '' -o ''
t -n -a -n
t full -ef full
bad x -foo y
printf '| three\n'

t ! x = y
t ! x = x
t '(' -n x ')'
t '(' -z x ')'
t ! ! x
t ! '(' x ')'
printf '| four\n'

t ! -d A -a ! -d B
t ! -d dir -a ! -d B
t -f x -o -d .
t -f x -o -d y
t x -a '' -o y
t '' -o x -a ''
t ! '' -a ''
t '(' x -o '' ')' -a ''
t '(' x -o '' ')' -a y
t ! '(' x = x ')'
t -d dir -a -f full -a -s full
t -n x -a -z '' -o -n ''
t '' -o '' -o x
t x = y -o y = y
t ! x = y -a ! y = z
printf '| combined\n'

t 3 -lt 10
t 10 -gt 3
t -5 -lt 2
t 007 -eq 7
t 3 -le 3
t 4 -ge 5
t 1 -ne 2
t ' 7 ' -eq 7
t +3 -eq 3
bad abc -eq 1
bad 1.5 -eq 1
bad '' -eq 0
bad 1 -eq
printf '| integers\n'

set_var=1
declare -a list=(a b)
t -v set_var
t -v unset_var
t -v list
t -v 'list[1]'
t -v 'list[5]'
t -o errexit
set -u
t -o nounset
set +u
t -o nounset
printf '| variables and options\n'

[ x ]
printf '%s ' "$?"
[ ]
printf '%s ' "$?"
[ x = x ] && printf 'and '
[ x = y ] || printf 'or '
[ x 2> /dev/null
printf '%s ' "$?"
[ x = x ] ] 2> /dev/null
printf '%s ' "$?"
printf '| brackets\n'

[[ 1+1 -eq 2 ]]
printf '%s ' "$?"
[[ 2*3 -gt 5 ]]
printf '%s ' "$?"
count=4
[[ count -eq 4 ]]
printf '%s ' "$?"
[[ -v count ]]
printf '%s ' "$?"
[[ -o errexit ]]
printf '%s ' "$?"
printf '| double brackets\n'
