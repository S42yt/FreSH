# Three bugs worth 23 milliseconds

*15 August 2026, FreSH 26.10.0*

Startup was 45 ms and is now 22 ms. Three things did it, and all three were
bugs rather than tuning:

- every line was offered to the PowerShell and cmd routing first, and the
  routing resolved the first word against `PATH` before checking whether it
  even looked foreign. A `.freshrc` whose first line is a comment paid a full
  scan of every directory on `PATH`, about 20 ms
- resolving any command name built a sorted list of every executable on `PATH`,
  again about 20 ms, only to decide whether to try the `PATHEXT` extensions.
  It probes directories directly now
- `wininet` and `shell32` were in the import table for the sake of
  `fresh update`, `admin` and `open`, so every start loaded them and everything
  they pull in. They load on demand

Two more fixes came out of the same measurements: switching a stream to binary
mode after its buffer was set left standard output unbuffered, which made
printing 2000 lines cost 2000 write calls, and reading a line character by
character took the file lock every character.

## Running it yourself

```sh
FreSH tools/bench.frsh
FRESH_BIN=/path/to/FreSH.exe FreSH tools/bench.frsh
```

It skips any shell it cannot find, so it works on a machine without PowerShell
7 or Git Bash. Timings come from `$EPOCHREALTIME`, which FreSH provides with
microsecond resolution.

