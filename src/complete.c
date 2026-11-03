/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "complete.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"

#include "builtins.h"
#include "commands.h"
#include "exec.h"
#include "expand.h"
#include "foreign.h"
#include "help.h"
#include "parser.h"
#include "shell.h"
#include "style.h"
#include "table.h"
#include "vars.h"

#define DEFAULT_WORDBREAKS " \t\n\"'><=;|&("

enum {
    ACT_ALIAS = 1 << 0,
    ACT_ARRAYVAR = 1 << 1,
    ACT_BUILTIN = 1 << 2,
    ACT_COMMAND = 1 << 3,
    ACT_DIRECTORY = 1 << 4,
    ACT_EXPORT = 1 << 5,
    ACT_FILE = 1 << 6,
    ACT_FUNCTION = 1 << 7,
    ACT_KEYWORD = 1 << 8,
    ACT_USER = 1 << 9,
    ACT_VARIABLE = 1 << 10,
};

enum {
    OPT_BASHDEFAULT = 1 << 0,
    OPT_DEFAULT = 1 << 1,
    OPT_DIRNAMES = 1 << 2,
    OPT_FILENAMES = 1 << 3,
    OPT_NOQUOTE = 1 << 4,
    OPT_NOSORT = 1 << 5,
    OPT_NOSPACE = 1 << 6,
    OPT_PLUSDIRS = 1 << 7,
    OPT_FULLQUOTE = 1 << 8,
};

enum { FLAG_PRINT = 1, FLAG_REMOVE = 2 };

static const struct {
    const char *name;
    char letter;
    unsigned bit;
} ACTIONS[] = {
    {"alias", 'a', ACT_ALIAS},         {"arrayvar", 0, ACT_ARRAYVAR},
    {"binding", 0, 0},                 {"builtin", 'b', ACT_BUILTIN},
    {"command", 'c', ACT_COMMAND},     {"directory", 'd', ACT_DIRECTORY},
    {"disabled", 0, 0},                {"enabled", 0, ACT_BUILTIN},
    {"export", 'e', ACT_EXPORT},       {"file", 'f', ACT_FILE},
    {"function", 0, ACT_FUNCTION},     {"group", 'g', 0},
    {"helptopic", 0, ACT_BUILTIN},     {"hostname", 0, 0},
    {"job", 'j', 0},                   {"keyword", 'k', ACT_KEYWORD},
    {"running", 0, 0},                 {"service", 's', 0},
    {"setopt", 0, 0},                  {"shopt", 0, 0},
    {"signal", 0, 0},                  {"stopped", 0, 0},
    {"user", 'u', ACT_USER},           {"variable", 'v', ACT_VARIABLE},
};

static const char *OPTION_NAMES[] = {"bashdefault", "default", "dirnames", "filenames", "noquote",
                                     "nosort",      "nospace", "plusdirs", "fullquote"};

typedef struct {
    unsigned actions;
    unsigned options;
    char *words;
    char *function;
    char *command;
    char *filter;
    char *prefix;
    char *suffix;
    char *glob;
} Spec;

typedef struct {
    int active;
    char *line;
    size_t point;
    unsigned options;
} Context;

static Table specs;
static Table tried;
static Context current;
static int last_nospace;

static size_t token_start(const char *buffer, size_t cursor) {
    size_t start = 0;
    char quote = 0;

    for (size_t i = 0; i < cursor; i++) {
        char c = buffer[i];
        if (quote) {
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            continue;
        }
        if (strchr(" \t|&;<>(", c)) start = i + 1;
    }
    return start;
}

static int at_command_position(const char *buffer, size_t start) {
    for (size_t i = start; i > 0; i--) {
        char c = buffer[i - 1];
        if (c == ' ' || c == '\t') continue;
        return c == '|' || c == '&' || c == ';' || c == '(';
    }
    return 1;
}

static void push_unique(StrList *list, const char *value) {
    if (!sl_contains(list, value)) sl_push_copy(list, value);
}

static void complete_variables(const char *token, StrList *out) {
    const char *prefix = token + 1;
    int braced = *prefix == '{';
    if (braced) prefix++;
    size_t prefix_len = strlen(prefix);

    StrList names;
    sl_init(&names);
    vars_list(&names);
    for (size_t i = 0; i < names.len; i++) {
        char *eq = strchr(names.items[i], '=');
        if (eq) *eq = '\0';
        if (strncmp(names.items[i], prefix, prefix_len) == 0) {
            StrBuf sb;
            sb_init(&sb);
            sb_printf(&sb, braced ? "${%s}" : "$%s", names.items[i]);
            push_unique(out, sb.data);
            sb_free(&sb);
        }
    }
    sl_free(&names);
}

