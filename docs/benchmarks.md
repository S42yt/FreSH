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

## What changed in 26.11

26.11 came out of [the Rust experiment](rust-experiment.md). A shell core was
written in Rust to see whether the language was holding FreSH back; it was not,
but it beat the C shell in three places, and all three turned out to be
structural. Those are what changed.

**Variables are looked up through a hash table.** Every lookup used to scan the
whole list, and `local` moved the list with a `memmove` on each call, so a shell
that had accumulated variables got slower at everything. Measured with 400
variables in scope, best of ten, on the machine above:

| Task | 26.10 | 26.11 |
| --- | --- | --- |
| 4,000 lookups | 116,797 us | **25,588 us** |
| 20,000 `while` iterations | 204,084 us | **72,819 us** |
| 5,000 parameter expansions | 211,701 us | **123,974 us** |
| 5,000 `[[ ]]` tests | 68,626 us | **27,561 us** |
| 5,000 `case` matches | 69,221 us | **25,206 us** |
| 2,000 function calls | 85,562 us | **43,403 us** |

With only a handful of variables set, the same tasks come out the same on both
builds, inside the noise of this machine. The win is not that lookups got
clever; it is that the number of variables you have stopped being a tax on
every other thing the shell does.

**Pipeline buffers stay open.** A stage that runs inside the shell writes into a
temporary file so the next stage can read it without a pipe that could
deadlock. That file was created, duplicated and closed once per stage boundary;
the handles are pooled now and a boundary truncates a file that is already
open. A three stage pipeline of builtins went from **1,093 us to under the
clock**.

**The binary is a third smaller.** `FreSH.exe` was 786 KB, of which about 400 KB
was an uncompressed icon at seven sizes; the 256 pixel entry alone was 262 KB.
Dropping it leaves Windows to scale the 128 pixel one, which for a two colour
glyph is not a visible difference.

| | 26.10 | 26.11 |
| --- | --- | --- |
| `FreSH.exe` | 786,432 bytes | **518,144 bytes** |
| `FreSH-Setup.exe` | 1,220,608 bytes | **681,984 bytes** |

Two things were tried and dropped, which is worth writing down. Link time
optimisation made the binary 32 KB *larger* with no measurable gain, and `-Os`
made it 60 KB smaller but the interpreter clearly slower. Neither earned its
place.

Startup did not move: 20.5 ms before, 20.7 ms after, which is the same number
twice. A smaller image does not start quicker here because the icon is a
resource the shell never reads, and `FRESH_TIMING=1` puts FreSH's own work at
about 4 ms of that 20. The rest is Windows creating a process, which is the same
floor `cmd` pays.

## The Rust compute core

The experiment said Rust loses at interpreting a shell language. It did not say
Rust loses at everything, so 26.11 also carries the smallest honest way to find
out: `rust/core`, a `no_std` static library linked straight into the C binary.
No allocator, no Rust runtime, no second copy of anything, and
`src/rustcore.c` holds a C version of every kernel so a machine without `cargo`
builds the same shell. `fresh` prints which core the binary carries.

Three kernels were written and measured. **One survived.**

`sort` calls its comparator through a function pointer in C, which the compiler
cannot inline; the Rust version inlines it into the sort. Best of five, 40,000
lines, both binaries built from the same commit on the same runner:

| Task | C core | Rust core |
| --- | --- | --- |
| `sort` 40k lines | 18,172 us | **16,224 us** |
| `sort -n` 40k numbers | 200,061 us | **173,219 us** |
| `sort -u` 40k lines | 19,219 us | **16,201 us** |

The other two were dropped for saying nothing. Counting lines and words came
out identical, because the win there was reading 64 KB blocks instead of one
`fgetc` per byte, which is a change the C path got as well. Merging the `PATH`
came out identical too, so that one stayed in C.

The kernel costs **11,776 bytes**, which is the whole price of having Rust in
the binary. It was 46,080 until the three sort instantiations were collapsed
into one; keeping them separate bought about a quarter more speed on `sort -n`
for another 34 KB, which is not a trade this shell should make.

**Startup is not one of the things Rust can fix**, and the numbers are worth
keeping so nobody tries again. Three builds from one commit, best of sixty:

| Build | Startup |
| --- | --- |
| C core | 6,050 us |
| Rust core | 6,067 us |
| C core with the registry `PATH` merge removed entirely | 6,065 us |

The same three on the benchmark machine land inside a millisecond of each
other. Startup is Windows creating a process and mapping an image; a language
cannot move that floor, and neither can a smaller binary.

What did move startup was doing less of it, and then carrying fewer bytes.

**Doing less.** The `PATH` merge, the registry reads behind it, `advapi32`
itself and the environment table are all deferred: the merge runs on the first
read of `$PATH` or the first command lookup, and a shell that never needs them
never pays. FreSH's own startup work fell from **4.2 ms to 0.7 ms**, measured
by `FRESH_TIMING=1`, which now also prints the time from process creation to
`main` so the two costs cannot be confused.

**Carrying fewer bytes.** With the shell's own work under a millisecond, an
experiment settled where the rest goes: a build whose `main` returns
immediately takes the same wall time as a full `--norc -c exit` run, so the
whole cost is Windows loading and scanning the image. That cost tracks size at
roughly 2 ms per 100 KB on the machine above, which is Defender reading
unsigned code on every launch. So the icon keeps only the sizes Windows
renders, and `FreSH.exe` went **519 KB to 447 KB**.

Interleaved in one session, best of 60 per round, across rounds:

| | best seen |
| --- | --- |
| `cmd /c exit` | 8.1 ms |
| **`FreSH --norc -c exit`, 26.11** | **11.2 ms** |
| `FreSH --norc -c exit`, 26.10 | 11.5 ms, and 3 to 7 ms worse under load |

The absolute numbers move several milliseconds with machine load, which is why
they are given as the best of interleaved rounds rather than a single pair.
The stable part is the gap: **2 to 3 ms behind `cmd`**, down from 5 to 6, and
none of it is FreSH's own work any more. What remains is that `cmd.exe` is a
small signed system binary Windows keeps warm and trusts, and FreSH is a
447 KB unsigned one Defender reads every time. The honest path to parity is
code signing, which is [in progress](code-signing.md); the dishonest one is
asking users to exclude their shell from their antivirus, which FreSH will not
do. `cmd` keeps the empty case; the [Doing something](#doing-something) table
above is what happens the moment either shell is asked to do anything.

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

## Would Rust be faster?

A shell core was written in Rust and measured against the C one on the same
scripts. Startup and binary size favoured the Rust build, loops, arithmetic,
tests and `case` were about twice as slow, and the two places Rust won it won on
structure rather than on language, which is where 26.11 came from. The numbers,
the method and the conclusion are in [the Rust experiment](rust-experiment.md).

## What is not measured here

Interactive latency, completion speed and prompt drawing. They are what the
shell feels like, and they are hard to measure honestly without a rig that
drives a real console. The prompt's git segment runs on a background thread
with a short cache for exactly this reason, so a large repository does not
stall typing.
