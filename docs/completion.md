# Tab completion

FreSH completes on Tab out of the box, with no setup and no plugin: commands,
files, variables, and the flags a command's help page lists. On top of that it
runs the bash completion scripts that tools already ship, so `git`, `gh`,
`kubectl`, `rustup` and the rest complete their subcommands, branches and
options the way they do in bash.

This page covers what Tab does on its own, how FreSH finds and runs a tool's
completion script, how to write one, and where it differs from bash.

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

### The arguments of a command

After the command name, FreSH first asks the command's completion, if it has
one: a script the tool ships, or a `complete` line of your own. The next
sections are about those.

When there is none, or it offers nothing, FreSH answers itself:

- a word starting with `-` completes to the flags the command's help page
  lists, so `grep -` offers only the flags FreSH's `grep` implements
- `cd`, `pushd`, `mkdir` and `rmdir` complete directories only
- `help`, `which`, `type`, `man` and `describe` complete command names
- everything else completes to a path, relative to the current directory or
  to the directory already typed. `~/` is your home directory, directories end
  in `/` so Tab keeps going into them, and files starting with `.` are only
  offered once you type the `.`

## Completion scripts

Most command line tools print a bash completion script when asked. FreSH runs
those scripts as they are.

### Where FreSH looks

The first time you press Tab after a command that has no completion yet, FreSH
looks for a script named after it and loads the first one it finds:

1. `~/.fresh/completions/`, your own folder
2. next to the program itself, in `../share/bash-completion/completions`,
   `../etc/bash_completion.d`, `../share/<command>/completion` and
   `../mingw64/share/<command>/completion`

In each folder the file can be called `<command>`, `<command>.bash`,
`<command>.sh` or `<command>-completion.bash`.

The second place is why git completes with nothing to set up. Git for Windows
ships `git-completion.bash` beside `git.exe`, Linux distributions put it in
`/usr/share/bash-completion/completions`, and Homebrew in its `etc` folder, and
FreSH finds each one from where `git` itself lives.

A script is loaded once per shell. Anything it prints while loading is thrown
away, so a noisy script cannot scribble over your prompt. `rehash` makes FreSH
look again, for a script you added while the shell was open.

`FRESH_HOME` moves the whole `~/.fresh` folder, completions included.

### Adding a tool's script

Save what the tool prints into `~/.fresh/completions`, named after the command:

```sh
gh completion -s bash      > ~/.fresh/completions/gh
kubectl completion bash    > ~/.fresh/completions/kubectl
helm completion bash       > ~/.fresh/completions/helm
docker completion bash     > ~/.fresh/completions/docker
rustup completions bash    > ~/.fresh/completions/rustup
rustup completions bash cargo > ~/.fresh/completions/cargo
```

The next Tab after `gh ` uses it. Nothing goes in `~/.freshrc`, and a shell
that never completes `gh` never loads the script, so startup stays as fast as
it was.

Some tools complete through a program rather than a script, and take a single
line in `~/.freshrc`:

```sh
complete -C terraform terraform
complete -C aws_completer aws
```

### Seeing what Tab would offer

`fresh complete` prints the matches for a line, as if Tab were pressed at its
end. It is the quickest way to check that a script loaded and answers:

```
λ fresh complete 'git sw'
switch
λ fresh complete 'gh pr li'
list
```

It returns 1 when nothing matches. `complete -p` lists every completion that
is loaded, as the commands that would recreate it.

## Writing your own

`complete` tells Tab how to complete the arguments of a command, with the same
options as in bash.

A fixed list of words:

```sh
complete -W 'start stop restart status' svc
```

`-W` is expanded each time Tab is pressed, so it can read something that
changes:

```sh
complete -W '$(ls ~/projects)' work
```

A function, for anything that depends on what came before. It is called with
the command, the word being completed and the word before it, sees the line
in `COMP_WORDS` and `COMP_CWORD`, and puts its answer in `COMPREPLY`:

