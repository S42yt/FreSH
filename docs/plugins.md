# Plugins

A plugin is a FreSH script in `~/.fresh/plugins` named `<name>.plugin`. It is
sourced into your shell at startup, so it can define aliases, functions and
variables. There is no manifest, no metadata and no install step.

```sh
plugin                 # list what is installed, loaded ones are marked
plugin load git        # load one right now, for this session
```

Load them on every start with:

```sh
# ~/.freshrc
FRESH_PLUGINS="git dirs sys"
```

Names are separated by spaces and loaded in that order.

## The bundled plugins

### git

```
g gs ga gc gco gb gd gl gp gpl
groot     cd to the top of the repository
gclean    list branches already merged
```

### dirs

```
.. ... ....   go up one, two, three levels
ll la l       ls variations
mkcd dir      create a directory and enter it
up            go up one level and list
```

### sys

```
path      print PATH
reload    source ~/.freshrc
h c       history, clear
ports     listening sockets
psg name  processes matching a name
```

### edit

```
e         run $EDITOR
rc        edit ~/.freshrc
edit x    open a file with its default program
```

## Writing one

Create `~/.fresh/plugins/docker.plugin`:

```sh
# docker shortcuts
alias d='docker'
alias dc='docker compose'
alias dps='docker ps'
alias di='docker images'

dsh() {
  docker exec -it "$1" sh
}

dclean() {
  docker container prune -f
  docker image prune -f
}
```

Add it:

```sh
FRESH_PLUGINS="git dirs sys docker"
```

or try it without editing anything:

```sh
plugin load docker
```

## What a plugin can do

Anything a script can do, because it is just a script. The useful patterns:

**Aliases** for short commands.

```sh
alias k='kubectl'
```

**Functions** when you need arguments in the middle, or several steps.

```sh
backup() {
  cp "$1" "$1.$(date +%Y%m%d)"
}
```

**Environment** for tools you always want configured.

```sh
export EDITOR=nvim
export PAGER=cat
```

**PATH additions**, guarded so repeated loading does not stack up.

```sh
test -d "$HOME/tools" && export PATH="$HOME/tools;$PATH"
```

**Conditional setup**, so a plugin can be harmless when the tool is missing.

```sh
if which cargo > nul 2>&1; then
  alias cb='cargo build'
  alias ct='cargo test'
fi
```

## Rules of thumb

- Keep it fast. Everything in a plugin runs before your first prompt appears.
  Avoid network calls, and avoid `$(...)` that shells out to a slow program.
- Do not print anything on load. A plugin that writes to the screen makes
  every new shell noisy.
- Do not change the current directory. Users do not expect a new shell to
  start somewhere unexpected.
- Guard anything machine specific with a `test`, so the same plugin works on
  a machine where the tool is not installed.
- Prefix helper functions that are not meant to be called directly, so they
  do not collide with a real command. FreSH resolves a function before a
  program on `PATH`, so naming a function `git` will shadow git everywhere.

## Load order and overriding

Plugins are sourced after `~/.freshrc` and after the theme. So:

- a plugin alias wins over an alias set earlier in `.freshrc`
- a plugin that sets `FRESH_PROMPT` wins over the theme

If you want the opposite, drop `FRESH_PLUGINS` and load them by hand at the
top of `.freshrc`:

```sh
plugin load git dirs
alias gs='git status --short'   # yours wins now
```

## Sharing

A plugin is one file with no dependencies, so sharing it is sending the file.
To install someone else's, copy it into `~/.fresh/plugins` and add its name to
`FRESH_PLUGINS`. Read it first: it runs with your privileges every time you
open a shell.