static void complete_commands(const char *token, StrList *out) {
    size_t length = strlen(token);

    StrList found;
    sl_init(&found);
    keyword_complete(token, length, &found);
    builtin_complete(token, length, &found);
    coreutil_complete(token, length, &found);
    function_complete(token, length, &found);
    alias_complete(token, length, &found);
    path_command_complete(token, &found);
    if (option_enabled("FRESH_FOREIGN", 1)) foreign_complete(token, length, &found);

    for (size_t i = 0; i < found.len; i++) push_unique(out, found.items[i]);
    sl_free(&found);
    sl_sort(out);
}

static char *command_word(const char *buffer, size_t start) {
    size_t begin = start;
    while (begin > 0) {
        char c = buffer[begin - 1];
        if (c == '|' || c == ';' || c == '&' || c == '(') break;
        begin--;
    }
    while (buffer[begin] == ' ' || buffer[begin] == '\t') begin++;
    size_t end = begin;
    while (buffer[end] && !isspace((unsigned char)buffer[end])) end++;
    return xstrndup(buffer + begin, end - begin);
}

static int wants_commands(const char *command) {
    return strcmp(command, "help") == 0 || strcmp(command, "which") == 0 ||
           strcmp(command, "type") == 0 || strcmp(command, "man") == 0 ||
           strcmp(command, "describe") == 0;
}

static int wants_directories(const char *command) {
    return strcmp(command, "cd") == 0 || strcmp(command, "rmdir") == 0 ||
           strcmp(command, "pushd") == 0 || strcmp(command, "mkdir") == 0;
}

