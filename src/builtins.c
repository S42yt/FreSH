/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "builtins.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

#ifdef _WIN32
#include <shellapi.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "action.h"
#include "commands.h"
#include "complete.h"
#include "exec.h"
#include "expand.h"
#include "foreign.h"
#include "gitinfo.h"
#include "help.h"
#include "history.h"
#include "keys.h"
#include "prompt.h"
#include "shell.h"
#include "style.h"
#include "table.h"
#include "task.h"
#include "term.h"
#include "theme.h"
#include "update.h"
#include "vars.h"

static void sync_cwd(void) {
    char cwd[PATH_BUF];
    if (GetCurrentDirectoryA(sizeof(cwd), cwd)) {
        path_to_slashes(cwd);
        var_set_exported("PWD", cwd);
        visits_record(cwd);
    }
    git_invalidate();
}

typedef struct {
    int index;
    int offset;
    int plus;
    const char *arg;
} Opts;

static int opt_next(Opts *o, int argc, char **argv, const char *name, const char *spec) {
    int allow_plus = spec[0] == '+';
    if (allow_plus) spec++;
    if (o->offset == 0) {
        if (o->index >= argc) return 0;
        const char *word = argv[o->index];
        if ((word[0] != '-' && !(allow_plus && word[0] == '+')) || !word[1]) return 0;
        if (strcmp(word, "--") == 0) {
            o->index++;
            return 0;
        }
        o->plus = word[0] == '+';
        o->offset = 1;
    }
    const char *word = argv[o->index];
    char letter = word[o->offset++];
    const char *found = letter != ':' ? strchr(spec, letter) : NULL;
    int last = word[o->offset] == '\0';
    o->arg = NULL;
    if (!found) {
        shell_error("%s: %c%c: invalid option", name, o->plus ? '+' : '-', letter);
        return '?';
    }
    if (found[1] == ':') {
        if (!last) {
            o->arg = word + o->offset;
        } else if (o->index + 1 < argc) {
            o->arg = argv[++o->index];
        } else {
            shell_error("%s: -%c: option requires an argument", name, letter);
            return '?';
        }
        last = 1;
    }
    if (last) {
        o->index++;
        o->offset = 0;
    }
    return letter;
}

static int options_only(int argc, char **argv, const char *name) {
    Opts o = {1, 0, 0, NULL};
    if (opt_next(&o, argc, argv, name, "") == '?') return -1;
    return o.index;
}

static int invalid_name(const char *builtin, const char *name) {
    shell_error("%s: `%s': not a valid identifier", builtin, name);
    return 1;
}

static int parse_number(const char *text, long long *out) {
    while (*text == ' ' || *text == '\t') text++;
    if (!*text) return 0;
    char *end;
    *out = strtoll(text, &end, 10);
    if (end == text || (end == text + 1 && (*text == '-' || *text == '+'))) return 0;
    while (*end == ' ' || *end == '\t') end++;
    return *end == '\0';
}

static int change_directory(const char *target) {
    char native[PATH_BUF];
    snprintf(native, sizeof(native), "%s", target);
    path_to_backslashes(native);
    return *target && SetCurrentDirectoryA(native);
}

static int cdpath_separator(const char *start, const char *p) {
#ifdef _WIN32
    if (*p == ':' && p == start + 1 && (p[1] == '/' || p[1] == '\\')) return 0;
#else
    (void)start;
#endif
    return *p == ':' || *p == PATH_LIST_SEP;
}

static int cd_through_cdpath(const char *target, int *show) {
    const char *cdpath = var_get("CDPATH");
    int dotted = target[0] == '.' &&
                 (!target[1] || target[1] == '/' || (target[1] == '.' && (!target[2] || target[2] == '/')));
    if (!cdpath || !*cdpath || path_is_absolute(target) || dotted) return 0;

    for (const char *start = cdpath;;) {
        const char *end = start;
        while (*end && !cdpath_separator(start, end)) end++;
        StrBuf dir;
        sb_init(&dir);
        if (end > start) {
            sb_putn(&dir, start, (size_t)(end - start));
            sb_putc(&dir, '/');
        }
        sb_puts(&dir, target);
        int entered = change_directory(dir.data);
        sb_free(&dir);
        if (entered) {
            if (end > start) *show = 1;
            return 1;
        }
        if (!*end) return 0;
        start = end + 1;
    }
}

static int enter_directory(const char *name, const char *target, int *show) {
    char previous[PATH_BUF];
    GetCurrentDirectoryA(sizeof(previous), previous);
    path_to_slashes(previous);

    if (!cd_through_cdpath(target, show) && !change_directory(target)) {
        shell_error("%s: %s: No such file or directory", name, target);
        return 0;
    }
    var_set_exported("OLDPWD", previous);
    sync_cwd();
    return 1;
}

static int builtin_cd(int argc, char **argv) {
    Opts o = {1, 0, 0, NULL};
    int c;
    while ((c = opt_next(&o, argc, argv, "cd", "LPe@")) > 0)
        if (c == '?') return 2;
    if (argc - o.index > 1) {
        shell_error("cd: too many arguments");
        return 1;
    }

    int show = 0;
    const char *target = o.index < argc ? argv[o.index] : var_get("HOME");
    if (!target) {
        shell_error("cd: HOME not set");
        return 1;
    }
    if (o.index < argc && strcmp(target, "-") == 0) {
        target = var_get("OLDPWD");
        if (!target) {
            shell_error("cd: OLDPWD not set");
            return 1;
        }
        show = 1;
    }
    if (!enter_directory("cd", target, &show)) return 1;
    if (show) printf("%s\n", var_get("PWD"));
    return 0;
}

static int builtin_pwd(int argc, char **argv) {
    Opts o = {1, 0, 0, NULL};
    int c;
    while ((c = opt_next(&o, argc, argv, "pwd", "LP")) > 0)
        if (c == '?') return 2;
    char cwd[PATH_BUF];
    if (!GetCurrentDirectoryA(sizeof(cwd), cwd)) return 1;
    path_to_slashes(cwd);
    printf("%s\n", cwd);
    return 0;
}

static int exit_status_argument(const char *name, int argc, char **argv, int *status) {
    int index = argc > 1 && strcmp(argv[1], "--") == 0 ? 2 : 1;
    long long value;
    if (index >= argc) {
        *status = shell.last_status;
        return 1;
    }
    if (!parse_number(argv[index], &value)) {
        shell_error("%s: %s: numeric argument required", name, argv[index]);
        *status = 2;
        return 1;
    }
    if (index + 1 < argc) {
        shell_error("%s: too many arguments", name);
        *status = 1;
        return 0;
    }
    *status = (int)(value & 255);
    return 1;
}

static int builtin_exit(int argc, char **argv) {
    int status;
    if (!exit_status_argument(argv[0], argc, argv, &status)) return status;
    shell.running = 0;
    return status;
}

