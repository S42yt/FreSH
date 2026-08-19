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

| Shell | Best of 60 |
| --- | --- |
| **`FreSH --norc -c exit`** | **9.5 ms** |
| **`FreSH -c exit`** | **11.8 ms** |
| `cmd /c exit` | 12.9 ms |
| `bash -c exit` (Git Bash) | 236 ms |
| `powershell -c exit` (5.1) | 271 ms |
| `pwsh -c exit` (7.6) | 570 ms |

FreSH starts about **25× faster than Git Bash and 60× faster than PowerShell 7**,
and since 26.11 it starts quicker than `cmd` as well, with or without its
configuration. The 11.8 ms includes reading `~/.freshrc`, loading the theme and
loading four plugins; `--norc` skips all of that, the way bash skips its rc file
for a non interactive shell.

`cmd` used to win the empty case. [What changed in 26.11](#what-changed-in-2611)
is how it stopped.

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

The honest summary: **cmd used to win the empty case and lose every real one.**
Since 26.11 it loses the empty case too, by about 3 ms. For scale on how little
of this is really about the shell, `hostname.exe` from System32 takes 20 ms to
start on this machine, twice what FreSH does.

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
$ FRESH_TIMING=1 FreSH --norc -c exit
     14406 us  before main
         3 us  entry
       182 us  descriptors
       299 us  terminal
       472 us  variables
       525 us  path
       647 us  command
       716 us  exit
```

Under a millisecond is FreSH's own work; with a `.freshrc`, a theme and
plugins the configuration adds what it costs to run, which `FRESH_TIMING=1`
will show per stage. The `before main` line is Windows creating the process
and loading the image, which is where the rest of the wall clock goes. The
`PATH` merge and the registry reads behind it no longer appear at all, because
they run on first use rather than at startup.

## How the numbers got here

This page is the current state. How each number was won, what was tried and
thrown away, and where a theory turned out to be wrong is written up in the
[blog](../blog/README.md):

- [Starting quicker than cmd](../blog/2026-08-19-starting-quicker-than-cmd.md),
  the import table, and the padded probe that killed the theory before it
- [Would Rust be faster?](../blog/2026-08-17-would-rust-be-faster.md), a whole
  shell core written in Rust to answer it with numbers
- [Three bugs worth 23 milliseconds](../blog/2026-08-15-three-bugs-worth-23-milliseconds.md)

## What is not measured here

Interactive latency, completion speed and prompt drawing. They are what the
shell feels like, and they are hard to measure honestly without a rig that
drives a real console. The prompt's git segment runs on a background thread
with a short cache for exactly this reason, so a large repository does not
stall typing.
