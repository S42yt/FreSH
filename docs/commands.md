# Command reference

Two kinds of command are built into FreSH.

**Builtins** run inside the shell and always win over a program on `PATH`,
because they have to: `cd` cannot be a separate process.

**Bundled commands** are the unix tools Windows does not ship. They are
fallbacks. If an executable of the same name is on `PATH`, that one runs
instead, so installing GNU coreutils or busybox transparently upgrades them.
`find`, `sort`, `kill`, `printf`, `echo`, `awk`, `more` and `where` are the
exceptions: Windows or other shells ship unrelated tools with those names, and
`kill` must speak FreSH's own job table, so FreSH always uses its own.

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
| `getopts optstring name` | | parses options, sets `OPTIND` and `OPTARG` |
| `pushd [dir]` / `popd` / `dirs` | `-c` | the directory stack, no argument swaps the top two |
| `mapfile` / `readarray` | `-t` | read the input into an array, `-t` drops the newlines |
| `shopt` | `-s` `-u` `-q` | `globstar` `nullglob` `dotglob` `extglob` `nocasematch` |
| `command name` | `-v` | run past a function of the same name, `-v` says what it is |
| `builtin name` | | run the builtin even when a function shadows it |
| `exec command` | | run it and leave |
| `readonly [name=value]` | `-p` | a variable that cannot be changed again |
| `time pipeline` | | how long it took, on stderr |
| `z [word ...]` | | jump to a directory you have used, see [shortcuts](shortcuts.md) |
| `copy` / `paste` | | the clipboard, `pbcopy` and `pbpaste` on macOS |
| `copypath [file]` | | put the full path on the clipboard |
| `extract archive [dir]` | | unpack zip, tar, gz, bz2, xz, 7z or rar |
| `admin [command]` | | reopen FreSH elevated, through `sudo` on macOS |
| `history [n]` | `-c` | `-c` clears |
| `which name...` | | path of a command |
| `type name...` | | says whether it is an alias, function, builtin or file |
| `ls [path]` | `-a` `-l` `-1` | colours by kind, `/` for directories, `*` for executables |
| `clear` | | erases the screen and the scrollback, the way `cls` does |
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
| `fresh update` | `--check` `--pre` `--selector` | fetch and install a release from github |

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
| `open [target]` | | opens with the default program, `.` opens Explorer or Finder |

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

## Platforms

FreSH is one set of sources on Windows, macOS and Linux, and the builtins and
the bundled commands above are the same everywhere. What differs:

- the PowerShell and cmd routing below, `ps1` and `cmd` exist only on Windows
- `copy` and `paste` use the Windows clipboard, the macOS pasteboard, or
  `xclip` / `wl-copy` on Linux
- `admin` reopens FreSH elevated on Windows and runs it through `sudo` elsewhere
- `extract` unpacks zip files with PowerShell on Windows and `unzip` elsewhere
- `open` uses the shell association on Windows, `open` on macOS, `xdg-open` on
  Linux
- `ps`, `pkill` and `df` are Windows fallbacks; unix systems have the real ones
- `md5sum`, `sha1sum` and `sha256sum` call `md5` and `shasum` on macOS and the
  coreutils of the same name on Linux
- `uname` says `Windows` on Windows and, elsewhere, what the system `uname` says
- `PATH` is split on `;` on Windows and `:` elsewhere, and only files with the
  execute bit count as commands there, the way bash sees them

Because a bundled command only runs when nothing on `PATH` has its name, a Mac
or a Linux box uses its own `ls`, `sort`, `sed` and so on for everything but
the shadowed list above, so scripts behave the way the rest of the machine
does.

## PowerShell and cmd

Windows only. You never have to leave FreSH to run something written for the
other shells.

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

Arrays, `split`, `sub`, `gsub`, `match`, `sprintf`, the math functions,
`system`, `getline`, ranges, `do while`, `break`, `continue`, user functions
and output redirection all work:

```sh
awk '{ total[$1] += $2 } END { for (k in total) print k, total[k] }' data.txt
awk '{ gsub(/[0-9]+/, "N"); print }' log.txt
awk 'BEGIN { if (match("hello world", /wor/)) print RSTART, RLENGTH }'
awk 'function double(x) { return x * 2 } { print double($2) }' data.txt
awk 'BEGIN { while ((getline line < "notes.txt") > 0) n++; print n }'
awk '{ print $1 > "names.txt" }' people.txt
awk -f report.awk data.txt
```

There is no line length limit and no field count limit, a function that does
not exist is an error rather than an empty string, and a program with a syntax
error does not run at all.

The whole subset, and everything it does not do, is written down in
[awk](awk.md). Every feature listed there as working has a test.

## Not bundled

`curl` and `tar` ship with Windows 10 and later, in System32, so they already
work. `zip`, `less`, `ssh` and the rest are not included: install them, put
them on `PATH`, run `rehash`, and FreSH will pick them up ahead of anything
bundled.