static void put_utf8(unsigned long code) {
    if (code < 0x80) {
        putchar((int)code);
    } else if (code < 0x800) {
        putchar((int)(0xC0 | (code >> 6)));
        putchar((int)(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        putchar((int)(0xE0 | (code >> 12)));
        putchar((int)(0x80 | ((code >> 6) & 0x3F)));
        putchar((int)(0x80 | (code & 0x3F)));
    } else {
        putchar((int)(0xF0 | ((code >> 18) & 0x07)));
        putchar((int)(0x80 | ((code >> 12) & 0x3F)));
        putchar((int)(0x80 | ((code >> 6) & 0x3F)));
        putchar((int)(0x80 | (code & 0x3F)));
    }
}

static int hex_value(char c) {
    return isdigit((unsigned char)c) ? c - '0' : tolower((unsigned char)c) - 'a' + 10;
}

static int echo_escaped(const char *text) {
    static const char SIMPLE[] = "a\ab\be\x1b" "E\x1b" "f\fn\nr\rt\tv\v\\\\";
    for (const char *p = text; *p; p++) {
        if (*p != '\\' || !p[1]) {
            putchar(*p);
            continue;
        }
        char kind = *++p;
        const char *simple = strchr(SIMPLE, kind);
        int digits = kind == 'x' ? 2 : kind == 'u' ? 4 : kind == 'U' ? 8 : kind == '0' ? 3 : 0;
        if (kind == 'c') return 1;
        if (simple && (simple - SIMPLE) % 2 == 0) {
            putchar(simple[1]);
        } else if (digits && (kind == '0' || isxdigit((unsigned char)p[1]))) {
            unsigned long value = 0;
            int base = kind == '0' ? 8 : 16;
            for (int i = 0; i < digits && (base == 8 ? p[1] >= '0' && p[1] <= '7'
                                                    : isxdigit((unsigned char)p[1]));
                 i++)
                value = value * (unsigned long)base + (unsigned long)hex_value(*++p);
            if (kind == 'u' || kind == 'U') put_utf8(value);
            else putchar((int)(value & 0xFF));
        } else {
            putchar('\\');
            putchar(kind);
        }
    }
    return 0;
}

static int builtin_echo(int argc, char **argv) {
    int newline = 1;
    int escapes = 0;
    int index = 1;

    while (index < argc && argv[index][0] == '-' && argv[index][1] &&
           strspn(argv[index] + 1, "neE") == strlen(argv[index] + 1)) {
        for (const char *flag = argv[index] + 1; *flag; flag++) {
            if (*flag == 'n') newline = 0;
            else if (*flag == 'e') escapes = 1;
            else escapes = 0;
        }
        index++;
    }

    for (int i = index; i < argc; i++) {
        if (i > index) putchar(' ');
        if (!escapes) fputs(argv[i], stdout);
        else if (echo_escaped(argv[i])) return 0;
    }
    if (newline) putchar('\n');
    return 0;
}

static void print_declarations(StrList *names) {
    sl_sort(names);
    StrBuf line;
    sb_init(&line);
    for (size_t i = 0; i < names->len; i++) {
        sb_clear(&line);
        if (var_describe(names->items[i], &line)) printf("%s\n", line.data);
    }
    sb_free(&line);
}

static int assign_word(const char *builtin, char *word);

static char *word_name(const char *word) {
    const char *eq = strchr(word, '=');
    if (!eq) return xstrdup(word);
    return xstrndup(word, (size_t)(eq - word) - (eq > word && eq[-1] == '+'));
}

static int builtin_export(int argc, char **argv) {
    Opts o = {1, 0, 0, NULL};
    int c;
    int unexport = 0;
    int functions = 0;
    while ((c = opt_next(&o, argc, argv, "export", "fnp")) > 0) {
        if (c == '?') return 2;
        if (c == 'n') unexport = 1;
        if (c == 'f') functions = 1;
    }
    if (o.index >= argc) {
        StrList names;
        sl_init(&names);
        vars_exported_names(&names);
        print_declarations(&names);
        sl_free(&names);
        return 0;
    }

    int status = 0;
    for (int i = o.index; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        char *name = word_name(argv[i]);
        if (functions) {
            if (!function_defined(name)) {
                shell_error("export: %s: not a function", name);
                status = 1;
            }
        } else if (!var_valid_name(name, 0)) {
            status = invalid_name("export", argv[i]);
        } else if (unexport) {
            if (eq) assign_word("export", argv[i]);
            var_unexport(name);
        } else {
            if (eq && assign_word("export", argv[i])) status = 1;
            var_export(name);
            if (strcmp(name, "PATH") == 0) path_rehash();
        }
        free(name);
    }
    return status;
}

static int builtin_unset(int argc, char **argv) {
    Opts o = {1, 0, 0, NULL};
    int c;
    int functions_only = 0;
    int variables_only = 0;
    while ((c = opt_next(&o, argc, argv, "unset", "fvn")) > 0) {
        if (c == '?') return 2;
        if (c == 'f') functions_only = 1;
        if (c == 'v') variables_only = 1;
    }

    int status = 0;
    for (int i = o.index; i < argc; i++) {
        const char *name = argv[i];
        if (functions_only) {
            function_undefine(name);
            continue;
        }
        if (!var_valid_name(name, 1)) {
            if (variables_only) status = invalid_name("unset", name);
            else function_undefine(name);
            continue;
        }

        char *open = strchr(name, '[');
        char *base = open ? xstrndup(name, (size_t)(open - name)) : xstrdup(name);
        if (var_is_readonly(base)) {
            shell_error("unset: %s: cannot unset: readonly variable", base);
            status = 1;
        } else if (open) {
            char *key = xstrndup(open + 1, strlen(open + 1) - 1);
            char *resolved = expand_single(key);
            if (strcmp(resolved, "@") == 0 || strcmp(resolved, "*") == 0) var_unset(base);
            else var_unset_element(base, resolved);
            free(resolved);
            free(key);
        } else if (variables_only || var_exists(base) || !function_undefine(base)) {
            var_unset(base);
        }
        free(base);
    }
    return status;
}

typedef struct {
    const char *name;
    char letter;
    int *slot;
    int state;
} SetOption;

static SetOption SET_OPTIONS[] = {
    {"allexport", 'a', NULL, 0},
    {"braceexpand", 'B', NULL, 1},
    {"emacs", 0, NULL, 0},
    {"errexit", 'e', &shell.errexit, 0},
    {"errtrace", 'E', NULL, 0},
    {"functrace", 'T', NULL, 0},
    {"hashall", 'h', NULL, 1},
    {"histexpand", 'H', NULL, 0},
    {"history", 0, NULL, 0},
    {"ignoreeof", 0, NULL, 0},
    {"interactive-comments", 0, NULL, 1},
    {"keyword", 'k', NULL, 0},
    {"monitor", 'm', NULL, 0},
    {"noclobber", 'C', NULL, 0},
    {"noexec", 'n', NULL, 0},
    {"noglob", 'f', &shell.noglob, 0},
    {"nolog", 0, NULL, 0},
    {"notify", 'b', NULL, 0},
    {"nounset", 'u', &shell.nounset, 0},
    {"onecmd", 't', NULL, 0},
    {"physical", 'P', NULL, 0},
    {"pipefail", 0, &shell.pipefail, 0},
    {"posix", 0, NULL, 0},
    {"privileged", 'p', NULL, 0},
    {"verbose", 'v', NULL, 0},
    {"vi", 0, NULL, 0},
    {"xtrace", 'x', &shell.xtrace, 0},
};

#define SET_OPTION_COUNT (sizeof(SET_OPTIONS) / sizeof(SET_OPTIONS[0]))

static int *set_option_slot(SetOption *option) {
    return option->slot ? option->slot : &option->state;
}

static SetOption *set_option_named(const char *name) {
    for (size_t i = 0; i < SET_OPTION_COUNT; i++)
        if (strcmp(SET_OPTIONS[i].name, name) == 0) return &SET_OPTIONS[i];
    return NULL;
}

static SetOption *set_option_letter(char letter) {
    for (size_t i = 0; i < SET_OPTION_COUNT; i++)
        if (SET_OPTIONS[i].letter == letter) return &SET_OPTIONS[i];
    return NULL;
}

void shell_option_flags(StrBuf *out) {
    for (const char *letter = "abefhkmnptuvxBCEHPT"; *letter; letter++) {
        SetOption *option = set_option_letter(*letter);
        if (option && *set_option_slot(option)) sb_putc(out, *letter);
    }
    if (shell.interactive) sb_putc(out, 'i');
}

static void print_set_options(int as_commands) {
    for (size_t i = 0; i < SET_OPTION_COUNT; i++) {
        SetOption *option = &SET_OPTIONS[i];
        int on = *set_option_slot(option);
        if (as_commands) printf("set %co %s\n", on ? '-' : '+', option->name);
        else printf("%-15s\t%s\n", option->name, on ? "on" : "off");
    }
}

static int builtin_set(int argc, char **argv) {
    if (argc < 2) {
        StrList list;
        sl_init(&list);
        vars_list(&list);
        for (size_t i = 0; i < list.len; i++) printf("%s\n", list.items[i]);
        sl_free(&list);
        return 0;
    }

    int i = 1;
    int replace = 0;
    for (; i < argc; i++) {
        char sign = argv[i][0];
        if ((sign != '-' && sign != '+') || (sign == '+' && !argv[i][1])) break;
        if (strcmp(argv[i], "--") == 0 || strcmp(argv[i], "-") == 0) {
            if (argv[i][1] == '\0') {
                shell.xtrace = 0;
                set_option_named("verbose")->state = 0;
            }
            replace = argv[i][1] == '-';
            i++;
            break;
        }
        for (const char *flag = argv[i] + 1; *flag; flag++) {
            if (*flag != 'o') {
                SetOption *option = set_option_letter(*flag);
                if (!option) {
                    shell_error("set: %c%c: invalid option", sign, *flag);
                    return 2;
                }
                *set_option_slot(option) = sign == '-';
                continue;
            }
            if (i + 1 >= argc || argv[i + 1][0] == '-' || argv[i + 1][0] == '+') {
                print_set_options(sign == '+');
                continue;
            }
            SetOption *option = set_option_named(argv[++i]);
            if (!option) {
                shell_error("set: %s: invalid option name", argv[i]);
                return 2;
            }
            *set_option_slot(option) = sign == '-';
            break;
        }
    }

    if (replace || i < argc) {
        char *name = shell.params.len > 0 ? xstrdup(shell.params.items[0]) : xstrdup("FreSH");
        sl_free(&shell.params);
        sl_init(&shell.params);
        sl_push(&shell.params, name);
        for (; i < argc; i++) sl_push_copy(&shell.params, argv[i]);
    }
    return 0;
}

static char *next_array_word(const char **cursor) {
    const char *p = *cursor;
    while (*p == ' ') p++;
    if (!*p) return NULL;
    const char *start = p;
    int depth = 0;
    char quote = 0;
    for (; *p && (quote || depth || *p != ' '); p++) {
        if (*p == '\\' && quote != '\'' && p[1]) p++;
        else if (quote && *p == quote) quote = 0;
        else if (!quote && (*p == '\'' || *p == '"')) quote = *p;
        else if (!quote && (*p == '(' || *p == '{')) depth++;
        else if (!quote && (*p == ')' || *p == '}') && depth) depth--;
    }
    *cursor = p;
    return xstrndup(start, (size_t)(p - start));
}

static void assign_array_text(const char *name, const char *inside, int append) {
    StrList raw;
    StrList values;
    sl_init(&raw);
    sl_init(&values);
    const char *cursor = inside;
    char *word;
    while ((word = next_array_word(&cursor)) != NULL) sl_push(&raw, word);
    expand_words(&raw, &values);

    VarKind kind = var_kind(name) == VAR_ASSOC ? VAR_ASSOC : VAR_INDEXED;
    if (!append) {
        StrList empty;
        sl_init(&empty);
        var_set_array(name, &empty, kind);
        sl_free(&empty);
    }
    long next = append ? var_count(name) : 0;
    for (size_t i = 0; i < values.len; i++) {
        const char *item = values.items[i];
        const char *close = item[0] == '[' ? strstr(item, "]=") : NULL;
        char index[32];
        if (kind == VAR_ASSOC && !close) {
            var_set_element(name, item, i + 1 < values.len ? values.items[++i] : "");
            continue;
        }
        if (close) {
            char *key = xstrndup(item + 1, (size_t)(close - item - 1));
            var_set_element(name, key, close + 2);
            if (kind == VAR_INDEXED) next = atol(key) + 1;
            free(key);
            continue;
        }
        snprintf(index, sizeof(index), "%ld", next++);
        var_set_element(name, index, item);
    }
    sl_free(&raw);
    sl_free(&values);
}

static int assign_word(const char *builtin, char *word) {
    char *eq = strchr(word, '=');
    if (!eq) {
        if (!var_valid_name(word, 0)) return invalid_name(builtin, word);
        if (!var_exists(word) && var_kind(word) == VAR_SCALAR) var_declare(word, VAR_SCALAR);
        return 0;
    }
    int append = eq > word && eq[-1] == '+';
    char *name = xstrndup(word, (size_t)(eq - word - append));
    int status = 0;
    if (!var_valid_name(name, 1)) {
        status = invalid_name(builtin, word);
    } else if (var_is_readonly(name)) {
        shell_error("%s: %s: readonly variable", builtin, name);
        status = 1;
    } else if (eq[1] == '(' && eq[strlen(eq) - 1] == ')' && !strchr(name, '[')) {
        char *inside = xstrndup(eq + 2, strlen(eq + 2) - 1);
        assign_array_text(name, inside, append);
        free(inside);
    } else if (var_is_integer(name)) {
        int ok = 1;
        long number = eval_arith(eq + 1, &ok);
        if (append) number += eval_arith(var_get(name) ? var_get(name) : "0", &ok);
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%ld", number);
        status = ok ? var_assign(name, buffer) : 1;
    } else if (append && !strchr(name, '[')) {
        var_append(name, eq + 1);
    } else {
        status = var_assign(name, eq + 1);
    }
    free(name);
    return status;
}

static int builtin_let(int argc, char **argv) {
    if (argc < 2) {
        shell_error("let: expression expected");
        return 1;
    }
    long value = 0;
    int ok = 1;
    for (int i = 1; i < argc; i++) value = eval_arith(argv[i], &ok);
    if (!ok) return 1;
    return value != 0 ? 0 : 1;
}

static void print_function(const char *name) {
    StrBuf text;
    sb_init(&text);
    if (function_source(name, &text)) printf("%s\n", text.data);
    sb_free(&text);
}

static void print_functions(int argc, char **argv, int index, int names_only) {
    StrList names;
    sl_init(&names);
    if (index >= argc) function_names(&names);
    else for (int i = index; i < argc; i++) if (function_defined(argv[i])) sl_push_copy(&names, argv[i]);
    sl_sort(&names);
    for (size_t i = 0; i < names.len; i++) {
        if (names_only && index >= argc) printf("declare -f %s\n", names.items[i]);
        else if (names_only) printf("%s\n", names.items[i]);
        else print_function(names.items[i]);
    }
    sl_free(&names);
}

static int declare_words(int argc, char **argv, int forced_local) {
    const char *builtin = argv[0];
    if (forced_local && shell.returnable <= 0) {
        shell_error("local: can only be used in a function");
        return 1;
    }

    char on[16] = "";
    char off[16] = "";
    int printing = 0;
    int functions = 0;
    int names_only = 0;
    int global = 0;
    Opts o = {1, 0, 0, NULL};
    int c;
    while ((c = opt_next(&o, argc, argv, builtin, "+aAfFgilnprtux")) > 0) {
        if (c == '?') return 2;
        if (c == 'p') printing = 1;
        else if (c == 'f') functions = 1;
        else if (c == 'F') functions = names_only = 1;
        else if (c == 'g') global = 1;
        else if (!strchr(o.plus ? off : on, c)) {
            char *flags = o.plus ? off : on;
            size_t used = strlen(flags);
            flags[used] = (char)c;
            flags[used + 1] = '\0';
        }
    }

    if (functions) {
        int status = 0;
        for (int i = o.index; i < argc; i++)
            if (!function_defined(argv[i])) status = 1;
        print_functions(argc, argv, o.index, names_only);
        return status;
    }

    if (o.index >= argc) {
        StrList names;
        sl_init(&names);
        if (printing || on[0]) {
            StrList list;
            sl_init(&list);
            vars_list(&list);
            for (size_t i = 0; i < list.len; i++) {
                list.items[i][strcspn(list.items[i], "=")] = '\0';
                StrBuf line;
                sb_init(&line);
                var_describe(list.items[i], &line);
                size_t flags = strcspn(line.data + 8, " ");
                int wanted = 1;
                for (const char *flag = on; *flag; flag++)
                    if (!memchr(line.data + 8, *flag, flags)) wanted = 0;
                if (wanted) sl_push_copy(&names, list.items[i]);
                sb_free(&line);
            }
            sl_free(&list);
            print_declarations(&names);
        } else {
            vars_list(&names);
            for (size_t i = 0; i < names.len; i++) printf("%s\n", names.items[i]);
        }
        sl_free(&names);
        return 0;
    }

    int local = forced_local || (shell.returnable > 0 && !global);
    int status = 0;
    for (int i = o.index; i < argc; i++) {
        char *name = word_name(argv[i]);

        if (printing) {
            StrBuf line;
            sb_init(&line);
            if (var_describe(name, &line)) {
                printf("%s\n", line.data);
            } else {
                shell_error("%s: %s: not found", builtin, name);
                status = 1;
            }
            sb_free(&line);
        } else if (!var_valid_name(name, 0)) {
            status = invalid_name(builtin, argv[i]);
        } else if (strchr(off, 'r') && var_is_readonly(name)) {
            shell_error("%s: %s: readonly variable", builtin, name);
            status = 1;
        } else {
            if (local) var_make_local(name);
            if (strchr(on, 'A')) var_declare(name, VAR_ASSOC);
            else if (strchr(on, 'a') && var_kind(name) != VAR_INDEXED) var_declare(name, VAR_INDEXED);
            for (const char *flag = "ilnu"; *flag; flag++) {
                if (strchr(on, *flag)) var_attribute(name, *flag, 1);
                if (strchr(off, *flag)) var_attribute(name, *flag, 0);
            }
            if (assign_word(builtin, argv[i])) status = 1;
            if (strchr(on, 'x')) var_export(name);
            if (strchr(off, 'x')) var_unexport(name);
            if (strchr(on, 'r')) var_mark_readonly(name);
        }
        free(name);
    }
    return status;
}

static int builtin_local(int argc, char **argv) {
    return declare_words(argc, argv, 1);
}

static int builtin_declare(int argc, char **argv) {
    return declare_words(argc, argv, 0);
}

typedef struct {
    const char *name;
    const char *number;
    char **slot;
} TrapSlot;

static int trap_table(TrapSlot *slots) {
    int count = 0;
    slots[count++] = (TrapSlot){"EXIT", "0", &shell.trap_exit};
    slots[count++] = (TrapSlot){"HUP", "1", &shell.trap_hup};
    slots[count++] = (TrapSlot){"INT", "2", &shell.trap_int};
    slots[count++] = (TrapSlot){"QUIT", "3", &shell.trap_quit};
    slots[count++] = (TrapSlot){"TERM", "15", &shell.trap_term};
    slots[count++] = (TrapSlot){"ERR", NULL, &shell.trap_err};
    slots[count++] = (TrapSlot){"DEBUG", NULL, &shell.trap_debug};
    slots[count++] = (TrapSlot){"RETURN", NULL, &shell.trap_return};
    return count;
}

static TrapSlot *trap_find(TrapSlot *slots, int count, const char *given) {
    if (_strnicmp(given, "SIG", 3) == 0) given += 3;
    for (int s = 0; s < count; s++) {
        if (str_ieq(given, slots[s].name) || (slots[s].number && strcmp(given, slots[s].number) == 0))
            return &slots[s];
    }
    return NULL;
}

static void trap_print(const TrapSlot *slot) {
    if (!*slot->slot) return;
    StrBuf line;
    sb_init(&line);
    sb_puts(&line, "trap -- ");
    sb_put_quoted(&line, *slot->slot);
    printf("%s %s%s\n", line.data, slot->number && strcmp(slot->number, "0") ? "SIG" : "",
           slot->name);
    sb_free(&line);
}

static int builtin_trap(int argc, char **argv) {
    TrapSlot slots[8];
    int count = trap_table(slots);
    Opts o = {1, 0, 0, NULL};
    int c;
    int printing = 0;
    int listing = 0;
    while ((c = opt_next(&o, argc, argv, "trap", "lp")) > 0) {
        if (c == '?') return 2;
        if (c == 'p') printing = 1;
        if (c == 'l') listing = 1;
    }

    if (listing) {
        for (int i = 1; i < 5; i++) printf("%2s) SIG%s\n", slots[i].number, slots[i].name);
        return 0;
    }
    if (printing || o.index >= argc) {
        int status = 0;
        for (int i = o.index; i < argc; i++) {
            TrapSlot *slot = trap_find(slots, count, argv[i]);
            if (slot) trap_print(slot);
            else status = 1;
        }
        for (int i = 0; o.index >= argc && i < count; i++) trap_print(&slots[i]);
        return status;
    }

    int first = o.index;
    const char *command = argv[first];
    int clearing = strcmp(command, "-") == 0;
    if (argc - first == 1 && trap_find(slots, count, command)) {
        clearing = 1;
        first--;
    } else if (argc - first == 1) {
        shell_error("trap: usage: trap [-lp] [[action] signal_spec ...]");
        return 2;
    }

    int status = 0;
    for (int i = first + 1; i < argc; i++) {
        TrapSlot *slot = trap_find(slots, count, argv[i]);
        if (!slot) {
            shell_error("trap: %s: invalid signal specification", argv[i]);
            status = 1;
            continue;
        }
        free(*slot->slot);
        *slot->slot = clearing ? NULL : xstrdup(command);
    }
    return status;
}

static int job_options(int argc, char **argv, const char *allowed, char *seen, const char **value) {
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        if (strcmp(argv[i], "--") == 0) return i + 1;
        for (const char *flag = argv[i] + 1; *flag; flag++) {
            const char *known = *flag == ':' ? NULL : strchr(allowed, *flag);
            if (!known) {
                shell_error("%s: -%c: invalid option", argv[0], *flag);
                return -1;
            }
            seen[(unsigned char)*flag] = 1;
            if (known[1] != ':') continue;
            *value = flag[1] ? flag + 1 : i + 1 < argc ? argv[++i] : NULL;
            if (!*value) {
                shell_error("%s: -%c: option requires an argument", argv[0], *flag);
                return -1;
            }
            break;
        }
    }
    return i;
}

static int pid_word(const char *word, unsigned long *pid) {
    char *end;
    if (!isdigit((unsigned char)word[0])) return 0;
    *pid = strtoul(word, &end, 10);
    return *end == '\0';
}

static int each_job(int argc, char **argv, int (*act)(int)) {
    int status = 0;
    for (int i = 1; i < argc || i == 1; i++) {
        int slot = jobs_lookup(argv[0], i < argc ? argv[i] : NULL);
        if (slot < 0 || act(slot) != 0) status = 1;
    }
    return status;
}

static int builtin_fg(int argc, char **argv) {
    int slot = jobs_lookup("fg", argc > 1 ? argv[1] : NULL);
    return slot < 0 ? 1 : jobs_foreground(slot);
}

static int builtin_bg(int argc, char **argv) {
    return each_job(argc, argv, jobs_resume);
}

static int builtin_stop(int argc, char **argv) {
    return each_job(argc, argv, jobs_suspend);
}

static int builtin_jobs(int argc, char **argv) {
    char seen[128] = {0};
    int i = job_options(argc, argv, "lprs", seen, NULL);
    if (i < 0) {
        fprintf(stderr, "jobs: usage: jobs [-lprs] [jobspec ...]\n");
        return 2;
    }
    int format = seen['p'] ? 'p' : seen['l'] ? 'l' : 0;
    int state = seen['r'] ? 'r' : seen['s'] ? 's' : 0;
    int status = 0;
    if (i == argc)
        for (int slot = jobs_next(-1, state); slot >= 0; slot = jobs_next(slot, state))
            jobs_report(slot, format);
    for (; i < argc; i++) {
        int slot = jobs_lookup("jobs", argv[i]);
        if (slot < 0) status = 1;
        else jobs_report(slot, format);
    }
    jobs_sweep();
    return status;
}

static int wait_for_any(int argc, char **argv, unsigned long *pid) {
    int *slots = xmalloc((size_t)(argc + 1) * sizeof(int));
    int count = 0;
    for (int i = 0; i < argc; i++) {
        unsigned long candidate;
        int numeric = pid_word(argv[i], &candidate);
        int slot = numeric ? jobs_find_pid(candidate) : jobs_lookup("wait", argv[i]);
        if (slot >= 0) slots[count++] = slot;
        else if (numeric) shell_error("wait: %s: no such job", argv[i]);
    }
    int status = argc > 0 && count == 0 ? -1 : jobs_wait_any(slots, count, pid);
    free(slots);
    return status < 0 ? 127 : status;
}

static int builtin_wait(int argc, char **argv) {
    char seen[128] = {0};
    const char *variable = NULL;
    int i = job_options(argc, argv, "fnp:", seen, &variable);
    if (i < 0) {
        fprintf(stderr, "wait: usage: wait [-fn] [-p var] [id ...]\n");
        return 2;
    }
    if (variable) var_unset(variable);

    unsigned long pid = 0;
    int status = 0;
    if (seen['n']) status = wait_for_any(argc - i, argv + i, &pid);
    else if (i == argc) jobs_wait_all();
    for (; !seen['n'] && i < argc; i++) {
        unsigned long candidate;
        int slot = argv[i][0] == '%' ? jobs_lookup("wait", argv[i]) : -1;
        if (pid_word(argv[i], &candidate)) {
            status = jobs_wait_pid(candidate);
            pid = status == 127 ? pid : candidate;
        } else if (slot >= 0) {
            pid = (unsigned long)jobs_pid(slot);
            status = jobs_wait(slot);
        } else if (argv[i][0] == '%') {
            status = 127;
        } else {
            shell_error("wait: `%s': not a pid or valid job spec", argv[i]);
            status = 1;
        }
    }
    if (variable && pid) {
        char text[32];
        snprintf(text, sizeof(text), "%lu", pid);
        var_set(variable, text);
    }
    return status;
}

static int builtin_disown(int argc, char **argv) {
    char seen[128] = {0};
    int i = job_options(argc, argv, "ahr", seen, NULL);
    if (i < 0) {
        fprintf(stderr, "disown: usage: disown [-h] [-ar] [jobspec ... | pid ...]\n");
        return 2;
    }
    if (i == argc && (seen['a'] || seen['r'])) {
        int state = seen['r'] ? 'r' : 0;
        for (int slot = jobs_next(-1, state); slot >= 0 && !seen['h']; slot = jobs_next(slot, state))
            jobs_disown(slot);
        return 0;
    }
    int status = 0;
    for (int first = i; i < argc || i == first; i++) {
        unsigned long pid;
        const char *word = i < argc ? argv[i] : NULL;
        int numeric = word && pid_word(word, &pid);
        int slot = numeric ? jobs_find_pid(pid) : jobs_lookup("disown", word);
        if (slot < 0 && numeric) shell_error("disown: %s: no such job", word);
        if (slot < 0) status = 1;
        else if (!seen['h']) jobs_disown(slot);
    }
    return status;
}

static int builtin_die(int argc, char **argv) {
    fflush(stdout);
    fprintf(stderr, "%s", style(S_ERROR));
    for (int i = 1; i < argc; i++) fprintf(stderr, "%s%s", i > 1 ? " " : "", argv[i]);
    if (argc < 2) fprintf(stderr, "aborted");
    fprintf(stderr, "%s\n", style(S_RESET));
    fflush(stderr);

    shell.running = 0;
    return 1;
}

static int builtin_have(int argc, char **argv) {
    if (argc < 2) return 1;
    for (int i = 1; i < argc; i++) {
        if (builtin_lookup(argv[i]) || coreutil_lookup(argv[i]) || function_defined(argv[i]) ||
            alias_get(argv[i]))
            continue;
        char path[PATH_BUF];
        if (!resolve_command(argv[i], path, sizeof(path))) return 1;
    }
    return 0;
}

static int print_styled(int argc, char **argv, const char *marker, const char *colour) {
    printf("%s%s%s ", style(colour), marker, style(S_RESET));
    for (int i = 1; i < argc; i++) printf("%s%s", i > 1 ? " " : "", argv[i]);
    printf("\n");
    return 0;
}

static int builtin_say(int argc, char **argv) {
    return print_styled(argc, argv, "\xe2\x80\xa2", S_LABEL);
}

static int builtin_ok(int argc, char **argv) {
    return print_styled(argc, argv, "\xe2\x9c\x93", S_ACCENT);
}

static int builtin_warn(int argc, char **argv) {
    return print_styled(argc, argv, "!", S_WARN);
}

static int builtin_alias(int argc, char **argv) {
    Opts o = {1, 0, 0, NULL};
    int c;
    while ((c = opt_next(&o, argc, argv, "alias", "p")) > 0)
        if (c == '?') return 2;
    if (o.index >= argc) {
        StrList list;
        sl_init(&list);
        alias_list(&list);
        for (size_t i = 0; i < list.len; i++) printf("alias %s\n", list.items[i]);
        sl_free(&list);
        return 0;
    }
    int status = 0;
    for (int i = o.index; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = '\0';
            alias_set(argv[i], eq + 1);
            *eq = '=';
            continue;
        }
        const char *value = alias_get(argv[i]);
        if (!value) {
            shell_error("alias: %s: not found", argv[i]);
            status = 1;
            continue;
        }
        StrBuf line;
        sb_init(&line);
        sb_put_quoted(&line, value);
        printf("alias %s=%s\n", argv[i], line.data);
        sb_free(&line);
    }
    return status;
}

