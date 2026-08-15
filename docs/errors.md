# How FreSH fails

A shell is infrastructure. The worst thing it can do is not crash, it is to
carry on and hand you a plausible answer that is quietly wrong: a line split in
half, a field truncated, a path shortened, a function that does not exist
evaluating to the empty string. You do not find out at the time. You find out
from the output, days later, if at all.

So FreSH has one rule about failure:

> **Never truncate silently. Grow, or say so.**

Everything below follows from that.

## Grow rather than cap

Anything whose size comes from your data grows to fit it: input lines, records
and fields in awk, command output, arrays, the line editor's buffer, the job
table. There is no maximum line length, no maximum field count, no maximum
number of background jobs.

Reading a line goes through one helper, `read_line` in `src/util.c`, which
returns the whole line however long it is. Every utility that reads lines uses
it, so `grep`, `sort`, `tail`, `paste`, `diff`, `read` and the rest all behave
the same on a 100KB line.

## Fail loudly where a limit is real

Some limits are structural rather than data-shaped. Those stay, and they say so
on stderr with a non-zero exit status instead of doing something almost right:

| Limit | What happens |
| --- | --- |
| 32 pipeline stages | `a pipeline cannot have more than 32 stages` |
| 8 process substitutions in one command | named, with the substitution that did not fit |
| `PATH_BUF` on a redirection target | `path is too long to redirect to` |
| a temporary file that cannot be made | named, rather than skipped |

## Report a bad expression rather than call it zero

- `$((1 + ))` and `$((5 / 0))` are errors, not `0`
- `((...))` reports a bad expression instead of just returning false
- `${v:?message}` prints the message and stops the script, which is the entire
  point of the form
- `${v:offset:length}` with an offset that is not an expression is an error,
  not "the whole string"
- awk calling a function that does not exist is an error, not the empty string
- awk dividing by zero, opening an output it cannot write, or being handed more
  arguments than a function declares are all errors with status 2
- an awk program with a syntax error does not run at all

## Where the empty string is still an answer

Not every empty result is a failure, and these are deliberate:

- `$missing` is empty, and `set -u` is how you ask for an error instead
- `$5` in awk when there are three fields is empty, as in every awk
- a glob that matches nothing stays as written, unless you set `nullglob`

## For contributors

When you add a code path that can fail, pick one of two behaviours and never a
third:

1. make the thing big enough, or
2. print what went wrong to stderr and return non-zero.

If you find yourself writing a fixed buffer with a `snprintf` whose result you
do not check, or a `return ""` on a path the caller cannot tell apart from a
real value, that is the bug. The tests in `tests/` and the bash differential in
[tests/diff](../tests/diff/README.md) exist to catch the rest.
