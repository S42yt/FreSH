/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "help.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell.h"
#include "style.h"
#include "term.h"
#include "util.h"
#include "vars.h"

typedef struct {
    const char *name;
    const char *usage;
    const char *summary;
    const char *detail;
} HelpEntry;

static const HelpEntry ENTRIES[] = {
    {"alias", "alias [<name>=<value> ...]", "name a command so a short word runs it",
     "With no arguments it lists what is defined.\n"
     "  alias ll='ls -l'\n"
     "  alias                 show every alias"},

    {"bg", "bg [<job>]", "let a stopped job carry on in the background",
     "The job number comes from jobs. With no argument it takes the most recent."},

    {"break", "break [<levels>]", "leave a loop early",
     "break 2 leaves two nested loops."},

    {"cd", "cd [<directory>]", "change the working directory",
     "No argument goes to $HOME. A single - goes back to where you were.\n"
     "  cd src/parser\n"
     "  cd -"},

    {":", ":", "do nothing and succeed",
     "The null command, the same as true. Handy as a placeholder:\n"
     "  while :; do work || break; done"},

    {"case", "case <word> in <pattern>) <commands> ;; ... esac", "branch on what a word looks like",
     "Patterns are globs, and | separates alternatives.\n"
     "  case $1 in\n"
     "    start|up) run ;;\n"
     "    *)        die \"unknown: $1\" ;;\n"
     "  esac"},

    {"admin", "admin [<command> ...]", "open a FreSH that can write anywhere",
     "Windows asks for consent, so it cannot be silent.\n"
     "  admin              a new elevated shell here\n"
     "  admin fresh update run one command elevated"},

    {"bind", "bind [<key> <action>] | -l | -r <key>", "put a key to work",
     "The action is an editing action, a command to run, or insert:<text> to type it.\n"
     "  bind ctrl+g \"git status\"\n"
     "  bind f5 insert:\"make \"\n"
     "  bind ctrl+a beginning-of-line\n"
     "  -l   list the editing actions      -r   give the key back\n"
     "Keys read as you say them: ctrl+<letter>, f1 to f12, home, end, delete.\n"
     "Put the lines in ~/.freshrc to keep them."},

    {"clear", "clear", "wipe the screen", NULL},

    {"command", "command [-v] <name> [<argument> ...]", "run past a function of the same name",
     "  -v   say what would run instead of running it"},

    {"copy", "copy [<text> ...]", "put text on the clipboard",
     "With no argument it takes what comes down the pipe.\n"
     "  ls | copy"},

    {"copypath", "copypath [<file>]", "put the full path on the clipboard",
     "With no argument it takes the current directory."},

    {"dirs", "dirs [-c]", "the directory stack",
     "  -c   forget it\n"
     "The current directory is first, then what pushd saved."},

    {"extract", "extract <archive> [<directory>]", "unpack an archive",
     "Knows zip, tar, gz, tgz, bz2, xz, 7z and rar.\n"
     "The directory is created when it does not exist.\n"
     "  extract release.tar.gz build"},

    {"getopts", "getopts <optstring> <name> [<argument> ...]", "read the options of a script",
     "A letter followed by : in the optstring takes a value, which lands in OPTARG.\n"
     "Start the optstring with : to report errors yourself.\n"
     "  while getopts ab:c flag; do case $flag in a) ... ;; esac; done"},

    {"cmd", "cmd [<command> ...]", "run a command in cmd.exe",
     "With no argument it opens an interactive cmd.exe.\n"
     "  cmd dir /s /b"},

    {"continue", "continue [<levels>]", "skip to the next turn of a loop", NULL},

    {"declare", "declare [-a|-A] [<name>[=<value>] ...]", "create a variable or an array",
     "  -a   an array with numbered keys\n"
     "  -A   an array with named keys\n"
     "With no argument it prints every variable."},

    {"describe", "describe <name> <summary> [<usage>] [<detail>]",
     "give a command a help page, for plugins to document what they add",
     "The page shows under help <name>. Name an existing command to replace its page,\n"
     "or an alias or a function your plugin defines to give it one.\n"
     "  describe greet \"say hello to someone\" \"greet <name> [<greeting>]\"\n"
     "  describe gpush \"push the branch you are on\" \"gpush [<remote>]\" \"  -f  force it\""},

    {"die", "die [<message> ...]", "print a message in red and end the script with status 1",
     "Replaces the echo to stderr and exit pair.\n"
     "  have gcc || die \"no compiler\""},

    {"echo", "echo [-n] [-e] [<word> ...]", "print words",
     "  -n   no trailing newline\n"
     "  -e   turn \\n \\t \\r \\e \\\\ into the characters they name"},

    {"eval", "eval <word> ...", "join the words and run the result as a command", NULL},

    {"exit", "exit [<status>]", "leave the shell",
     "Inside a script called as a command it ends that script only."},

    {"export", "export [<name>[=<value>] ...]", "mark a variable so programs you start can see it",
     "With no argument it lists the environment.\n"
     "  export EDITOR=nvim"},

    {"false", "false", "do nothing and report failure", NULL},

    {"fg", "fg [<job>]", "resume a job if stopped and wait for it",
     "With no argument it takes the most recent job."},

    {"for", "for <name> in <word> ...; do <commands>; done", "run commands once per word",
     "With no in list it walks the arguments the script was given.\n"
     "  for f in *.txt; do echo $f; done\n"
     "  for i in $(seq 1 3); do echo $i; done"},

    {"function", "function <name> { <commands>; } | <name>() { <commands>; }", "define a function",
     "Arguments arrive as $1 $2 and $@, and local keeps a variable inside.\n"
     "  greet() { echo \"hi $1\"; }"},

    {"fresh", "fresh [version|doctor|update [--check] [--pre] [--pre-selector]]",
     "about this shell, and looking after it",
     "  fresh                        version, paths, theme and plugins\n"
     "  fresh doctor                 check ~/.freshrc and say where it breaks\n"
     "  fresh update                 fetch and install the newest release\n"
     "  fresh update --check         only say whether there is one\n"
     "  fresh update --pre           take the newest prerelease as well\n"
     "  fresh update --pre-selector  list the prereleases and pick one"},

    {"gitinfo", "gitinfo", "repository, branch, user and whether the tree is dirty", NULL},

    {"have", "have <name> ...", "succeed when every name can be run",
     "Prints nothing, so there is no output to send to nul.\n"
     "  have docker || warn \"docker missing\""},

    {"help", "help [<command>]", "this help",
     "With no argument it lists everything.\n"
     "  help cd     what cd does and the arguments it takes"},

    {"history", "history [-c] [<count>]", "commands you have run",
     "  -c        forget them all\n"
     "  history 20   the last twenty"},

    {"if", "if <command>; then <commands>; [elif ...] [else ...] fi", "run commands when one succeeds",
     "It branches on the exit status, so the condition is a command, not an expression.\n"
     "  if test -f build.frsh; then echo found; fi\n"
     "  if have gcc; then cc=gcc; else cc=clang; fi"},

    {"jobs", "jobs", "background jobs, running or stopped", NULL},

    {"mapfile", "mapfile [-t] [<name>]", "read the input into an array",
     "  -t   drop the newline from each line\n"
     "readarray is the same command. The default name is MAPFILE."},

    {"paste", "paste", "print what is on the clipboard", NULL},

    {"popd", "popd", "go back to the directory pushd saved", NULL},

    {"pushd", "pushd [<directory>]", "go somewhere and remember where you were",
     "With no argument it swaps the top two entries.\n"
     "  pushd ../build   then   popd"},

    {"readonly", "readonly [-p] [<name>[=<value>] ...]", "a variable that cannot change again",
     "  -p   list what is already read only"},

    {"shopt", "shopt [-s|-u|-q] [<option> ...]", "switch a shell option",
     "  -s   set        -u   unset        -q   say nothing, use the status\n"
     "  globstar     ** walks the tree\n"
     "  nullglob     a pattern that matches nothing disappears\n"
     "  dotglob      * also matches names starting with a dot\n"
     "  extglob      ?( *( +( @( and !( groups\n"
     "  nocasematch  patterns ignore case\n"
     "With no option it lists them all."},

    {"local", "local <name>[=<value>] ...", "a variable that belongs to this function",
     "The previous value comes back when the function returns."},

    {"ls", "ls [-a] [-l] [-1] [<path>]", "list a directory",
     "  -a   include names starting with a dot\n"
     "  -l   one per line with size and time\n"
     "  -1   one name per line\n"
     "Directories end in / and programs in *."},

    {"ok", "ok [<message> ...]", "print a message with a green tick", NULL},

    {"plugin", "plugin [list|load <name> ...]", "plugins in ~/.fresh/plugins",
     "  plugin            list them, loaded ones marked\n"
     "  plugin load git   load one now\n"
     "Set FRESH_PLUGINS in ~/.freshrc to load them at startup."},

    {"ps1", "ps1 [<command> ...]", "run a command in PowerShell",
     "With no argument it opens an interactive PowerShell.\n"
     "  ps1 \"Get-Service | Where-Object Status -eq Running\""},

    {"pwd", "pwd", "print the working directory", NULL},

    {"read", "read [-p <prompt>] [<name> ...]", "read a line into variables",
     "The last name takes the rest of the line. With no name it fills REPLY.\n"
     "  read -p \"branch: \" branch"},

    {"rehash", "rehash", "rescan PATH and reload the theme and plugins",
     "Run this after installing a program or editing a plugin."},

    {"return", "return [<status>]", "leave a function or a sourced script", NULL},

    {"say", "say [<message> ...]", "print a message with a dim bullet", NULL},

    {"set", "set [-e|-x|-u] [+e|+x|+u]", "shell options, or every variable",
     "  -e   stop the script at the first command that fails\n"
     "  -x   print each command before running it\n"
     "  -u   treat an unset variable as an error\n"
     "A plus instead of a minus turns one back off. No argument lists variables."},

    {"shift", "shift [<count>]", "drop the first positional arguments", NULL},

    {"source", "source <file> [<argument> ...]", "run a file in this shell, keeping its variables",
     "Also spelled as a single dot.\n"
     "  source ~/.freshrc"},

    {"stop", "stop [<job>]", "suspend a job",
     "FreSH addition. Windows has no Ctrl+Z, so this is a command.\n"
     "bg or fg starts it again."},

    {"test", "test <expression>   |   [ <expression> ]", "check a file or compare values",
     "  -e -f -d -s <path>    exists, regular file, directory, not empty\n"
     "  -r -w -x <path>       readable, writable, executable\n"
     "  -z -n <string>        empty, not empty\n"
     "  a = b   a != b        string comparison\n"
     "  a -eq b               also -ne -lt -le -gt -ge for numbers\n"
     "  ! expr, expr -a expr, expr -o expr"},

    {"theme", "theme [<name>|reset]", "the prompt theme",
     "  theme           list them, the active one marked\n"
     "  theme minimal   switch now\n"
     "  theme reset     rewrite the bundled themes and plugins\n"
     "Set FRESH_THEME in ~/.freshrc to keep one."},

    {"trap", "trap <command> <signal> ...", "run a command when something happens",
     "Signals: EXIT HUP INT QUIT TERM ERR DEBUG RETURN, by name or number.\n"
     "  trap 'rm -r $tmp' EXIT\n"
     "  trap - ERR      remove it"},

    {"true", "true", "do nothing and report success", NULL},

    {"type", "type <name> ...", "say whether a name is an alias, function, builtin or file", NULL},

    {"unalias", "unalias <name> ...", "forget an alias", NULL},

    {"unset", "unset <name> ...", "forget a variable", NULL},

    {"wait", "wait [<job>]", "wait for a background job, or for all of them", NULL},

    {"warn", "warn [<message> ...]", "print a message with a yellow bang", NULL},

    {"which", "which <name> ...", "the path a command resolves to", NULL},

    {"awk", "awk [-F <sep>] [-v <name>=<value>] '<program>' [<file> ...]",
     "run a program over each line of input",
     "  -F   the field separator\n"
     "  -v   set a variable before it starts\n"
     "Fields are $1 upwards, $0 is the line. NR is the line number, NF the field count.\n"
     "  awk '{ print $2, $1 }' data.txt\n"
     "  awk -F: '$3 > 100 { print $1 }' list.txt\n"
     "  awk '{ n[$1] += $2 } END { for (k in n) print k, n[k] }' data.txt"},

    {"basename", "basename <path> [<suffix>]", "the last part of a path", NULL},
    {"cat", "cat [-n] [<file> ...]", "print files, or standard input",
     "  -n   number the lines"},
    {"chmod", "chmod <+w|-w> <file> ...", "make a file writable or read only",
     "Windows has no execute bit, so only the write flag moves."},
    {"cmp", "cmp <file> <file>", "report the first byte where two files differ", NULL},
    {"column", "column [<file> ...]", "lay lines out in columns", NULL},
    {"comm", "comm <file> <file>", "compare two sorted files line by line", NULL},
    {"cp", "cp [-r] <source> ... <destination>", "copy files",
     "  -r   copy directories and what is inside them"},
    {"cut", "cut -d <char> -f <field> [<file> ...]", "take one field from each line",
     "  cut -d : -f 2 pairs.txt"},
    {"date", "date [+<format>]", "the date and time",
     "  date +%Y-%m-%d"},
    {"df", "df", "drives with their size, used and free space", NULL},
    {"diff", "diff <file> <file>", "show the lines that differ", NULL},
    {"dirname", "dirname <path>", "the path without its last part", NULL},
    {"du", "du [-h] [<path>]", "how much space a directory uses",
     "  -h   human readable sizes"},
    {"env", "env [<name>=<value> ... <command>]", "the environment, or a command with additions",
     NULL},
    {"expr", "expr <expression>", "evaluate an integer expression", NULL},
    {"file", "file <path> ...", "guess what a file contains", NULL},
    {"find", "find [<path>] [-name <pattern>] [-type f|d]", "walk a directory tree",
     "  find src -name '*.c' -type f"},
    {"fold", "fold [-w <width>] [<file> ...]", "wrap long lines", NULL},
    {"grep", "grep [-i] [-v] [-n] [-c] [-l] [-E] [-F] <pattern> [<file> ...]",
     "print the lines that match",
     "  -i   ignore case          -v   lines that do not match\n"
     "  -n   number them          -c   count them instead\n"
     "  -l   only name the files  -E   extended expressions\n"
     "  -F   plain text, no pattern\n"
     "Basic regular expressions by default, the same as grep elsewhere."},
    {"groups", "groups", "the groups you belong to", NULL},
    {"head", "head [-n <count>] [<file> ...]", "the first lines, ten by default", NULL},
    {"hostname", "hostname", "the name of this computer", NULL},
    {"id", "id", "your user and whether the shell is elevated", NULL},
    {"kill", "kill <pid> ...", "end a process", NULL},
    {"ln", "ln [-s] <target> <link>", "link a name to a file",
     "  -s   a symbolic link, which needs developer mode or admin"},
    {"md5sum", "md5sum <file> ...", "the MD5 of each file", NULL},
    {"mkdir", "mkdir [-p] <directory> ...", "make directories",
     "  -p   make the parents too, and do not complain if it exists"},
    {"mktemp", "mktemp [-d]", "make a temporary file and print its path",
     "  -d   a directory instead"},
    {"mv", "mv <source> ... <destination>", "move or rename", NULL},
    {"nl", "nl [<file> ...]", "number the lines", NULL},
    {"open", "open [<path>]", "open a file or folder with its usual program", NULL},
    {"paste", "paste <file> ...", "join files side by side", NULL},
    {"pkill", "pkill <name>", "end processes whose name matches", NULL},
    {"printf", "printf <format> [<argument> ...]", "print with a format",
     "Understands %s %d %i %c %% and \\n \\t \\r"},
    {"ps", "ps", "processes with their id and name", NULL},
    {"realpath", "realpath <path> ...", "the full path", NULL},
    {"rev", "rev [<file> ...]", "reverse each line", NULL},
    {"rm", "rm [-r] [-f] <path> ...", "delete files",
     "  -r   delete directories and what is inside\n"
     "  -f   do not complain when something is missing"},
    {"rmdir", "rmdir <directory> ...", "remove empty directories", NULL},
    {"sed", "sed [-n] [-E] '<script>' [<file> ...]", "edit a stream of lines",
     "  s/pattern/replacement/     replace the first match on each line\n"
     "  s/pattern/replacement/g    replace every match\n"
     "  d                          delete the lines\n"
     "  -n   only print what changed      -E   extended expressions\n"
     "& is the whole match and \\1 the first group.\n"
     "  sed 's/\\(a\\) \\(b\\)/\\2 \\1/' file"},
    {"seq", "seq [<first> [<step>]] <last>", "count", NULL},
    {"sha1sum", "sha1sum <file> ...", "the SHA1 of each file", NULL},
    {"sha256sum", "sha256sum <file> ...", "the SHA256 of each file", NULL},
    {"shuf", "shuf [<file> ...]", "shuffle the lines", NULL},
    {"sleep", "sleep <seconds>", "wait, fractions allowed", NULL},
    {"sort", "sort [-r] [-n] [-u] [<file> ...]", "sort lines",
     "  -r   reverse   -n   compare as numbers   -u   drop duplicates"},
    {"stat", "stat <file> ...", "size, type and when it changed", NULL},
    {"tac", "tac [<file> ...]", "print the lines last first", NULL},
    {"tail", "tail [-n <count>] [<file> ...]", "the last lines, ten by default", NULL},
    {"tee", "tee [-a] <file> ...", "copy input to files and on through",
     "  -a   add to the files instead of replacing them"},
    {"touch", "touch <file> ...", "make a file, or update its time", NULL},
    {"tr", "tr [-d] <set> [<set>]", "swap or drop characters",
     "  tr a-z A-Z        upper case\n"
     "  tr -d '\\r'        drop carriage returns"},
    {"uname", "uname [-a]", "the system name", NULL},
    {"until", "until <command>; do <commands>; done", "loop while a command keeps failing",
     "  until test -f ready; do sleep 1; done"},
    {"uniq", "uniq [-c] [<file> ...]", "collapse repeated neighbouring lines",
     "  -c   count them. Sort first if the duplicates are apart."},
    {"wc", "wc [-l] [-w] [-c] [<file> ...]", "count lines, words and characters", NULL},
    {"while", "while <command>; do <commands>; done", "loop while a command keeps succeeding",
     "  while read line; do echo \"$line\"; done < file"},
    {"z", "z [<word> ...]", "jump to a directory you have used before",
     "Every word has to appear somewhere in the path, and the place you use most wins.\n"
     "With no word it lists what it remembers.\n"
     "  z fresh src\n"
     "Set FRESH_JUMP=0 in ~/.freshrc to stop recording."},
    {"wget", "wget [-O <file>] <url>", "download a url",
     "  -O   the name to save it as"},
    {"whoami", "whoami", "your user name", NULL},
    {"xargs", "xargs [<command> ...]", "run a command once with the input lines as arguments",
     "  find . -name '*.tmp' | xargs rm"},
    {"yes", "yes [<text>]", "repeat a line", NULL},
};