static int builtin_unalias(int argc, char **argv) {
    Opts o = {1, 0, 0, NULL};
    int c;
    while ((c = opt_next(&o, argc, argv, "unalias", "a")) > 0) {
        if (c == '?') return 2;
        alias_clear();
        return 0;
    }
    if (o.index >= argc) {
        shell_error("unalias: usage: unalias [-a] name [name ...]");
        return 2;
    }
    int status = 0;
    for (int i = o.index; i < argc; i++) {
        if (!alias_unset(argv[i])) {
            shell_error("unalias: %s: not found", argv[i]);
            status = 1;
        }
    }
    return status;
}

static int builtin_source(int argc, char **argv) {
    int first = options_only(argc, argv, argv[0]);
    if (first < 0) return 2;
    if (first >= argc) {
        shell_error("%s: filename argument required", argv[0]);
        return 2;
    }
    const char *file = argv[first];
    char path[PATH_BUF];
    if (path_is_file(file)) snprintf(path, sizeof(path), "%s", file);
    else if (strpbrk(file, "/\\") || !resolve_command(file, path, sizeof(path))) {
        shell_error("%s: %s: No such file or directory", argv[0], file);
        return 1;
    }

    StrList args;
    sl_init(&args);
    for (int i = first + 1; i < argc; i++) sl_push_copy(&args, argv[i]);
    shell.returnable++;
    int status = exec_script_file(path, argc > first + 1 ? &args : NULL);
    shell.returnable--;
    sl_free(&args);
    return status;
}

static int using_resolve(const char *file, char *path, size_t size) {
    const char *script = var_get_element("BASH_SOURCE", "0");
    const char *leaf = script ? path_last_sep(script) : NULL;
    if (leaf && !path_is_absolute(file)) {
        snprintf(path, size, "%.*s%c%s", (int)(leaf - script), script, PATH_SEP, file);
        if (path_is_file(path)) return 1;
    }
    snprintf(path, size, "%s", file);
    return path_is_file(path) || resolve_command(file, path, size);
}

static int builtin_using(int argc, char **argv) {
    StrList names;
    sl_init(&names);
    int from = 1;
    for (; from < argc && strcmp(argv[from], "from") != 0; from++) {
        char *copy = xstrdup(argv[from]);
        char *cursor = copy;
        char *name;
        while ((name = str_next_field(&cursor, ',')) != NULL) {
            if (*name) sl_push_copy(&names, name);
        }
        free(copy);
    }
    if (names.len == 0 || from + 2 != argc) {
        shell_error("using: usage: using <function> [<function> ...] from <file>");
        sl_free(&names);
        return 2;
    }

    char path[PATH_BUF];
    int status;
    if (using_resolve(argv[from + 1], path, sizeof(path))) {
        status = exec_import(path, &names);
    } else {
        shell_error("using: %s: not found", argv[from + 1]);
        status = 1;
    }
    sl_free(&names);
    return status;
}

