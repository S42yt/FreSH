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
| `set` | | lists variables, does not set options |
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
| `history [n]` | `-c` | `-c` clears |
| `which name...` | | path of a command |
| `type name...` | | says whether it is an alias, function, builtin or file |
| `ls [path]` | `-a` `-l` `-1` | colours by kind, `/` for directories, `*` for executables |
| `clear` | | |
| `rehash` | | rescan `PATH` after installing something |
| `help` | | |
| `theme [name]` | | list or switch, see [themes](themes.md) |
| `plugin [list\|load name...]` | | see [plugins](plugins.md) |
| `gitinfo` | | repository, branch, user, clean or dirty |

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
| `grep pattern [files]` | `-i` `-v` `-n` `-c` `-l` | substring match, not regex |
| `head [files]` | `-n N` `-N` | default 10 lines |
| `tail [files]` | `-n N` `-N` | default 10 lines |
| `wc [files]` | `-l` `-w` `-c` | all three when no flag |
| `sort [files]` | `-r` `-n` `-u` | |
| `uniq [files]` | `-c` | collapses adjacent duplicates, sort first |
| `cut [files]` | `-d C` `-f N` | one field |
| `tr SET1 [SET2]` | `-d` | reads stdin |
| `sed script [files]` | `-n` | `s/pattern/replacement/[g]` and `d` only |
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

## Not bundled

`awk`, `tar`, `curl`, `wget`, `zip`, `less`, `ssh` and the rest are not
included. Install them however you like, put them on `PATH`, run `rehash`,
and FreSH will pick them up.
