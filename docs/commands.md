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

Only the flags listed here are implemented, and every listed flag behaves like
the GNU original, checked against it on every push. An unknown flag is
rejected with the same message and status GNU uses.

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
| `ls [path]` | `-a` `-l` `-1` | colours by kind, `/` for directories, `*` for executables; one plain name per line when piped |
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
| `cat [files]` | `-n` `-b` `-s` `-E` `-T` `-v` `-A` `-e` `-t` | `-` means stdin |
| `cp src... dst` | `-r` `-R` `-a` `-f` `-n` `-p` `-u` `-v` `-t DIR` `-T` | |
| `mv src... dst` | `-f` `-n` `-u` `-v` `-t DIR` `-T` | moves across drives |
| `rm paths` | `-r` `-R` `-f` `-d` `-v` | refuses `.`, `..` and `/` |
| `mkdir dirs` | `-p` `-v` `-m MODE` | |
| `rmdir dirs` | `-p` `-v` `--ignore-fail-on-non-empty` | |
| `touch files` | `-c` `-a` `-m` `-d DATE` `-r FILE` | `-d` takes `@epoch` or `YYYY-MM-DD [HH:MM:SS]` |
| `ln target link` | `-s` `-f` `-n` `-v` `-t DIR` `-T` | on Windows `-s` needs developer mode or admin |
| `chmod mode files` | `-R` `-v` `-c` `-f` `--reference` | octal and symbolic modes; Windows only has the write bit, so `u-w` maps to read-only |
| `stat files` | `-c FORMAT` `--printf` `-t` `-L` | GNU layout and format letters; inode, links and owners are real on unix, placeholders on Windows |
| `du [paths]` | `-a` `-b` `-c` `-d N` `-h` `-k` `-m` `-s` `--si` `--apparent-size` `-B SIZE` | |
| `df [paths]` | `-h` `-H` `-k` `-m` `-T` `-B SIZE` | |
| `find [path...] [expr]` | `-name` `-iname` `-path` `-ipath` `-regex` `-type f\|d\|l` `-size` `-empty` `-mtime` `-mmin` `-newer` `-perm` `-maxdepth` `-mindepth` `-print` `-print0` `-delete` `-exec ... ;` `-exec ... +` `-prune` `-quit` `!` `-not` `-a` `-o` `( )` | |
| `basename path [suffix]` | `-a` `-s SUFFIX` `-z` | |
| `dirname paths` | `-z` | |
| `realpath paths` | `-e` `-m` `-q` `-z` `--relative-to=DIR` | |
| `readlink files` | `-f` `-e` `-m` `-n` `-q` `-v` `-z` | |
| `mktemp [template]` | `-d` `-u` `-q` `-p DIR` `-t` `--suffix=S` | |
| `truncate files` | `-s SIZE` `-c` `-r FILE` | `SIZE` takes `+` `-` `<` `>` `/` `%` prefixes and K/M/G suffixes |
| `file files` | `-b` | guesses from the first bytes |
| `open [target]` | | opens with the default program, `.` opens Explorer or Finder |

## Text

| Command | Flags | Notes |
| --- | --- | --- |
| `grep pattern [files]` | `-i` `-v` `-n` `-c` `-l` `-L` `-q` `-s` `-E` `-F` `-G` `-w` `-x` `-o` `-h` `-H` `-r` `-R` `-b` `-m N` `-A N` `-B N` `-C N` `-e PAT` `-f FILE` | basic expressions by default, `[[:class:]]`, `\b`, `\<`, `\>`; exit 0 match, 1 none, 2 error |
| `head [files]` | `-n [-]N` `-c [-]N` `-N` `-q` `-v` | |
| `tail [files]` | `-n [+]N` `-c [+]N` `-N` `-q` `-v` | `-f` is not supported |
| `wc [files]` | `-l` `-w` `-c` `-m` `-L` | GNU column widths and the `total` line |
| `sort [files]` | `-r` `-n` `-g` `-h` `-V` `-f` `-b` `-d` `-i` `-u` `-s` `-c` `-C` `-k KEYDEF` `-t SEP` `-o FILE` | keys take the same `F[.C][opts][,F[.C][opts]]` form as GNU |
| `uniq [in [out]]` | `-c` `-d` `-D` `-u` `-i` `-f N` `-s N` `-w N` | |
| `cut [files]` | `-b LIST` `-c LIST` `-f LIST` `-d C` `-s` `--complement` `--output-delimiter` | lists take `N`, `N-M`, `N-`, `-M`, comma separated |
| `tr SET1 [SET2]` | `-d` `-s` `-c` `-C` `-t` | ranges, `[:class:]`, `[x*n]`, `\ooo` escapes |
| `sed script [files]` | `-n` `-e` `-f` `-E` `-r` `-i[SUFFIX]` `-s` | see below |
| `awk program [files]` | `-F sep` `-v n=v` `-f file` | see [awk](awk.md) |
| `printf format [args]` | `-v var` | every conversion, `*` widths, `%b`, `%q`, `\ooo` and `\xHH`; matches bash's builtin |
| `nl [files]` | `-b STYLE` `-n FORMAT` `-s SEP` `-w N` `-v N` `-i N` `-l N` | |
| `tac [files]` | | |
| `rev [files]` | | |
| `fold [files]` | `-w N` `-N` `-s` `-b` | |
| `column [files]` | `-t` `-s SEP` `-o SEP` `-c WIDTH` `-x` | |
| `paste files` | `-d LIST` `-s` | with no operand at all, `paste` is the clipboard builtin; `paste -` reads stdin |
| `comm file1 file2` | `-1` `-2` `-3` `-i` `--output-delimiter` `--total` | both must be sorted |
| `diff file1 file2` | `-q` `-s` `-i` `-w` `-b` `-B` `-u` `-U N` | normal and unified output, exit 1 when they differ |
| `cmp file1 [file2]` | `-s` `-l` `-b` `-n N` | |
| `shuf [file]` | `-n N` `-e ARGS` `-i LO-HI` `-r` `-o FILE` | |
| `tee files` | `-a` | |
| `base64 [file]` | `-d` `-w N` `-i` | |
| `expand` / `unexpand` | `-t N` `-i` `-a` | |
| `yes [text]` | | stops after a million lines, so a pipeline stage that never ends cannot hang the shell |

