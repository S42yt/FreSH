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
the same clock, the same machine, best of twenty runs. Best-of rather than
average, because the minimum is the run least disturbed by everything else the
machine was doing. Windows Defender was left on, as it is on a real machine.

## Startup

How long a shell takes to start, run nothing, and exit.

| Shell | Best of 20 |
| --- | --- |
| `cmd /c exit` | 16 ms |
| **`FreSH -c exit`** | **42 ms** |
| `bash -c exit` (Git Bash) | 241 ms |
| `powershell -c exit` (5.1) | 271 ms |
| `pwsh -c exit` (7.6) | 570 ms |

FreSH starts about **6× faster than Git Bash and 13× faster than PowerShell 7**.
That 42 ms includes reading `~/.freshrc`, loading the theme and loading the
plugins; it is what you actually wait for, not a stripped down number.

Only `cmd` is quicker, and it is quicker because it is already resident in
Windows and does far less.

## Work

| Task | FreSH |
| --- | --- |
| 20,000 `while` iterations with arithmetic | 359 ms |
| 20,000 parameter expansions | 194 ms |
| 5,000 `[[ ]]` tests | 94 ms |
| glob a directory of 60 files | under 1 ms |
| one command substitution | 1 ms |
| a three stage pipeline | 36 ms |

For scale, the same 20,000 iteration loop and the same 20,000 expansions in Git
Bash on this machine take 391 ms and 278 ms. FreSH is in the same class as bash
on loops and quicker on expansion, which is the honest summary: the large win
is startup, not raw interpretation.

The three stage pipeline is 36 ms because each stage is a Windows process, and
process creation on Windows costs about 10 ms whoever asks for it. A pipeline
of builtins inside one process is far cheaper, which is why FreSH ships its own
`grep`, `sort`, `awk` and the rest.

## Running it yourself

```sh
FreSH tools/bench.frsh
FRESH_BIN=/path/to/FreSH.exe FreSH tools/bench.frsh
```

It skips any shell it cannot find, so it works on a machine without PowerShell
7 or Git Bash. Timings come from `$EPOCHREALTIME`, which FreSH provides with
microsecond resolution.

## What is not measured here

Interactive latency, completion speed and prompt drawing are not in the table.
They are what the shell feels like, and they are hard to measure honestly
without a rig that drives a real console. The prompt's git segment runs on a
background thread with a short cache for exactly this reason, so a large
repository does not stall typing.