static void list_paths(const char *token, StrList *out, int directories_only, int mark) {
    char expanded[PATH_BUF];
    const char *home = var_get("HOME");
    if (token[0] == '~' && (token[1] == '/' || token[1] == '\\' || token[1] == '\0'))
        snprintf(expanded, sizeof(expanded), "%s%s", home ? home : home_dir(), token + 1);
    else snprintf(expanded, sizeof(expanded), "%s", token);

    char native[PATH_BUF];
    snprintf(native, sizeof(native), "%s", expanded);
    path_to_backslashes(native);

    char directory[PATH_BUF] = "";
    const char *leaf = native;
    char *slash = path_last_sep(native);
    if (slash) {
        size_t length = (size_t)(slash - native) + 1;
        memcpy(directory, native, length);
        directory[length] = '\0';
        leaf = slash + 1;
    }

    char pattern[PATH_BUF];
    snprintf(pattern, sizeof(pattern), "%s%s*", directory, leaf);

    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return;

    const char *token_slash = strpbrk(token, "/\\");
    const char *token_last = NULL;
    while (token_slash) {
        token_last = token_slash;
        token_slash = strpbrk(token_slash + 1, "/\\");
    }
    size_t prefix_length = token_last ? (size_t)(token_last - token) + 1 : 0;

    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        if (data.cFileName[0] == '.' && leaf[0] != '.') continue;
        if (directories_only && !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;

        StrBuf sb;
        sb_init(&sb);
        sb_putn(&sb, token, prefix_length);
        sb_puts(&sb, data.cFileName);
        if (mark && (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) sb_putc(&sb, '/');
        path_to_slashes(sb.data);
        push_unique(out, sb.data);
        sb_free(&sb);
    } while (FindNextFileA(find, &data));
    FindClose(find);
    sl_sort(out);
}

static void complete_paths(const char *token, StrList *out, int directories_only) {
    list_paths(token, out, directories_only, 1);
}

static void spec_clear(Spec *spec) {
    free(spec->words);
    free(spec->function);
    free(spec->command);
    free(spec->filter);
    free(spec->prefix);
    free(spec->suffix);
    free(spec->glob);
}

static void spec_free(void *value) {
    if (!value) return;
    spec_clear(value);
    free(value);
}

static Spec *spec_copy(const Spec *spec) {
    Spec *copy = xmalloc(sizeof(Spec));
    *copy = *spec;
    copy->words = spec->words ? xstrdup(spec->words) : NULL;
    copy->function = spec->function ? xstrdup(spec->function) : NULL;
    copy->command = spec->command ? xstrdup(spec->command) : NULL;
    copy->filter = spec->filter ? xstrdup(spec->filter) : NULL;
    copy->prefix = spec->prefix ? xstrdup(spec->prefix) : NULL;
    copy->suffix = spec->suffix ? xstrdup(spec->suffix) : NULL;
    copy->glob = spec->glob ? xstrdup(spec->glob) : NULL;
    return copy;
}

static void specs_ready(void) {
    if (!specs.buckets) table_init(&specs, 32, 1, spec_free);
    if (!tried.buckets) table_init(&tried, 32, 1, NULL);
}

static unsigned option_bit(const char *name) {
    for (size_t i = 0; i < sizeof(OPTION_NAMES) / sizeof(OPTION_NAMES[0]); i++) {
        if (strcmp(OPTION_NAMES[i], name) == 0) return 1u << i;
    }
    return 0;
}

static int action_index(const char *name, char letter) {
    for (size_t i = 0; i < sizeof(ACTIONS) / sizeof(ACTIONS[0]); i++) {
        if (name ? strcmp(ACTIONS[i].name, name) == 0 : ACTIONS[i].letter == letter) return (int)i;
    }
    return -1;
}

static void replace_string(char **slot, const char *value) {
    free(*slot);
    *slot = xstrdup(value);
}

static int spec_parse(const char *tool, int argc, char **argv, Spec *spec, int *flags) {
    int i = 1;
    for (; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--") == 0) return i + 1;
        if (arg[0] != '-' || !arg[1]) break;

        for (const char *p = arg + 1; *p; p++) {
            int action = action_index(NULL, *p);
            if (action >= 0) {
                spec->actions |= ACTIONS[action].bit;
                continue;
            }
            if (*p == 'p' && flags) {
                *flags |= FLAG_PRINT;
                continue;
            }
            if (*p == 'r' && flags) {
                *flags |= FLAG_REMOVE;
                continue;
            }
            if (!strchr("oAGWFCXPS", *p)) {
                shell_error("%s: -%c: invalid option", tool, *p);
                return -1;
            }
            const char *value = p[1] ? p + 1 : (i + 1 < argc ? argv[++i] : NULL);
            if (!value) {
                shell_error("%s: -%c: option requires an argument", tool, *p);
                return -1;
            }
            switch (*p) {
            case 'o': {
                unsigned bit = option_bit(value);
                if (!bit) {
                    shell_error("%s: %s: invalid option name", tool, value);
                    return -1;
                }
                spec->options |= bit;
                break;
            }
            case 'A': {
                int found = action_index(value, 0);
                if (found < 0) {
                    shell_error("%s: %s: invalid action name", tool, value);
                    return -1;
                }
                spec->actions |= ACTIONS[found].bit;
                break;
            }
            case 'G': replace_string(&spec->glob, value); break;
            case 'W': replace_string(&spec->words, value); break;
            case 'F': replace_string(&spec->function, value); break;
            case 'C': replace_string(&spec->command, value); break;
            case 'X': replace_string(&spec->filter, value); break;
            case 'P': replace_string(&spec->prefix, value); break;
            case 'S': replace_string(&spec->suffix, value); break;
            }
            break;
        }
    }
    return i;
}

static void spec_print(const char *name, const Spec *spec) {
    StrBuf sb;
    sb_init(&sb);
    sb_puts(&sb, "complete");
    for (size_t i = 0; i < sizeof(OPTION_NAMES) / sizeof(OPTION_NAMES[0]); i++) {
        if (spec->options & (1u << i)) sb_printf(&sb, " -o %s", OPTION_NAMES[i]);
    }
    for (size_t i = 0; i < sizeof(ACTIONS) / sizeof(ACTIONS[0]); i++) {
        if (!ACTIONS[i].bit || !(spec->actions & ACTIONS[i].bit)) continue;
        if (action_index(NULL, ACTIONS[i].letter) != (int)i) continue;
        if (ACTIONS[i].letter) sb_printf(&sb, " -%c", ACTIONS[i].letter);
        else sb_printf(&sb, " -A %s", ACTIONS[i].name);
    }
    const struct {
        char letter;
        const char *value;
    } strings[] = {{'G', spec->glob},   {'W', spec->words},  {'P', spec->prefix},
                   {'S', spec->suffix}, {'X', spec->filter}, {'C', spec->command}};
    for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); i++) {
        if (!strings[i].value) continue;
        sb_printf(&sb, " -%c ", strings[i].letter);
        sb_put_quoted(&sb, strings[i].value);
    }
    if (spec->function) sb_printf(&sb, " -F %s", spec->function);
    sb_printf(&sb, " %s\n", name);
    fputs(sb.data, stdout);
    sb_free(&sb);
}

static void filter_prefix(const StrList *from, const char *word, StrList *out) {
    size_t length = strlen(word);
    for (size_t i = 0; i < from->len; i++) {
        if (*from->items[i] && strncmp(from->items[i], word, length) == 0)
            push_unique(out, from->items[i]);
    }
}