static int man_system(const char *name) {
    char path[PATH_BUF];
    if (!resolve_command("man", path, sizeof(path))) return -1;
    StrBuf command;
    sb_init(&command);
    sb_put_quoted(&command, path);
    sb_putc(&command, ' ');
    sb_put_quoted(&command, name);
    int status = exec_text(command.data);
    sb_free(&command);
    return status;
}

static int man_one(const char *name) {
    char path[PATH_BUF];
    int bundled = coreutil_lookup(name) && (coreutil_preferred(name) || !resolve_command(name, path, sizeof(path)));
    if ((builtin_lookup(name) || bundled) && help_page(name)) return 0;

    int status = man_system(name);
    if (status >= 0) return status;
    if (help_page(name)) return 0;
    shell_error("No manual entry for %s", name);
    return 1;
}

static int builtin_man(int argc, char **argv) {
    if (argc < 2) {
        shell_error("What manual page do you want?");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        int one = man_one(argv[i]);
        if (one != 0) status = one;
    }
    return status;
}

static int builtin_history(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "-c") == 0) {
        history_clear();
        return 0;
    }
    int count = history_count();
    int limit = argc > 1 ? atoi(argv[1]) : count;
    if (limit <= 0 || limit > count) limit = count;
    for (int i = count - limit; i < count; i++)
        printf("  %s%4d%s  %s\n", style(S_DIM), i + 1, style(S_RESET), history_get(i));
    return 0;
}

static int shell_keyword(const char *name) {
    return keyword_known(name) || strcmp(name, "!") == 0 || strcmp(name, "time") == 0;
}

static const char *command_kind(const char *name, char *path, size_t size) {
    if (alias_get(name)) return "alias";
    if (shell_keyword(name)) return "keyword";
    if (function_defined(name)) return "function";
    if (builtin_lookup(name) || coreutil_preferred(name)) return "builtin";
    if (resolve_command(name, path, size)) {
        path_to_slashes(path);
        return "file";
    }
    if (coreutil_lookup(name)) return "builtin";
    return NULL;
}

static int describe_command(const char *name, char style) {
    char path[PATH_BUF];
    const char *kind = command_kind(name, path, sizeof(path));
    if (!kind) return 0;
    int file = strcmp(kind, "file") == 0;

    if (style == 't') {
        printf("%s\n", kind);
    } else if (style == 'v') {
        if (*kind == 'a') {
            StrBuf quoted;
            sb_init(&quoted);
            sb_put_quoted(&quoted, alias_get(name));
            printf("alias %s=%s\n", name, quoted.data);
            sb_free(&quoted);
        } else {
            printf("%s\n", file ? path : name);
        }
    } else if (style == 'p') {
        if (file) printf("%s\n", path);
    } else if (*kind == 'a') {
        printf("%s is aliased to `%s'\n", name, alias_get(name));
    } else if (*kind == 'k') {
        printf("%s is a shell keyword\n", name);
    } else if (strcmp(kind, "function") == 0) {
        printf("%s is a function\n", name);
        print_function(name);
    } else if (*kind == 'b') {
        printf("%s is a shell builtin\n", name);
    } else {
        printf("%s is %s\n", name, path);
    }
    return 1;
}

static int builtin_which(int argc, char **argv) {
    if (argc < 2) {
        shell_error("which: command required");
        return 1;
    }
    int status = 0;
    for (int i = 1; i < argc; i++) {
        char path[PATH_BUF];
        const char *kind = command_kind(argv[i], path, sizeof(path));
        if (!kind) {
            shell_error("%s: not found", argv[i]);
            status = 1;
        } else if (strcmp(kind, "file") == 0) {
            printf("%s\n", path);
        } else if (*kind == 'a') {
            printf("%s: aliased to %s\n", argv[i], alias_get(argv[i]));
        } else if (*kind == 'b' && !builtin_lookup(argv[i])) {
            printf("%s is a command bundled with FreSH\n", argv[i]);
        } else {
            printf("%s is a shell %s\n", argv[i], kind);
        }
    }
    return status;
}

static int builtin_type(int argc, char **argv) {
    Opts o = {1, 0, 0, NULL};
    int c;
    char style = 'V';
    int forced = 0;
    while ((c = opt_next(&o, argc, argv, "type", "afptP")) > 0) {
        if (c == '?') return 2;
        if (c == 't' || c == 'p') style = (char)c;
        if (c == 'P') forced = 1;
    }

    int status = 0;
    for (int i = o.index; i < argc; i++) {
        char path[PATH_BUF];
        if (forced) {
            if (resolve_command(argv[i], path, sizeof(path))) {
                path_to_slashes(path);
                printf("%s\n", path);
            } else {
                status = 1;
            }
            continue;
        }
        if (describe_command(argv[i], style)) continue;
        if (style == 'V') shell_error("type: %s: not found", argv[i]);
        status = 1;
    }
    return status;
}

static int run_words(int argc, char **argv, int bypass) {
    StrBuf line;
    sb_init(&line);
    for (int i = 0; i < argc; i++) {
        if (i > 0) sb_putc(&line, ' ');
        sb_put_quoted(&line, argv[i]);
    }
    int status = bypass ? exec_bypassing_functions(line.data) : exec_text(line.data);
    sb_free(&line);
    return status;
}

static int builtin_command(int argc, char **argv) {
    Opts o = {1, 0, 0, NULL};
    int c;
    char style = 0;
    while ((c = opt_next(&o, argc, argv, "command", "pvV")) > 0) {
        if (c == '?') return 2;
        if (c == 'v' || c == 'V') style = (char)c;
    }
    if (o.index >= argc) return 0;

    if (style) {
        int status = 0;
        for (int i = o.index; i < argc; i++) {
            if (describe_command(argv[i], style == 'v' ? 'v' : 'V')) continue;
            if (style == 'V') shell_error("command: %s: not found", argv[i]);
            status = 1;
        }
        return status;
    }
    return run_words(argc - o.index, argv + o.index, 1);
}

static int builtin_builtin(int argc, char **argv) {
    int first = options_only(argc, argv, "builtin");
    if (first < 0) return 2;
    if (first >= argc) return 0;
    BuiltinFn fn = builtin_lookup(argv[first]);
    if (!fn) fn = coreutil_lookup(argv[first]);
    if (!fn) {
        shell_error("builtin: %s: not a shell builtin", argv[first]);
        return 1;
    }
    return fn(argc - first, argv + first);
}

static int builtin_exec(int argc, char **argv) {
    Opts o = {1, 0, 0, NULL};
    int c;
    while ((c = opt_next(&o, argc, argv, "exec", "a:cl")) > 0)
        if (c == '?') return 2;
    if (o.index >= argc) {
        exec_keep_redirections();
        return 0;
    }

    int status = run_words(argc - o.index, argv + o.index, 1);
    shell.running = 0;
    shell.last_status = status;
    return status;
}

static int builtin_readonly(int argc, char **argv) {
    Opts o = {1, 0, 0, NULL};
    int c;
    VarKind kind = VAR_SCALAR;
    while ((c = opt_next(&o, argc, argv, "readonly", "aAfp")) > 0) {
        if (c == '?') return 2;
        if (c == 'a') kind = VAR_INDEXED;
        if (c == 'A') kind = VAR_ASSOC;
    }
    if (o.index >= argc) {
        StrList names;
        sl_init(&names);
        vars_readonly_names(&names);
        print_declarations(&names);
        sl_free(&names);
        return 0;
    }

    int status = 0;
    for (int i = o.index; i < argc; i++) {
        char *name = word_name(argv[i]);
        if (!var_valid_name(name, 0)) {
            status = invalid_name("readonly", argv[i]);
        } else {
            if (kind != VAR_SCALAR && var_kind(name) != kind) var_declare(name, kind);
            if (assign_word("readonly", argv[i])) status = 1;
            var_mark_readonly(name);
        }
        free(name);
    }
    return status;
}

static int builtin_clear(int argc, char **argv) {
    (void)argc;
    (void)argv;
    term_clear_screen();
    return 0;
}

static int builtin_rehash(int argc, char **argv) {
    (void)argc;
    (void)argv;

    path_reload_environment();
    foreign_forget();
    complete_forget();
    fresh_home_init();
    theme_load_configured();
    plugins_load_configured();
    return 0;
}

static int entry_executable(const WIN32_FIND_DATAA *data) {
#ifdef _WIN32
    const char *ext = path_ext(data->cFileName);
    return str_ieq(ext, ".exe") || str_ieq(ext, ".bat") || str_ieq(ext, ".cmd") ||
           str_ieq(ext, ".com") || str_ieq(ext, ".ps1") || str_ieq(ext, ".sh") ||
           str_ieq(ext, ".msi");
#else
    return (data->dwFileAttributes & FRESH_ATTRIBUTE_EXECUTABLE) != 0;
#endif
}

static int extension_in(const char *ext, const char *list) {
    char needle[32];
    snprintf(needle, sizeof(needle), "%s;", ext);
    return ext[0] && _stricmp(ext, "") != 0 && strstr(list, needle) != NULL;
}

static const char *entry_color(const WIN32_FIND_DATAA *data) {
    if (!style_enabled()) return "";
    if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return S_DIR;
    if (data->cFileName[0] == '.') return S_HIDDEN;

    const char *ext = path_ext(data->cFileName);
    if (entry_executable(data)) return S_EXEC;
    if (extension_in(ext, ".zip;.7z;.rar;.tar;.gz;.xz;.zst;.bz2;.cab;")) return S_ARCHIVE;
    if (extension_in(ext, ".png;.jpg;.jpeg;.gif;.bmp;.ico;.svg;.webp;.tif;")) return S_IMAGE;
    if (extension_in(ext, ".mp3;.mp4;.mkv;.wav;.flac;.mov;.avi;.webm;.ogg;")) return S_MEDIA;
    if (extension_in(ext, ".c;.h;.cpp;.hpp;.cs;.py;.js;.ts;.tsx;.rs;.go;.java;.rb;.php;.lua;"))
        return S_CODE;
    if (extension_in(ext, ".json;.yml;.yaml;.toml;.ini;.cfg;.xml;.conf;.rc;.env;")) return S_CONFIG;
    return "";
}

static char entry_marker(const WIN32_FIND_DATAA *data) {
    if (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return '/';
    if (entry_executable(data)) return '*';
    return '\0';
}

static void human_size(ULONGLONG size, char *out, size_t out_size) {
    const char *units[] = {"B", "K", "M", "G", "T"};
    double value = (double)size;
    int unit = 0;
    while (value >= 1024 && unit < 4) {
        value /= 1024;
        unit++;
    }
    if (unit == 0) snprintf(out, out_size, "%.0f %s", value, units[unit]);
    else snprintf(out, out_size, "%.1f %s", value, units[unit]);
}

static void print_long_entry(const WIN32_FIND_DATAA *data) {
    SYSTEMTIME st;
    FILETIME local;
    FileTimeToLocalFileTime(&data->ftLastWriteTime, &local);
    FileTimeToSystemTime(&local, &st);

    int is_dir = (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    ULONGLONG size = ((ULONGLONG)data->nFileSizeHigh << 32) | data->nFileSizeLow;

    char size_text[32];
    if (is_dir) snprintf(size_text, sizeof(size_text), "%s", "-");
    else human_size(size, size_text, sizeof(size_text));

    static const char *MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const char *color = entry_color(data);
    char marker = entry_marker(data);

    char decorated[MAX_PATH + 2];
    snprintf(decorated, sizeof(decorated), "%s%c", data->cFileName, marker);

    printf("%s%c%c%c%s  %s%8s%s  %s%2d %s %02d:%02d%s  %s%s%s\n", style(S_DIM), is_dir ? 'd' : '-',
           (data->dwFileAttributes & FILE_ATTRIBUTE_READONLY) ? '-' : 'w',
           entry_executable(data) || is_dir ? 'x' : '-', style(S_RESET),
           style(S_VALUE), size_text, style(S_RESET), style(S_DIM), st.wDay,
           MONTHS[(st.wMonth - 1) % 12], st.wHour, st.wMinute, style(S_RESET), color, decorated,
           *color ? style(S_RESET) : "");
}

static int builtin_ls(int argc, char **argv) {
    int show_all = 0;
    int long_format = 0;
    int one_per_line = 0;
    const char *target = ".";

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1]) {
            for (const char *flag = argv[i] + 1; *flag; flag++) {
                if (*flag == 'a') show_all = 1;
                else if (*flag == 'l') long_format = 1;
                else if (*flag == '1') one_per_line = 1;
            }
        } else {
            target = argv[i];
        }
    }

    char pattern[PATH_BUF];
    if (path_is_dir(target)) snprintf(pattern, sizeof(pattern), "%s\\*", target);
    else snprintf(pattern, sizeof(pattern), "%s", target);
    path_to_backslashes(pattern);

    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        shell_error("ls: cannot access '%s': No such file or directory", target);
        return 2;
    }
    int plain = !_isatty(_fileno(stdout));
    if (plain) one_per_line = 1;

    StrList names;
    sl_init(&names);
    size_t longest = 0;
    int directories = 0;
    int files = 0;
    ULONGLONG total = 0;

    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        if (!show_all && data.cFileName[0] == '.') continue;

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            directories++;
        } else {
            files++;
            total += ((ULONGLONG)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        }

        if (long_format) {
            print_long_entry(&data);
            continue;
        }

        const char *color = entry_color(&data);
        char marker = plain ? (char)0 : entry_marker(&data);
        size_t visible = strlen(data.cFileName) + (marker ? 1 : 0);
        if (visible > longest) longest = visible;

        StrBuf entry;
        sb_init(&entry);
        sb_puts(&entry, color);
        sb_puts(&entry, data.cFileName);
        if (marker) sb_putc(&entry, marker);
        if (*color) sb_puts(&entry, style(S_RESET));
        sl_push(&names, sb_take(&entry));
    } while (FindNextFileA(find, &data));
    FindClose(find);

    if (!long_format && names.len > 0) {
        sl_sort(&names);
        int width = term_width();
        int column_width = (int)longest + 2;
        int columns = one_per_line ? 1 : (column_width > 0 ? width / column_width : 1);
        if (columns < 1) columns = 1;

        for (size_t i = 0; i < names.len; i++) {
            const char *entry = names.items[i];
            fputs(entry, stdout);
            if ((i + 1) % (size_t)columns == 0 || i + 1 == names.len) {
                putchar('\n');
            } else {
                for (int pad = display_width(entry); pad < column_width; pad++) putchar(' ');
            }
        }
    }
    sl_free(&names);

    if (directories + files > 0 && !plain) {
        char size_text[32];
        human_size(total, size_text, sizeof(size_text));
        printf("%s%s %d director%s, %d file%s, %s%s\n", style(S_DIM), S_LAMBDA, directories,
               directories == 1 ? "y" : "ies", files, files == 1 ? "" : "s", size_text,
               style(S_RESET));
    }
    return 0;
}

