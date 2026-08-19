# FreSH documentation

## Using the shell

- [Shortcuts](shortcuts.md), the everyday wins: jumping to a directory,
  auto cd, the clipboard, archives, key bindings, and what Tab and the arrow
  keys do
- [Configuration](configuration.md), every setting in `~/.freshrc`, where
  files live, how startup works, and `fresh doctor`
- [Themes](themes.md), the prompt format language and how to write a theme
- [Plugins](plugins.md), how plugins are loaded, how to write one, and how to
  give what you add its own help page

## Writing scripts

- [Scripting](scripting.md), `.frsh` and `.sh` scripts: syntax, expansion,
  control flow, functions, and the FreSH extensions
- [Bash compatibility](bash.md), the inventory of what works, what does not,
  and where FreSH differs on purpose, checked against real bash on every push
- [awk](awk.md), the awk FreSH ships, what it supports and what it does not
- [Commands](commands.md), reference for the builtins and the bundled unix
  commands, including which flags are actually implemented
- [How FreSH fails](errors.md), the rule that it never truncates silently, and
  what it does instead

## Installing and trusting it

- [Installing](installing.md), Scoop, winget, the installer, the portable
  build, and how to run FreSH from another program
- [Verifying a download](verifying.md), build provenance, checksums, and what
  the SmartScreen prompt actually means
- [Code signing](code-signing.md), what would be signed, who holds the key, and
  who reviews what

## Working on FreSH

- [Building from source](building.md), what the build needs and what it makes
- [Benchmarks](benchmarks.md), startup and throughput against cmd, Git Bash and
  PowerShell, with the method written down
- [Tests](../tests/README.md), the suite that runs on every build
- [Differential tests](../tests/diff/README.md), the same scripts through real
  bash and through FreSH, diffed
- [Fuzzing](../fuzz/README.md), the parser, the regular expression engine and
  awk under libFuzzer and the sanitizers

## The story behind the numbers

This folder is reference: what the shell does today. How it got there, what
was measured, and what was tried and thrown away lives in the
[blog](../blog/README.md).

New here? Read [Shortcuts](shortcuts.md) first, it is the shortest path to a
shell that feels like your own. Then skim
[Configuration](configuration.md).