static void generate_actions(unsigned actions, const char *word, StrList *out) {
    size_t length = strlen(word);
    StrList found;
    sl_init(&found);

    if (actions & ACT_ALIAS) alias_complete(word, length, &found);
    if (actions & ACT_BUILTIN) builtin_complete(word, length, &found);
    if (actions & ACT_KEYWORD) keyword_complete(word, length, &found);
    if (actions & ACT_FUNCTION) function_complete(word, length, &found);
    if (actions & ACT_COMMAND) complete_commands(word, &found);
    if (actions & ACT_FILE) list_paths(word, &found, 0, 0);
    else if (actions & ACT_DIRECTORY) list_paths(word, &found, 1, 0);
    if (actions & ACT_USER) {
        const char *user = var_get("USER");
        if (!user) user = var_get("USERNAME");
        if (user) sl_push_copy(&found, user);
    }

    StrList names;
    sl_init(&names);
    if (actions & (ACT_VARIABLE | ACT_ARRAYVAR)) vars_list(&names);
    else if (actions & ACT_EXPORT) vars_exported_names(&names);
    int arrays_only = (actions & ACT_ARRAYVAR) && !(actions & ACT_VARIABLE);
    for (size_t i = 0; i < names.len; i++) {
        char *eq = strchr(names.items[i], '=');
        if (arrays_only && (!eq || eq[1] != '(')) names.items[i][0] = '\0';
        else if (eq) *eq = '\0';
    }
    filter_prefix(&names, word, &found);
    sl_free(&names);

    sl_sort(&found);
    filter_prefix(&found, word, out);
    sl_free(&found);
}

typedef struct {
    size_t start;
    size_t end;
} Span;

static int is_blank(char c) { return c == ' ' || c == '\t' || c == '\n'; }

static char *word_breaks(const char *exclude) {
    const char *breaks = var_get("COMP_WORDBREAKS");
    if (!breaks) breaks = DEFAULT_WORDBREAKS;
    StrBuf sb;
    sb_init(&sb);
    for (const char *p = breaks; *p; p++) {
        if (is_blank(*p) || *p == '"' || *p == '\'' || *p == '\\') continue;
        if (exclude && strchr(exclude, *p)) continue;
        sb_putc(&sb, *p);
    }
    return sb_take(&sb);
}

static size_t skip_group(const char *line, size_t i, size_t n) {
    char open = line[i];
    char close = open == '(' ? ')' : '}';
    int depth = 0;
    char quote = 0;
    for (; i < n; i++) {
        char c = line[i];
        if (quote) {
            if (c == quote) quote = 0;
            else if (c == '\\' && quote == '"' && i + 1 < n) i++;
            continue;
        }
        if (c == '\\' && i + 1 < n) {
            i++;
            continue;
        }
        if (open == '`') {
            if (c == '`' && depth++ > 0) return i + 1;
            continue;
        }
        if (c == '\'' || c == '"') quote = c;
        else if (c == open) depth++;
        else if (c == close && --depth == 0) return i + 1;
    }
    return n;
}

static size_t split_line(const char *line, const char *breaks, Span **spans) {
    size_t count = 0, cap = 8;
    *spans = xmalloc(cap * sizeof(Span));
    size_t n = strlen(line);

    for (size_t i = 0; i < n;) {
        if (is_blank(line[i])) {
            i++;
            continue;
        }
        size_t start = i;
        if (strchr(breaks, line[i])) {
            while (i < n && !is_blank(line[i]) && strchr(breaks, line[i])) i++;
        } else {
            char quote = 0;
            while (i < n) {
                char c = line[i];
                if (quote) {
                    if (c == quote) quote = 0;
                    else if (c == '\\' && quote == '"' && i + 1 < n) i++;
                    i++;
                    continue;
                }
                if (c == '\\' && i + 1 < n) {
                    i += 2;
                    continue;
                }
                if (c == '\'' || c == '"') {
                    quote = c;
                    i++;
                    continue;
                }
                if (c == '$' && i + 1 < n && (line[i + 1] == '(' || line[i + 1] == '{')) {
                    i = skip_group(line, i + 1, n);
                    continue;
                }
                if (c == '`') {
                    i = skip_group(line, i, n);
                    continue;
                }
                if (is_blank(c) || strchr(breaks, c)) break;
                i++;
            }
        }
        if (count == cap) *spans = xrealloc(*spans, (cap *= 2) * sizeof(Span));
        (*spans)[count].start = start;
        (*spans)[count].end = i;
        count++;
    }
    return count;
}

static int run_quietly(const char *text) {
    int saved_status = shell.last_status;
    int errexit = shell.errexit;
    int nounset = shell.nounset;
    int xtrace = shell.xtrace;
    int running = shell.running;
    shell.errexit = shell.nounset = shell.xtrace = 0;

    StrBuf sb;
    sb_init(&sb);
    sb_printf(&sb, "{ %s\n} > /dev/null 2>&1", text);
    int status = exec_text(sb.data);
    sb_free(&sb);

    if (!shell.running) shell.running = running;
    shell.returning = 0;
    shell.errexit = errexit;
    shell.nounset = nounset;
    shell.xtrace = xtrace;
    shell.last_status = saved_status;
    return status;
}

static void append_call(StrBuf *sb, const char *head, const char *command, const char *word,
                        const char *previous) {
    sb_puts(sb, head);
    sb_putc(sb, ' ');
    sb_put_quoted(sb, command);
    sb_putc(sb, ' ');
    sb_put_quoted(sb, word);
    sb_putc(sb, ' ');
    sb_put_quoted(sb, previous);
}