static const size_t ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

static HelpEntry *described;
static size_t described_count;

static HelpEntry *described_find(const char *name) {
    for (size_t i = 0; i < described_count; i++) {
        if (strcmp(described[i].name, name) == 0) return &described[i];
    }
    return NULL;
}

static char *own_text(const char *text) { return text && *text ? xstrdup(text) : NULL; }

void help_describe(const char *name, const char *summary, const char *usage, const char *detail) {
    HelpEntry *entry = described_find(name);
    if (entry) {
        free((char *)entry->summary);
        free((char *)entry->usage);
        free((char *)entry->detail);
    } else {
        described = xrealloc(described, (described_count + 1) * sizeof(*described));
        entry = &described[described_count++];
        entry->name = xstrdup(name);
    }
    entry->summary = own_text(summary);
    entry->usage = own_text(usage);
    entry->detail = own_text(detail);
}

void help_described_names(StrList *out) {
    for (size_t i = 0; i < described_count; i++) sl_push_copy(out, described[i].name);
}

int builtin_describe(int argc, char **argv) {
    if (argc < 3) {
        shell_error("describe: usage: describe name summary [usage] [detail]");
        return 1;
    }
    help_describe(argv[1], argv[2], argc > 3 ? argv[3] : NULL, argc > 4 ? argv[4] : NULL);
    return 0;
}