int test_unary_operator(const char *op) {
    return op[0] == '-' && op[1] && !op[2] && strchr("abcdefghknoprstuvwxzGLNORS", op[1]);
}

static int file_type_is(const char *path, char op) {
#ifdef _WIN32
    (void)path;
    (void)op;
    return 0;
#else
    struct stat info;
    if (stat(path, &info) != 0) return 0;
    switch (op) {
    case 'b': return S_ISBLK(info.st_mode);
    case 'c': return S_ISCHR(info.st_mode);
    case 'p': return S_ISFIFO(info.st_mode);
    case 'S': return S_ISSOCK(info.st_mode);
    case 'u': return (info.st_mode & S_ISUID) != 0;
    case 'g': return (info.st_mode & S_ISGID) != 0;
    case 'k': return (info.st_mode & S_ISVTX) != 0;
    case 'O': return info.st_uid == geteuid();
    case 'G': return info.st_gid == getegid();
    case 'N': return info.st_mtime >= info.st_atime;
    default: return 0;
    }
#endif
}

static int variable_is_set(const char *name) {
    const char *open = strchr(name, '[');
    if (!open || name[strlen(name) - 1] != ']') return var_exists(name);
    char *base = xstrndup(name, (size_t)(open - name));
    char *index = xstrndup(open + 1, strlen(open + 1) - 1);
    char *resolved = expand_single(index);
    int set = var_get_element(base, resolved) != NULL;
    free(resolved);
    free(index);
    free(base);
    return set;
}

int test_unary(char op, const char *arg) {
    DWORD attributes = GetFileAttributesA(arg);
    int exists = attributes != INVALID_FILE_ATTRIBUTES;
    switch (op) {
    case 'n': return *arg != '\0';
    case 'z': return *arg == '\0';
    case 'v': return variable_is_set(arg);
    case 'R': return var_exists(arg) && var_is_nameref(arg);
    case 'o': {
        SetOption *option = set_option_named(arg);
        return option && *set_option_slot(option);
    }
    case 't': return _isatty(atoi(arg)) != 0;
    case 'a':
    case 'e': return exists;
    case 'f': return exists && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    case 'd': return exists && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    case 'h':
    case 'L': return path_is_link(arg);
#ifdef _WIN32
    case 'r':
    case 'x': return exists;
    case 'w': return exists && !(attributes & FILE_ATTRIBUTE_READONLY);
#else
    case 'r': return exists && access(arg, R_OK) == 0;
    case 'w': return exists && access(arg, W_OK) == 0;
    case 'x': return exists && access(arg, X_OK) == 0;
#endif
    case 's': {
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (!GetFileAttributesExA(arg, GetFileExInfoStandard, &info)) return 0;
        return info.nFileSizeLow > 0 || info.nFileSizeHigh > 0;
    }
    default: return file_type_is(arg, op);
    }
}

static int same_file(const char *left, const char *right) {
#ifdef _WIN32
    char a[PATH_BUF];
    char b[PATH_BUF];
    if (GetFileAttributesA(left) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesA(right) == INVALID_FILE_ATTRIBUTES)
        return 0;
    if (!GetFullPathNameA(left, sizeof(a), a, NULL) || !GetFullPathNameA(right, sizeof(b), b, NULL))
        return 0;
    path_to_slashes(a);
    path_to_slashes(b);
    return _stricmp(a, b) == 0;
#else
    struct stat a;
    struct stat b;
    return stat(left, &a) == 0 && stat(right, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
#endif
}

int test_file_binary(const char *op, const char *left, const char *right, int *result) {
    if (strcmp(op, "-ef") == 0) {
        *result = same_file(left, right);
        return 1;
    }
    if (strcmp(op, "-nt") != 0 && strcmp(op, "-ot") != 0) return 0;
    WIN32_FILE_ATTRIBUTE_DATA left_info;
    WIN32_FILE_ATTRIBUTE_DATA right_info;
    int has_left = GetFileAttributesExA(left, GetFileExInfoStandard, &left_info) != 0;
    int has_right = GetFileAttributesExA(right, GetFileExInfoStandard, &right_info) != 0;
    LONG compared = has_left && has_right
                    ? CompareFileTime(&left_info.ftLastWriteTime, &right_info.ftLastWriteTime)
                    : has_left - has_right;
    *result = op[1] == 'n' ? compared > 0 : compared < 0;
    return 1;
}

typedef struct {
    char **argv;
    int argc;
    int pos;
    int failed;
} TestParse;

static int test_fail(TestParse *t, const char *word, const char *message) {
    if (!t->failed) {
        if (word) shell_error("test: %s: %s", word, message);
        else shell_error("test: %s", message);
    }
    t->failed = 1;
    t->pos = t->argc;
    return 0;
}

static int test_binary_operator(const char *op) {
    static const char *BINARY[] = {"=",   "==",  "!=",  "<",   ">",   "-eq", "-ne",
                                   "-lt", "-le", "-gt", "-ge", "-nt", "-ot", "-ef", NULL};
    for (int i = 0; BINARY[i]; i++)
        if (strcmp(BINARY[i], op) == 0) return 1;
    return 0;
}

static int test_binary(TestParse *t, const char *left, const char *op, const char *right) {
    int result = 0;
    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) return strcmp(left, right) == 0;
    if (strcmp(op, "!=") == 0) return strcmp(left, right) != 0;
    if (strcmp(op, "<") == 0) return strcmp(left, right) < 0;
    if (strcmp(op, ">") == 0) return strcmp(left, right) > 0;
    if (test_file_binary(op, left, right, &result)) return result;

    long long a;
    long long b;
    if (!parse_number(left, &a)) return test_fail(t, left, "integer expression expected");
    if (!parse_number(right, &b)) return test_fail(t, right, "integer expression expected");
    if (op[1] == 'e') return a == b;
    if (op[1] == 'n') return a != b;
    if (op[1] == 'l') return op[2] == 't' ? a < b : a <= b;
    return op[2] == 't' ? a > b : a >= b;
}

static const char *test_word(TestParse *t, int offset) {
    return t->pos + offset < t->argc ? t->argv[t->pos + offset] : NULL;
}

static int test_is(TestParse *t, int offset, const char *text) {
    const char *word = test_word(t, offset);
    return word && strcmp(word, text) == 0;
}

static int test_or(TestParse *t);

static int test_term(TestParse *t) {
    if (t->pos >= t->argc) return test_fail(t, NULL, "argument expected");
    if (test_is(t, 0, "!")) {
        t->pos++;
        return !test_term(t);
    }
    if (test_is(t, 0, "(")) {
        t->pos++;
        int value = test_or(t);
        if (!test_is(t, 0, ")")) return test_fail(t, NULL, "`)' expected");
        t->pos++;
        return value;
    }
    if (t->pos + 2 < t->argc && test_binary_operator(test_word(t, 1))) {
        int value = test_binary(t, test_word(t, 0), test_word(t, 1), test_word(t, 2));
        if (!t->failed) t->pos += 3;
        return value;
    }
    if (t->pos + 1 < t->argc && test_unary_operator(test_word(t, 0))) {
        int value = test_unary(test_word(t, 0)[1], test_word(t, 1));
        t->pos += 2;
        return value;
    }
    return *t->argv[t->pos++] != '\0';
}

static int test_and(TestParse *t) {
    int value = test_term(t);
    while (test_is(t, 0, "-a")) {
        t->pos++;
        int right = test_term(t);
        value = value && right;
    }
    return value;
}

static int test_or(TestParse *t) {
    int value = test_and(t);
    while (test_is(t, 0, "-o")) {
        t->pos++;
        int right = test_and(t);
        value = value || right;
    }
    return value;
}

static int test_two(TestParse *t) {
    const char *first = test_word(t, 0);
    const char *second = test_word(t, 1);
    t->pos = t->argc;
    if (strcmp(first, "!") == 0) return *second == '\0';
    if (test_unary_operator(first)) return test_unary(first[1], second);
    return test_fail(t, first, "unary operator expected");
}

static int test_three(TestParse *t) {
    const char *first = test_word(t, 0);
    const char *op = test_word(t, 1);
    const char *last = test_word(t, 2);
    if (test_binary_operator(op)) {
        t->pos = t->argc;
        return test_binary(t, first, op, last);
    }
    if (strcmp(op, "-a") == 0 || strcmp(op, "-o") == 0) {
        t->pos = t->argc;
        return op[1] == 'a' ? *first && *last : *first || *last;
    }
    if (strcmp(first, "!") == 0) {
        t->pos++;
        return !test_two(t);
    }
    if (strcmp(first, "(") == 0 && strcmp(last, ")") == 0) {
        t->pos = t->argc;
        return *op != '\0';
    }
    return test_fail(t, op, "binary operator expected");
}

static int test_expression(TestParse *t) {
    switch (t->argc - t->pos) {
    case 0: return 0;
    case 1: return *t->argv[t->pos++] != '\0';
    case 2: return test_two(t);
    case 3: return test_three(t);
    case 4:
        if (test_is(t, 0, "!")) {
            t->pos++;
            return !test_three(t);
        }
        if (test_is(t, 0, "(") && test_is(t, 3, ")")) {
            t->pos++;
            int value = test_two(t);
            t->pos = t->argc;
            return value;
        }
        return test_or(t);
    default: return test_or(t);
    }
}

static int builtin_test(int argc, char **argv) {
    int count = argc - 1;
    if (strcmp(argv[0], "[") == 0) {
        if (count < 1 || strcmp(argv[argc - 1], "]") != 0) {
            shell_error("[: missing `]'");
            return 2;
        }
        count--;
    }
    TestParse t = {argv + 1, count, 0, 0};
    int value = test_expression(&t);
    if (!t.failed && t.pos < t.argc) test_fail(&t, NULL, "too many arguments");
    return t.failed ? 2 : value ? 0 : 1;
}

static int builtin_true(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 0;
}

static int builtin_false(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return 1;
}

static int read_count(const char *builtin, const char *text, long long *out) {
    if (parse_number(text, out) && *out >= 0) return 1;
    shell_error("%s: %s: invalid number", builtin, text);
    return 0;
}

static void split_fields(const char *line, const char *escaped, const char *ifs, int wanted,
                         StrList *out) {
    size_t length = strlen(line);
    size_t i = 0;
#define IS_IFS(k) (!escaped[k] && strchr(ifs, line[k]))
#define IS_SPACE(k) (IS_IFS(k) && strchr(" \t\n", line[k]))
    while (i < length && IS_SPACE(i)) i++;
    while (i < length) {
        if (wanted > 0 && (int)out->len == wanted - 1) {
            size_t end = length;
            while (end > i && IS_SPACE(end - 1)) end--;
            sl_push(out, xstrndup(line + i, end - i));
            return;
        }
        size_t start = i;
        while (i < length && !IS_IFS(i)) i++;
        sl_push(out, xstrndup(line + start, i - start));
        while (i < length && IS_SPACE(i)) i++;
        if (i < length && IS_IFS(i)) {
            i++;
            while (i < length && IS_SPACE(i)) i++;
        }
    }
#undef IS_SPACE
#undef IS_IFS
}