static void generate(const Spec *spec, const char *command, const char *word, const char *previous,
                     StrList *out) {
    generate_actions(spec->actions, word, out);

    if (spec->glob) {
        StrList globbed;
        sl_init(&globbed);
        glob_expand(spec->glob, &globbed);
        for (size_t i = 0; i < globbed.len; i++) push_unique(out, globbed.items[i]);
        sl_free(&globbed);
    }

    if (spec->words) {
        StrList in, words;
        sl_init(&in);
        sl_init(&words);
        Span *spans;
        size_t count = split_line(spec->words, "", &spans);
        for (size_t i = 0; i < count; i++)
            sl_push(&in, xstrndup(spec->words + spans[i].start, spans[i].end - spans[i].start));
        free(spans);
        expand_words(&in, &words);
        filter_prefix(&words, word, out);
        sl_free(&in);
        sl_free(&words);
    }

    if (spec->function && current.active) {
        var_unset("COMPREPLY");
        StrBuf sb;
        sb_init(&sb);
        append_call(&sb, spec->function, command, word, previous);
        run_quietly(sb.data);
        sb_free(&sb);

        StrList reply;
        sl_init(&reply);
        var_values("COMPREPLY", &reply);
        for (size_t i = 0; i < reply.len; i++) push_unique(out, reply.items[i]);
        sl_free(&reply);
        var_unset("COMPREPLY");
    }

    if (spec->command) {
        StrBuf sb, output;
        sb_init(&sb);
        sb_init(&output);
        append_call(&sb, spec->command, command, word, previous);
        sb_puts(&sb, " 2> /dev/null");
        capture_command(sb.data, &output);
        char *cursor = output.data;
        char *line;
        while (cursor && (line = str_next_field(&cursor, '\n')) != NULL) {
            size_t length = strlen(line);
            if (length && line[length - 1] == '\r') line[length - 1] = '\0';
            if (*line) push_unique(out, line);
        }
        sb_free(&sb);
        sb_free(&output);
    }

    if (spec->filter) {
        int negate = spec->filter[0] == '!';
        StrBuf pattern;
        sb_init(&pattern);
        for (const char *p = spec->filter + negate; *p; p++) {
            if (*p == '&') sb_puts(&pattern, word);
            else sb_putc(&pattern, *p);
        }
        size_t kept = 0;
        for (size_t i = 0; i < out->len; i++) {
            int match = pattern_match(pattern.data, out->items[i]);
            if (match == negate) out->items[kept++] = out->items[i];
            else free(out->items[i]);
        }
        out->len = kept;
        sb_free(&pattern);
    }

    if (spec->prefix || spec->suffix) {
        for (size_t i = 0; i < out->len; i++) {
            StrBuf sb;
            sb_init(&sb);
            sb_printf(&sb, "%s%s%s", spec->prefix ? spec->prefix : "", out->items[i],
                      spec->suffix ? spec->suffix : "");
            free(out->items[i]);
            out->items[i] = sb_take(&sb);
        }
    }
}

static int words_at(const char *line, size_t point, const char *exclude, StrList *words,
                    size_t *word_start) {
    char *breaks = word_breaks(exclude);
    Span *spans;
    size_t count = split_line(line, breaks, &spans);
    free(breaks);

    int cword = -1;
    size_t before = 0;
    for (size_t i = 0; i < count; i++) {
        if (spans[i].start <= point && point <= spans[i].end) {
            cword = (int)i;
            break;
        }
        if (spans[i].end < point) before = i + 1;
    }

    for (size_t i = 0; i < count; i++) {
        if (cword < 0 && i == before) sl_push_copy(words, "");
        sl_push(words, xstrndup(line + spans[i].start, spans[i].end - spans[i].start));
    }
    if (cword < 0 && before == count) sl_push_copy(words, "");

    if (cword < 0) {
        cword = (int)before;
        *word_start = point;
    } else {
        *word_start = spans[cword].start;
    }
    free(spans);
    return cword;
}

static int is_separator(char c) { return c && strchr(";|&()\n", c) != NULL; }

