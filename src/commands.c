/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "commands.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "shell.h"
#include "table.h"
#include "vars.h"

static Table command_table;

static void register_group(const Command *group, size_t count) {
    for (size_t i = 0; i < count; i++) table_put(&command_table, group[i].name, (void *)&group[i]);
}

static const Command *command_find(const char *name) {
    if (!command_table.buckets) {
        table_init(&command_table, 128, 0, NULL);
        register_group(TEXT_COMMANDS, TEXT_COMMAND_COUNT);
        register_group(FILE_COMMANDS, FILE_COMMAND_COUNT);
        register_group(SYSTEM_COMMANDS, SYSTEM_COMMAND_COUNT);
    }
    return table_get(&command_table, name);
}

BuiltinFn coreutil_lookup(const char *name) {
    const Command *command = command_find(name);
    return command ? command->fn : NULL;
}

const char *coreutil_usage(const char *name) {
    const Command *command = command_find(name);
    return command ? command->usage : NULL;
}

int coreutil_preferred(const char *name) {
    static const char *SHADOWED[] = {"find", "sort", "more",  "where",
                                     "printf", "echo", "awk", "kill", NULL};
    if (!coreutil_lookup(name)) return 0;
    const char *forced = var_get("FRESH_PREFER_BUNDLED");
    if (forced && *forced && strcmp(forced, "0") != 0) return 1;
    for (int i = 0; SHADOWED[i]; i++) {
        if (strcmp(SHADOWED[i], name) == 0) return 1;
    }
    return 0;
}

static void each_group(void (*visit)(const Command *, void *), void *context) {
    for (size_t i = 0; i < TEXT_COMMAND_COUNT; i++) visit(&TEXT_COMMANDS[i], context);
    for (size_t i = 0; i < FILE_COMMAND_COUNT; i++) visit(&FILE_COMMANDS[i], context);
    for (size_t i = 0; i < SYSTEM_COMMAND_COUNT; i++) visit(&SYSTEM_COMMANDS[i], context);
}

static void collect_name(const Command *command, void *context) {
    sl_push_copy(context, command->name);
}

void coreutil_names(StrList *out) {
    each_group(collect_name, out);
}

typedef struct {
    const char *prefix;
    size_t length;
    StrList *out;
    int found;
} PrefixSearch;

static void match_prefix(const Command *command, void *context) {
    PrefixSearch *search = context;
    if (_strnicmp(command->name, search->prefix, search->length) != 0) return;
    search->found = 1;
    if (search->out) sl_push_copy(search->out, command->name);
}

int coreutil_name_prefix(const char *prefix, size_t length) {
    PrefixSearch search = {prefix, length, NULL, 0};
    each_group(match_prefix, &search);
    return search.found;
}

void coreutil_complete(const char *prefix, size_t length, StrList *out) {
    PrefixSearch search = {prefix, length, out, 0};
    each_group(match_prefix, &search);
}

static const OptSpec *spec_for_letter(const OptSpec *specs, char letter) {
    for (const OptSpec *s = specs; s->letter || s->name; s++) {
        if (s->letter == letter) return s;
    }
    return NULL;
}

static const OptSpec *spec_for_name(const OptSpec *specs, const char *name, size_t length,
                                    int *ambiguous) {
    const OptSpec *found = NULL;
    *ambiguous = 0;
    for (const OptSpec *s = specs; s->letter || s->name; s++) {
        if (!s->name) continue;
        if (strlen(s->name) == length && strncmp(s->name, name, length) == 0) return s;
        if (strncmp(s->name, name, length) == 0) {
            if (found) *ambiguous = 1;
            found = s;
        }
    }
    return *ambiguous ? NULL : found;
}

static void args_add(Args *args, char letter, const char *value) {
    if (args->count >= ARGS_MAX) return;
    args->letters[args->count] = letter;
    args->values[args->count] = value;
    args->count++;
}

static void args_operand(Args *args, char *operand) {
    args->operands = xrealloc(args->operands, (size_t)(args->operand_count + 2) * sizeof(char *));
    args->operands[args->operand_count++] = operand;
    args->operands[args->operand_count] = NULL;
}

