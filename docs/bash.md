# Bash compatibility

A bash script runs in FreSH unchanged. `.sh` and `.frsh` go through the same
parser; the extension only decides which icon Explorer shows and whether the
file is found on `PATH` without typing it.

This page is the honest inventory. Everything under **Works** has a test in
`tests/`, so it keeps working.

## Works

### Expansion

```sh
${v:-default}  ${v:=default}  ${v:+alternate}  ${v:?message}
${#v}          ${v:offset}    ${v:offset:len}  ${v: -3}
${v#pat}  ${v##pat}  ${v%pat}  ${v%%pat}
${v/a/b}  ${v//a/b}  ${v/#a/b}  ${v/%a/b}
${v^^}  ${v,,}  ${v@U}  ${v@L}  ${v@Q}
${!name}       ${!prefix*}     $'ansi\tquoting'
$(command)  `command`  $(<file)  $((1 + 2))  $((2#1010))
```

### Arrays

```sh
arr=(one two three)     arr+=(four)      arr[7]=eight
${arr[0]}  ${arr[-1]}  ${arr[@]}  ${arr[*]}  ${#arr[@]}  ${#arr[1]}
${!arr[@]}  ${arr[@]:1:2}  ${arr[@]/a/b}  ${arr[@]^^}
declare -a list    declare -A table    declare -i counter    declare -p name
```

### Parameters

```sh
$0 $1 $9  $#  $@  $*  $?  $$  $_  ${@:2}  ${@:2:2}  ${#@}
$RANDOM  $SECONDS  $LINENO  $FUNCNAME  $PIPESTATUS  $OLDPWD  $BASH_REMATCH
```

### Control flow

`if/elif/else/fi`, `while`, `until`, `for x in`, `for ((i=0;i<n;i++))`,
`case` with `;;` and `;&` fallthrough, `select`, functions in both spellings,
`local`, `return`, `break n`, `continue n`, `&&`, `||`, `!`, `{ }`, `( )`.

### Redirection

`>` `>>` `<` `2>` `2>&1` `&>`, here documents `<<` and `<<-`, quoted
delimiters, here strings `<<<`, pipes, `|&`, process substitution `<(...)`
and `>(...)`, and `/dev/null`, `/dev/stdout`, `/dev/stderr`.

### Builtins

```
:  .  alias  bg  break  builtin  cd  command  continue  declare  dirs  echo
eval  exec  exit  export  false  fg  getopts  hash  help  history  jobs  kill
let  local  logout  mapfile  popd  printf  pushd  pwd  read  readarray
readonly  return  set  shift  shopt  source  test  times  trap  true  type
typeset  ulimit  umask  unalias  unset  wait
```

`set -e -u -x -o pipefail --`, `shopt -s globstar nullglob dotglob extglob
nocasematch`, `trap` for EXIT HUP INT QUIT TERM ERR DEBUG RETURN.

### Patterns

`*` `?` `[a-z]` `[!a-z]`, extended groups `?(a)` `*(a)` `+(a)` `@(a|b)`
`!(a)`, and `**` for a recursive walk when `globstar` is on. `[[ ... ]]`
including `=~` with `BASH_REMATCH`, `-v`, `&&` and `||`.

### At the prompt

History expansion `!!`, `!$`, `!42` and `!prefix`, which print what they
became before running.

## Does not work

- `coproc`, and namerefs (`local -n`).
- `${!prefix@}` in its array form, and `${v@a}` attribute listing.
- `bind`, `caps`, `compgen` and `complete`: FreSH has its own completion.
- Job control beyond `jobs`, `fg`, `bg`, `wait` and `stop`. Backgrounding a
  bundled command runs it in the foreground, because it runs inside the shell.
- Signals other than the eight above, which Windows does not deliver.

## Where FreSH differs on purpose

- Paths use forward slashes, and `\` stays an escape character.
- `printf` and `echo` always use the built in version, as they do in bash,
  even when another one is on `PATH`.
- A quoted `${array[*]}` joins with a space; `IFS` is used for splitting but
  not for that join.
- Line numbers in errors count the lines the parser saw, so a here document
  body shifts the ones after it.
