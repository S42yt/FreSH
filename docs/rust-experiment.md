# The Rust experiment

FreSH is written in C. This is the answer to a fair question: what would it
look like in Rust, and would it be faster?

`rust/` holds `fresh-rs`, a shell core in Rust. It is not a port of FreSH and
it is not going to replace it. It exists to be measured, so the question is
settled with numbers rather than opinions. It lives on the
`rust/26.11-experiment` branch and is never merged into master.

## What was built

A shell core covering what the benchmarks touch: lexer, parser, expansion,
arithmetic, pattern matching, globbing, redirection, pipelines, functions with
local scope, and the builtins the scripts call (`echo`, `printf`, `cat`,
`test`, `[`, `[[`, `local`, `export`, `read`, `mkdir`, `rm`, `command`,
`source`, `eval`, `cd`, `exit`). About 2,300 lines of Rust using nothing
outside the standard library, against 15,700 lines of C.

What it does not have: the line editor, highlighting, completion, history,
prompts, themes, plugins, `awk`, the coreutils, the PowerShell and cmd routing,
`fresh update`, and everything else that makes FreSH a shell you would sit in
front of. That is most of the C, and none of it is on the path being measured.

Two differences matter when reading the numbers:

- a pipeline in FreSH runs each stage as a process; `fresh-rs` runs the stages
  one after another inside itself and passes the bytes along. That is an
  architecture difference, not a language one
- `fresh-rs` reads no configuration at all, so its startup has nothing to skip

## How it was measured

Both binaries run the same scripts, from the same harness, on the machine in
[benchmarks](benchmarks.md):

- `tests/rust/parity.frsh` for correctness, diffed line by line in CI. The Rust
  core has to agree with the C shell on every line before any timing is
  believed
- `tools/bench-core.frsh` for work inside one process, best of ten
- `tools/bench-startup.frsh` for start and exit, best of forty

The same comparison runs on every push to the branch in the `Rust experiment`
workflow. Those runners are shared and noisy, so the numbers below are from the
local machine and the CI job is there to keep the two shells honest with each
other rather than to produce timings.

## Startup

| | Best of 40 |
| --- | --- |
| `FreSH -c exit` | 27.8 ms |
| `FreSH --norc -c exit` | 21.8 ms |
| **`fresh-rs -c exit`** | **14.8 ms** |

| | Size |
| --- | --- |
| `FreSH.exe` | 786 KB |
| `fresh-rs.exe` | 442 KB |

These are higher than the 22 ms in [benchmarks](benchmarks.md) because both
binaries were run out of a temporary directory with Defender watching it. The
comparison is what matters: `fresh-rs` starts in about two thirds of the time
`FreSH --norc` takes, from an image about half the size, and it has no
configuration, no theme, no plugins and no terminal setup to pay for.

## Work inside one process

Best of ten, in microseconds.

| Task | C | Rust | Rust is |
| --- | --- | --- | --- |
| 20,000 `while` iterations | 83,660 | 147,823 | 1.8× slower |
| 5,000 arithmetic rounds | 33,122 | 70,799 | 2.1× slower |
| 5,000 parameter expansions | 144,869 | 201,650 | 1.4× slower |
| 5,000 `[[ ]]` tests | 27,723 | 59,856 | 2.2× slower |
| 5,000 `case` matches | 27,771 | 62,648 | 2.3× slower |
| 2,000 function calls | 47,101 | 30,586 | **1.5× faster** |
| glob 60 files | under the clock | 2,453 | not comparable |
| one command substitution | under the clock | 44 | not comparable |
| three stage pipeline | 1,096 | 41 | not comparable |

The last three rows are honest about being useless as a comparison. The C shell
reported zero microseconds for the glob and the substitution, which is below
what the harness can see rather than a real zero, and the pipeline is the
architecture difference above: process creation against passing a buffer
between two function calls.

## What this says

- **Rust did not make the interpreter faster.** Loops, arithmetic, expansion,
  tests and `case` are all roughly twice as slow. The C shell works on bytes in
  place; the Rust core allocates on paths the C code walks with a pointer. That
  is the port being straightforward rather than the language being slow, and
  closing it is exactly the work a real rewrite would have to do
- **Where Rust won, it won on structure, not on language.** Function calls are
  quicker because scopes are hash maps rather than lists, and pipelines are
  quicker because they never leave the process. Both are changes the C shell
  could make
- **Startup and size favour the Rust build**, and both would shrink as it grew
  the features it is missing
- **The safety argument is real and is not measured here.** The C shell has
  fuzz targets and sanitizers in CI because it needs them; the heap overflow in
  [errors](errors.md) is a class of bug the Rust core cannot have
- **The cost is the other 13,000 lines**, which is everything that took the
  longest to get right in C

One number is worth keeping as a lesson rather than a result. The first Rust
build ran the 20,000 iteration loop in 3.09 seconds, 37 times slower than C,
and none of it was the language: a bare `[` was being treated as the start of a
character class, so every loop test listed the current directory. Fixing that
one line took the loop to 148 ms. A benchmark that disagrees with itself by a
factor of twenty is a bug report, not a measurement.

The conclusion for 26.11: **FreSH stays in C, and takes the two lessons.** A
rewrite would trade a slower interpreter and a great deal of work for memory
safety in a program that is already fuzzed, sanitised and differentially tested
against bash. The places Rust won were a hash table and an in-process pipeline,
so the C shell got both: variables are looked up through a hash table and
pipeline buffers stay open. What that was worth is in
[benchmarks](benchmarks.md).

## Running it yourself

The Rust core is not in master. It lives on the `rust/26.11-experiment` branch,
which is kept for exactly this and is never merged:

```sh
git checkout rust/26.11-experiment
cd rust
cargo build --release

FreSH --norc tools/bench-startup.frsh ./rust/target/release/fresh-rs.exe 40
./rust/target/release/fresh-rs.exe tools/bench-core.frsh 10
./rust/target/release/fresh-rs.exe tests/rust/parity.frsh
```

The experiment build is published as an `experiment` prerelease, so
`fresh update --pre-selector` lists it and `fresh update --pre` never offers
it. `fresh-rs.exe` is attached to that release next to the usual files.

## Round two: Rust in the shipping binary

The experiment above ruled out a rewrite, not Rust. So 26.11 links a `no_std`
static library into the C binary and asks a narrower question: is there a leaf
routine where Rust pays for itself? Three were tried, one survived, and it is
`sort`, where C calls its comparator through a function pointer and Rust inlines
it. Counting and `PATH` merging measured identical and stayed in C. The whole
Rust side costs 11,776 bytes of binary. The numbers are in
[benchmarks](benchmarks.md).

Startup was measured again on the way past, three builds from one commit, and
it does not move: not with Rust, not with a third of the binary removed, not
with the registry `PATH` merge deleted outright. That floor is Windows creating
a process.

## What a round two would measure

The Rust core has had one bug fixed and no tuning pass. Removing the per-word
allocations and keeping patterns as byte slices would say how much of the
remaining two times is the port rather than the language. Nothing else about
the answer would change.
