# Tests

The suite is written in FreSH and run by FreSH, so it exercises the shell the
same way a script does.

```sh
FreSH tests/run.frsh
```

It prints a line for every failure and a count at the end, and exits with the
number of failures, so CI fails when anything breaks. The release workflow runs
it on every build.

## Layout

| File | Covers |
| --- | --- |
| `lib.frsh` | `check`, `ok` and the counters |
| `cases/expansion.frsh` | parameters, substitution, positional arguments |
| `cases/arithmetic.frsh` | `$(( ))`, `(( ))`, `let`, `declare -i`, C style `for` |
| `cases/arrays.frsh` | indexed and associative arrays, slices, keys |
| `cases/control.frsh` | `if`, loops, `case`, functions, `local`, recursion |
| `cases/words.frsh` | quoting, splitting, globbing, brace expansion, `printf` |
| `cases/redirect.frsh` | redirection, pipes, here documents, subshells |
| `cases/status.frsh` | exit status of every construct |
| `cases/commands.frsh` | bundled commands: `grep`, `sed`, `awk`, `sort` and friends |

## Adding a case

Give the file a `suite` name and compare what you got with what you want:

```sh
suite arithmetic

check power "$((2 ** 8))" 256
ok truthy "$(true && echo yes)"
```

`check <label> <got> <want>` records a pass or prints the difference. `ok` is
the same with `yes` as the expected value. Put a new file in `cases/` and the
runner picks it up.

Write the expectation as bash would answer it. Several of these tests exist
because a case was written from the bash manual, failed here, and the shell was
fixed to agree.
