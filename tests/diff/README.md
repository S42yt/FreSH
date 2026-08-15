# Differential tests

The suite in `tests/cases` checks FreSH against values written down by hand.
That proves FreSH matches what its author believed bash does. This directory
proves it against bash itself.

Every script in `cases/` is portable bash. It uses no FreSH extension, no test
helper, and no external tool beyond the ones both shells provide. It prints
deterministic output and exits with a status.

On every push, CI runs each script twice:

1. an `ubuntu-latest` job runs it through real `bash` and uploads the stdout,
   the exit status, and whether anything reached stderr
2. the `windows-latest` job runs the same script through the freshly built
   `FreSH.exe` and compares

Any difference fails the build. That turns "a bash script runs in FreSH
unchanged" from a promise into something checked on every commit.

## Running it yourself

```sh
tests/diff/record.sh bash tests/diff/expected
tests/diff/record.sh "$PWD/build/FreSH.exe" tests/diff/actual
tests/diff/compare.sh tests/diff/expected tests/diff/actual
```

## Adding a case

Write a `.sh` script in `cases/`. Keep it deterministic: no dates, no process
ids, no directory listings that depend on the machine, no locale-sensitive
sorting. Create the files you need in the working directory, which is a scratch
directory the harness makes and removes for you.

Error message wording is allowed to differ between the two shells, so only
stdout and the exit status are compared exactly. Whether a script wrote
anything at all to stderr is compared, which catches FreSH failing where bash
succeeded.

## Deliberate differences

Nothing in `known-differences.txt` is a bug that was shrugged off. An entry
there means the difference was examined and kept, and the same difference is
written down in [docs/bash.md](../../docs/bash.md). If a case starts failing,
the fix is to fix FreSH, not to add a line to that file.
