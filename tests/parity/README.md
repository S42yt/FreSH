# Command parity tests

The bundled unix commands are supposed to behave like the GNU originals. This
directory proves it the same way `tests/diff` proves the shell against bash:
every script in `cases/` runs through real GNU coreutils, grep, sed, findutils
and diffutils on the Linux runner, and through FreSH with
`FRESH_PREFER_BUNDLED=1` on every platform, so the bundled version is used even
where the system has the real command. Any difference in stdout or exit status
fails the build.

```sh
CASES=tests/parity/cases tests/diff/record.sh bash tests/parity/expected
FRESH_PREFER_BUNDLED=1 CASES=tests/parity/cases tests/diff/record.sh "$PWD/build/fresh" tests/parity/actual
KNOWN=tests/parity/known-differences.txt tests/diff/compare.sh tests/parity/expected tests/parity/actual
```

The cases are deterministic: no clock, no random order, no absolute paths in
the output (`realpath` output is rewritten through `$PWD`). Where a command
depends on the machine, only the shape of the output is checked, the way
`uname -s | grep -c .` does.

`known-differences.txt` lists the cases that cannot match on a given platform,
one per line with the reason, and `windows-differences.txt` the same for
Windows, where there are no inodes, no permission bits and no real users.
Every entry is a limitation written down in [docs/commands.md](../../docs/commands.md).
