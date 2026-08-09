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

Functions see and change the same variables as the rest of the script, there
is no `local`.

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

## What FreSH does not support

Worth knowing before you port a large script:

- no arrays, no associative arrays
- no `local`, functions share the global variables
- no process substitution `<(...)`, no here documents `<<EOF`
- no `select`, no `trap`, no job control beyond a background `&`
- no `[[ ... ]]`, use `test` or `[ ... ]`
- no brace expansion `{a,b}`, no `~user`, only bare `~`
- `set -e` and `set -u` are not implemented, `set` only lists variables
- `sed` handles `s/pattern/replacement/[g]`, it is not a regex engine
- `awk`, `tar`, `curl` and friends are not bundled, install them if you need
  them and FreSH will find them on `PATH`

## Debugging

There is no `set -x`. The quickest way to see what a line expands to is to
echo it:

```sh
echo "argument is [$1], path is [$(pwd)]"
```

A syntax error is reported before anything runs, because the whole script is
parsed first. If a script fails immediately with `unexpected end of input`,
look for an unclosed quote, `fi`, `done` or `esac`.

Scripts saved with Windows line endings work fine, carriage returns are
treated as whitespace.
