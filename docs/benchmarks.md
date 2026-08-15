# Benchmarks

"Fast" is worth nothing without a number, so here are the numbers, how they
were taken, and how to take them yourself.

## The machine

| | |
| --- | --- |
| CPU | AMD Ryzen 3 7330U |
| Memory | 7 GB |
| OS | Windows 11 Pro, 10.0.26100 |
| FreSH | 26.10.0 |
| Git Bash | GNU bash 5.2.21 |
| PowerShell | 7.6.3 |
| Windows PowerShell | 5.1 |

Every shell is measured the same way by `tools/bench.frsh`: the same harness,
the same clock, the same machine, best of many runs. Best-of rather than
average, because the minimum is the run least disturbed by everything else the
machine was doing. Windows Defender was left on, as it is on a real machine.

## Startup

How long a shell takes to start, run nothing, and exit.

| Shell | Best of 40 |
| --- | --- |
| `cmd /c exit` | 14 ms |
| **`FreSH --norc -c exit`** | **19 ms** |
| **`FreSH -c exit`** | **22 ms** |
| `bash -c exit` (Git Bash) | 236 ms |
| `powershell -c exit` (5.1) | 271 ms |
| `pwsh -c exit` (7.6) | 570 ms |

FreSH starts about **10× faster than Git Bash and 25× faster than PowerShell 7**.
The 22 ms includes reading `~/.freshrc`, loading the theme and loading four
plugins; `--norc` skips all of that, the way bash skips its rc file for a non
interactive shell.

Only `cmd` starts quicker, and only when it does nothing at all. See below.

## Doing something

Starting and exiting is not what a shell is for. The same tasks, once through
`cmd.exe` running a `.bat`, and once through FreSH:

| Task | cmd | FreSH |
| --- | --- | --- |
| print one line | 23 ms | **18 ms** |
| sort a 2000 line file | 23 ms | **20 ms** |
| count matching lines in a 2000 line file | 24 ms | **22 ms** |

FreSH is quicker at all three, because `sort`, `grep` and `echo` run inside the
shell, while cmd spawns `sort.exe`, `findstr.exe` and `find.exe`. On Windows a
process costs about 10 ms whoever asks for it, so the shell that spawns fewer
of them wins.

The honest summary: **cmd wins the empty case and loses every real one.** Its
empty case is hard to beat because `cmd.exe` is a small system binary that
Windows keeps warm; for scale, `hostname.exe` from System32 takes 20 ms to
start on this machine, more than FreSH does.

## Work inside one shell

Measured with `$EPOCHREALTIME` inside a single FreSH process, so no process
creation is involved:

| Task | FreSH |
| --- | --- |
| sort a 2000 line file | 1.0 ms |
| 20,000 `while` iterations with arithmetic | 359 ms |
| 20,000 parameter expansions | 194 ms |
| 5,000 `[[ ]]` tests | 94 ms |
| glob a directory of 60 files | under 1 ms |
| one command substitution | 1 ms |
| a three stage pipeline | 36 ms |

The same 20,000 iteration loop and 20,000 expansions in Git Bash take 391 ms
and 278 ms, so FreSH is level with bash on loops and quicker on expansion. The
large win is startup and the absence of process spawns, not raw interpretation.
A loop is still the slowest thing a shell does: cmd's `for /l` is quicker than
FreSH's `while`, and neither is a good way to count to 20,000.

## Where the startup time goes

Set `FRESH_TIMING=1` and FreSH prints how long each stage took:

```
$ FRESH_TIMING=1 FreSH -c exit
         3 us  entry
       446 us  descriptors
      1454 us  terminal
      1945 us  variables
      2895 us  path
      4041 us  freshrc
      5055 us  fresh home
      5640 us  theme
      8383 us  plugins
      8900 us  command
      9500 us  exit
```

About 9 ms is FreSH's own work, of which the configuration, the theme and the
plugins are most; the rest of the wall clock time is Windows creating the
process and loading the image, which is roughly what `cmd.exe` costs too.

## What changed in 26.10

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

## What is not measured here

Interactive latency, completion speed and prompt drawing. They are what the
shell feels like, and they are hard to measure honestly without a rig that
drives a real console. The prompt's git segment runs on a background thread
with a short cache for exactly this reason, so a large repository does not
stall typing.
