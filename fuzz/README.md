# Fuzzing

FreSH is 16k lines of C doing pointer arithmetic over strings it did not write:
scripts you paste, scripts a tool generated, patterns from a config file. A
parser that crashes on odd quoting is the kind of thing that ends trust in a
tool for good, so the parts that read untrusted text are fuzzed.

Three targets, each built with libFuzzer plus the address and undefined
behaviour sanitizers:

| Target | What it eats |
| --- | --- |
| `fuzz_parser` | a whole script, through `tokenize` and the parser |
| `fuzz_regex` | a pattern and a subject, through search, replace and the BRE to ERE conversion |
| `fuzz_awk` | an awk program, through the awk lexer, parser and interpreter |

## Building and running

Needs clang. The sources build on Linux through the same platform layer the
macOS build uses, `src/platform.h` and `src/platform_posix.c`, so the parser,
the regular expression engine and awk are fuzzed untouched. Nothing here ships
in `FreSH.exe`.

```sh
fuzz/build.sh
fuzz/build/fuzz_parser fuzz/corpus/parser -max_total_time=60
fuzz/build/fuzz_regex  fuzz/corpus/regex  -max_total_time=60
fuzz/build/fuzz_awk    fuzz/corpus/awk    -max_total_time=60
```

CI builds all three on every push and runs each over its corpus, which is the
memory check for the parser, the expander's regular expressions and awk. A
nightly job fuzzes each target for several minutes and uploads anything that
crashes.

## When something crashes

libFuzzer writes the input that crashed to `crash-<hash>`. Put it in
`fuzz/crashes/`, fix the bug, and add the same input to the corpus so it is
tried on every run from then on. A crash is never closed by making the fuzzer
stop generating that input.