static void command_bounds(const char *buffer, size_t cursor, size_t *start, size_t *end) {
    char quote = 0;
    *start = 0;
    for (size_t i = 0; i < cursor; i++) {
        char c = buffer[i];
        if (quote) {
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') quote = c;
        else if (c == '\\' && i + 1 < cursor) i++;
        else if (is_separator(c)) *start = i + 1;
    }

    size_t i = cursor;
    while (buffer[i]) {
        char c = buffer[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else if (c == '\'' || c == '"') {
            quote = c;
        } else if (is_separator(c)) {
            break;
        }
        i++;
    }
    *end = i;

    for (;;) {
        size_t s = *start;
        while (s < cursor && is_blank(buffer[s])) s++;
        size_t name = s;
        while (name < cursor && (isalnum((unsigned char)buffer[name]) || buffer[name] == '_'))
            name++;
        if (name == s || name >= cursor || buffer[name] != '=' ||
            isdigit((unsigned char)buffer[s])) {
            *start = s;
            return;
        }
        size_t after = name;
        while (after < cursor && !is_blank(buffer[after])) after++;
        if (after >= cursor) {
            *start = s;
            return;
        }
        *start = after;
    }
}

static char *completions_dir(void) {
    const char *custom = var_get("FRESH_HOME");
    char *home = custom && *custom ? xstrdup(custom) : config_path(".fresh");
    char *directory = path_join(home, "completions");
    free(home);
    return directory;
}

static int source_from(const char *directory, const char *name) {
    const char *patterns[] = {"%s", "%s.bash", "%s.sh", "%s-completion.bash"};
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        char file[PATH_BUF];
        snprintf(file, sizeof(file), patterns[i], name);
        char *path = path_join(directory, file);
        int found = path_is_file(path);
        if (found) {
            StrBuf sb;
            sb_init(&sb);
            sb_puts(&sb, "source ");
            sb_put_quoted(&sb, path);
            run_quietly(sb.data);
            sb_free(&sb);
        }
        free(path);
        if (found) return 1;
    }
    return 0;
}

static void load_completion(const char *name) {
    specs_ready();
    if (!*name || table_get(&tried, name)) return;
    table_put(&tried, name, (void *)1);

    char *own = completions_dir();
    int found = source_from(own, name);
    free(own);
    if (found) return;

    char resolved[PATH_BUF];
    if (!resolve_command(name, resolved, sizeof(resolved))) return;
    char *slash = path_last_sep(resolved);
    if (!slash) return;
    *slash = '\0';

    char share[PATH_BUF], mingw[PATH_BUF];
    snprintf(share, sizeof(share), "share/%s/completion", name);
    snprintf(mingw, sizeof(mingw), "mingw64/share/%s/completion", name);
    const char *places[] = {"share/bash-completion/completions", "etc/bash_completion.d", share,
                            mingw};
    char *parent = path_join(resolved, "..");
    for (size_t i = 0; i < sizeof(places) / sizeof(places[0]); i++) {
        char *directory = path_join(parent, places[i]);
        found = path_is_dir(directory) && source_from(directory, name);
        free(directory);
        if (found) break;
    }
    free(parent);
}

static Spec *spec_find(const char *name) {
    specs_ready();
    Spec *spec = table_get(&specs, name);
    if (spec) return spec;

    const char *base = name;
    for (const char *p = name; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    char *plain = xstrdup(base);
    char *dot = strrchr(plain, '.');
    if (dot && dot != plain) *dot = '\0';

    spec = table_get(&specs, base);
    if (!spec) spec = table_get(&specs, plain);
    if (!spec) {
        load_completion(plain);
        spec = table_get(&specs, plain);
    }
    free(plain);
    return spec;
}

static char *alias_target(const char *name) {
    const char *alias = alias_get(name);
    if (!alias) return NULL;
    while (is_blank(*alias)) alias++;
    size_t length = 0;
    while (alias[length] && !is_blank(alias[length])) length++;
    for (size_t i = length; alias[i]; i++) {
        if (!is_blank(alias[i])) return NULL;
    }
    return length ? xstrndup(alias, length) : NULL;
}

static int programmable(const char *buffer, size_t cursor, StrList *matches,
                        size_t *replace_start) {
    size_t start, end;
    command_bounds(buffer, cursor, &start, &end);
    if (start >= cursor) return 0;

    char *line = xstrndup(buffer + start, end - start);
    size_t point = cursor - start;

    StrList words;
    sl_init(&words);
    size_t word_start = 0;
    int cword = words_at(line, point, NULL, &words, &word_start);
    if (cword < 1 || words.len == 0) {
        sl_free(&words);
        free(line);
        return 0;
    }

    char *command = xstrdup(words.items[0]);
    Spec *found = spec_find(command);
    if (!found) {
        char *target = alias_target(command);
        if (target) {
            found = spec_find(target);
            if (found) {
                free(words.items[0]);
                words.items[0] = xstrdup(target);
                free(command);
                command = xstrdup(target);
            }
            free(target);
        }
    }
    if (!found) {
        free(command);
        sl_free(&words);
        free(line);
        return 0;
    }

    Spec *spec = spec_copy(found);
    char *word = xstrndup(line + word_start, point - word_start);
    const char *previous = cword > 0 ? words.items[cword - 1] : "";

    char number[32];
    current.active = 1;
    current.line = line;
    current.point = point;
    current.options = spec->options;
    var_set_exported("COMP_LINE", line);
    snprintf(number, sizeof(number), "%zu", point);
    var_set_exported("COMP_POINT", number);
    var_set_exported("COMP_KEY", "9");
    var_set_exported("COMP_TYPE", "37");
    snprintf(number, sizeof(number), "%d", cword);
    var_set("COMP_CWORD", number);
    var_set_array("COMP_WORDS", &words, VAR_INDEXED);

    StrList found_words;
    sl_init(&found_words);
    generate(spec, command, word, previous, &found_words);

    const char *unset[] = {"COMP_LINE", "COMP_POINT", "COMP_KEY",
                           "COMP_TYPE", "COMP_CWORD", "COMP_WORDS"};
    for (size_t i = 0; i < sizeof(unset) / sizeof(unset[0]); i++) var_unset(unset[i]);
    unsigned options = current.options;
    current.active = 0;
    current.line = NULL;

    if (options & OPT_PLUSDIRS) list_paths(word, &found_words, 1, 1);
    if (found_words.len == 0 && (options & OPT_DIRNAMES)) list_paths(word, &found_words, 1, 1);

    int marks = (options & (OPT_FILENAMES | OPT_DIRNAMES)) ||
                (spec->actions & (ACT_FILE | ACT_DIRECTORY));
    for (size_t i = 0; i < found_words.len; i++) {
        char *item = found_words.items[i];
        size_t length = strlen(item);
        if (marks && length && item[length - 1] != '/' && path_is_dir(item)) {
            StrBuf sb;
            sb_init(&sb);
            sb_printf(&sb, "%s/", item);
            free(item);
            found_words.items[i] = sb_take(&sb);
        }
        push_unique(matches, found_words.items[i]);
    }
    if (!(options & OPT_NOSORT)) sl_sort(matches);

    int answered = matches->len > 0;
    if (answered) {
        *replace_start = start + word_start;
        last_nospace = (options & OPT_NOSPACE) != 0;
    }

    sl_free(&found_words);
    spec_free(spec);
    free(word);
    free(command);
    sl_free(&words);
    free(line);
    return answered;
}

int complete_nospace(void) { return last_nospace; }

void complete_forget(void) {
    if (tried.buckets) table_free(&tried);
    memset(&tried, 0, sizeof(tried));
}

void complete_at(const char *buffer, size_t cursor, StrList *matches, size_t *replace_start) {
    size_t start = token_start(buffer, cursor);
    *replace_start = start;
    last_nospace = 0;

    char *raw = xstrndup(buffer + start, cursor - start);
    char *token = raw;
    if (*token == '"' || *token == '\'') token++;

    if (token[0] == '$') {
        complete_variables(token, matches);
    } else if (at_command_position(buffer, start) && !strpbrk(token, "/\\.")) {
        complete_commands(token, matches);
        complete_paths(token, matches, 0);
    } else if (!programmable(buffer, cursor, matches, replace_start)) {
        *replace_start = start;
        char *command = command_word(buffer, start);
        if (token[0] == '-') help_flags(command, token, matches);
        else if (wants_commands(command)) complete_commands(token, matches);
        else complete_paths(token, matches, wants_directories(command));
        free(command);
    }
    free(raw);
}

int builtin_complete_spec(int argc, char **argv) {
    Spec spec;
    memset(&spec, 0, sizeof(spec));
    int flags = 0;
    int index = spec_parse("complete", argc, argv, &spec, &flags);
    if (index < 0) {
        spec_clear(&spec);
        return 2;
    }
    specs_ready();

    int status = 0;
    if (flags & FLAG_REMOVE) {
        if (index >= argc) {
            table_free(&specs);
            memset(&specs, 0, sizeof(specs));
            specs_ready();
        }
        for (int i = index; i < argc; i++) table_remove(&specs, argv[i]);
    } else if ((flags & FLAG_PRINT) || index >= argc) {
        StrList names;
        sl_init(&names);
        if (index >= argc) {
            table_names(&specs, &names);
            sl_sort(&names);
        } else {
            for (int i = index; i < argc; i++) sl_push_copy(&names, argv[i]);
        }
        for (size_t i = 0; i < names.len; i++) {
            Spec *found = table_get(&specs, names.items[i]);
            if (found) {
                spec_print(names.items[i], found);
            } else {
                shell_error("complete: %s: no completion specification", names.items[i]);
                status = 1;
            }
        }
        sl_free(&names);
    } else {
        for (int i = index; i < argc; i++) table_put(&specs, argv[i], spec_copy(&spec));
    }

    spec_clear(&spec);
    return status;
}

int builtin_compgen(int argc, char **argv) {
    Spec spec;
    memset(&spec, 0, sizeof(spec));
    int index = spec_parse("compgen", argc, argv, &spec, NULL);
    int status = 2;
    if (index >= 0) {
        const char *word = index < argc ? argv[index] : "";
        StrList out;
        sl_init(&out);
        generate(&spec, "", word, "", &out);
        for (size_t i = 0; i < out.len; i++) printf("%s\n", out.items[i]);
        status = out.len ? 0 : 1;
        sl_free(&out);
    }
    spec_clear(&spec);
    return status;
}

int builtin_compopt(int argc, char **argv) {
    unsigned on = 0, off = 0;
    int i = 1;
    for (; i < argc; i++) {
        const char *arg = argv[i];
        if ((arg[0] != '-' && arg[0] != '+') || strcmp(arg + 1, "o") != 0) break;
        if (i + 1 >= argc) {
            shell_error("compopt: %s: option requires an argument", arg);
            return 2;
        }
        unsigned bit = option_bit(argv[i + 1]);
        if (!bit) {
            shell_error("compopt: %s: invalid option name", argv[i + 1]);
            return 2;
        }
        if (arg[0] == '-') on |= bit;
        else off |= bit;
        i++;
    }

    if (i >= argc) {
        if (!current.active) {
            shell_error("compopt: not currently executing completion function");
            return 1;
        }
        current.options = (current.options | on) & ~off;
        return 0;
    }

    specs_ready();
    int status = 0;
    for (; i < argc; i++) {
        Spec *spec = table_get(&specs, argv[i]);
        if (!spec) {
            shell_error("compopt: %s: no completion specification", argv[i]);
            status = 1;
            continue;
        }
        spec->options = (spec->options | on) & ~off;
    }
    return status;
}

static void set_named(const char *name, const char *value) {
    if (name) var_set(name, value);
}

int builtin_comp_words_by_ref(int argc, char **argv) {
    if (!current.active) return 1;

    const char *exclude = NULL;
    const char *cur = NULL, *prev = NULL, *words_name = NULL, *cword_name = NULL;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg[0] == '-' && strchr("ncpwi", arg[1]) && arg[1] && !arg[2] && i + 1 < argc) {
            const char *value = argv[++i];
            switch (arg[1]) {
            case 'n': exclude = value; break;
            case 'c': cur = value; break;
            case 'p': prev = value; break;
            case 'w': words_name = value; break;
            case 'i': cword_name = value; break;
            }
        } else if (strcmp(arg, "cur") == 0) {
            cur = arg;
        } else if (strcmp(arg, "prev") == 0) {
            prev = arg;
        } else if (strcmp(arg, "words") == 0) {
            words_name = arg;
        } else if (strcmp(arg, "cword") == 0) {
            cword_name = arg;
        }
    }

    StrList words;
    sl_init(&words);
    size_t word_start = 0;
    int cword = words_at(current.line, current.point, exclude, &words, &word_start);

    char *typed = xstrndup(current.line + word_start, current.point - word_start);
    set_named(cur, typed);
    free(typed);
    set_named(prev, cword > 0 ? words.items[cword - 1] : "");
    if (words_name) var_set_array(words_name, &words, VAR_INDEXED);
    if (cword_name) {
        char number[32];
        snprintf(number, sizeof(number), "%d", cword);
        var_set(cword_name, number);
    }
    sl_free(&words);
    return 0;
}

