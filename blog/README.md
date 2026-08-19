# The FreSH blog

What changed, how it was measured, and why it was done that way. The
[documentation](../docs/README.md) says what the shell does today; these posts
say how it got there, including the attempts that were thrown away and the
theory that turned out to be wrong.

## Posts

- **[Starting quicker than cmd](2026-08-19-starting-quicker-than-cmd.md)**,
  19 August 2026. Startup went from 16.6 ms to 9.5 ms and passed `cmd`. The
  cost was three libraries loading before `main` for three commands almost
  nobody runs in a given shell, and the measurement that found it was a
  control, not a profile.
- **[Would Rust be faster?](2026-08-17-would-rust-be-faster.md)**,
  17 August 2026. A whole shell core written in Rust to answer the question
  with numbers rather than opinions. It lost at interpreting a shell language
  and won twice on structure, and one kernel of it ships.
- **[Three bugs worth 23 milliseconds](2026-08-15-three-bugs-worth-23-milliseconds.md)**,
  15 August 2026. Startup was 45 ms and became 22 ms. All three causes were
  bugs rather than tuning.

## Why this is a folder and not a changelog

A changelog says what changed. These say what was measured, what it cost, and
what was tried and dropped, which is the part worth keeping: the `-Os` build
that was smaller and slower, the link time optimisation that was bigger for
nothing, and the size theory that a padded probe disproved in one run.

Release notes for each version are on the
[releases page](https://github.com/S42yt/FreSH/releases).
