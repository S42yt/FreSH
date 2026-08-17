# The Rust experiment

FreSH is written in C. This is the answer to a fair question: what would it
look like in Rust, and would it be faster?

`rust/` holds `fresh-rs`, a shell core in Rust. It is not a port of FreSH and
it is not going to replace it. It exists to be measured, so the question is
settled with numbers instead of opinions. It lives on the
`rust/26.11-experiment` branch and is never merged into master.

## What was built

A shell core covering what the benchmarks touch: lexer, parser, expansion,
arithmetic, pattern matching, globbing, redirection, pipelines, functions with
local scope, and the builtins the scripts call (`echo`, `printf`, `cat`,
`test`, `[`, `[[`, `local`, `export`, `read`, `mkdir`, `rm`, `command`,
`source`, `eval`, `cd`, `exit`). About 2,300 lines of Rust with no crates
outside the standard library, against 15,700 lines of C.

What it does not have: the line editor, highlighting, completion, history,
prompts, themes, plugins, `awk`, the coreutils, the PowerShell and cmd routing,
`fresh update`, jobs, and everything else that makes FreSH a shell you would
sit in front of. Those are most of the C, and none of them are on the path the
benchmarks measure.

Two differences matter when reading the numbers:

- a pipeline in FreSH runs each stage as a process; `fresh-rs` runs the stages
  one after another inside itself and passes the bytes along. That is an
  architecture difference, not a language one
- `fresh-rs` reads no configuration at all, so its startup has nothing to skip

## How it was measured

Both binaries run the same two scripts, from the same harness, on the same
machine, in the same CI job:

- `tests/rust/parity.frsh` for correctness, diffed line by line. The Rust core
  has to agree with the C shell on every line before any timing is believed
- `tools/bench-core.frsh` for work inside one process, best of ten
- `tools/bench-startup.frsh` for start and exit, best of forty

The machine is a GitHub `windows-latest` runner, which is shared and noisy: a
second run of the same commit moved the Rust numbers by up to 40%, the C ones
by about 3%. Ratios below are given as a range across two runs for that reason.
See the `Rust experiment` workflow for the raw output.

## Startup

| | Best of 40 |
| --- | --- |
| `FreSH -c exit` | 10.9 ms |
| `FreSH --norc -c exit` | 8.6 ms |
| **`fresh-rs -c exit`** | **4.0 ms** |

| | Size |
| --- | --- |
| `FreSH.exe` | 786 KB |
| `fresh-rs.exe` | 442 KB |

`fresh-rs` starts in less than half the time, and it should: it is a smaller
image with fewer imports, and it has no configuration, no theme, no plugins and
no terminal setup. Compare it against `--norc` and the gap is 8.6 ms against
4.0 ms, of which a good part is simply doing less.

## Work inside one process

Best of ten, in microseconds, from one run. The ratio column is the range seen
across two runs.

| Task | C | Rust | Rust is |
| --- | --- | --- | --- |
| 20,000 `while` iterations | 331,000 | 919,000 | 2.0–2.8× slower |
| 5,000 arithmetic rounds | 88,700 | 261,700 | 2.2–2.9× slower |
| 5,000 parameter expansions | 141,700 | 331,000 | 1.7–2.3× slower |
| 5,000 `[[ ]]` tests | 86,900 | 455,600 | 3.7–5.2× slower |
| 5,000 `case` matches | 87,000 | 246,200 | 2.1–2.8× slower |
| 2,000 function calls | 136,900 | 106,900 | **1.3–1.8× faster** |
| glob 60 files | under the clock | 1,033 | not comparable |
| one command substitution | under the clock | 28 | not comparable |
| three stage pipeline | 15,895 | 25 | not comparable |

The last three rows are honest about being useless as a comparison. The C shell
reported zero microseconds for the glob and the substitution, which is below
what the harness can see rather than a real zero, and the pipeline is the
architecture difference above: 15.9 ms of process creation against 25 µs of
passing a buffer between two function calls.

## What this says

- **Rust did not make the interpreter faster.** On every loop, expansion and
  test, the Rust core is two to three times slower than the C one. The C shell
  works on bytes in place; the Rust core allocates a `Vec<char>` and a `String`
  on paths the C code walks with a pointer. That is the port being
  straightforward, not the language being slow, and it is exactly the work a
  real rewrite would have to do
- **Where Rust won, it won on structure, not on language.** Function calls are
  quicker because scopes are hash maps rather than lists, and pipelines are
  quicker because they never leave the process. Both are changes the C shell
  could make
- **Startup and size favour the Rust build**, and both shrink as soon as it
  grows the features it is missing
- **The safety argument is real and is not measured here.** The C shell has
  fuzz targets and sanitizers in CI because it needs them; the crash the fuzzer
  found in `docs/errors.md` is a class of bug the Rust core cannot have
- **The cost is the other 13,000 lines.** Everything a rewrite would have to
  reimplement is the part that took the longest to get right in C

The conclusion for 26.11: **FreSH stays in C.** A rewrite would trade a slower
interpreter and a year of work for memory safety in a program that is already
fuzzed, sanitised and differentially tested against bash.

## Running it yourself

```sh
cd rust
cargo build --release

FreSH --norc tools/bench-startup.frsh ./rust/target/release/fresh-rs.exe 40
./rust/target/release/fresh-rs.exe tools/bench-core.frsh 10
./rust/target/release/fresh-rs.exe tests/rust/parity.frsh
```

The experiment build is published as an `experiment` prerelease, so
`fresh update --pre-selector` lists it and `fresh update --pre` does not. The
`fresh-rs.exe` binary is attached to that release next to the usual ones.

## What a round two would measure

The Rust core was written once and not tuned. The obvious next step is to
remove the per-word allocations, keep patterns as byte slices, and measure
again, which would say how much of the two to three times is the port rather
than the language. Nothing else about the answer would change.
