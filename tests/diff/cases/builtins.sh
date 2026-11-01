true extra words
printf 'true %s\n' "$?"
false extra words
printf 'false %s\n' "$?"
: anything at all
printf 'colon %s\n' "$?"

(exit 3)
printf 'exit %s\n' "$?"
(exit 256)
printf 'exit wraps %s\n' "$?"
(exit -1)
printf 'exit negative %s\n' "$?"
(exit abc) 2> /dev/null
printf 'exit word %s\n' "$?"
(exit 4 5) 2> /dev/null
printf 'exit two %s\n' "$?"
(false; exit)
printf 'exit keeps %s\n' "$?"
(exit -- 6)
printf 'exit dashes %s\n' "$?"

r7() { return 7; }
r7
printf 'return %s\n' "$?"
rlast() { false; return; }
rlast
printf 'return keeps %s\n' "$?"
rbig() { return 300; }
rbig
printf 'return wraps %s\n' "$?"
rword() { return abc; printf 'after\n'; }
rword 2> /dev/null
printf 'return word %s\n' "$?"
return 2> /dev/null
printf 'return outside %s\n' "$?"

eval 'evaluated=5'
printf '%s\n' "$evaluated"
eval printf "'%s\n'" '$evaluated'
eval "first=1; second=2"
printf '%s %s\n' "$first" "$second"
eval 'printf "[%s]\n" "a  b"'
eval
printf 'eval empty %s\n' "$?"
eval false
printf 'eval false %s\n' "$?"
eval -- printf "'%s\n'" dashes
eval 'if' 2> /dev/null
printf 'eval syntax %s\n' "$?"

printf 'sourced=yes\nreturn 4\nprintf "not reached\\n"\n' > lib.sh
. ./lib.sh
printf 'source %s %s\n' "$?" "$sourced"
printf 'printf "%%s %%s\\n" "$#" "$1"\n' > args.sh
set -- outer
. ./args.sh one two
printf 'after %s %s\n' "$#" "$1"
source ./args.sh
source ./missing.sh 2> /dev/null
printf 'source missing %s\n' "$?"
source 2> /dev/null
printf 'source nothing %s\n' "$?"
printf 'false\n' > fails.sh
. ./fails.sh
printf 'source status %s\n' "$?"

unalias -a
alias
shopt -s expand_aliases
alias greet='printf "hello %s\n"'
alias quote="printf '%s\n' it's"
alias greet
alias quote
alias
alias -p
greet world
alias missing_alias 2> /dev/null
printf 'alias missing %s\n' "$?"
unalias greet
alias greet 2> /dev/null
printf 'unaliased %s\n' "$?"
unalias greet 2> /dev/null
printf 'unalias missing %s\n' "$?"
unalias 2> /dev/null
printf 'unalias nothing %s\n' "$?"
unalias -a
alias
printf 'all gone\n'

myfunc() { :; }
type -t myfunc echo if printf
printf 'type -t %s\n' "$?"
type -t no_such_command
printf 'type missing %s\n' "$?"
type echo
type if
type myfunc > /dev/null
printf 'type function %s\n' "$?"
type no_such_command 2> /dev/null
printf 'type missing %s\n' "$?"
type -p echo
printf 'type -p builtin %s\n' "$?"
alias shown='echo shown'
type shown
type -t shown
unalias shown

command printf '%s\n' 'a b' 'c'
echo() { printf 'wrapped\n'; }
echo direct
command echo bypassed
builtin echo builtin too
unset -f echo
command -v printf
command -v myfunc
command -v if
command -v no_such_command
printf 'command -v missing %s\n' "$?"
alias shown='echo shown'
command -v shown
unalias shown
command -V printf
command -V myfunc | sed -n 1p
command -V no_such_command 2> /dev/null
printf 'command -V missing %s\n' "$?"
command no_such_command 2> /dev/null
printf 'command missing %s\n' "$?"
command
printf 'command alone %s\n' "$?"

builtin printf '%s\n' 'x y'
builtin no_such_builtin 2> /dev/null
printf 'builtin missing %s\n' "$?"
builtin
printf 'builtin alone %s\n' "$?"
cd() { printf 'wrapper\n'; builtin cd "$@"; }
cd .
unset -f cd

(exec printf '%s\n' 'replaced shell'; printf 'not reached\n')
(exec no_such_command) 2> /dev/null
printf 'exec missing %s\n' "$?"
exec 3> fd3.txt
printf 'to three\n' >&3
exec 3>&-
cat fd3.txt
exec 4< fd3.txt
read -r from_four <&4
exec 4<&-
printf '%s\n' "$from_four"
