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