static int usage_message(const Args *args) {
    const char *usage = coreutil_usage(args->tool);
    if (usage) printf("Usage: %s %s\n", args->tool, usage);
    else printf("Usage: %s [OPTION]... [FILE]...\n", args->tool);
    return ARGS_DONE;
}

static int try_help(const char *tool) {
    fprintf(stderr, "Try '%s --help' for more information.\n", tool);
    return ARGS_ERROR;
}

int args_parse(int argc, char **argv, const OptSpec *specs, const char *tool, Args *out) {
    int number_shorthand = out->number_shorthand;
    int stop_at_operand = out->stop_at_operand;
    int negative_operands = out->negative_operands;
    memset(out, 0, sizeof(*out));
    out->number_shorthand = number_shorthand;
    out->stop_at_operand = stop_at_operand;
    out->negative_operands = negative_operands;
    out->tool = tool;
    out->operands = xmalloc(sizeof(char *));
    out->operands[0] = NULL;
    int only_operands = 0;

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (only_operands || arg[0] != '-' || !arg[1] ||
            (out->negative_operands && (isdigit((unsigned char)arg[1]) || (arg[1] == '.' && isdigit((unsigned char)arg[2]))))) {
            args_operand(out, arg);
            if (out->stop_at_operand) only_operands = 1;
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            only_operands = 1;
            continue;
        }

        if (arg[1] == '-') {
            const char *name = arg + 2;
            const char *equals = strchr(name, '=');
            size_t length = equals ? (size_t)(equals - name) : strlen(name);
            if (length == 4 && strncmp(name, "help", 4) == 0) {
                args_free(out);
                return usage_message(out);
            }
            if (length == 7 && strncmp(name, "version", 7) == 0) {
                printf("%s (FreSH) %s\n", tool, FRESH_VERSION);
                args_free(out);
                return ARGS_DONE;
            }
            int ambiguous = 0;
            const OptSpec *spec = spec_for_name(specs, name, length, &ambiguous);
            if (!spec) {
                fprintf(stderr, ambiguous ? "%s: option '--%.*s' is ambiguous\n"
                                          : "%s: unrecognized option '--%.*s'\n",
                        tool, (int)length, name);
                return try_help(tool);
            }
            if (spec->takes_value == 1) {
                if (equals) args_add(out, spec->letter, equals + 1);
                else if (i + 1 < argc) args_add(out, spec->letter, argv[++i]);
                else {
                    fprintf(stderr, "%s: option '--%s' requires an argument\n", tool, spec->name);
                    return try_help(tool);
                }
            } else if (spec->takes_value == 2) {
                args_add(out, spec->letter, equals ? equals + 1 : NULL);
            } else {
                if (equals) {
                    fprintf(stderr, "%s: option '--%s' doesn't allow an argument\n", tool,
                            spec->name);
                    return try_help(tool);
                }
                args_add(out, spec->letter, NULL);
            }
            continue;
        }

        if (out->number_shorthand && isdigit((unsigned char)arg[1])) {
            args_add(out, out->number_shorthand, arg + 1);
            continue;
        }

        for (const char *p = arg + 1; *p; p++) {
            const OptSpec *spec = spec_for_letter(specs, *p);
            if (!spec) {
                fprintf(stderr, "%s: invalid option -- '%c'\n", tool, *p);
                return try_help(tool);
            }
            if (spec->takes_value == 1) {
                if (p[1]) args_add(out, *p, p + 1);
                else if (i + 1 < argc) args_add(out, *p, argv[++i]);
                else {
                    fprintf(stderr, "%s: option requires an argument -- '%c'\n", tool, *p);
                    return try_help(tool);
                }
                break;
            }
            if (spec->takes_value == 2) {
                args_add(out, *p, p[1] ? p + 1 : NULL);
                break;
            }
            args_add(out, *p, NULL);
        }
    }
    return ARGS_OK;
}

void args_free(Args *args) {
    free(args->operands);
    args->operands = NULL;
    args->operand_count = 0;
}

int args_has(const Args *args, char letter) {
    return args_count(args, letter) > 0;
}

int args_count(const Args *args, char letter) {
    int count = 0;
    for (int i = 0; i < args->count; i++)
        if (args->letters[i] == letter) count++;
    return count;
}

const char *args_value(const Args *args, char letter) {
    const char *value = NULL;
    for (int i = 0; i < args->count; i++)
        if (args->letters[i] == letter) value = args->values[i];
    return value;
}

