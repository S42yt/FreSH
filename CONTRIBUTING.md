# Contributing

## The short version

Branch, work, open a pull request, let CI go green, merge to master. Releases
are cut from master by tagging. Nothing lands on master directly.

## Branches

Every branch is named `<type>/<what-it-does>`, where the type is the same word
the commit will start with and the rest is kebab case:

```
feat/lambda-right-prompt
fix/heredoc-tab-stripping
perf/command-resolution
docs/awk-inventory
test/differential-globbing
ci/sanitizer-job
refactor/tokenizer-split
chore/bump-manifests
```

| Type | For |
| --- | --- |
| `feat` | something the shell could not do before |
| `fix` | something it did wrong |
| `perf` | same behaviour, less time or memory |
| `refactor` | same behaviour, clearer code |
| `docs` | documentation only |
| `test` | tests only |
| `ci` | the build and the workflows |
| `chore` | housekeeping, manifests, versions |

Branch from `master`, keep the branch to one subject, and delete it after it
merges. A branch that has grown two subjects wants to be two branches.

## Commits

The subject is lowercase, starts with the type, and says what changed in plain
words:

```
fix: keep a quoted word whole when a substitution inside it is quoted too
perf: resolve a command by probing, not by listing every directory on PATH
```

If the change deserves an explanation, the body carries it: what was wrong,
what it did to somebody using the shell, and why this is the fix. Write it for
a reader who finds the commit in a year, not for a changelog generator. Do not
add trailers naming the tools you used.

## Code

- C11, and no new runtime dependencies. The whole point is one self contained
  executable; a build time tool such as a fuzzer or a sanitizer is fine because
  it never ships.
- Format before committing:
  ```sh
  astyle --style=google --indent=spaces=4 --suffix=none src/*.c src/*.h
  ```
- The code carries no comments. Names and small functions do that work, so a
  function that needs a comment usually wants a better name or splitting in
  two. The exception is the licence header every new file gets:
  ```c
  /*
   * Copyright (c) 2025-2026 Musa Bostanci
   * FreSH - First-Run Experience Shell
   * GNU General Public License v3.0 - See LICENSE file for details
   */
  ```
- Never truncate silently. Grow the buffer or fail loudly with a message on
  stderr and a non zero status. [How FreSH fails](docs/errors.md) is the rule
  in full, and it is not negotiable: a shell that dies with a clear message is
  trustworthy, one that quietly produces different output than bash is not.
- `StrList` and `StrBuf` carry borrow discipline. Code that holds pointers
  into one, the way `argv` points into its word list, wraps the hold in
  `sl_borrow`/`sl_release` (or `sb_borrow`/`sb_release`). In a release build
  those are free; built with `-DFRESH_BORROWS` every mutation of a borrowed
  container, every use after free and every unbalanced release aborts with a
  message. C cannot check borrows at compile time, so the checked build runs
  where mistakes surface: the fuzzers always build with it, and CI runs the
  whole test suite under it before building the release binary.
  ```sh
  FRESH_EXTRA_CFLAGS=-DFRESH_BORROWS ./build.sh
  ```

### The platform layer

FreSH runs on Windows, macOS and Linux, on x64 and ARM, from one set of
sources. No file includes
`<windows.h>`; they include `"platform.h"`, which is the Win32 API on Windows
and, on POSIX, the subset of it FreSH uses, implemented in
`src/platform_posix.c` over `open`, `opendir`, `posix_spawn` and `termios`.
That keeps the shell logic in one copy: a `FindFirstFileA` loop in
`coreutils.c` is the same loop on a Mac.

The rule for new code is to call the Win32 name and let the layer carry it.
When a call has no sensible POSIX meaning, PowerShell routing, the registry,
the clipboard, it gets an `#ifdef _WIN32` fork at the smallest point that
works, with the POSIX side doing the equivalent thing (`pbcopy`, `sudo`,
`unzip`) or saying plainly that it is not available. Paths use `PATH_SEP` and
`PATH_LIST_SEP`, never a literal backslash or semicolon, and `path_last_sep`
finds the leaf on both. CI builds, runs the suite and runs the bash
differential on Windows x64, Windows ARM64, macOS and Linux on every push, so
a change that only compiles on one side does not merge.

### The Rust core