### sed

A real stream editor, not a wrapper around `s///`. Addresses: `N`, `$`,
`/re/` with `I`, `first~step`, `addr1,addr2`, `addr,+N`, `addr,~N`, and `!`.
Commands: `s` with `g`, `p`, `i`/`I`, a number and `w file`; `y`; `d`, `D`;
`p`, `P`; `n`, `N`; `h`, `H`, `g`, `G`, `x`; `a`, `i`, `c` in both the
one-line and the `\` forms; `=`, `l`, `z`, `q`, `Q`, `r`, `w`; `{ }`; and
`:label`, `b`, `t`, `T`, so the classic `:a;N;$!ba;s/\n/ /g` works. `-i`
edits in place with an optional backup suffix. `\L`, `\U` and `M` are not
implemented.

## System

| Command | Flags | Notes |
| --- | --- | --- |
| `env [NAME=value] [command]` | `-i` `-u NAME` `-0` `-C DIR` | no arguments prints the environment |
| `printenv [names]` | `-0` | |
| `date [+format]` | `-u` `-d STRING` `-r FILE` `-R` `-I[FMT]` `--rfc-3339` | every GNU format letter with `-`, `_`, `^`, `0` flags; `-d` takes `@epoch`, ISO dates and times, `now`, `yesterday`, `tomorrow`, `N days ago`, `next week`, `last year` |
| `sleep durations` | | `s` `m` `h` `d` suffixes, several arguments add up |
| `whoami` | | |
| `hostname` | `-s` `-f` `-d` | |
| `uname` | `-a` `-s` `-n` `-r` `-v` `-m` `-p` `-i` `-o` | |
| `arch` | | |
| `nproc` | `--all` `--ignore=N` | |
| `id` | `-u` `-g` `-G` `-n` `-r` `-z` | real ids on unix, placeholders on Windows |
| `groups` | | |
| `ps` | | Windows only; unix has the real one |
| `kill pids` | `-s SIG` `-SIG` `-l [SIG]` `%job` | real signals on unix; on Windows `STOP` and `CONT` suspend and resume, anything else terminates |
| `pkill pattern` | `-x` `-i` `-e` `-c` | Windows only |
| `xargs [command]` | `-0` `-d DELIM` `-n N` `-L N` `-I REPL` `-i` `-r` `-t` `-a FILE` `-E EOF` | splits on whitespace with quotes like GNU; exit 123 when a command fails, 124 on 255 |
| `seq [first [incr]] last` | `-s SEP` `-w` `-f FORMAT` | decimals follow the arguments, `seq 1 0.5 2` prints `1.0 1.5 2.0` |
| `expr expression` | | `+ - * / %`, comparisons, `\|`, `\&`, `:`, `match`, `substr`, `index`, `length`; exit 1 when the result is 0 or empty |
| `md5sum` `sha1sum` `sha256sum` `sha512sum` | `-c` `-b` `-z` `--tag` `--quiet` `--status` | |
| `wget urls` | `-O file` `-q` `-P DIR` | downloads over https |

Every flag in these tables is checked by [tests/parity](../tests/parity), which
runs the same scripts through the GNU originals on Linux and through the
bundled versions on every platform. A flag that is not listed is not there:
the command says `invalid option` and exits, the way GNU does, instead of
guessing.

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
