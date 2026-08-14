# Writing FreSH scripts

FreSH scripts use the `.frsh` extension. They are executed by FreSH itself,
in the running shell process. There is no bash, no WSL and no MSYS2 involved,
which is the whole point: a `.frsh` script runs on a plain Windows machine
that has nothing installed but FreSH.

`.sh` files run the same way, so existing scripts keep working. Use `.frsh`
when a script relies on FreSH behaviour and you want that to be obvious.

## Running a script

```sh
./deploy.frsh            # from the shell, if it is in the current directory
deploy.frsh              # from anywhere on PATH
FreSH deploy.frsh a b    # from PowerShell or cmd, with arguments
source lib.frsh          # run it in the current shell, keeping its variables
```

`.frsh` is part of the command search, so a script in `~/bin` can be called by
name without the extension:

```sh
deploy
```

After installing, double clicking a `.frsh` file runs it in FreSH.

A shebang line is allowed and ignored, so a script can stay portable:

```sh
#!/usr/bin/env sh
echo this runs on both
```

## Typing and pasting at the prompt

Everything in this page can be typed straight at the prompt. When a line is
not finished, because a block is open or a quote is unclosed, FreSH prompts
with `…` and keeps reading until it is:

```sh
λ if test -d src; then
…   echo yes
… fi
yes
```

Ctrl+C abandons the block and gives you a clean prompt back.

Pasting a script works the same way. Tabs and indentation come through as
written, and each complete command runs as it arrives, so a pasted block of
lines behaves like a file that was sourced.

## Exit status

The script exits with the status of its last command, or whatever you pass to
`exit`. `$?` holds the status of the previous command.

```sh
grep -q TODO notes.txt
echo "grep said $?"
exit 3
```

`exit` inside a script called as a command ends that script only. `exit`
inside a script you `source` ends the shell, which is the same rule bash uses.

## Variables

```sh
name=world                # no spaces around =
greeting="hello $name"
export PATH="$HOME/bin;$PATH"   # visible to programs you start
unset name
```

Expansion forms:

| Form | Meaning |
| --- | --- |
| `$name` `${name}` | value, empty when unset |
| `${name:-other}` | value, or `other` when unset or empty |
| `${name:+other}` | `other` when set, otherwise empty |
| `${name:=other}` | value, assigning `other` first when unset |
| `${#name}` | length of the value |
| `$1` `$2` ... | positional arguments |
| `$0` | script path |
| `$#` | number of arguments |
| `$@` `$*` | all arguments, `"$@"` keeps them separate |
| `$?` | exit status of the last command |
| `$$` | process id |

```sh
target=${1:-.}
echo "checking ${#target} characters of $target"
```

## Quoting

```sh
echo 'single quotes keep $everything literal'
echo "double quotes expand $name and $(date +%H:%M)"
echo one\ word\ with\ spaces
```

