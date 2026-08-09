# Themes

A theme is a FreSH script in `~/.fresh/themes` named `<name>.theme`. It sets
`FRESH_PROMPT`, usually `FRESH_RPROMPT`, and nothing else. That is the entire
contract.

```sh
theme              # list what is installed, the active one is marked
theme minimal      # switch for this session
```

Keep it by setting `FRESH_THEME=minimal` in `~/.freshrc`.

## The bundled themes

| Name | Look |
| --- | --- |
| `josh` | two lines, user, path, git branch, lambda. The default |
| `minimal` | one line, path and an arrow |
| `classic` | the bash look, `user@host:/full/path$` |
| `powerline` | coloured blocks, needs a font with powerline glyphs |
| `lambda` | just the lambda, path and branch on the right |
| `full` | time, user, host, path, git, exit code |

They are ordinary files. Open one and edit it, or copy it to a new name.

## Writing one

```sh
# ~/.fresh/themes/mine.theme
FRESH_PROMPT='%F{magenta}%~%f%g\n%F{white}%#%f '
FRESH_RPROMPT='%t'
```

Then:

```sh
theme mine
```

Use single quotes. The escapes are expanded when the prompt is drawn, not when
the file is read, so the prompt stays current as you move around.

## Escapes

### Information

| Escape | Prints |
| --- | --- |
| `%n` | user name |
| `%m` | computer name |
| `%~` | current directory, `~` for home, last `FRESH_PATH_DEPTH` parts |
| `%d` | current directory in full |
| `%g` | git segment: ` (branch)` with a red `!` when dirty, empty outside a repository |
| `%b` | branch name on its own, empty outside a repository |
| `%t` | time as `HH:MM` |
| `%D` | date as `YYYY-MM-DD` |
| `%?` | exit status of the last command, always |
| `%e` | exit status only when the last command failed, in red, with an arrow |
| `%#` | the prompt character, `FRESH_PROMPT_CHAR` |
| `%%` | a literal `%` |

### Colour and weight

| Escape | Effect |
| --- | --- |
| `%F{name}` | foreground colour on |
| `%f` | foreground back to default |
| `%K{name}` | background colour on |
| `%k` | background back to default |
| `%S` | bold on |
| `%s` | bold off, and resets colour |

Colour names: `black`, `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`,
`white`, `grey`, and the `bright` versions, `brightred` through
`brightwhite`. A number from 0 to 255 selects a 256 colour palette entry:

```sh
FRESH_PROMPT='%F{208}%~%f %# '
```

Colour escapes produce nothing at all when `FRESH_COLOR=0`, so a theme stays
readable on a terminal without colour.

### Layout

`\n` starts a second line. Everything before it is the information line,
everything after is what you type on.

```sh
FRESH_PROMPT='%F{cyan}%~%f%g\n%F{grey}>%f '
```

`FRESH_RPROMPT` is drawn right aligned on the first line. It is skipped when
the line is too narrow, rather than wrapping.

`FRESH_PROMPT2` is the prompt for continued lines, shown when you press Enter
on an unfinished command such as an open `if`.

## Examples

A single line with the exit code inline:

```sh
FRESH_PROMPT='%F{blue}%~%f %F{red}%e%f%# '
FRESH_RPROMPT=''
```

Time on the right, branch on the left:

```sh
FRESH_PROMPT='%F{green}%b%f %F{cyan}%~%f\n%# '
FRESH_RPROMPT='%F{grey}%t%f'
```

Host first, useful over SSH or in a container:

```sh
FRESH_PROMPT='%S%F{yellow}%m%f%s %~%g\n%# '
```

Powerline style blocks, with a font that has the glyphs:

```sh
FRESH_PROMPT='%K{blue}%F{black} %~ %k%f%F{blue}%f%g\n%# '
```

## Notes

- The prompt is redrawn on every keystroke, so keep it cheap. `%g` is already
  cached and the dirty check runs on a background thread, but a command
  substitution in `FRESH_PROMPT` would run constantly. Do not put `$(...)` in
  a prompt.
- Width is measured ignoring escape sequences and counting UTF-8 characters
  once, so accented characters and box drawing line up correctly.
- `FRESH_THEME=none` skips theme loading entirely, for when you want to set
  `FRESH_PROMPT` directly in `~/.freshrc`.