const char *args_value_at(const Args *args, char letter, int index) {
    int seen = 0;
    for (int i = 0; i < args->count; i++) {
        if (args->letters[i] != letter) continue;
        if (seen == index) return args->values[i];
        seen++;
    }
    return NULL;
}

char args_last_of(const Args *args, const char *letters) {
    char last = 0;
    for (int i = 0; i < args->count; i++)
        if (strchr(letters, args->letters[i])) last = args->letters[i];
    return last;
}

void cmd_error(const char *tool, const char *fmt, ...) {
    fflush(stdout);
    fprintf(stderr, "%s: ", tool);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

void cmd_file_error(const char *tool, const char *path) {
    cmd_error(tool, "%s: %s", path, strerror(errno));
}

FILE *cmd_open(const char *tool, const char *path) {
    if (!path || strcmp(path, "-") == 0) return stdin;
    if (path_is_dir(path)) {
        errno = EISDIR;
        cmd_file_error(tool, path);
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    if (!f) cmd_file_error(tool, path);
    return f;
}

void cmd_close(FILE *f) {
    if (f && f != stdin) fclose(f);
}

int cmd_read_line(FILE *f, StrBuf *out) {
    sb_clear(out);
    int c;
    int any = 0;
    _lock_file(f);
    while ((c = _fgetc_nolock(f)) != EOF) {
        any = 1;
        if (c == '\n') {
            _unlock_file(f);
            return 1;
        }
        sb_putc(out, (char)c);
    }
    _unlock_file(f);
    return any ? 2 : 0;
}

int cmd_want_stdin(const Args *args) {
    return args->operand_count == 0;
}

int cmd_read_lines(const char *tool, const Args *args, StrList *out) {
    int status = 0;
    int count = args->operand_count ? args->operand_count : 1;
    for (int i = 0; i < count; i++) {
        const char *name = args->operand_count ? args->operands[i] : NULL;
        FILE *f = cmd_open(tool, name);
        if (!f) {
            status = 1;
            continue;
        }
        StrBuf line;
        sb_init(&line);
        while (cmd_read_line(f, &line) != 0) sl_push_copy(out, line.data);
        sb_free(&line);
        cmd_close(f);
    }
    return status;
}

int cmd_read_all(const char *tool, const Args *args, StrBuf *out) {
    int status = 0;
    int count = args->operand_count ? args->operand_count : 1;
    for (int i = 0; i < count; i++) {
        const char *name = args->operand_count ? args->operands[i] : NULL;
        FILE *f = cmd_open(tool, name);
        if (!f) {
            status = 1;
            continue;
        }
        char block[65536];
        size_t n;
        while ((n = fread(block, 1, sizeof(block), f)) > 0) sb_putn(out, block, n);
        cmd_close(f);
    }
    return status;
}

long long cmd_parse_size(const char *text, int *ok) {
    *ok = 1;
    char *end;
    errno = 0;
    long long value = strtoll(text, &end, 10);
    if (end == text || errno) {
        *ok = 0;
        return 0;
    }
    long long unit = 1;
    if (*end) {
        char suffix = *end;
        int binary = end[1] != 'B';
        long long base = binary ? 1024 : 1000;
        switch (suffix) {
        case 'b': unit = 512; break;
        case 'k': case 'K': unit = base; break;
        case 'm': case 'M': unit = base * base; break;
        case 'g': case 'G': unit = base * base * base; break;
        case 't': case 'T': unit = base * base * base * base; break;
        default: *ok = 0; return 0;
        }
        end++;
        if (*end == 'B') end++;
        if (*end == 'i' && end[1] == 'B') end += 2;
        if (*end) {
            *ok = 0;
            return 0;
        }
    }
    return value * unit;
}

long long cmd_parse_number(const char *tool, const char *what, const char *text, int *ok) {
    long long value = cmd_parse_size(text, ok);
    if (!*ok) cmd_error(tool, "invalid %s: '%s'", what, text);
    return value;
}

const char *cmd_name(const char *path) {
    const char *leaf = path_last_sep(path);
    return leaf ? leaf + 1 : path;
}

int cmd_status_from_errno(void) {
    return errno == ENOENT ? 1 : 1;
}
