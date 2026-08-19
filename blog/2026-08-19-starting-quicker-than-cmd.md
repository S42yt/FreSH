# Starting quicker than cmd

*19 August 2026, FreSH 26.11.0*

26.10 got startup from 45 ms to 22 ms by removing three bugs. 26.11 set out
to find whether anything was left, and the answer turned into a lesson about
measuring the thing you think you are measuring.

26.11 came out of [the Rust experiment](2026-08-17-would-rust-be-faster.md). A shell core was
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

## The Rust core

The experiment said Rust loses at interpreting a shell language. It did not say
Rust loses at everything, so 26.11 carries `rust/core`: a `no_std` static
library linked straight into the C binary, with no allocator and no Rust
runtime. As of this release it is **the only implementation** of the kernels it
holds: sorting, the line and word counting behind `wc`, and the `PATH` merge
all live in Rust, the C fallbacks are gone, and building FreSH needs `cargo`.
`fresh` prints `rust core` so a binary can say so itself.

The kernels earned their place with numbers before the C fell away. `sort`
calls its comparator through a function pointer in C, which the compiler
cannot inline; the Rust version inlines it into the sort. Best of five, 40,000
lines, both cores built from the same commit on the same runner:

| Task | C core | Rust core |
| --- | --- | --- |
| `sort` 40k lines | 18,172 us | **16,224 us** |
| `sort -n` 40k numbers | 200,061 us | **173,219 us** |
| `sort -u` 40k lines | 19,219 us | **16,201 us** |

Counting and the `PATH` merge measured identical in either language, because
their wins were structural, reading 64 KB blocks instead of one `fgetc` per
byte. They moved to Rust anyway when the C fallbacks were retired, at no
measured cost either way.

The Rust side costs about **12 KB** of binary, which was 46 KB until the three
sort instantiations were collapsed into one; keeping them separate bought about
a quarter more speed on `sort -n` for another 34 KB, which is not a trade this
shell should make.

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

What moved startup was doing less of it, and then importing less of Windows.

**Doing less.** The `PATH` merge, the registry reads behind it and the
environment table are all deferred: the merge runs on the first read of `$PATH`
or the first command lookup, and a shell that never needs them never pays.
FreSH's own startup work fell from **4.2 ms to 0.7 ms**, measured by
`FRESH_TIMING=1`, which now also prints the time from process creation to
`main` so the two costs cannot be confused.

**Importing less.** With the shell's own work under a millisecond, everything
left was in front of `main`, and the first theory was wrong. It looked like
size, because a smaller binary had measured quicker, so the guess was Defender
reading unsigned bytes at roughly 2 ms per 100 KB. Padded probes killed it: an
otherwise empty program grown to **462 KB starts in 8.9 ms**, while FreSH at
**418 KB took 16.6 ms**. Bytes were not the tax, and a probe stuffed with the
strings a scanner ought to dislike started just as fast, so it was not the
contents either.

The import table was. FreSH pulled in three libraries before `main` for three
commands almost nobody runs in a given shell: `advapi32` for the `Crypt`
functions behind `md5sum` and friends, `shell32` for the one call `id` and
`groups` make, and `user32` for the five clipboard calls behind `copy` and
`paste`. `shell32` alone drags a long dependency chain in with it. All three
load on first use now, and a shell that starts and exits imports `kernel32`
and the C runtime and nothing else:

| | before | after |
| --- | --- | --- |
| libraries at load | kernel32, msvcrt, advapi32, shell32, user32 | **kernel32, msvcrt** |
| `FreSH --norc -c exit` | 16.6 ms | **9.5 ms** |

Interleaved in one session, best of 60 per round, across rounds:

| | best seen |
| --- | --- |
| **`FreSH --norc -c exit`** | **9.5 ms** |
| **`FreSH -c exit`**, reading `.freshrc` | **11.8 ms** |
| `cmd /c exit` | 12.9 ms |
| `FreSH --norc -c exit`, 26.10 | 20.5 ms |

**FreSH now starts quicker than `cmd`**, with or without its configuration,
having been half again slower at the start of 26.11. The empty case was the
last thing `cmd` won; the [Doing something](../docs/benchmarks.md#doing-something) table above is
what happens once either shell is asked to do anything.

Two things are worth keeping from how this was found. The measurement that
mattered was a control, not a profile: an empty program built the same way,
padded to the same size, told us in one run that 7 ms had nothing to do with
bytes. And the number that finally moved was not in FreSH's code at all, which
is why `FRESH_TIMING=1` prints `before main` now.

