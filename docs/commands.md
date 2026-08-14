# Command reference

Two kinds of command are built into FreSH.

**Builtins** run inside the shell and always win over a program on `PATH`,
because they have to: `cd` cannot be a separate process.

**Bundled commands** are the unix tools Windows does not ship. They are
fallbacks. If an executable of the same name is on `PATH`, that one runs
instead, so installing GNU coreutils or busybox transparently upgrades them.
`find` and `sort` are the exceptions: Windows has unrelated tools with those
names, so FreSH always uses its own.

Only the flags listed here are implemented. Anything else is ignored rather
than rejected, so check this page when a script behaves oddly.

## Builtins

| Command | Flags | Notes |
| --- | --- | --- |
| `cd [dir]` | | no argument goes `$HOME`, `-` goes to the previous directory |
| `pwd` | | forward slashes |
| `echo [args]` | `-n` `-e` `-E` | `-e` enables `\n \t \r \e \\` |
| `export [NAME=value]` | | no arguments lists the environment |
| `unset NAME` | | |
| `set` | `-e` `-x` `-u` | no arguments lists variables |
| `local name[=value]` | | function scoped variable |
| `declare` / `typeset` | `-a` `-A` | declare an indexed or associative array |
| `trap command SIGNAL...` | | EXIT HUP INT QUIT TERM ERR DEBUG RETURN, `trap -` removes |
| `jobs` | | background jobs, running or stopped |
| `wait [id]` | | wait for one job or all of them |
| `fg [id]` | | resume if stopped and wait for it |
| `bg [id]` | | resume a stopped job in the background |
| `stop [id]` | | suspend a job, the FreSH stand in for Ctrl+Z |
| `alias [name=value]` | | no arguments lists aliases |
| `unalias name` | | |
| `source file` / `. file` | | runs in the current shell |
| `eval words` | | |
| `exit [code]` | | |
| `return [code]` | | in a function or sourced script |
| `break [n]` / `continue [n]` | | |
| `shift [n]` | | |
| `read [-p prompt] [names]` | `-p` | fills `REPLY` when no name given |
| `test expr` / `[ expr ]` | | see [scripting](scripting.md#conditionals) |
| `true` / `false` | | |
| `:` | | the null command, always succeeds, the same as `true` |
| `history [n]` | `-c` | `-c` clears |
| `which name...` | | path of a command |
| `type name...` | | says whether it is an alias, function, builtin or file |
| `ls [path]` | `-a` `-l` `-1` | colours by kind, `/` for directories, `*` for executables |
| `clear` | | |
| `rehash` | | rescan `PATH`, reload the theme and plugins after editing them |
| `help [command...]` | | a page per command, arguments in `<required> [optional] ...` form |
| `describe name summary [usage] [detail]` | | write a help page, see [plugins](plugins.md#help-pages) |
| `theme [name\|reset]` | | list, switch, or rewrite the bundled files |
| `die [message]` | | print in red and end the script with status 1 |
| `have name...` | | succeeds when every name is runnable, prints nothing |
| `say` / `ok` / `warn` | | styled progress output |
| `set -e` / `set -x` | | stop on failure, trace commands |
| `plugin [list\|load name...]` | | see [plugins](plugins.md) |
| `gitinfo` | | repository, branch, user, clean or dirty |
| `fresh` | | version, paths, active theme and plugins |
| `fresh update` | `--check` | fetch and install the newest release from github |

## Files and directories

| Command | Flags | Notes |
| --- | --- | --- |
| `cat [files]` | `-n` | reads stdin with no arguments, `-` means stdin |
| `cp src... dst` | `-r` | `-r` copies directories |
| `mv src... dst` | | overwrites, moves across drives |
| `rm paths` | `-r` `-f` | |
| `mkdir dirs` | `-p` | |
| `rmdir dirs` | | |
| `touch files` | | creates, or updates the timestamp |
| `ln target link` | `-s` | hard link by default, `-s` needs developer mode or admin |
| `chmod mode files` | | only `+w` and `-w`, Windows has no execute bit |
| `stat files` | | size, type, modified time |
| `file files` | | guesses from the first bytes |
| `du [path]` | `-h` | recursive total, kilobytes or `-h` |
| `df` | | drives, size, used, free |
| `find [path]` | `-name pat` `-type f\|d` | `pat` accepts `*` and `?` |
| `basename path [suffix]` | | |
| `dirname path` | | |
| `realpath paths` | | absolute path |
| `mktemp` | `-d` | prints the path it made |
| `open [target]` | | opens with the default program, `.` opens Explorer |

## Text

| Command | Flags | Notes |
| --- | --- | --- |
| `grep pattern [files]` | `-i` `-v` `-n` `-c` `-l` `-E` `-F` | basic expressions by default, `-E` extended, `-F` fixed |
| `head [files]` | `-n N` `-N` | default 10 lines |
| `tail [files]` | `-n N` `-N` | default 10 lines |
| `wc [files]` | `-l` `-w` `-c` | all three when no flag |
| `sort [files]` | `-r` `-n` `-u` | |
| `uniq [files]` | `-c` | collapses adjacent duplicates, sort first |
| `cut [files]` | `-d C` `-f N` | one field |
| `tr SET1 [SET2]` | `-d` | reads stdin |
| `sed script [files]` | `-n` `-E` | `s/pattern/replacement/[g]` and `d`, with `&` and `\1` |
| `nl [files]` | | numbers lines |
| `tac [files]` | | reverses line order |
| `rev [files]` | | reverses each line |
| `fold [files]` | `-w N` | default 80 |
| `column [files]` | | |
| `paste files` | | joins side by side with tabs |
| `comm file1 file2` | | both must be sorted |
| `diff file1 file2` | | line by line, exit 1 when they differ |
| `cmp file1 file2` | | first differing byte |
| `shuf [files]` | | |
| `tee files` | `-a` | |
| `printf format [args]` | | `%s %d %i %c %%`, `\n \t \r \\` |
| `awk program [files]` | `-F sep` `-v n=v` | see below |
| `yes [text]` | | bounded, safe in a pipe |

## System

| Command | Flags | Notes |
| --- | --- | --- |
| `env [NAME=value command]` | | no arguments prints the environment |
| `date [+format]` | | strftime format |
| `sleep seconds` | | fractions allowed |
| `whoami` | | |
| `hostname` | | |
| `uname` | `-a` | |
| `id` | | user, host, whether elevated |
| `groups` | | |
| `ps` | | pid, threads, name |
| `kill pids` | | |
| `pkill name` | | accepts `*` and `?` |
| `xargs [command]` | | one argument per input line, `echo` by default |
| `seq [first [incr]] last` | | |
| `expr expression` | | integer arithmetic and comparisons |
| `md5sum files` | | |
| `sha1sum files` | | |
| `sha256sum files` | | |
| `wget url` | `-O file` | downloads over https |

## PowerShell and cmd

You never have to leave FreSH to run something written for the other shells.

A line whose first word is a PowerShell cmdlet (`Verb-Noun`, with a real
PowerShell verb) or a known PowerShell alias is handed to PowerShell whole, so
its own syntax survives:

```sh
Get-Process | Select-Object -First 5 -ExpandProperty Name
Get-ChildItem -Recurse -Filter *.c | Measure-Object
```

A line starting with a cmd builtin goes to cmd.exe:

```
dir /s /b
ver
mklink /d link target
```

Routing only happens when nothing in FreSH claims the name and it does not
resolve on `PATH`, so `docker-compose` is not mistaken for a cmdlet and the
bundled `ps` still lists processes rather than starting PowerShell.

To be explicit:

| Command | Runs |
| --- | --- |
| `ps1 <command>` | the command in PowerShell |
| `ps1` | an interactive PowerShell |
| `cmd <command>` | the command in cmd.exe |
| `cmd` | an interactive cmd.exe |

Cmdlet names complete on Tab. The list is read from PowerShell the first time
it is needed, which takes a moment, then cached in `~/.fresh/cmdlets.cache`.
`rehash` refreshes it.

`FRESH_FOREIGN=0` turns automatic routing off, leaving `ps1` and `cmd`.

## Running other programs

FreSH resolves a command in this order: function, builtin, `PATH`, bundled
command. `PATH` lookup uses `PATHEXT` plus `.frsh`, `.ps1` and `.sh`.

| Extension | How it runs |
| --- | --- |
| `.exe` `.com` | started directly |
| `.bat` `.cmd` | `cmd.exe /c` |
| `.ps1` | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File` |
| `.frsh` `.sh` `.fresh` | by FreSH itself |
| no extension, `#!` first line | by FreSH if the interpreter ends in `sh`, otherwise that interpreter |

## awk

A real interpreter, not a wrapper:

```sh
awk '{ print $1 }' file
awk -F: '{ print $2, $1 }' /etc/passwd
awk '$2 > 30 { print $1 " is " $2 }' people.txt
awk '/error/ { count++ } END { print count " errors" }' build.log
awk 'BEGIN { print "start" } { print NR ": " NF } END { print "rows " NR }' file
awk '{ printf "%-10s %5d\n", $1, $2 }' file
```

Supported: `BEGIN` and `END`, regular expression and expression patterns,
fields `$0` to `$NF`, the variables `NR`, `NF`, `FS`, `OFS`, `FILENAME`,
arithmetic, string and numeric comparison, concatenation, `~` and `!~`,
assignment including `+=`, `++`, `if`/`else`, `while`, `for`, `next`, `exit`,
`print` and `printf`, and the functions `length`, `substr`, `index`,
`toupper`, `tolower` and `int`. `-F` sets the separator, `-v` presets a
variable.

Arrays, `split`, `in`, `delete`, `for (key in array)`, user defined functions
with `return`, and `getline` all work:

```sh
awk '{ total[$1] += $2 } END { for (k in total) print k, total[k] }' data.txt
awk 'BEGIN { n = split("a:b:c", parts, ":"); print n, parts[2] }'
awk 'function double(x) { return x * 2 } { print double($2) }' data.txt
awk 'BEGIN { while ((getline line < "notes.txt") > 0) n++; print n }'
```

Not supported: `cmd | getline`, `printf` into a file, `ENVIRON`, and multiple
`-f` program files.

## Not bundled

`curl` and `tar` ship with Windows 10 and later, in System32, so they already
work. `zip`, `less`, `ssh` and the rest are not included: install them, put
them on `PATH`, run `rehash`, and FreSH will pick them up ahead of anything
bundled.