static int builtin_read(int argc, char **argv) {
    Opts o = {1, 0, 0, NULL};
    int c;
    const char *array = NULL;
    const char *prompt = NULL;
    int raw = 0;
    int delimiter = '\n';
    long long limit = -1;
    int exact = 0;
    long long fd = 0;
    int silent = 0;
    double timeout = -1;
    while ((c = opt_next(&o, argc, argv, "read", "a:d:ei:n:N:p:rst:u:")) > 0) {
        switch (c) {
        case 'a': array = o.arg; break;
        case 'd': delimiter = (unsigned char)o.arg[0]; break;
        case 'n':
        case 'N':
            if (!read_count("read", o.arg, &limit)) return 1;
            exact = c == 'N';
            break;
        case 'p': prompt = o.arg; break;
        case 'r': raw = 1; break;
        case 'u':
            if (!read_count("read", o.arg, &fd)) return 1;
            break;
        case 's': silent = 1; break;
        case 't': {
            char *end;
            timeout = strtod(o.arg, &end);
            if (end == o.arg || *end || timeout < 0) {
                shell_error("read: %s: invalid timeout specification", o.arg);
                return 1;
            }
            break;
        }
        case 'e':
        case 'i': break;
        default: return 2;
        }
    }
    for (int i = o.index; i < argc; i++)
        if (!var_valid_name(argv[i], 1)) return invalid_name("read", argv[i]);
    if (array && !var_valid_name(array, 0)) return invalid_name("read", array);

    if (timeout == 0) return term_input_wait((int)fd, 0) ? 0 : 1;
    if (prompt && _isatty((int)fd)) {
        fputs(prompt, stderr);
        fflush(stderr);
    }
    fflush(stdout);
    if (silent) term_echo((int)fd, 0);
    DWORD deadline = GetTickCount() + (DWORD)(timeout * 1000);

    StrBuf line;
    StrBuf escaped;
    sb_init(&line);
    sb_init(&escaped);
    int ended = 0;
    int expired = 0;
    char ch;
    while (limit < 0 || (long long)line.len < limit) {
        long remaining = (long)(deadline - GetTickCount());
        if (timeout > 0 && (remaining <= 0 || !term_input_wait((int)fd, (int)remaining))) {
            expired = 1;
            break;
        }
        if (_read((int)fd, &ch, 1) != 1) {
            ended = 1;
            break;
        }
        if (!exact && (unsigned char)ch == delimiter) break;
        int quoted = 0;
        if (!raw && ch == '\\') {
            if (_read((int)fd, &ch, 1) != 1) {
                ended = 1;
                break;
            }
            if (ch == '\n') continue;
            quoted = 1;
        }
        sb_putc(&line, ch);
        sb_putc(&escaped, (char)quoted);
    }
    if (delimiter == '\n' && line.len && line.data[line.len - 1] == '\r' && !escaped.data[line.len - 1])
        line.data[--line.len] = '\0';

    if (silent) term_echo((int)fd, 1);

    const char *ifs = var_get("IFS");
    if (!ifs) ifs = " \t\n";
    int status = expired ? 142 : ended ? 1 : 0;

    if (array || o.index >= argc || exact) {
        const char *name = array ? array : o.index < argc ? argv[o.index] : "REPLY";
        if (array) {
            StrList fields;
            sl_init(&fields);
            split_fields(line.data, escaped.data, ifs, 0, &fields);
            var_set_array(name, &fields, VAR_INDEXED);
            sl_free(&fields);
        } else if (var_assign(name, line.data)) {
            status = 1;
        }
        for (int i = o.index + 1; !array && i < argc; i++) var_assign(argv[i], "");
    } else {
        StrList fields;
        sl_init(&fields);
        split_fields(line.data, escaped.data, ifs, argc - o.index, &fields);
        for (int i = o.index; i < argc; i++) {
            size_t field = (size_t)(i - o.index);
            if (var_assign(argv[i], field < fields.len ? fields.items[field] : "")) status = 1;
        }
        sl_free(&fields);
    }
    sb_free(&line);
    sb_free(&escaped);
    return status;
}

typedef struct {
    const char *name;
    int *slot;
    int state;
} ShoptOption;

static ShoptOption SHOPT_OPTIONS[] = {
    {"dotglob", &shell.dotglob, 0},       {"expand_aliases", NULL, 0}, {"extglob", NULL, 0},
    {"globstar", &shell.globstar, 0},     {"nocasematch", &shell.nocasematch, 0},
    {"nullglob", &shell.nullglob, 0},
};

#define SHOPT_COUNT (sizeof(SHOPT_OPTIONS) / sizeof(SHOPT_OPTIONS[0]))

static int *shopt_slot(const char *name, int set_options) {
    if (set_options) {
        SetOption *option = set_option_named(name);
        return option ? set_option_slot(option) : NULL;
    }
    for (size_t i = 0; i < SHOPT_COUNT; i++) {
        ShoptOption *option = &SHOPT_OPTIONS[i];
        if (strcmp(option->name, name) == 0) return option->slot ? option->slot : &option->state;
    }
    return NULL;
}

static void shopt_show(const char *name, int on, int quiet, int reusable, int set_options) {
    if (quiet) return;
    if (reusable && set_options) printf("set %co %s\n", on ? '-' : '+', name);
    else if (reusable) printf("shopt %s %s\n", on ? "-s" : "-u", name);
    else printf("%-15s\t%s\n", name, on ? "on" : "off");
}

static int builtin_shopt(int argc, char **argv) {
    Opts o = {1, 0, 0, NULL};
    int c;
    int setting = 0;
    int clearing = 0;
    int quiet = 0;
    int reusable = 0;
    int set_options = 0;
    while ((c = opt_next(&o, argc, argv, "shopt", "opqsu")) > 0) {
        if (c == '?') return 2;
        if (c == 's') setting = 1;
        if (c == 'u') clearing = 1;
        if (c == 'q') quiet = 1;
        if (c == 'p') reusable = 1;
        if (c == 'o') set_options = 1;
    }
    if (setting && clearing) {
        shell_error("shopt: cannot set and unset shell options simultaneously");
        return 1;
    }

    if (o.index >= argc) {
        size_t count = set_options ? SET_OPTION_COUNT : SHOPT_COUNT;
        for (size_t i = 0; i < count; i++) {
            const char *name = set_options ? SET_OPTIONS[i].name : SHOPT_OPTIONS[i].name;
            int on = *shopt_slot(name, set_options);
            if ((!setting && !clearing) || on == setting) shopt_show(name, on, quiet, reusable, set_options);
        }
        return 0;
    }

    int status = 0;
    for (int i = o.index; i < argc; i++) {
        int *slot = shopt_slot(argv[i], set_options);
        if (!slot) {
            shell_error("shopt: %s: invalid shell option name", argv[i]);
            status = 1;
        } else if (setting || clearing) {
            *slot = setting;
        } else {
            shopt_show(argv[i], *slot, quiet, reusable, set_options);
            if (!*slot) status = 1;
        }
    }
    return status;
}

static char *visits_path(void) {
    return config_path(".fresh_visits");
}

void visits_record(const char *directory) {
    if (!option_enabled("FRESH_JUMP", 1)) return;

    char *path = visits_path();
    StrList lines;
    sl_init(&lines);

    FILE *f = fopen(path, "rb");
    int seen = 0;
    if (f) {
        char line[PATH_BUF + 32];
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = '\0';
            if (!*line) continue;

            char *bar = strrchr(line, '|');
            if (!bar) continue;
            *bar = '\0';

            long score = atol(bar + 1);
            if (_stricmp(line, directory) == 0) {
                score += 10;
                seen = 1;
            } else if (score > 1) {
                score = score * 99 / 100;
            }
            if (score < 1) continue;

            StrBuf entry;
            sb_init(&entry);
            sb_printf(&entry, "%s|%ld", line, score);
            sl_push(&lines, sb_take(&entry));
        }
        fclose(f);
    }

    if (!seen) {
        StrBuf entry;
        sb_init(&entry);
        sb_printf(&entry, "%s|%d", directory, 10);
        sl_push(&lines, sb_take(&entry));
    }

    f = fopen(path, "wb");
    if (f) {
        for (size_t i = 0; i < lines.len && i < 500; i++) fprintf(f, "%s\n", lines.items[i]);
        fclose(f);
    }
    sl_free(&lines);
    free(path);
}

static int builtin_jump(int argc, char **argv) {
    char *path = visits_path();
    FILE *f = fopen(path, "rb");
    free(path);

    if (!f) {
        shell_error("z: nowhere has been visited yet");
        return 1;
    }

    char best[PATH_BUF] = "";
    long best_score = 0;
    StrList listing;
    sl_init(&listing);

    char line[PATH_BUF + 32];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *bar = strrchr(line, '|');
        if (!bar) continue;
        *bar = '\0';
        long score = atol(bar + 1);

        int wanted = 1;
        for (int i = 1; i < argc && wanted; i++) {
            StrBuf pattern;
            sb_init(&pattern);
            sb_printf(&pattern, "*%s*", argv[i]);
            wanted = pattern_match(pattern.data, line);
            sb_free(&pattern);
        }
        if (!wanted) continue;

        if (argc < 2) {
            StrBuf row;
            sb_init(&row);
            sb_printf(&row, "%6ld  %s", score, line);
            sl_push(&listing, sb_take(&row));
        }
        if (score > best_score && path_is_dir(line)) {
            best_score = score;
            snprintf(best, sizeof(best), "%s", line);
        }
    }
    fclose(f);

    if (argc < 2) {
        sl_sort(&listing);
        for (size_t i = 0; i < listing.len; i++) printf("%s\n", listing.items[i]);
        sl_free(&listing);
        return 0;
    }
    sl_free(&listing);

    if (!*best) {
        shell_error("z: %s: no directory remembered", argv[1]);
        return 1;
    }

    char native[PATH_BUF];
    snprintf(native, sizeof(native), "%s", best);
    path_to_backslashes(native);
    if (!SetCurrentDirectoryA(native)) {
        shell_error("z: %s: gone", best);
        return 1;
    }
    printf("%s\n", best);
    sync_cwd();
    return 0;
}

#ifdef _WIN32

typedef BOOL(WINAPI *ClipOpenFn)(HWND);
typedef BOOL(WINAPI *ClipCloseFn)(void);
typedef BOOL(WINAPI *ClipEmptyFn)(void);
typedef HANDLE(WINAPI *ClipGetFn)(UINT);
typedef HANDLE(WINAPI *ClipSetFn)(UINT, HANDLE);

static ClipOpenFn clip_open;
static ClipCloseFn clip_close;
static ClipEmptyFn clip_empty;
static ClipGetFn clip_get;
static ClipSetFn clip_set;

static int clipboard_ready(void) {
    static HMODULE library = NULL;
    if (library) return clip_open != NULL;

    library = LoadLibraryA("user32.dll");
    if (!library) return 0;

    clip_open = (ClipOpenFn)(void *)GetProcAddress(library, "OpenClipboard");
    clip_close = (ClipCloseFn)(void *)GetProcAddress(library, "CloseClipboard");
    clip_empty = (ClipEmptyFn)(void *)GetProcAddress(library, "EmptyClipboard");
    clip_get = (ClipGetFn)(void *)GetProcAddress(library, "GetClipboardData");
    clip_set = (ClipSetFn)(void *)GetProcAddress(library, "SetClipboardData");

    return clip_open && clip_close && clip_empty && clip_get && clip_set;
}

static int clipboard_open(void) {
    if (!clipboard_ready()) return 0;
    for (int attempt = 0; attempt < 10; attempt++) {
        if (clip_open(NULL)) return 1;
        Sleep(20);
    }
    return 0;
}

static int clipboard_write(const char *text, size_t length) {
    int status = 1;
    if (clipboard_open()) {
        clip_empty();
        HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, length + 1);
        if (block) {
            memcpy(GlobalLock(block), text, length + 1);
            GlobalUnlock(block);
            clip_set(CF_TEXT, block);
            status = 0;
        }
        clip_close();
    }
    return status;
}

static int clipboard_read(StrBuf *out) {
    if (!clipboard_open()) return 1;
    HANDLE data = clip_get(CF_TEXT);
    if (data) {
        const char *text = GlobalLock(data);
        if (text) {
            sb_puts(out, text);
            GlobalUnlock(data);
        }
    }
    clip_close();
    return 0;
}

#else

#ifdef __APPLE__
#define CLIPBOARD_WRITE "pbcopy"
#define CLIPBOARD_READ "pbpaste"
#else
#define CLIPBOARD_WRITE "xclip -selection clipboard -i 2>/dev/null || wl-copy 2>/dev/null"
#define CLIPBOARD_READ "xclip -selection clipboard -o 2>/dev/null || wl-paste 2>/dev/null"
#endif

static int clipboard_write(const char *text, size_t length) {
    FILE *pipe = popen(CLIPBOARD_WRITE, "w");
    if (!pipe) return 1;
    fwrite(text, 1, length, pipe);
    return pclose(pipe) == 0 ? 0 : 1;
}

static int clipboard_read(StrBuf *out) {
    FILE *pipe = popen(CLIPBOARD_READ, "r");
    if (!pipe) return 1;
    char buffer[4096];
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), pipe)) > 0) sb_putn(out, buffer, read);
    return pclose(pipe) == 0 ? 0 : 1;
}

#endif

static int builtin_copy(int argc, char **argv) {
    StrBuf text;
    sb_init(&text);

    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (i > 1) sb_putc(&text, ' ');
            sb_puts(&text, argv[i]);
        }
    } else {
        char buffer[4096];
        size_t read;
        while ((read = fread(buffer, 1, sizeof(buffer), stdin)) > 0) sb_putn(&text, buffer, read);
        while (text.len > 0 && (text.data[text.len - 1] == '\n' || text.data[text.len - 1] == '\r'))
            text.data[--text.len] = '\0';
    }

    int status = clipboard_write(text.data, text.len);
    if (status) shell_error("copy: the clipboard would not open");
    sb_free(&text);
    return status;
}

