# Everyday shortcuts

The things an oh-my-zsh user reaches for, built into FreSH rather than bolted
on. None of them need a plugin, and each can be turned off in `~/.freshrc`.

## Jump to a directory you have used

FreSH remembers where you spend time, weighted so that recent and frequent
places win.

```sh
z fresh          # jump to the best match for "fresh"
z src parser     # every word has to appear in the path
z                # list what is remembered, lowest score first
```

The list lives in `~/.fresh_visits`. Delete it to start again, or set
`FRESH_JUMP=0` to stop recording.

## Enter a directory by naming it

```sh
λ src
λ ../build
```

A word on its own that is a directory becomes a `cd`. Set `FRESH_AUTOCD=0` if
you would rather it stayed an error.

## The clipboard

```sh
copy hello world          # put text on the clipboard
ls | copy                 # or whatever came down the pipe
paste                     # print what is on it
paste > notes.txt
copypath                  # the current directory
copypath build.frsh       # the full path of a file
```

## Archives

```sh
extract release.zip            # into the current directory
extract release.tar.gz build   # into build, created if missing
extract bundle.7z
```

`.zip` uses PowerShell, `.tar` `.gz` `.tgz` `.bz2` `.xz` use the `tar` that
Windows ships, and `.7z` `.rar` need 7-Zip on `PATH`.

## A shell that can write to Program Files

```sh
admin                     # a new elevated FreSH here
admin fresh update        # run one command elevated
```

Windows asks for consent, so this cannot be silent.

## Keys of your own

`bind` puts any key to work. Put the lines in `~/.freshrc` to keep them.

```sh
bind ctrl+g "git status"        # press it, the command runs
bind f5 "build.frsh"
bind ctrl+o insert:"docker "    # types the text, leaves the cursor after it
bind ctrl+a beginning-of-line   # move an editing action somewhere else
bind                            # what is bound
bind -l                         # the actions you can bind
bind -r ctrl+g                  # give the key back
```

Keys are named the way you say them: `ctrl+a` to `ctrl+z`, `f1` to `f12`,
`home`, `end`, `delete`, `pageup`, `pagedown`, `up`, `down`, `left`, `right`,
`tab`, `shift+tab`, `enter`, `escape`, `backspace`, and `ctrl+left`,
`ctrl+right`, `ctrl+delete`.

The actions:

| Action | Does |
| --- | --- |
| `accept-line` | run what is typed |
| `beginning-of-line` / `end-of-line` | jump to either end |
| `backward-char` / `forward-char` | one character |
| `backward-word` / `forward-word` | one word |
| `delete-char` / `backward-delete` | remove one character |
| `delete-word` / `backward-kill-word` | remove one word |
| `kill-line` / `kill-to-start` | cut to the end or the start |
| `history-previous` / `history-next` | walk history |
| `history-search` | search it |
| `complete` | the Tab completion |
| `accept-suggestion` | take the grey suggestion |
| `clear-screen` | wipe the screen |
| `cancel-line` | throw the line away |

Anything that is not an action and does not start with `insert:` is a command,
so `bind f2 "fresh update"` works as it reads. A binding wins over the built in
meaning of that key, so bind carefully: `bind ctrl+c "echo hi"` takes away the
way to interrupt.

## Already there

- **Tab** completes commands, files, variables and the flags a command's help
  page lists, ignoring case.
- **Up** and **Down** filter history by what you have typed, and **Ctrl+R**
  searches it.
- **Right** or **End** accepts the grey suggestion from history.
- A command is green while it can run and red once it cannot, so a typo shows
  before you press Enter.
- `!!` and `!$` bring back the last command or its last word.
- `help <command>` prints what a command does and the arguments it takes.
- `describe` gives your own functions the same treatment, see
  [plugins](plugins.md#help-pages).
- Themes and plugins live in `~/.fresh`, see [themes](themes.md) and
  [plugins](plugins.md).
