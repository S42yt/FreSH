# Bash compatibility

A bash script runs in FreSH unchanged. `.sh` and `.frsh` go through the same
parser; the extension only decides which icon Explorer shows and whether the
file is found on `PATH` without typing it.

This page is the honest inventory. Everything under **Works** has a test in
`tests/`, so it keeps working.

It is not only checked against what the author believed bash does. Every script
in [tests/diff](../tests/diff/README.md) is run through real bash on Linux and
through FreSH on Windows on every push, and any difference in output or exit
status fails the build. The differences listed at the bottom of this page are
the ones that survive that check on purpose.

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

### Descriptors

```sh
exec 3> log            exec 3>&-            printf 'x\n' >&3
exec > file            cmd 4< input         cmd 2>&1 >file
```

Descriptors 3 to 9 work for the shell and its builtins, `exec` with no command
keeps its redirections, and `N>&-` closes one.

### Words and fields

Splitting follows the bash rules exactly: `IFS` whitespace collapses, each
non whitespace `IFS` character delimits one field so `a::b` keeps the empty
one, a quoted `"$*"` and `"${a[*]}"` join with the first character of `IFS`,
and an unquoted `${a[@]}` splits its elements again.

## Does not work

- `coproc`.
- `${!prefix@}` in its array form, and `${v@a}` attribute listing.
- `compgen` and `complete`: FreSH has its own completion, and `bind` binds keys
  in the FreSH way, described in [shortcuts](shortcuts.md).
- Job control beyond `jobs`, `fg`, `bg`, `wait` and `stop`. Backgrounding a
  bundled command runs it in the foreground, because it runs inside the shell.
- Signals other than the eight above, which Windows does not deliver.
- A descriptor above 2 is not passed to an external program. `printf x >&3`
  works, `some-program.exe >&3` does not, because Windows only inherits the
  three standard handles.

## Where FreSH differs on purpose

- Paths use forward slashes, and `\` stays an escape character.
- `printf`, `echo`, `find`, `sort` and `awk` always use the built in version,
  so a script behaves the same on every machine. Call `gawk` or a full path for
  a different one.
- `${#v}`, `${v:offset:length}` and `substr` count bytes, not characters, so a
  string with an umlaut in it measures longer than it looks. Everything else
  about non ASCII text, including paths, works.
- Filename matching ignores case, because Windows filenames do. Matching in
  `case` and `[[ ]]` is case sensitive unless `nocasematch` is set, as in bash.
- Output to a console keeps Windows line endings; output to a file or a pipe is
  written as bytes, so a redirected FreSH produces the same file bash does.
- Line numbers in errors count the lines the parser saw, so a here document
  body shifts the ones after it.
- An error is louder than in bash in a few places, all listed in
  [how it fails](errors.md), because a wrong answer is worse than a stop.
