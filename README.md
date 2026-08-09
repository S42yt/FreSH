# FreSH

A fast, zsh flavoured shell for Windows, written in C with no runtime
dependencies. No Git Bash, no WSL, no MSYS2. FreSH runs shell scripts itself
and brings its own unix commands.

![FreSH](./assets/FreSH_tui.png)

**Docs:** [scripting](docs/scripting.md) with `.frsh` files,
[configuration](docs/configuration.md), [themes](docs/themes.md),
[plugins](docs/plugins.md), [command reference](docs/commands.md).

## What you get

**A real shell**

- pipelines, `&&`, `||`, `;`, background `&`
- redirection: `>`, `>>`, `<`, `2>`, `2>&1`
- quoting, `\` escapes, globs (`*.c`, `src/?.h`)
- variables, `export`, `$?`, `$#`, `$@`, `$1`, `${VAR:-default}`, `${#VAR}`
- command substitution `$(...)` and `` `...` ``, arithmetic `$((1 + 2))`
- `if / elif / else / fi`, `while`, `until`, `for x in ...`, `case ... esac`,
  `{ ...; }`, `!`
- shell functions, `return`, `break`, `continue`, `shift`
- `.frsh` and `.sh` scripts run natively, FreSH is the interpreter

**zsh style line editing**

| Key | Action |
| --- | --- |
| `Tab` | complete commands, files, directories and `$variables` |
| `Tab` again | cycle through the candidates |
| `Up` / `Down` | history, filtered by what you have already typed |
| `Right` / `End` | accept the greyed out suggestion from history |
| `Ctrl+R` | search history |
| `Ctrl+A` / `Ctrl+E` | start and end of line |
| `Ctrl+Backspace` / `Ctrl+W` | delete the previous word |
| `Ctrl+U` / `Ctrl+K` | cut to start, cut to end |
| `Ctrl+Left` / `Ctrl+Right` | move by word |
| `Ctrl+L` | clear the screen |
| `Ctrl+C` | abandon the line |
| `Ctrl+D` | exit on an empty line |

Commands are colour highlighted as you type: known commands green, unknown
red, strings yellow, variables cyan, operators magenta.

## Themes

Themes are plain FreSH scripts in `~/.fresh/themes`. Six ship with the shell:

```
josh        two lines, lambda, git branch      (default)
minimal     one line, path and an arrow
classic     the bash look, user@host:path$
powerline   block segments, needs a powerline font
lambda      just the lambda, path on the right
full        time, user, host, path, git, exit code
```

```sh
theme              # list them, the active one is marked
theme minimal      # switch right now
```

Set `FRESH_THEME=minimal` in `~/.freshrc` to keep it. Writing your own is one
file:

```sh
# ~/.fresh/themes/mine.theme
FRESH_PROMPT='%F{magenta}%~%f%g\n%F{white}%#%f '
FRESH_RPROMPT='%t'
```

Prompt escapes:

| Escape | Meaning |
| --- | --- |
| `%n` `%m` | user name, host name |
| `%~` `%d` | short path, full path |
| `%g` `%b` | git segment with dirty marker, branch name only |
| `%t` `%D` | time, date |
| `%?` `%e` | exit code, exit code only when the last command failed |
| `%#` | prompt character, `FRESH_PROMPT_CHAR` |
| `%F{green}` `%f` | colour on, colour off |
| `%K{blue}` `%k` | background on, background off |
| `%S` `%s` | bold on, bold off |
| `\n` | new line, the prompt becomes two lines |

Colours are named (`red`, `green`, `brightcyan`, `grey`, ...) or a number from
0 to 255.

## Plugins

Plugins are FreSH scripts in `~/.fresh/plugins`, loaded by name:

```sh
FRESH_PLUGINS="git dirs sys"     # in ~/.freshrc
```

```
git     g gs ga gc gco gb gd gl gp gpl, groot, gclean
dirs    .. ... .... ll la l, mkcd, up
sys     path reload h c, ports, psg
edit    e rc, edit
```

```sh
plugin             # list them
plugin load git    # load one for this session
```

Adding your own means dropping `name.plugin` in the folder and adding `name`
to `FRESH_PLUGINS`. Aliases, functions and variables all work.

## Configuration

`~/.freshrc` is created on first run and sourced every start. It is grouped
into startup, theme and plugins, prompt, editing, history and aliases:

```sh
FRESH_BANNER=1            # banner on start
FRESH_BANNER_TEXT=""      # your own line instead
FRESH_THEME=josh
FRESH_PLUGINS="git dirs sys"
FRESH_PROMPT_CHAR="λ"
FRESH_PATH_DEPTH=3        # trailing path components to show
FRESH_SHOW_USER=1
FRESH_SHOW_GIT=1
FRESH_SHOW_GIT_DIRTY=1
FRESH_SHOW_RPROMPT=1
FRESH_TITLE=1             # current directory in the window title
FRESH_COLOR=1
FRESH_HIGHLIGHT=1
FRESH_SUGGEST=1
HISTSIZE=5000
```

History lives in `~/.fresh_history`.

## Bundled commands

Windows ships none of the tools a bash or zsh user expects, so FreSH carries
its own:

```
basename  cat     chmod   cmp     column  comm    cp      cut     date
df        diff    dirname du      env     expr    file    find    fold
groups    grep    head    hostname id     kill    ln      md5sum  mkdir
mktemp    mv      nl      open    paste   pkill   printf  ps      realpath
rev       rm      rmdir   sed     seq     sha1sum sha256sum shuf  sleep
sort      stat    tac     tail    tee     touch   tr      uname   uniq
wc        whoami  xargs   yes
```

These are fallbacks. If a real executable of the same name is on `PATH` it
wins, so an installed GNU `grep` keeps its regexes. The exceptions are `find`
and `sort`, where the Windows tools of that name do something entirely
different, so FreSH always uses its own.

`sed` supports `s/pattern/replacement/[g]`. It is not a regex engine.

## Builtins

```
alias   break   cd      clear   continue  echo    eval    exit    export
false   gitinfo help    history ls        plugin  pwd     read    rehash
return  set     shift   source  test  [   theme   true    type    unalias
unset   which   .
```

External programs are resolved through `PATH` using `PATHEXT`, plus `.frsh`,
`.ps1` and `.sh`. `.ps1` files are handed to PowerShell, `.bat` and `.cmd` to
cmd.exe, `.frsh` and `.sh` files are executed by FreSH itself. Full details in
the [command reference](docs/commands.md).

## Installation

![Setup](./assets/FreSH_wizard.png)

1. Download `FreSH-Setup.exe` from the releases page
2. Run it and pick **Just for me** (no admin) or **For all users**
3. Say whether FreSH should become your default shell
4. Open a new terminal and type `FreSH`

The installer registers FreSH the way Windows expects a shell to be
registered:

- adds a **Windows Terminal profile**, so FreSH sits next to PowerShell in the
  dropdown
- optionally makes it the **default profile**, backing up `settings.json` first
- registers under **App Paths**, so `FreSH` works from the Run dialog
- adds it to **PATH**
- adds **Open FreSH here** to the Explorer folder context menu
- registers FreSH as a handler for `.frsh` and `.sh` scripts
- creates Start Menu and Desktop shortcuts
- adds an entry to **Apps & Features** with a working uninstaller

Restart Windows Terminal after installing. Fragments are only read at startup.

Silent install:

```
FreSH-Setup.exe /silent /user /default
```

Uninstall from *Settings > Apps > FreSH*, or run `Uninstall-FreSH.exe` from the
install folder.

## Usage outside the prompt

```sh
FreSH script.sh arg1 arg2   # run a script
FreSH -c "echo hello"       # run one command
FreSH C:\some\folder        # start in a folder
FreSH --version
```

## Building from source

Requires GCC (MinGW-w64 or MSYS2). No other dependencies.

```sh
./build.sh
```

or, in PowerShell:

```powershell
.\build.ps1
```

Both produce `build/FreSH.exe` and `build/FreSH-Setup.exe`. The icon is
generated by `tools/makeicon.c` and the installer embeds `FreSH.exe` as a byte
array generated by `tools/bin2c.c`, so the setup binary is self contained.

CMake works too:

```sh
cmake -B build && cmake --build build
```

Format before committing:

```sh
astyle --style=google --indent=spaces=4 --suffix=none src/*.c src/*.h
```

## License

GNU General Public License v3.0, see [LICENSE](LICENSE).

## Contributing

Issues and pull requests welcome.