static int builtin_paste(int argc, char **argv) {
    if (argc > 1) return coreutil_lookup("paste")(argc, argv);

    StrBuf text;
    sb_init(&text);
    if (clipboard_read(&text)) {
        sb_free(&text);
        shell_error("paste: the clipboard would not open");
        return 1;
    }
    while (text.len > 0 && (text.data[text.len - 1] == '\n' || text.data[text.len - 1] == '\r'))
        text.data[--text.len] = '\0';
    printf("%s\n", text.data);
    sb_free(&text);
    return 0;
}

static int builtin_copypath(int argc, char **argv) {
    char cwd[PATH_BUF];
    if (argc > 1) {
        snprintf(cwd, sizeof(cwd), "%s", argv[1]);
        char full[PATH_BUF];
        if (GetFullPathNameA(cwd, sizeof(full), full, NULL)) snprintf(cwd, sizeof(cwd), "%s", full);
    } else if (!GetCurrentDirectoryA(sizeof(cwd), cwd)) {
        return 1;
    }
    path_to_slashes(cwd);

    char *arguments[2] = {"copy", cwd};
    return builtin_copy(2, arguments);
}

static int builtin_extract(int argc, char **argv) {
    if (argc < 2) {
        shell_error("extract: usage: extract <archive> [<directory>]");
        return 2;
    }

    const char *archive = argv[1];
    if (!path_is_file(archive)) {
        shell_error("extract: %s: no such file", archive);
        return 1;
    }
    const char *into = argc > 2 ? argv[2] : ".";
    const char *ext = path_ext(archive);
    if (argc > 2 && !path_is_dir(into)) path_mkdirs(into);

    StrBuf command;
    sb_init(&command);
    if (str_ieq(ext, ".zip")) {
#ifdef _WIN32
        sb_printf(&command,
                  "powershell.exe -NoLogo -NoProfile -Command \"Expand-Archive -LiteralPath '%s' "
                  "-DestinationPath '%s' -Force\"",
                  archive, into);
#else
        sb_printf(&command, "unzip -o -q \"%s\" -d \"%s\"", archive, into);
#endif
    } else if (str_ieq(ext, ".gz") || str_ieq(ext, ".tgz") || str_ieq(ext, ".bz2") ||
               str_ieq(ext, ".xz") || str_ieq(ext, ".tar")) {
        sb_printf(&command, "tar -x -f \"%s\" -C \"%s\"", archive, into);
    } else if (str_ieq(ext, ".7z") || str_ieq(ext, ".rar")) {
        sb_printf(&command, "7z x \"%s\" -o\"%s\"", archive, into);
    } else {
        shell_error("extract: %s: unknown archive kind", ext);
        sb_free(&command);
        return 1;
    }

    int status = exec_text(command.data);
    sb_free(&command);
    return status;
}

#ifndef _WIN32

static int builtin_admin(int argc, char **argv) {
    char exe[PATH_BUF];
    GetModuleFileNameA(NULL, exe, sizeof(exe));

    StrBuf command;
    sb_init(&command);
    sb_puts(&command, "sudo -E ");
    sb_put_quoted(&command, exe);
    if (argc > 1) {
        StrBuf joined;
        sb_init(&joined);
        for (int i = 1; i < argc; i++) {
            if (i > 1) sb_putc(&joined, ' ');
            sb_puts(&joined, argv[i]);
        }
        sb_puts(&command, " -c ");
        sb_put_quoted(&command, joined.data);
        sb_free(&joined);
    }
    int status = exec_text(command.data);
    sb_free(&command);
    return status;
}

#else

static int builtin_admin(int argc, char **argv) {
    char exe[PATH_BUF];
    GetModuleFileNameA(NULL, exe, sizeof(exe));

    StrBuf parameters;
    sb_init(&parameters);
    if (argc > 1) {
        sb_puts(&parameters, "-c \"");
        for (int i = 1; i < argc; i++) {
            if (i > 1) sb_putc(&parameters, ' ');
            sb_puts(&parameters, argv[i]);
        }
        sb_putc(&parameters, '"');
    }

    char directory[PATH_BUF];
    GetCurrentDirectoryA(sizeof(directory), directory);

    SHELLEXECUTEINFOA info;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.lpVerb = "runas";
    info.lpFile = exe;
    info.lpParameters = parameters.len ? parameters.data : NULL;
    info.lpDirectory = directory;
    info.nShow = SW_SHOWNORMAL;

    typedef BOOL(WINAPI * ExecuteFn)(SHELLEXECUTEINFOA *);
    HMODULE shell_library = LoadLibraryA("shell32.dll");
    ExecuteFn execute =
        shell_library ? (ExecuteFn)(void *)GetProcAddress(shell_library, "ShellExecuteExA") : NULL;

    int status = execute && execute(&info) ? 0 : 1;
    if (status) shell_error("admin: the elevated shell was refused");
    sb_free(&parameters);
    return status;
}

#endif

static int builtin_bind(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "-l") == 0) {
        StrList widgets;
        sl_init(&widgets);
        widget_names(&widgets);
        for (size_t i = 0; i < widgets.len; i++) printf("%s\n", widgets.items[i]);
        sl_free(&widgets);
        return 0;
    }

    if (argc > 2 && strcmp(argv[1], "-r") == 0) {
        if (!bind_remove(argv[2])) {
            shell_error("bind: %s: not bound", argv[2]);
            return 1;
        }
        return 0;
    }

    if (argc < 3) {
        StrList rows;
        sl_init(&rows);
        bind_list(&rows);
        for (size_t i = 0; i < rows.len; i++) printf("  %s\n", rows.items[i]);
        if (rows.len == 0)
            printf("  %snothing bound, try bind ctrl+g \"git status\"%s\n", style(S_DIM),
                   style(S_RESET));
        sl_free(&rows);
        return 0;
    }

    if (!key_code_from_name(argv[1])) {
        shell_error("bind: %s: not a key\n  try ctrl+<letter>, f1 to f12, home, end or delete",
                    argv[1]);
        return 1;
    }

    StrBuf action;
    sb_init(&action);
    for (int i = 2; i < argc; i++) {
        if (i > 2) sb_putc(&action, ' ');
        sb_puts(&action, argv[i]);
    }
    bind_set(argv[1], action.data);
    sb_free(&action);
    return 0;
}

static int builtin_getopts(int argc, char **argv) {
    static long scanned_optind = 0;
    static long letter = 1;

    if (argc < 3) {
        shell_error("getopts: usage: getopts optstring name [arg ...]");
        return 2;
    }

    const char *spec = argv[1];
    const char *name = argv[2];
    int silent = spec[0] == ':';
    if (silent) spec++;
    const char *opterr = var_get("OPTERR");
    int report = !silent && !(opterr && strcmp(opterr, "0") == 0);

    StrList args;
    sl_init(&args);
    if (argc > 3) {
        for (int i = 3; i < argc; i++) sl_push_copy(&args, argv[i]);
    } else {
        for (size_t i = 1; i < shell.params.len; i++) sl_push_copy(&args, shell.params.items[i]);
    }

    const char *stored = var_get("OPTIND");
    long position = stored ? atol(stored) : 1;
    if (position < 1) position = 1;
    if (position != scanned_optind) letter = 1;

    int status = 1;
    var_unset("OPTARG");
    var_set(name, "?");

    while ((size_t)position <= args.len) {
        const char *word = args.items[position - 1];
        if (word[0] != '-' || !word[1]) break;
        if (strcmp(word, "--") == 0) {
            position++;
            break;
        }

        char option = word[letter];
        if (!option) {
            position++;
            letter = 1;
            continue;
        }
        letter++;

        const char *found = option != ':' ? strchr(spec, option) : NULL;
        char text[2] = {option, '\0'};
        status = 0;

        if (!found) {
            if (silent) var_set("OPTARG", text);
            else if (report) shell_error("getopts: illegal option -- %c", option);
        } else if (found[1] == ':') {
            const char *value = NULL;
            if (word[letter]) value = word + letter;
            else if ((size_t)position < args.len) value = args.items[position++];
            position++;
            letter = 1;
            if (value) {
                var_set("OPTARG", value);
                var_set(name, text);
            } else if (silent) {
                var_set("OPTARG", text);
                var_set(name, ":");
            } else if (report) {
                shell_error("getopts: option requires an argument -- %c", option);
            }
        } else {
            var_set(name, text);
        }
        if (!(found && found[1] == ':') && !word[letter]) {
            position++;
            letter = 1;
        }
        break;
    }

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%ld", position);
    var_set("OPTIND", buffer);
    scanned_optind = position;

    sl_free(&args);
    return status;
}

static StrList directory_stack;

static void put_polite(StrBuf *out, const char *path, int long_form) {
    const char *home = var_get("HOME");
    size_t length = home ? strlen(home) : 0;
    if (!long_form && length > 1 && strncmp(path, home, length) == 0 &&
        (path[length] == '\0' || path[length] == '/')) {
        sb_putc(out, '~');
        path += length;
    }
    sb_puts(out, path);
}

static size_t stack_size(void) {
    return directory_stack.len + 1;
}

static const char *stack_entry(size_t index) {
    if (index == 0) {
        const char *pwd = var_get("PWD");
        return pwd ? pwd : "";
    }
    return directory_stack.items[index - 1];
}

static int stack_index(const char *word, size_t *index) {
    long long n;
    if ((word[0] != '+' && word[0] != '-') || !parse_number(word + 1, &n) || n < 0) return 0;
    if ((size_t)n >= stack_size()) return -1;
    *index = word[0] == '+' ? (size_t)n : stack_size() - 1 - (size_t)n;
    return 1;
}

static void print_stack(int long_form, int per_line, int numbered) {
    StrBuf line;
    sb_init(&line);
    for (size_t i = 0; i < stack_size(); i++) {
        if (numbered) sb_printf(&line, "%2zu  ", i);
        put_polite(&line, stack_entry(i), long_form);
        sb_putc(&line, per_line || numbered || i + 1 == stack_size() ? '\n' : ' ');
    }
    fputs(line.data, stdout);
    sb_free(&line);
}

static int stack_range_error(const char *builtin, const char *word) {
    shell_error("%s: %s: directory stack index out of range", builtin, word);
    return 1;
}

static int builtin_dirs(int argc, char **argv) {
    int long_form = 0;
    int per_line = 0;
    int numbered = 0;
    for (int i = 1; i < argc; i++) {
        size_t index;
        int found = stack_index(argv[i], &index);
        if (found < 0) return stack_range_error("dirs", argv[i]);
        if (found) {
            StrBuf line;
            sb_init(&line);
            put_polite(&line, stack_entry(index), long_form);
            printf("%s\n", line.data);
            sb_free(&line);
            return 0;
        }
        if (argv[i][0] != '-' || strspn(argv[i] + 1, "clpv") != strlen(argv[i] + 1)) {
            shell_error("dirs: %s: invalid option", argv[i]);
            return 2;
        }
        if (strchr(argv[i], 'c')) sl_clear(&directory_stack);
        if (strchr(argv[i], 'l')) long_form = 1;
        if (strchr(argv[i], 'p')) per_line = 1;
        if (strchr(argv[i], 'v')) numbered = 1;
        if (strchr(argv[i], 'c')) return 0;
    }
    print_stack(long_form, per_line, numbered);
    return 0;
}

static void stack_insert(size_t position, const char *path) {
    sl_push_copy(&directory_stack, path);
    char *added = directory_stack.items[directory_stack.len - 1];
    memmove(directory_stack.items + position + 1, directory_stack.items + position,
            (directory_stack.len - 1 - position) * sizeof(char *));
    directory_stack.items[position] = added;
}

static char *stack_remove(size_t position) {
    char *removed = directory_stack.items[position];
    memmove(directory_stack.items + position, directory_stack.items + position + 1,
            (directory_stack.len - position - 1) * sizeof(char *));
    directory_stack.len--;
    return removed;
}

static int builtin_pushd(int argc, char **argv) {
    int index = 1;
    int stay = 0;
    if (index < argc && strcmp(argv[index], "-n") == 0) {
        stay = 1;
        index++;
    }
    if (index < argc && strcmp(argv[index], "--") == 0) index++;

    int show = 0;
    char *current = xstrdup(stack_entry(0));
    int status = 0;
    if (index >= argc) {
        if (directory_stack.len == 0) {
            shell_error("pushd: no other directory");
            status = 1;
        } else if (stay || enter_directory("pushd", directory_stack.items[0], &show)) {
            free(directory_stack.items[0]);
            directory_stack.items[0] = xstrdup(current);
        } else {
            status = 1;
        }
    } else {
        size_t rotate;
        int found = stack_index(argv[index], &rotate);
        if (found < 0) {
            status = stack_range_error("pushd", argv[index]);
        } else if (found) {
            StrList all;
            sl_init(&all);
            for (size_t i = 0; i < stack_size(); i++) sl_push_copy(&all, stack_entry((rotate + i) % stack_size()));
            if (enter_directory("pushd", all.items[0], &show)) {
                sl_clear(&directory_stack);
                for (size_t i = 1; i < all.len; i++) sl_push_copy(&directory_stack, all.items[i]);
            } else {
                status = 1;
            }
            sl_free(&all);
        } else if (stay) {
            stack_insert(0, argv[index]);
        } else if (enter_directory("pushd", argv[index], &show)) {
            stack_insert(0, current);
        } else {
            status = 1;
        }
    }
    free(current);
    if (status == 0) print_stack(0, 0, 0);
    return status;
}