static size_t total_count(void) { return described_count + ENTRY_COUNT; }

static const HelpEntry *entry_at(size_t index) {
    return index < described_count ? &described[index] : &ENTRIES[index - described_count];
}

static const HelpEntry *find_entry(const char *name) {
    const HelpEntry *entry = described_find(name);
    if (entry) return entry;
    for (size_t i = 0; i < ENTRY_COUNT; i++) {
        if (strcmp(ENTRIES[i].name, name) == 0) return &ENTRIES[i];
    }
    if (strcmp(name, ".") == 0) return find_entry("source");
    if (strcmp(name, "[") == 0) return find_entry("test");
    if (strcmp(name, "typeset") == 0) return find_entry("declare");
    if (strcmp(name, "quit") == 0) return find_entry("exit");
    return NULL;
}

static void collect_flags(const char *text, const char *prefix, size_t length, StrList *out) {
    if (!text) return;

    for (const char *p = text; *p; p++) {
        if (*p != '-' || (p != text && p[-1] != ' ' && p[-1] != '\n' && p[-1] != '[')) continue;

        size_t span = 1;
        if (p[span] == '-') span++;
        while (isalnum((unsigned char)p[span]) || p[span] == '-') span++;
        if (span < 2) continue;

        char flag[32];
        if (span >= sizeof(flag)) continue;
        memcpy(flag, p, span);
        flag[span] = '\0';

        if (_strnicmp(flag, prefix, length) == 0 && !sl_contains(out, flag)) sl_push_copy(out, flag);
        p += span - 1;
    }
}

