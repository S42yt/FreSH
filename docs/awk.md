# awk

FreSH ships its own awk. It is a real implementation, not a shim: a lexer, a
recursive descent parser, an AST and a tree walking interpreter, all in
`src/awk.c`, sharing the regular expression engine in `src/regex.c` with the
rest of the shell.

Like [bash compatibility](bash.md), this page is the honest inventory.
Everything under **Works** has a test in `tests/cases/awk.frsh`, so it keeps
working. Everything under **Does not work** is a real gap, listed so you find
out here rather than from a wrong number in a report.

FreSH's awk is used even when another awk is on `PATH`, the same way `find`,
`sort` and `printf` are, so a script behaves the same on every machine. Call
`gawk`, `mawk` or the full path when you want a different one.

## Works

### Program structure

```awk
BEGIN { ... }        END { ... }
/regex/ { ... }      $1 == "x" { ... }     NR > 2 { ... }
/start/,/stop/ { ... }
function name(a, b) { return a + b }
```

Patterns without an action print the record. Actions without a pattern run on
every record. A range pattern runs from the record that matches the left side
through the record that matches the right side. `#` starts a comment.

### Fields and records

`$0`, `$1` … `$NF`, `$(expr)`, and assignment to any of them. Assigning to a
field rebuilds `$0` using `OFS`; assigning to `$0` re-splits the fields;
assigning to `NF` truncates or extends the record. There is no limit on record
length or field count other than memory.

### Variables

| Name | Meaning |
| --- | --- |
| `NR` | records read so far |
| `FNR` | records read from the current file |
| `NF` | fields in the current record |
| `FS` | input field separator, one character or a regular expression |
| `OFS` | output field separator, used by `print` and field assignment |
| `RS` | input record separator, one character, or empty for paragraph mode |
| `ORS` | output record separator |
| `FILENAME` | name of the current input file |
| `SUBSEP` | joins `a[i, j]` subscripts, `\034` by default |
| `RSTART` | where the last `match` started, 0 when it failed |
| `RLENGTH` | how long the last `match` was, `-1` when it failed |
| `CONVFMT` | number to string format, `%.6g` by default |
| `OFMT` | number format used by `print`, `%.6g` by default |

### Operators

```awk
+  -  *  /  %  ^  **        ++  --       unary -  !
=  +=  -=  *=  /=  %=  ^=
==  !=  <  <=  >  >=        &&  ||       ?:
~  !~        in        string concatenation by juxtaposition
```

Precedence follows awk: concatenation binds looser than arithmetic, so
`print 1 2 * 3` is `16`. `^` is right associative. A `/` after a value is
division, so `$1/2` divides instead of starting a regular expression.

### Statements

`if`/`else`, `while`, `do … while`, `for (init; test; step)`,
`for (key in array)`, `break`, `continue`, `next`, `exit [status]`,
`return [value]`, `delete a[key]`, `delete a`, `{ }` blocks, `;` separators.

### Builtins

```awk
length([s])   substr(s, m [, n])   index(s, t)   split(s, a [, fs])
sub(re, repl [, target])          gsub(re, repl [, target])
match(s, re)  sprintf(fmt, ...)   toupper(s)     tolower(s)
int(x)  sqrt(x)  exp(x)  log(x)  sin(x)  cos(x)  atan2(y, x)
rand()  srand([seed])   system(cmd)   close(name)   fflush()
```

`sub` and `gsub` return how many replacements they made, understand `&` as the
matched text and `\&` as a literal ampersand, write back through a field or an
array element when you pass one, and cannot loop forever on an empty match.

`system` runs the command through FreSH itself, in a subshell, so it sees your
functions and aliases and cannot change the calling shell. It returns the exit
status.

### Output

```awk
print                       print $1, $2
printf "%-8s %5.2f\n", name, value
print "line" > "file"       print "line" >> "file"
print "line" > "/dev/stderr"
```

A redirection target stays open until `close()`, so repeated `print > "file"`
appends to the same handle rather than truncating each time. `printf`
understands `%d %i %o %u %x %X %e %E %f %g %G %c %s %%`, field widths,
precision and `*`.

### Input

```awk
getline               getline line
getline < "file"      getline line < "file"
```

`getline` returns 1 for a record, 0 at end of input and -1 when the file cannot
be opened. Plain `getline` advances `NR` and `FNR`; a redirected one does not.

### Command line

```
awk [-F sep] [-v name=value] [-f program.awk] ['program'] [file | var=value ...]
```

`-F t` means tab. `-f` may be repeated and the programs are concatenated. A
`name=value` argument between files assigns a variable at that point in the
input. `-` reads standard input. With no file, awk reads standard input.

## Does not work

- **`printf`/`print` into a pipe**: `print | "sort"` opens the pipe, but the
  command runs through the C runtime rather than FreSH, so it does not see
  shell functions or aliases.
- **`"command" | getline`**: reading from a command is not parsed. Use
  `getline < "file"` or a shell pipe into awk.
- **`RS` as a regular expression**: only the first character of `RS` is used.
  An empty `RS` is paragraph mode, which does work.
- **Locale-aware case and collation**: `toupper`, `tolower` and string
  comparison are byte oriented and ASCII only.
- **`gensub`, `asort`, `asorti`, `strftime`, `systime`, `toupper` on arrays,
  `ENVIRON`, `ARGV`, `ARGC`, `PROCINFO`**: gawk extensions, not implemented.
- **Uninitialised scalars passed to a function as arrays**: a function
  parameter used as an array inside the function is not shared back with the
  caller. Pass the array by name and use a global instead.
- **Deep recursion**: user functions recurse on the C stack, so a runaway
  recursion ends as a stack overflow crash report rather than an awk error.

## Errors

awk follows the same rule as the rest of FreSH, described in
[errors](errors.md): it fails loudly rather than producing a plausible wrong
answer. Calling a function that does not exist, dividing by zero, opening an
output that cannot be written, and passing more arguments than a function
declares are all errors on stderr with exit status 2, not silent empty strings.