static int builtin_popd(int argc, char **argv) {
    int index = 1;
    int stay = 0;
    if (index < argc && strcmp(argv[index], "-n") == 0) {
        stay = 1;
        index++;
    }
    if (directory_stack.len == 0) {
        shell_error("popd: directory stack empty");
        return 1;
    }

    size_t target = 0;
    if (index < argc) {
        int found = stack_index(argv[index], &target);
        if (found < 0) return stack_range_error("popd", argv[index]);
        if (!found) {
            shell_error("popd: %s: invalid argument", argv[index]);
            return 2;
        }
    }

    int show = 0;
    if (target == 0 && !stay) {
        if (!enter_directory("popd", directory_stack.items[0], &show)) return 1;
        free(stack_remove(0));
    } else {
        free(stack_remove(target == 0 ? 0 : target - 1));
    }
    print_stack(0, 0, 0);
    return 0;
}

static int builtin_mapfile(int argc, char **argv) {
    Opts o = {1, 0, 0, NULL};
    int c;
    int strip = 0;
    int delimiter = '\n';
    long long count = 0;
    long long origin = 0;
    long long skip = 0;
    long long fd = 0;
    long long quantum = 5000;
    int keep = 0;
    const char *callback = NULL;
    while ((c = opt_next(&o, argc, argv, argv[0], "d:n:O:s:tu:C:c:")) > 0) {
        switch (c) {
        case 'd': delimiter = (unsigned char)o.arg[0]; break;
        case 't': strip = 1; break;
        case 'C': callback = o.arg; break;
        case 'n': if (!read_count(argv[0], o.arg, &count)) return 1; break;
        case 's': if (!read_count(argv[0], o.arg, &skip)) return 1; break;
        case 'u': if (!read_count(argv[0], o.arg, &fd)) return 1; break;
        case 'c': if (!read_count(argv[0], o.arg, &quantum)) return 1; break;
        case 'O':
            if (!read_count(argv[0], o.arg, &origin)) return 1;
            keep = 1;
            break;
        default: return 2;
        }
    }
    const char *name = o.index < argc ? argv[o.index] : "MAPFILE";
    if (!var_valid_name(name, 0)) return invalid_name(argv[0], name);

    StrList rows;
    sl_init(&rows);
    StrBuf row;
    sb_init(&row);
    char ch;
    long long seen = 0;
    while (count == 0 || (long long)rows.len < count) {
        int got = 0;
        int ended = 1;
        sb_clear(&row);
        while (_read((int)fd, &ch, 1) == 1) {
            got = 1;
            if ((unsigned char)ch == delimiter) {
                ended = 0;
                if (!strip) sb_putc(&row, ch);
                break;
            }
            sb_putc(&row, ch);
        }
        if (!got) break;
        if (seen++ < skip) continue;
        if (callback && quantum > 0 && (rows.len + 1) % (size_t)quantum == 0) {
            StrBuf call;
            sb_init(&call);
            sb_printf(&call, "%s %lld ", callback, origin + (long long)rows.len);
            sb_put_quoted(&call, row.data);
            exec_text(call.data);
            sb_free(&call);
        }
        sl_push_copy(&rows, row.data);
        if (ended) break;
    }
    sb_free(&row);

    if (!keep) {
        var_set_array(name, &rows, VAR_INDEXED);
    } else {
        if (var_kind(name) != VAR_INDEXED) var_declare(name, VAR_INDEXED);
        for (size_t i = 0; i < rows.len; i++) {
            char index[32];
            snprintf(index, sizeof(index), "%lld", origin + (long long)i);
            var_set_element(name, index, rows.items[i]);
        }
    }
    sl_free(&rows);
    return 0;
}

static int builtin_return(int argc, char **argv) {
    if (shell.returnable <= 0) {
        shell_error("return: can only `return' from a function or sourced script");
        return 2;
    }
    int status;
    if (!exit_status_argument("return", argc, argv, &status)) return status;
    shell.returning = 1;
    return status;
}

static int builtin_break(int argc, char **argv) {
    shell.break_level = argc > 1 ? atoi(argv[1]) : 1;
    if (shell.break_level < 1) shell.break_level = 1;
    return 0;
}

static int builtin_continue(int argc, char **argv) {
    shell.continue_level = argc > 1 ? atoi(argv[1]) : 1;
    if (shell.continue_level < 1) shell.continue_level = 1;
    return 0;
}

static int builtin_shift(int argc, char **argv) {
    int index = argc > 1 && strcmp(argv[1], "--") == 0 ? 2 : 1;
    long long count = 1;
    if (index < argc && !parse_number(argv[index], &count)) {
        shell_error("shift: %s: numeric argument required", argv[index]);
        return 1;
    }
    size_t available = shell.params.len > 0 ? shell.params.len - 1 : 0;
    if (count < 0) {
        shell_error("shift: %s: shift count out of range", argv[index]);
        return 1;
    }
    if ((unsigned long long)count > available) return 1;
    for (long long i = 0; i < count; i++) free(shell.params.items[1 + i]);
    memmove(shell.params.items + 1, shell.params.items + 1 + count,
            (available - (size_t)count) * sizeof(char *));
    shell.params.len -= (size_t)count;
    return 0;
}

static int builtin_eval(int argc, char **argv) {
    int first = options_only(argc, argv, "eval");
    if (first < 0) return 2;
    StrBuf sb;
    sb_init(&sb);
    for (int i = first; i < argc; i++) {
        if (i > first) sb_putc(&sb, ' ');
        sb_puts(&sb, argv[i]);
    }
    int status = exec_text(sb.data);
    sb_free(&sb);
    return status;
}

static int builtin_gitinfo(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char root[PATH_BUF];
    if (!git_repo_root(root, sizeof(root))) {
        printf("not a git repository\n");
        return 1;
    }
    path_to_slashes(root);
    const char *rows[][2] = {{"repository", git_repo_name() ? git_repo_name() : "?"},
                             {"root", root},
                             {"branch", git_branch() ? git_branch() : "detached"},
                             {"user", git_user() ? git_user() : "not configured"},
                             {"state", git_dirty() ? "dirty" : "clean"}};
    for (int i = 0; i < 5; i++)
        printf("  %s%-11s%s %s%s%s\n", style(S_LABEL), rows[i][0], style(S_RESET), style(S_VALUE),
               rows[i][1], style(S_RESET));
    return 0;
}

static int builtin_help(int argc, char **argv) {
    if (argc > 1) {
        int status = 0;
        for (int i = 1; i < argc; i++) status |= help_show(argv[i]);
        return status;
    }

    printf("\n  %s%s%s  %sFreSH%s %s%s%s\n", style(S_ACCENT), S_LAMBDA, style(S_RESET),
           style(S_HEADING), style(S_RESET), style(S_DIM), FRESH_VERSION, style(S_RESET));
    printf("  %sa zsh flavoured shell for Windows%s\n\n", style(S_DIM), style(S_RESET));
    int width = term_width();
    int columns = width / 14;
    if (columns < 1) columns = 1;

    const char *titles[] = {"Shell builtins:", "Bundled commands (used when no exe is on PATH):"};
    for (int group = 0; group < 2; group++) {
        StrList names;
        sl_init(&names);
        if (group == 0) builtin_names(&names);
        else coreutil_names(&names);
        sl_sort(&names);

        printf("  %s%s%s\n", style(S_LABEL), titles[group], style(S_RESET));
        for (size_t i = 0; i < names.len; i++) {
            printf("  %-12s", names.items[i]);
            if ((i + 1) % (size_t)columns == 0) putchar('\n');
        }
        if (names.len % (size_t)columns) putchar('\n');
        putchar('\n');
        sl_free(&names);
    }

    StrList described;
    sl_init(&described);
    help_described_names(&described);
    if (described.len > 0) {
        sl_sort(&described);
        printf("  %sDescribed by plugins:%s\n", style(S_LABEL), style(S_RESET));
        for (size_t i = 0; i < described.len; i++) {
            printf("  %-12s", described.items[i]);
            if ((i + 1) % (size_t)columns == 0) putchar('\n');
        }
        if (described.len % (size_t)columns) putchar('\n');
        putchar('\n');
    }
    sl_free(&described);

    printf("  %sLine editing%s\n", style(S_LABEL), style(S_RESET));
    printf("  Tab              complete commands, files and variables\n");
    printf("  Up / Down        history, filtered by what is already typed\n");
    printf("  Right / End      accept the inline history suggestion\n");
    printf("  Ctrl+R           search history\n");
    printf("  Ctrl+A / Ctrl+E  start and end of line\n");
    printf("  Ctrl+W / Backsp  delete the previous word\n");
    printf("  Ctrl+U / Ctrl+K  cut to start and to end\n");
    printf("  Ctrl+L           clear the screen\n");
    printf("  Ctrl+C           abandon the line, or the block you are part way through\n\n");
    printf("  %shelp <command> for what it does and the arguments it takes%s\n", style(S_DIM),
           style(S_RESET));
    printf("  %sConfiguration lives in ~/.freshrc%s\n\n", style(S_DIM), style(S_RESET));
    return 0;
}

typedef struct {
    const char *name;
    BuiltinFn fn;
} Builtin;

static const Builtin BUILTINS[] = {
    {"alias", builtin_alias},       {"break", builtin_break},
    {"cd", builtin_cd},             {"clear", builtin_clear},
    {"cmd", builtin_cmd},           {"ps1", builtin_ps1},
    {"continue", builtin_continue}, {"declare", builtin_declare},
    {"describe", builtin_describe},
    {"local", builtin_local},       {"typeset", builtin_declare},
    {"let", builtin_let},           {"getopts", builtin_getopts},
    {"pushd", builtin_pushd},       {"popd", builtin_popd},
    {"dirs", builtin_dirs},         {"mapfile", builtin_mapfile},
    {"readarray", builtin_mapfile}, {"shopt", builtin_shopt},
    {"command", builtin_command},   {"builtin", builtin_builtin},
    {"exec", builtin_exec},         {"readonly", builtin_readonly},
    {"z", builtin_jump},            {"copy", builtin_copy},
    {"bind", builtin_bind},
    {"paste", builtin_paste},       {"copypath", builtin_copypath},
    {"extract", builtin_extract},   {"admin", builtin_admin},
    {"die", builtin_die},
    {"echo", builtin_echo},         {"eval", builtin_eval},
    {"exit", builtin_exit},         {"have", builtin_have},
    {"ok", builtin_ok},             {"say", builtin_say},
    {"warn", builtin_warn},
    {"export", builtin_export},     {"false", builtin_false},       {"fresh", builtin_fresh},
    {"gitinfo", builtin_gitinfo},   {"help", builtin_help},
    {"history", builtin_history},   {"ls", builtin_ls},
    {"pwd", builtin_pwd},           {"quit", builtin_exit},
    {"read", builtin_read},         {"rehash", builtin_rehash},
    {"return", builtin_return},     {"set", builtin_set},
    {"shift", builtin_shift},       {"source", builtin_source},
    {"trap", builtin_trap},         {"jobs", builtin_jobs},
    {"wait", builtin_wait},         {"fg", builtin_fg},
    {"bg", builtin_bg},             {"stop", builtin_stop},
    {"disown", builtin_disown},     {"task", builtin_task},
    {"test", builtin_test},         {"theme", builtin_theme},
    {"plugin", builtin_plugin},     {"true", builtin_true},
    {"type", builtin_type},         {"unalias", builtin_unalias},
    {"unset", builtin_unset},       {"which", builtin_which},
    {"[", builtin_test},            {".", builtin_source},
    {":", builtin_true},            {"using", builtin_using},
    {"man", builtin_man},
    {"complete", builtin_complete_spec}, {"compgen", builtin_compgen},
    {"compopt", builtin_compopt},
    {"yield", builtin_yield},       {"action", builtin_action},
    {"_get_comp_words_by_ref", builtin_comp_words_by_ref},
    {"_filedir", builtin_filedir},
};

static Table builtin_table;

static void builtin_table_ready(void) {
    if (builtin_table.buckets) return;

    table_init(&builtin_table, sizeof(BUILTINS) / sizeof(BUILTINS[0]), 0, NULL);
    for (size_t i = 0; i < sizeof(BUILTINS) / sizeof(BUILTINS[0]); i++)
        table_put(&builtin_table, BUILTINS[i].name, (void *)&BUILTINS[i]);
}

BuiltinFn builtin_lookup(const char *name) {
    builtin_table_ready();
    const Builtin *entry = table_get(&builtin_table, name);
    return entry ? entry->fn : NULL;
}

int builtin_name_prefix(const char *prefix, size_t length) {
    for (size_t i = 0; i < sizeof(BUILTINS) / sizeof(BUILTINS[0]); i++) {
        if (_strnicmp(BUILTINS[i].name, prefix, length) == 0) return 1;
    }
    return 0;
}

void builtin_complete(const char *prefix, size_t length, StrList *out) {
    for (size_t i = 0; i < sizeof(BUILTINS) / sizeof(BUILTINS[0]); i++) {
        if (_strnicmp(BUILTINS[i].name, prefix, length) == 0) sl_push_copy(out, BUILTINS[i].name);
    }
}

void builtin_names(StrList *out) {
    for (size_t i = 0; i < sizeof(BUILTINS) / sizeof(BUILTINS[0]); i++) {
        if (BUILTINS[i].name[0] != '_') sl_push_copy(out, BUILTINS[i].name);
    }
}
