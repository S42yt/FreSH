# Configuration

## Where things live

| Path | What it is |
| --- | --- |
| `~/.freshrc` | your settings, a FreSH script sourced every start |
| `~/.fresh/themes/*.theme` | prompt themes |
| `~/.fresh/plugins/*.plugin` | plugins |
| `~/.fresh_history` | command history |

`~` is `%USERPROFILE%` unless you set `HOME`. The `~/.fresh` folder can be
moved with `FRESH_HOME`.

All of these are created on first run. Deleting `~/.freshrc` and starting the
shell again gives you a fresh copy of the defaults, which is the easiest way
to see what changed after an update.

## Startup order

1. `PATH` gains your user bin folders (`~/bin`, `~/.local/bin`,
   `~/AppData/Local/bin`, `~/scoop/shims`, `~/AppData/Roaming/npm`) if they
   exist
2. `~/.freshrc` is sourced
3. `~/.fresh` is created if missing and filled with the bundled themes and
   plugins
4. the theme named by `FRESH_THEME` is sourced
5. every plugin named in `FRESH_PLUGINS` is sourced, in order
6. history is loaded and the prompt appears

Because the theme is sourced after `~/.freshrc`, a theme overrides a
`FRESH_PROMPT` you set in `.freshrc`. To write your own prompt inline, set
`FRESH_THEME=none`.

Because plugins are sourced last, a plugin can override an alias from your
`.freshrc`. Load plugins first and define your own aliases after by putting
`plugin load name` where you want it instead of using `FRESH_PLUGINS`.

## Settings

Every setting is an ordinary shell variable. Anything that takes a switch
accepts `1`, `0`, `true`, `false`, `on`, `off`, `yes`, `no`.

### Startup

| Variable | Default | Meaning |
| --- | --- | --- |
| `FRESH_BANNER` | `1` | show the banner in interactive shells |
| `FRESH_BANNER_TEXT` | empty | print this instead of the default banner |
| `HOME` | `%USERPROFILE%` | where `~` points, and where `cd` with no argument goes |
| `FRESH_HOME` | `~/.fresh` | folder holding `themes` and `plugins` |

```sh
FRESH_BANNER_TEXT="$(date +%A), let's go"
```

### Theme and plugins

| Variable | Default | Meaning |
| --- | --- | --- |
| `FRESH_THEME` | `josh` | theme to load, or `none` |
| `FRESH_PLUGINS` | `git dirs sys` | space separated plugin names |

### Prompt

| Variable | Default | Meaning |
| --- | --- | --- |
| `FRESH_PROMPT` | from theme | prompt format, see [themes](themes.md) |
| `FRESH_RPROMPT` | from theme | right aligned part of the first line |
| `FRESH_PROMPT2` | `…` | prompt for continued lines |
| `FRESH_PROMPT_CHAR` | `λ` | what `%#` prints |
| `FRESH_PATH_DEPTH` | `3` | trailing path components shown by `%~` |
| `FRESH_SHOW_USER` | `1` | user name in the default prompt |
| `FRESH_SHOW_GIT` | `1` | git segment, `%g` |
| `FRESH_SHOW_GIT_DIRTY` | `1` | `!` marker when the tree has changes |
| `FRESH_SHOW_RPROMPT` | `1` | exit code on the right when no `FRESH_RPROMPT` |
| `FRESH_TITLE` | `1` | put the current directory in the window title |

The dirty marker costs one `git status` per prompt, run on a background
thread with a short cache, so it never blocks typing. Turn it off on very
large repositories if you notice the marker lagging behind.

### Editing

| Variable | Default | Meaning |
| --- | --- | --- |
| `FRESH_COLOR` | `1` | colour in the prompt, `ls`, and other builtins |
| `FRESH_HIGHLIGHT` | `1` | colour the command line as you type |
| `FRESH_SUGGEST` | `1` | grey inline suggestion from history |
| `FRESH_FOREIGN` | `1` | send PowerShell cmdlets and cmd builtins to their own shell |

Colour is disabled automatically when output is redirected, so
`ls > files.txt` never writes escape codes into the file.

Tab completes commands, files, variables and flags. Command names match
without regard to case and are corrected as they complete, so `get-childi`
becomes `Get-ChildItem`. After a command, a word starting with `-` completes
from the flags its help page lists, so `grep -` offers only the flags FreSH
actually implements. `cd` and `rmdir` complete directories alone, and `help`,
`which`, `type` and `describe` complete command names rather than files.

While you type, a command that can be run is bright green. A word is only red
once it cannot become a real command, so a half typed name stays plain and a
genuine typo turns red. Keywords such as `if`, `for` and `done` are green,
function definitions like `name()` are green, and a redirect target such as
`> /dev/null` is left plain rather than treated as a command.

### History

| Variable | Default | Meaning |
| --- | --- | --- |
| `HISTSIZE` | `5000` | entries kept, in memory and on disk |
| `HISTFILE` | `~/.fresh_history` | where history is written |

Consecutive duplicates are collapsed, and a command typed with a leading
space is not recorded.

## Aliases and functions

`.freshrc` is a script, so anything from [scripting](scripting.md) works:

```sh
alias ll='ls -l'
alias gs='git status'

mkcd() { mkdir -p "$1"; cd "$1"; }

export EDITOR=nvim
export PATH="$HOME/tools;$PATH"
```

Alias expansion happens at the start of a command, so `alias ..='cd ..'` makes
`..` a command. An alias body can contain pipes and arguments.

## Reloading

```sh
source ~/.freshrc
```

The `sys` plugin defines this as `reload`. Note that sourcing again re-runs
everything, including `cd` lines, and does not remove aliases you have since
deleted from the file. For that, start a new shell.

## Per project settings

FreSH does not read a config file from the current directory. If you want
project settings, source them yourself:

```sh
# in ~/.freshrc
test -f ./.fresh_project && source ./.fresh_project
```

Be aware this runs whatever is in that file, so only do it if you trust the
directories you work in.