void help_flags(const char *name, const char *prefix, StrList *out) {
    const HelpEntry *entry = find_entry(name);
    if (!entry) return;

    size_t length = strlen(prefix);
    collect_flags(entry->usage, prefix, length, out);
    collect_flags(entry->detail, prefix, length, out);
}

const char *help_summary(const char *name) {
    const HelpEntry *entry = find_entry(name);
    return entry ? entry->summary : NULL;
}

static void print_detail(const char *detail) {
    const char *line = detail;
    while (line && *line) {
        const char *end = strchr(line, '\n');
        int length = end ? (int)(end - line) : (int)strlen(line);
        printf("  %.*s\n", length, line);
        if (!end) break;
        line = end + 1;
    }
}

static char *first_word(const char *text) {
    while (*text == ' ' || *text == '\t') text++;
    size_t length = 0;
    while (text[length] && text[length] != ' ' && text[length] != '\t') length++;
    return xstrndup(text, length);
}

int help_show(const char *name) {
    const HelpEntry *entry = find_entry(name);
    const char *alias = alias_get(name);

    if (!entry && alias) {
        char *target = first_word(alias);
        entry = find_entry(target);
        free(target);
        printf("\n  %s%s%s  %san alias for %s%s\n", style(S_HEADING), name, style(S_RESET),
               style(S_DIM), alias, style(S_RESET));
        if (!entry) {
            printf("\n");
            return 0;
        }
    }

    if (!entry) {
        printf("  %sno help for %s%s\n", style(S_DIM), name, style(S_RESET));

        StrList near;
        sl_init(&near);
        size_t length = strlen(name);

        for (size_t width = length; width > 0 && near.len == 0; width--) {
            for (size_t i = 0; i < total_count(); i++) {
                if (strncmp(entry_at(i)->name, name, width) == 0)
                    sl_push_copy(&near, entry_at(i)->name);
            }
        }
        if (near.len == 0) {
            for (size_t i = 0; i < total_count(); i++) {
                if (strstr(entry_at(i)->name, name)) sl_push_copy(&near, entry_at(i)->name);
            }
        }
        sl_sort(&near);
        if (near.len > 0) {
            printf("  %sdid you mean%s", style(S_DIM), style(S_RESET));
            for (size_t i = 0; i < near.len && i < 8; i++) printf(" %s", near.items[i]);
            printf("\n");
        }
        sl_free(&near);
        return 1;
    }

    printf("%s  %s%s%s  %s%s%s\n", alias ? "" : "\n", style(S_HEADING), entry->name, style(S_RESET),
           style(S_DIM), entry->summary ? entry->summary : "", style(S_RESET));
    printf("  %s%s%s\n", style(S_ACCENT), entry->usage ? entry->usage : entry->name,
           style(S_RESET));

    if (entry->detail) {
        printf("\n");
        print_detail(entry->detail);
    }
    printf("\n  %s<required>  [optional]  ... repeatable%s\n\n", style(S_DIM), style(S_RESET));
    return 0;
}