static int extension_matches(const char *name, const char *extensions) {
    char *copy = xstrdup(extensions);
    char *list = copy;
    if (str_has_prefix(list, "@(")) list += 2;
    size_t length = strlen(list);
    if (length && list[length - 1] == ')') list[length - 1] = '\0';

    int match = 0;
    char *cursor = list;
    char *extension;
    while (!match && (extension = str_next_field(&cursor, '|')) != NULL) {
        char suffix[128];
        snprintf(suffix, sizeof(suffix), ".%s", extension);
        match = str_has_suffix_i(name, suffix);
    }
    free(copy);
    return match;
}

int builtin_filedir(int argc, char **argv) {
    int directories = argc > 1 && strcmp(argv[1], "-d") == 0;
    const char *extensions = !directories && argc > 1 && *argv[1] ? argv[1] : NULL;
    const char *cur = var_get("cur");
    if (!cur) cur = "";
    if (*cur == '"' || *cur == '\'') cur++;

    StrList found, reply;
    sl_init(&found);
    sl_init(&reply);
    list_paths(cur, &found, directories, 1);
    var_values("COMPREPLY", &reply);
    for (size_t i = 0; i < found.len; i++) {
        const char *item = found.items[i];
        size_t length = strlen(item);
        int directory = length && item[length - 1] == '/';
        if (extensions && !directory && !extension_matches(item, extensions)) continue;
        sl_push_copy(&reply, item);
    }
    var_set_array("COMPREPLY", &reply, VAR_INDEXED);
    if (current.active) current.options |= OPT_FILENAMES;
    sl_free(&found);
    sl_free(&reply);
    return 0;
}
