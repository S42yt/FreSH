# Tab completion

FreSH completes on Tab with its own engine, written in C and built into the
shell. It needs no setup, no plugin and no completion scripts, and it answers
from what FreSH already knows: the commands it can run, the files on disk, the
variables it holds and the help pages it ships.

This page is what it does today, what it does not do, and how to teach it the
flags of a tool it does not know.

## Pressing Tab

| What happens | When |
| --- | --- |
| the word is replaced | there is exactly one match, and a space follows unless it is a directory |
| the shared start is filled in and the matches are listed | there are several |
| the next match replaces the word | Tab is pressed again while the list is showing |
| nothing | there is no match |

Command names and flags match without regard to case, and the match replaces
what you typed, so `get-childi` becomes `Get-ChildItem`. File names do the same
on Windows, where `readme` becomes `README.md`, and match case on macOS and
Linux, where the file system does. A match with a space in it is wrapped in
double quotes.

## What a word completes to

The word under the cursor ends at a space, a tab or one of `| & ; < > (`,
unless it is inside quotes. What it completes to depends on where it stands.

### The first word of a command

At the start of the line, or after `|`, `&`, `;` or `(`, a word completes to
everything that can run there:

- keywords such as `if`, `for` and `while`
- builtins and the bundled unix commands
- functions and aliases
- programs on `PATH`
- on Windows with `FRESH_FOREIGN` on, the cmd.exe builtins and the PowerShell
  cmdlets

Files and directories in the current directory are offered alongside, because
a directory name on its own is a `cd` when `FRESH_AUTOCD` is on.

A word that contains `/`, `\` or `.` is taken as a path, so `./bu` and
`tools/re` complete to files.

### A word starting with `$`

`$` and `${` complete to the names of the variables the shell holds,
environment variables included. `$HO` becomes `$HOME`, `${HO` becomes
`${HOME}`.

### A word starting with `-`

After a command, `-` completes to the flags that command's help page lists, so
`grep -` offers only the flags FreSH's `grep` implements and `sort --` only its
long options. The flags come from the same text `help <command>` prints, which
keeps the two from disagreeing.

A program FreSH has no help page for gets no flag completion, until you give
it one, see [Teaching FreSH a tool's flags](#teaching-fresh-a-tools-flags).

### Any other word

Everything else completes to a path, relative to the current directory or to
the directory already typed. `~/` is your home directory. Directories end in
`/`, so Tab keeps going into them, and paths are written with forward slashes.
Files starting with `.` are only offered once you type the `.`.

A few commands complete something narrower:

| Command | Completes |
| --- | --- |
| `cd`, `pushd`, `mkdir`, `rmdir` | directories only |
| `help`, `which`, `type`, `man`, `describe` | command names, not files |

## Where the names come from

Program names on `PATH` are read once and kept in memory, so Tab does not walk
the disk on every key. Setting or exporting `PATH` reads them again, and
`rehash` does the same for a program installed while the shell is open.

The cmdlet list is read from PowerShell the first time it is needed, which
takes a moment, then cached in `~/.fresh/cmdlets.cache`. `rehash` refreshes it
too. `FRESH_FOREIGN=0` leaves cmdlets out of completion as well as out of
routing.

## Teaching FreSH a tool's flags

Flag completion reads help pages, and `describe` adds one. Anything written as
a flag in the usage line or the detail of a page becomes something `-`
completes to, for any command, alias or function:

```sh
describe rg "search files with ripgrep" \
  "rg [options] <pattern> [path...]" \
  "  -i --ignore-case   match without regard to case
  -w --word-regexp   whole words only
  -l --files-with-matches
  --hidden           search hidden files too"
```

After this, `rg --h` completes to `--hidden`, and `help rg` prints the page.
Put the line in `~/.freshrc` or in a plugin, as described in
[Plugins](plugins.md#help-pages). A word counts as a flag when it starts with
`-` or `--` and stands at the start of a line, after a space or after `[`.

## What it does not do

FreSH does not run completion scripts written for another shell. The
`complete` and `compgen` builtins of bash, `compdef` of zsh, and the files fish
and PowerShell read are not supported, so the completions a tool ships for
those shells, through something like `tool completion bash`, do nothing in
FreSH.

It follows that FreSH does not complete what comes after a subcommand: `git
checkout` does not offer branch names, and `docker exec` does not offer
containers. Flags of a subcommand complete only when they are on the page of
the command itself.

The list of what bash does and FreSH does not is kept in
[Bash compatibility](bash.md#does-not-work).