```sh
_deploy() {
  local cur=${COMP_WORDS[COMP_CWORD]}
  case $COMP_CWORD in
    1) COMPREPLY=($(compgen -W 'staging production' -- "$cur")) ;;
    2) COMPREPLY=($(compgen -W '--dry-run --force' -- "$cur")) ;;
  esac
}
complete -F _deploy deploy
```

The options that matter most:

| Option | Meaning |
| --- | --- |
| `-W words` | complete from these words |
| `-F function` | call a function and offer its `COMPREPLY` |
| `-C command` | run a command and offer each line it prints; it gets `COMP_LINE` and `COMP_POINT` in its environment |
| `-f` `-d` `-c` `-v` `-a` `-b` `-k` `-e` `-u` | files, directories, commands, variables, aliases, builtins, keywords, exported variables, the user |
| `-A action` | the same by name, such as `-A function` or `-A arrayvar` |
| `-X pattern` | drop the matches that fit the pattern, or with a leading `!` keep only those; `&` stands for the word |
| `-P prefix` `-S suffix` | put text before or after every match |
| `-o nospace` | do not add a space after a single match |
| `-o filenames` | the matches are paths, so directories get a `/` |
| `-o dirnames` `-o plusdirs` | directories when nothing else matched, or always as well |
| `-o nosort` | keep the order the matches came in |
| `-p` `-r` | print or remove completions |

`compgen` takes the same options and prints the matches for one word, which is
how a function usually filters its candidates. `compopt` changes the options
of the completion that is running, from inside its function, for example
`compopt -o nospace` after offering `--output=`.

The variables a function sees:

| Variable | Holds |
| --- | --- |
| `COMP_WORDS` | the words of the command being completed, as an array |
| `COMP_CWORD` | the index in `COMP_WORDS` of the word the cursor is on |
| `COMP_LINE` | the command as typed |
| `COMP_POINT` | where the cursor is in `COMP_LINE` |
| `COMP_TYPE` | always `37`, the bash value for cycling through matches one at a time |
| `COMPREPLY` | the answer, as an array |

An alias of a single word completes like the command it names, so with
`alias k=kubectl` the line `k get po` completes as `kubectl get po` would.

## Teaching FreSH a tool's flags without a script

FreSH's own flag completion reads help pages, and `describe` adds one.
Anything written as a flag in the usage line or the detail of a page becomes
something `-` completes to:

```sh
describe rg "search files with ripgrep" \
  "rg [options] <pattern> [path...]" \
  "  -i --ignore-case   match without regard to case
  -w --word-regexp   whole words only
  --hidden           search hidden files too"
```

After this, `rg --h` completes to `--hidden`, and `help rg` prints the page.
It is the lighter choice when a tool ships no completion script and a list of
flags is all you want. See [Plugins](plugins.md#help-pages) for `describe`.

## Where it differs from bash

- When a completion offers nothing, FreSH falls back to its own completion,
  so a script that fails or does not know a word still leaves you with files
  and flags. In bash that takes `-o default` on every `complete` line.
- `COMP_WORDBREAKS` starts without the `:` bash puts in it, so a Windows path
  like `C:/Users` stays one word. Scripts read the variable, so they adjust,
  and git's script adds the colon back for itself.
- `COMP_TYPE` is `37`, because Tab cycles through the matches, which is what
  bash calls menu completion. Scripts that print descriptions next to their
  matches, such as those cobra generates for `gh` and `kubectl`, leave them
  out, so cycling inserts the word and not the description.
- What a completion function prints is thrown away rather than drawn over the
  prompt, `set -e` and `set -u` are off while it runs, and an `exit` inside it
  does not end the shell.
- Of the bash-completion library, `_get_comp_words_by_ref` and `_filedir` are
  built in, because the scripts tools generate call them. `_init_completion`
  is left out on purpose, so a script that checks for it takes its own
  fallback. Scripts in `/usr/share/bash-completion` that depend on the rest of
  that library do nothing, and Tab falls back to FreSH's own completion.
- `complete -D`, `-E` and `-I` are not supported. The actions `group`,
  `hostname`, `job`, `service`, `signal`, `setopt`, `shopt`, `binding`,
  `running`, `stopped` and `disabled` are accepted and offer nothing.