Inside double quotes, `\"`, `\\`, `\$` and `` \` `` escape the next character.
Everything else is literal.

## Command substitution and arithmetic

```sh
branch=$(git rev-parse --abbrev-ref HEAD)
count=`ls | wc -l`
total=$(( count * 2 + 1 ))
```

Arithmetic understands `+ - * / %`, comparisons, `==`, `!=`, `&&`, `||`,
parentheses and bare variable names.

## Globs

`*` and `?` expand against the file system. A pattern that matches nothing is
left as it is, exactly like bash without `nullglob`.

```sh
for file in src/*.c; do
  echo "$file"
done
```

`[abc]` and ranges work in `case` patterns, not in file globs.

## Redirection and pipelines

```sh
echo hi > out.txt          # truncate
echo more >> out.txt       # append
sort < names.txt           # read from a file
build 2> errors.txt        # stderr only
build > log.txt 2>&1       # both into one file
cat big.txt | grep TODO | wc -l
long-running-thing &       # background
```

Redirection works on compound commands too:

```sh
{
  echo one
  echo two
} > pair.txt

for i in 1 2 3; do echo $i; done > counted.txt
```

## Conditionals

```sh
if test -f config.json; then
  echo found
elif test -d config; then
  echo directory
else
  echo missing
fi

[ "$name" = world ] && echo greeting
[ -z "$name" ] || echo "name is $name"

if ! grep -q TODO notes.txt; then
  echo nothing to do
fi
```

`test` and `[` support:

```
-e path      exists            -f path      regular file
-d path      directory         -s path      not empty
-r -w -x     readable, writable, executable
-z string    empty             -n string    not empty
a = b        equal             a != b       not equal
a -eq b      numeric equal     -ne -lt -le -gt -ge
! expr       negate            expr -a expr, expr -o expr
```

## Loops

```sh
for name in alpha beta gamma; do
  echo "$name"
done

count=0
while test $count -lt 3; do
  echo $count
  count=$(( count + 1 ))
done

until test -f ready.flag; do
  sleep 1
done
```

`break` and `continue` accept a level, `break 2` leaves two loops.

## case

```sh
case "$1" in
  start)        echo starting ;;
  stop|halt)    echo stopping ;;
  [0-9])        echo "single digit" ;;
  *.frsh)       echo "a script" ;;
  *)            echo "unknown: $1" ;;
esac
```

Patterns support `*`, `?` and `[...]` with ranges and `[!...]` negation.

## Functions

```sh
greet() {
  echo "hello $1"
  return 0
}

function backup {
  cp "$1" "$1.bak"
}

greet world
backup notes.txt
```

Inside a function, `$1` and friends are the function arguments and `$#` is
their count. `return` sets the exit status. `shift` drops arguments.

A function name can be punctuation, `:` included, and the opening brace may
sit right against the body, so both of these define the same thing:

```sh
work() { echo run; }
work(){echo run;}
```

That last leniency is a FreSH extension; bash needs a space after the `{`.
It is what lets the classic fork bomb parse: `:(){ :|:& };:` defines a
function named `:` that pipes itself into a backgrounded copy, then runs it.
Do not run that unless you mean it.

Functions see and change the same variables as the rest of the script, unless
a name is made `local`.

## A complete example

```sh
#!/usr/bin/env sh
# release.frsh, tag and push a release

set_version() {
  version="$1"
  if test -z "$version"; then
    echo "usage: release.frsh <version>"
    return 2
  fi
  return 0
}

set_version "$1" || exit $?

if ! git diff --quiet; then
  echo "working tree is dirty"
  exit 1
fi

for file in src/*.c; do
  grep -q "Copyright" "$file" || echo "no copyright header: $file"
done

tag="v$version"
git tag -a "$tag" -m "$tag" && git push origin "$tag"
echo "pushed $tag at $(date +%H:%M)"
```

## FreSH extensions

Everything above is bash syntax and runs in bash unchanged. These are the
parts FreSH adds, and the reason a `.frsh` script is usually shorter than the
`.sh` that does the same job.

### Stop on the first failure

```sh
set -e          # abort the script when a command fails
set -x          # print each command before running it, on stderr
set +e          # back off again
```

Conditions are exempt, exactly as in bash: the left of `&&` and `||`, the
test in `if`, `while` and `until`, and anything after `!` may fail without
ending the script. So `grep -q x file || echo missing` is safe under `set -e`.

### Failing on purpose

```sh
die "no compiler found"          # message in red, script exits with 1
```

`die` replaces the `echo ... >&2; exit 1` pair that appears in every build
script.

### Checking a command exists

```sh
have gcc || die "install a compiler"
have git docker node && say "all present"
```

`have` succeeds when every name given is a function, alias, builtin, bundled
command or program on `PATH`. It prints nothing, so there is no
`> /dev/null 2>&1` to write, which on Windows would be `> nul 2>&1` anyway.

### Saying things

```sh
say "compiling"      # a dim bullet and your message
ok "done"            # a green tick
warn "no tests"      # a yellow bang
```

These write to stdout with colour when the terminal supports it and plain
text when the output is redirected, so a build log stays readable.

### Side by side

The bash way:

```sh
GREEN='\033[32m'
RESET='\033[0m'
info() { printf "${GREEN}%s${RESET}\n" "$1"; }

if ! command -v gcc > /dev/null 2>&1; then
  echo "gcc not found" >&2
  exit 1
fi

info "compiling"
gcc -O2 -o build/app src/*.c || exit 1
info "done"
```

The same thing in FreSH:

```sh
set -e
have gcc || die "gcc not found"
say "compiling"
gcc -O2 -o build/app src/*.c
ok "done"
```

`build.frsh` in the repository root builds the whole project, installer
included, and is worth comparing with `build.sh` next to it.

## Arrays

```sh
fruits=(apple banana cherry)
echo "${fruits[0]}"        # apple
echo "${fruits[@]}"        # every element, one word each when quoted
echo "${#fruits[@]}"       # how many
fruits+=(date)             # append
fruits[1]=blueberry        # replace one
for f in "${fruits[@]}"; do echo "$f"; done
```

Associative arrays need declaring first:

```sh
declare -A colour
colour[sky]=blue
colour[grass]=green
echo "${colour[sky]}"      # blue
echo "${!colour[@]}"       # the keys
echo "${#colour[@]}"       # how many
```

`declare -a` makes an indexed array explicitly, and `declare` on its own
prints everything.

## Local variables

```sh
counter=global

work() {
  local counter=inner
  local scratch
  echo "$counter"
}

work            # inner
echo "$counter" # global
```

`local` saves whatever the name held and restores it when the function
returns, so a helper cannot clobber its caller.

## Here documents

```sh
cat <<EOF
expanded: $HOME and $(date +%H:%M)
EOF

cat <<'EOF'
literal $HOME, nothing is expanded
EOF
```

Quoting the delimiter turns expansion off, exactly as in bash. `<<-` is
accepted too.

## Double brackets

```sh
[[ $name == fre* ]]           # pattern, not a literal
[[ $name != other ]]
[[ -f build.sh && -d src ]]
[[ 5 -gt 3 || 1 -gt 9 ]]
[[ ! -e missing ]]
[[ $version =~ ^[0-9]+\.[0-9]+$ ]]
```

No word splitting happens inside, so unquoted variables with spaces are safe.
`=~` takes an extended regular expression.

## Brace expansion

```sh
echo {a,b,c}          # a b c
echo item-{1..4}      # item-1 item-2 item-3 item-4
echo {x,y}{1,2}       # x1 x2 y1 y2
mkdir -p build/{bin,lib}
```

## Process substitution

```sh
comm <(sort left.txt) <(sort right.txt)
wc -l < <(grep TODO -r src)
tee >(grep ERROR > errors.txt) < build.log
```

`<(...)` gives the command's output as a file, `>(...)` collects what is
written and feeds it in afterwards.

## Trimming and replacing in a variable

```sh
path=/home/me/project/main.tar.gz

${path##*/}        # main.tar.gz     longest prefix removed
${path%/*}         # /home/me/project
${path%.gz}        # ...main.tar     shortest suffix removed
${path%%.*}        # ...main         longest suffix removed

${name/old/new}    # first match replaced
${name//old/new}   # every match
${name/#old/new}   # only at the start
${name/%old/new}   # only at the end

${name:6}          # from offset 6
${name:0:5}        # 5 characters from 0
${name^} ${name^^} # capitalise first, capitalise all
${name,} ${name,,} # lowercase first, lowercase all
```

Patterns are shell patterns, the same ones `case` uses.

## Traps and jobs

```sh
trap 'echo "cleaning up"' EXIT
trap 'echo "that failed"' ERR
trap 'echo "closing"' HUP TERM
trap - ERR                        # remove it

long-running-thing &
jobs                              # what is still going
fg                                # bring the last one to the foreground
fg 2                              # or a numbered one
stop 2                            # suspend it
bg 2                              # let it carry on in the background
wait                              # for all of them
wait 2                            # for job 2, or a process id
```

`trap` takes EXIT, HUP, INT, QUIT, TERM, ERR, DEBUG and RETURN, by name or by
number, with or without the `SIG` prefix. DEBUG runs before every command,
RETURN when a function returns, ERR after any command that fails outside a
condition, HUP and TERM when the window is closed.

`stop` is a FreSH addition: Windows has no Ctrl+Z, so suspending a job is a
command rather than a keystroke. It suspends every thread in the process, and
`bg` or `fg` resumes them.

Only external programs become jobs. A bundled command with `&` runs in the
foreground, because it runs inside the shell itself.

## select

```sh
select choice in build test clean; do
  echo "you picked $choice"
  break
done
```

## What FreSH does not support

- Backgrounding a bundled command. `&` only makes a job out of an external
  program, because bundled commands run inside the shell process.
- Signals beyond EXIT, HUP, INT, QUIT, TERM, ERR, DEBUG and RETURN. Windows
  has no others to deliver.
- `coproc`, `shopt`, `${!prefix*}`, and `$'ansi strings'`.
- `tar`, `curl`, `zip`, `ssh` and friends are not bundled, though Windows
  ships `tar` and `curl` itself. Anything else: install it, put it on `PATH`,
  run `rehash`.

## Debugging

`set -x` traces every command to stderr, and `set -e` stops at the first
failure. To see what a single line expands to, echo it:

```sh
echo "argument is [$1], path is [$(pwd)]"
```

A syntax error is reported before anything runs, because the whole script is
parsed first. The message names the script, the line, what is missing and how
that construct is written:

```
FreSH: deploy.frsh: line 12: this if has no fi
  close it with fi, and note that it is fi rather than end or endif
```

Line numbers count the lines the parser saw, so a here document body shifts
the ones after it.

A command that cannot be found suggests the nearest name it knows, which
covers a swapped pair of letters:

```
FreSH: gti: command not found
  did you mean git
```

Scripts saved with Windows line endings work fine, carriage returns are
treated as whitespace.