`rust/core` is a `no_std` static library holding the compute kernels the shell
calls: sorting, counting, the `PATH` merge. It is the only implementation of
them, so building FreSH needs `rust` installed; `build.sh` builds the library
itself when `cargo` is on the path and stops with a clear message when it is
not. The library allocates nothing, carries no runtime, and only touches
memory the C side hands it, which is what keeps it a dozen kilobytes rather
than a second program.

A kernel earns its place with a measurement on the same machine, best of many
runs, or it does not go in. The rule ran in both directions already: sorting
stayed because it measured faster, and two early candidates were folded back
until the C fell away entirely.

## Tests

Every behavioural change gets a test, in the same pull request.

- `tests/cases/*.frsh` is the suite, written in FreSH and run by FreSH:
  ```sh
  FreSH tests/run.frsh
  ```
- If the change is something bash also does, add a case to `tests/diff/cases`
  instead or as well. Those scripts run through real bash on Linux and through
  FreSH on Windows on every push, and any difference fails the build. That is
  what keeps "a bash script runs in FreSH unchanged" honest.
- A crash found by the fuzzer goes in `fuzz/crashes/` and into the corpus, so
  it is tried again for ever.
- New limitations get written down in [docs/bash.md](docs/bash.md) under "Does
  not work" or "Where FreSH differs on purpose". A deliberate difference from
  bash also gets a line in `tests/diff/known-differences.txt`, and the two must
  agree.

## Pull requests

CI runs the build, the test suite, the bash differential and the fuzz corpus
under the address and undefined sanitizers on every pull request, and on
master. All of it is green before a merge, and no it does not get merged red
with a follow up promised.

A branch on its own does not build, so open the pull request early if you want
CI on it; a draft is fine. Running it on both the branch and the pull request
would only build everything twice.

Say in the description what changed and how you know it works. A measurement
beats an adjective: `docs/benchmarks.md` was written that way, and every
performance claim in this repository has a number behind it.

## Releases

Only from `master`, and only by tagging:

```sh
git checkout master
git pull
git tag -a v26.11.0 -m 'FreSH 26.11.0'
git push origin v26.11.0
```

The version is `year.major.minor`: the year it was released, the major number
for a release that changes how something behaves, the minor for everything
else. The workflow does the rest: it stamps the version into the sources,
builds, runs the suite and the differential, writes `SHA256SUMS.txt`, attaches
build provenance, publishes the release, and prepares the package manifests.

Pushing to `master` alone releases nothing. A tag on any other branch is a
mistake; cut it from master.

### Prereleases

A version that wants trying before it is the one everybody gets is tagged with
a `-prerelease-n` suffix, counting up from one:

```sh
git tag -a v26.10.1-prerelease-1 -m 'FreSH 26.10.1 prerelease 1'
git push origin v26.10.1-prerelease-1
```

The workflow reads the suffix and behaves differently in three ways:

- the GitHub release is marked as a prerelease, so it is not the "latest"
  release and `fresh update` does not offer it to anybody
- the Scoop and winget manifests are not touched, so the package managers keep
  pointing at the last stable version
- nothing else changes: it is built, tested, diffed against bash, checksummed
  and given build provenance exactly like a release, because a prerelease that
  skipped the tests would not be worth trying

Anyone who wants them asks for them:

```sh
fresh update --pre       # the newest prerelease, never an experiment
fresh update --selector  # browse every release ever and install any of them
```

`--selector` is a small interactive browser over the whole release history,
newest first, prereleases and experiments included: arrows move, left and
right switch pages, enter installs, escape leaves. Outside a terminal or with
`--check` it prints the same list numbered and plain.

## Experiments

A tag can also name a kind other than `prerelease`:

```sh
git tag -a v26.11.0-experiment-1 -m 'FreSH 26.11.0 experiment 1'
```

An experiment is a branch being measured rather than a version on its way to
release, and it is tagged from that branch rather than from master. It is built
and released the same way, and the selector lists it as `26.11.0 experiment 1`,
but `fresh update --pre` skips it: `--pre` offers prereleases and finished
releases only. Somebody who wants an experiment picks it out of
`fresh update --selector` on purpose, and can leave it the same way.

A prerelease sorts below the release of the same number, so someone running
`26.10.1-prerelease-2` is offered `26.10.1` when it arrives, and
`-prerelease-2` is newer than `-prerelease-1`. When the version is ready, tag
it without the suffix; the prerelease tags stay where they are.

### After a release

Scoop follows the release on its own. winget needs a pull request per version,
which `tools/winget-submit.sh` opens; see
[manifests/README.md](manifests/README.md).
