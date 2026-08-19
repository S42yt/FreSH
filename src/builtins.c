/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "builtins.h"

#include <ctype.h>
#include <direct.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "coreutils.h"
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
#include "term.h"
#include "theme.h"
#include "update.h"
#include "vars.h"

static void sync_cwd(void) {
    char cwd[PATH_BUF];
    if (GetCurrentDirectoryA(sizeof(cwd), cwd)) {
        var_set_exported("PWD", cwd);
        path_to_slashes(cwd);
        visits_record(cwd);
    }
    git_invalidate();
}

static int builtin_cd(int argc, char **argv) {
    const char *target;
    char previous[PATH_BUF];
    GetCurrentDirectoryA(sizeof(previous), previous);

    if (argc < 2) {
        target = var_get("HOME");
        if (!target) target = home_dir();
    } else if (strcmp(argv[1], "-") == 0) {
        target = var_get("OLDPWD");
        if (!target) {
            shell_error("cd: OLDPWD not set");
            return 1;
        }
        char shown[PATH_BUF];
        snprintf(shown, sizeof(shown), "%s", target);
        path_to_slashes(shown);
        printf("%s\n", shown);
    } else {
        target = argv[1];
    }

    char native[PATH_BUF];
    snprintf(native, sizeof(native), "%s", target);
    path_to_backslashes(native);

    if (!SetCurrentDirectoryA(native)) {
        shell_error("cd: %s: no such directory", target);
        return 1;
    }
    var_set_exported("OLDPWD", previous);
    sync_cwd();
    return 0;
}

static int builtin_pwd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char cwd[PATH_BUF];
    if (!GetCurrentDirectoryA(sizeof(cwd), cwd)) return 1;
    path_to_slashes(cwd);
    printf("%s\n", cwd);
    return 0;
}

static int builtin_exit(int argc, char **argv) {
    shell.running = 0;
    return argc > 1 ? atoi(argv[1]) : shell.last_status;
}

static void echo_escaped(const char *text) {
    for (const char *p = text; *p; p++) {
        if (*p != '\\' || !p[1]) {
            putchar(*p);
            continue;
        }
        p++;
        switch (*p) {
        case 'n': putchar('\n'); break;
        case 't': putchar('\t'); break;
        case 'r': putchar('\r'); break;
        case '0': putchar('\0'); break;
        case 'e': putchar('\x1b'); break;
        case '\\': putchar('\\'); break;
        default:
            putchar('\\');
            putchar(*p);
            break;
        }
    }
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
        if (escapes) echo_escaped(argv[i]);
        else fputs(argv[i], stdout);
    }
    if (newline) putchar('\n');
    return 0;
}

static int builtin_export(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "-p") == 0) {
        StrList names;
        sl_init(&names);
        vars_exported_names(&names);
        sl_sort(&names);
        for (size_t i = 0; i < names.len; i++)
            printf("export %s=\"%s\"\n", names.items[i], var_get(names.items[i]));
        sl_free(&names);
        return 0;
    }
    if (argc < 2) {
        StrList list;
        sl_init(&list);
        vars_list(&list);
        for (size_t i = 0; i < list.len; i++) printf("export %s\n", list.items[i]);
        sl_free(&list);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = '\0';
            var_set_exported(argv[i], eq + 1);
            *eq = '=';
        } else {
            var_export(argv[i]);
        }
        if (strcmp(argv[i], "PATH") == 0 || strncmp(argv[i], "PATH=", 5) == 0) path_rehash();
    }
    return 0;
}

static int builtin_unset(int argc, char **argv) {
    int functions_only = 0;
    int variables_only = 0;
    int index = 1;

    for (; index < argc && argv[index][0] == '-' && argv[index][1]; index++) {
        if (strchr(argv[index], 'f')) functions_only = 1;
        if (strchr(argv[index], 'v')) variables_only = 1;
    }

    for (int i = index; i < argc; i++) {
        if (!variables_only && function_undefine(argv[i])) continue;
        if (functions_only) continue;

        char *open = strchr(argv[i], '[');
        size_t length = strlen(argv[i]);
        if (open && open != argv[i] && argv[i][length - 1] == ']') {
            char *name = xstrndup(argv[i], (size_t)(open - argv[i]));
            char *key = xstrndup(open + 1, length - (size_t)(open - argv[i]) - 2);
            var_unset_element(name, key);
            free(name);
            free(key);
            continue;
        }
        var_unset(argv[i]);
    }
    return 0;
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
    for (; i < argc; i++) {
        char sign = argv[i][0];
        if (sign != '-' && sign != '+') break;
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        if (argv[i][1] == 'o' && i + 1 >= argc) {
            printf("%-12s%s\n", "errexit", shell.errexit ? "on" : "off");
            printf("%-12s%s\n", "nounset", shell.nounset ? "on" : "off");
            printf("%-12s%s\n", "xtrace", shell.xtrace ? "on" : "off");
            printf("%-12s%s\n", "pipefail", shell.pipefail ? "on" : "off");
            continue;
        }
        if (argv[i][1] == 'o') {
            const char *name = i + 1 < argc ? argv[++i] : "";
            int on = sign == '-';
            if (strcmp(name, "pipefail") == 0) shell.pipefail = on;
            else if (strcmp(name, "errexit") == 0) shell.errexit = on;
            else if (strcmp(name, "xtrace") == 0) shell.xtrace = on;
            else if (strcmp(name, "nounset") == 0) shell.nounset = on;
            continue;
        }
        for (const char *flag = argv[i] + 1; *flag; flag++) {
            if (*flag == 'e') shell.errexit = sign == '-';
            else if (*flag == 'x') shell.xtrace = sign == '-';
            else if (*flag == 'u') shell.nounset = sign == '-';
        }
    }

    int saw_dashes = i > 1 && strcmp(argv[i - 1], "--") == 0;
    if (saw_dashes || i < argc) {
        char *name = shell.params.len > 0 ? xstrdup(shell.params.items[0]) : xstrdup("FreSH");
        sl_free(&shell.params);
        sl_init(&shell.params);
        sl_push(&shell.params, name);
        for (; i < argc; i++) sl_push_copy(&shell.params, argv[i]);
    }
    return 0;
}

static void assign_word(char *word) {
    char *eq = strchr(word, '=');
    if (!eq) {
        if (!var_exists(word)) var_set(word, "");
        return;
    }
    *eq = '\0';
    size_t value_length = strlen(eq + 1);
    if (eq[1] == '(' && value_length >= 2 && eq[value_length] == ')') {
        char *inside = xstrndup(eq + 2, value_length - 2);
        StrList items;
        sl_init(&items);
        char *cursor = inside;
        char *item;
        while ((item = str_next_field(&cursor, ' ')) != NULL) {
            if (*item) sl_push_copy(&items, item);
        }
        var_set_array(word, &items, VAR_INDEXED);
        sl_free(&items);
        free(inside);
        *eq = '=';
        return;
    }
    char *open = strchr(word, '[');
    if (open && word[strlen(word) - 1] == ']') {
        *open = '\0';
        word[strlen(word) + strlen(open + 1)] = '\0';
        char index[128];
        snprintf(index, sizeof(index), "%s", open + 1);
        index[strcspn(index, "]")] = '\0';
        var_set_element(word, index, eq + 1);
    } else if (var_is_integer(word)) {
        int ok = 1;
        long number = eval_arith(eq + 1, &ok);
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%ld", number);
        var_set(word, buffer);
    } else {
        var_set(word, eq + 1);
    }
    *eq = '=';
}

static int builtin_let(int argc, char **argv) {
    long value = 0;
    int ok = 1;
    for (int i = 1; i < argc; i++) value = eval_arith(argv[i], &ok);
    if (!ok) return 2;
    return value != 0 ? 0 : 1;
}

static int builtin_local(int argc, char **argv) {
    int reference = 0;
    int index = 1;

    for (; index < argc && argv[index][0] == '-' && argv[index][1]; index++) {
        if (strchr(argv[index], 'n')) reference = 1;
    }

    for (int i = index; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        char *name = eq ? xstrndup(argv[i], (size_t)(eq - argv[i])) : xstrdup(argv[i]);
        var_make_local(name);
        if (eq) assign_word(argv[i]);
        if (reference) var_mark_nameref(name);
        free(name);
    }
    return 0;
}

static int builtin_declare(int argc, char **argv) {
    VarKind kind = VAR_SCALAR;
    int local_wanted = 0;
    int integer_wanted = 0;
    int reference_wanted = 0;
    int printing = 0;
    int index = 1;

    for (; index < argc && argv[index][0] == '-'; index++) {
        if (strchr(argv[index], 'p')) printing = 1;
        if (strchr(argv[index], 'A')) kind = VAR_ASSOC;
        if (strchr(argv[index], 'a')) kind = VAR_INDEXED;
        if (strchr(argv[index], 'i')) integer_wanted = 1;
        if (strchr(argv[index], 'n')) reference_wanted = 1;
        if (strchr(argv[index], 'g')) local_wanted = 0;
    }

    if (index >= argc) {
        StrList list;
        sl_init(&list);
        vars_list(&list);
        for (size_t i = 0; i < list.len; i++) printf("%s%s\n", printing ? "declare -- " : "", list.items[i]);
        sl_free(&list);
        return 0;
    }

    if (printing) {
        int status = 0;
        for (; index < argc; index++) {
            if (!var_exists(argv[index])) {
                shell_error("declare: %s: not found", argv[index]);
                status = 1;
                continue;
            }
            const char *flags = var_kind(argv[index]) == VAR_ASSOC   ? "-A"
                                : var_kind(argv[index]) == VAR_INDEXED ? "-a"
                                                                       : "--";
            printf("declare %s %s=\"%s\"\n", flags, argv[index], var_get(argv[index]));
        }
        return status;
    }

    for (; index < argc; index++) {
        char *eq = strchr(argv[index], '=');
        char *name = eq ? xstrndup(argv[index], (size_t)(eq - argv[index])) : xstrdup(argv[index]);
        if (local_wanted) var_make_local(name);
        if (kind != VAR_SCALAR) var_declare(name, kind);
        if (integer_wanted) var_mark_integer(name);
        if (reference_wanted) var_mark_nameref(name);
        free(name);
        if (eq) assign_word(argv[index]);
    }
    return 0;
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

static int builtin_trap(int argc, char **argv) {
    TrapSlot slots[8];
    int count = trap_table(slots);

    if (argc < 2 || strcmp(argv[1], "-p") == 0) {
        for (int i = 0; i < count; i++)
            if (*slots[i].slot) printf("trap -- '%s' %s\n", *slots[i].slot, slots[i].name);
        return 0;
    }

    const char *command = argv[1];
    int clearing = strcmp(command, "-") == 0 || !*command;

    if (argc == 2) {
        shell_error("trap: usage: trap command SIGNAL...");
        return 2;
    }

    for (int i = 2; i < argc; i++) {
        const char *given = argv[i];
        if (str_has_prefix(given, "SIG") || str_has_prefix(given, "sig")) given += 3;

        char **slot = NULL;
        for (int s = 0; s < count; s++) {
            if (str_ieq(given, slots[s].name) ||
                (slots[s].number && strcmp(given, slots[s].number) == 0))
                slot = slots[s].slot;
        }
        if (!slot) {
            shell_error("trap: %s: use EXIT HUP INT QUIT TERM ERR DEBUG or RETURN", argv[i]);
            return 1;
        }
        free(*slot);
        *slot = clearing ? NULL : xstrdup(command);
    }
    return 0;
}

static int builtin_fg(int argc, char **argv) {
    int id = argc > 1 ? atoi(argv[1]) : 0;
    return jobs_foreground(id);
}

static int builtin_bg(int argc, char **argv) {
    int id = argc > 1 ? atoi(argv[1]) : 0;
    return jobs_resume(id);
}

static int builtin_stop(int argc, char **argv) {
    int id = argc > 1 ? atoi(argv[1]) : 0;
    return jobs_suspend(id);
}

static int builtin_jobs(int argc, char **argv) {
    (void)argc;
    (void)argv;
    StrList lines;
    sl_init(&lines);
    jobs_list(&lines);

    for (size_t i = 0; i < lines.len; i++)
        printf("  %s%s%s\n", style(S_DIM), lines.items[i], style(S_RESET));
    sl_free(&lines);
    return 0;
}

static int builtin_wait(int argc, char **argv) {
    if (argc < 2) return jobs_wait_all();

    int status = 0;
    for (int i = 1; i < argc; i++) status = jobs_wait(atoi(argv[i]));
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
    if (argc < 2) {
        StrList list;
        sl_init(&list);
        alias_list(&list);
        for (size_t i = 0; i < list.len; i++) printf("alias %s\n", list.items[i]);
        sl_free(&list);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq) {
            const char *value = alias_get(argv[i]);
            if (value) printf("alias %s='%s'\n", argv[i], value);
            else {
                shell_error("alias: %s: not found", argv[i]);
                return 1;
            }
            continue;
        }
        *eq = '\0';
        alias_set(argv[i], eq + 1);
        *eq = '=';
    }
    return 0;
}

static int builtin_unalias(int argc, char **argv) {
    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (!alias_unset(argv[i])) {
            shell_error("unalias: %s: not found", argv[i]);
            status = 1;
        }
    }
    return status;
}

static int builtin_source(int argc, char **argv) {
    if (argc < 2) {
        shell_error("source: filename required");
        return 1;
    }
    char path[PATH_BUF];
    if (!path_is_file(argv[1]) && !resolve_command(argv[1], path, sizeof(path))) {
        shell_error("source: %s: not found", argv[1]);
        return 1;
    }
    if (path_is_file(argv[1])) snprintf(path, sizeof(path), "%s", argv[1]);

    StrList args;
    sl_init(&args);
    for (int i = 2; i < argc; i++) sl_push_copy(&args, argv[i]);
    int status = exec_script_file(path, argc > 2 ? &args : NULL);
    sl_free(&args);
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

static int describe_command(const char *name, int verbose) {
    if (alias_get(name)) {
        if (verbose) printf("%s is an alias for %s\n", name, alias_get(name));
        else printf("%s: aliased to %s\n", name, alias_get(name));
        return 0;
    }
    if (function_defined(name)) {
        printf("%s is a shell function\n", name);
        return 0;
    }
    if (builtin_lookup(name)) {
        printf("%s is a shell builtin\n", name);
        return 0;
    }
    char path[PATH_BUF];
    if (resolve_command(name, path, sizeof(path))) {
        path_to_slashes(path);
        printf("%s\n", path);
        return 0;
    }
    if (coreutil_lookup(name)) {
        printf("%s is a command bundled with FreSH\n", name);
        return 0;
    }
    shell_error("%s: not found", name);
    return 1;
}

static int builtin_which(int argc, char **argv) {
    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (describe_command(argv[i], 0) != 0) status = 1;
    }
    if (argc < 2) {
        shell_error("which: command required");
        return 1;
    }
    return status;
}

static const char *command_kind(const char *name) {
    if (alias_get(name)) return "alias";
    if (function_defined(name)) return "function";
    if (builtin_lookup(name) || coreutil_lookup(name)) return "builtin";

    char path[PATH_BUF];
    if (resolve_command(name, path, sizeof(path))) return "file";
    return NULL;
}

static int builtin_type(int argc, char **argv) {
    int brief = argc > 1 && strcmp(argv[1], "-t") == 0;
    int status = 0;

    for (int i = brief ? 2 : 1; i < argc; i++) {
        if (!brief) {
            if (describe_command(argv[i], 1) != 0) status = 1;
            continue;
        }
        const char *kind = command_kind(argv[i]);
        if (kind) printf("%s\n", kind);
        else status = 1;
    }
    return status;
}

static int builtin_command(int argc, char **argv) {
    int index = 1;
    int describe = 0;

    for (; index < argc && argv[index][0] == '-' && argv[index][1]; index++) {
        if (strchr(argv[index], 'v') || strchr(argv[index], 'V')) describe = 1;
    }
    if (index >= argc) return 0;

    if (describe) {
        int status = 0;
        for (int i = index; i < argc; i++) {
            const char *kind = command_kind(argv[i]);
            if (!kind) {
                status = 1;
                continue;
            }
            if (strcmp(kind, "file") == 0) {
                char path[PATH_BUF];
                resolve_command(argv[i], path, sizeof(path));
                path_to_slashes(path);
                printf("%s\n", path);
            } else {
                printf("%s\n", argv[i]);
            }
        }
        return status;
    }

    StrBuf line;
    sb_init(&line);
    for (int i = index; i < argc; i++) {
        if (i > index) sb_putc(&line, ' ');
        sb_puts(&line, argv[i]);
    }
    int status = exec_bypassing_functions(line.data);
    sb_free(&line);
    return status;
}

static int builtin_builtin(int argc, char **argv) {
    if (argc < 2) return 0;
    BuiltinFn fn = builtin_lookup(argv[1]);
    if (!fn) fn = coreutil_lookup(argv[1]);
    if (!fn) {
        shell_error("builtin: %s: not a shell builtin", argv[1]);
        return 1;
    }
    return fn(argc - 1, argv + 1);
}

static int builtin_exec(int argc, char **argv) {
    if (argc < 2) {
        exec_keep_redirections();
        return 0;
    }

    StrBuf line;
    sb_init(&line);
    for (int i = 1; i < argc; i++) {
        if (i > 1) sb_putc(&line, ' ');
        sb_puts(&line, argv[i]);
    }
    int status = exec_text(line.data);
    sb_free(&line);

    shell.running = 0;
    shell.last_status = status;
    return status;
}

static int builtin_readonly(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "-p") == 0) {
        StrList names;
        sl_init(&names);
        vars_readonly_names(&names);
        for (size_t i = 0; i < names.len; i++)
            printf("readonly %s=\"%s\"\n", names.items[i], var_get(names.items[i]));
        sl_free(&names);
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        char *name = eq ? xstrndup(argv[i], (size_t)(eq - argv[i])) : xstrdup(argv[i]);
        if (eq) assign_word(argv[i]);
        var_mark_readonly(name);
        free(name);
    }
    return 0;
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
    fresh_home_init();
    theme_load_configured();
    plugins_load_configured();
    return 0;
}

static int is_executable_name(const char *name) {
    const char *ext = path_ext(name);
    return str_ieq(ext, ".exe") || str_ieq(ext, ".bat") || str_ieq(ext, ".cmd") ||
           str_ieq(ext, ".com") || str_ieq(ext, ".ps1") || str_ieq(ext, ".sh") ||
           str_ieq(ext, ".msi");
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
    if (is_executable_name(data->cFileName)) return S_EXEC;
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
    if (is_executable_name(data->cFileName)) return '*';
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
           is_executable_name(data->cFileName) || is_dir ? 'x' : '-', style(S_RESET),
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
        shell_error("ls: %s: no such file or directory", target);
        return 1;
    }

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
        char marker = entry_marker(&data);
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

    if (directories + files > 0) {
        char size_text[32];
        human_size(total, size_text, sizeof(size_text));
        printf("%s%s %d director%s, %d file%s, %s%s\n", style(S_DIM), S_LAMBDA, directories,
               directories == 1 ? "y" : "ies", files, files == 1 ? "" : "s", size_text,
               style(S_RESET));
    }
    return 0;
}

static int test_file_op(char op, const char *path) {
    DWORD attributes = GetFileAttributesA(path);
    switch (op) {
    case 'e': return attributes != INVALID_FILE_ATTRIBUTES;
    case 'f': return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    case 'd': return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    case 'r': return attributes != INVALID_FILE_ATTRIBUTES;
    case 'w': return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_READONLY);
    case 'x': return attributes != INVALID_FILE_ATTRIBUTES;
    case 's': {
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) return 0;
        return info.nFileSizeLow > 0 || info.nFileSizeHigh > 0;
    }
    default: return 0;
    }
}

static int evaluate_test(int argc, char **argv) {
    if (argc == 0) return 1;
    if (argc == 1) return *argv[0] ? 0 : 1;

    if (strcmp(argv[0], "!") == 0) return evaluate_test(argc - 1, argv + 1) == 0 ? 1 : 0;

    if (argc == 2 && argv[0][0] == '-' && argv[0][2] == '\0') {
        char op = argv[0][1];
        if (op == 'z') return *argv[1] ? 1 : 0;
        if (op == 'n') return *argv[1] ? 0 : 1;
        return test_file_op(op, argv[1]) ? 0 : 1;
    }

    if (argc >= 3) {
        const char *left = argv[0];
        const char *op = argv[1];
        const char *right = argv[2];
        int result;

        if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) result = strcmp(left, right) == 0;
        else if (strcmp(op, "!=") == 0) result = strcmp(left, right) != 0;
        else if (strcmp(op, "-eq") == 0) result = atol(left) == atol(right);
        else if (strcmp(op, "-ne") == 0) result = atol(left) != atol(right);
        else if (strcmp(op, "-lt") == 0) result = atol(left) < atol(right);
        else if (strcmp(op, "-le") == 0) result = atol(left) <= atol(right);
        else if (strcmp(op, "-gt") == 0) result = atol(left) > atol(right);
        else if (strcmp(op, "-ge") == 0) result = atol(left) >= atol(right);
        else if (strcmp(op, "\\<") == 0 || strcmp(op, "<") == 0) result = strcmp(left, right) < 0;
        else if (strcmp(op, "\\>") == 0 || strcmp(op, ">") == 0) result = strcmp(left, right) > 0;
        else return 1;

        int status = result ? 0 : 1;
        if (argc > 3) {
            if (strcmp(argv[3], "-a") == 0) {
                int rest = evaluate_test(argc - 4, argv + 4);
                return status == 0 && rest == 0 ? 0 : 1;
            }
            if (strcmp(argv[3], "-o") == 0) {
                int rest = evaluate_test(argc - 4, argv + 4);
                return status == 0 || rest == 0 ? 0 : 1;
            }
        }
        return status;
    }
    return 1;
}

static int builtin_test(int argc, char **argv) {
    int count = argc - 1;
    if (strcmp(argv[0], "[") == 0) {
        if (count < 1 || strcmp(argv[argc - 1], "]") != 0) {
            shell_error("[: missing closing ]");
            return 2;
        }
        count--;
    }
    return evaluate_test(count, argv + 1);
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

static int builtin_read(int argc, char **argv) {
    int index = 1;
    const char *prompt = NULL;
    while (index < argc && argv[index][0] == '-' && argv[index][1]) {
        if (strcmp(argv[index], "-p") == 0 && index + 1 < argc) {
            prompt = argv[index + 1];
            index += 2;
        } else if (strchr(argv[index], 'r')) {
            index++;
        } else {
            index++;
        }
    }
    if (prompt) {
        fputs(prompt, stdout);
        fflush(stdout);
    }

    StrBuf input;
    sb_init(&input);
    fflush(stdout);
    if (read_line_fd(0, &input) == 0) {
        sb_free(&input);
        return 1;
    }
    char *line = sb_take(&input);

    if (index >= argc) {
        var_set("REPLY", line);
        free(line);
        return 0;
    }

    char *cursor = line;
    for (int i = index; i < argc; i++) {
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (i == argc - 1) {
            var_set(argv[i], cursor);
            break;
        }
        char *start = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t') cursor++;
        char saved = *cursor;
        *cursor = '\0';
        var_set(argv[i], start);
        if (saved) cursor++;
    }
    free(line);
    return 0;
}

static int *shopt_slot(const char *name) {
    if (strcmp(name, "globstar") == 0) return &shell.globstar;
    if (strcmp(name, "nullglob") == 0) return &shell.nullglob;
    if (strcmp(name, "dotglob") == 0) return &shell.dotglob;
    if (strcmp(name, "nocasematch") == 0) return &shell.nocasematch;
    if (strcmp(name, "extglob") == 0) return NULL;
    return NULL;
}

static int builtin_shopt(int argc, char **argv) {
    static const char *KNOWN[] = {"globstar", "nullglob", "dotglob", "extglob", "nocasematch", NULL};

    int setting = 0;
    int clearing = 0;
    int quiet = 0;
    int index = 1;

    for (; index < argc && argv[index][0] == '-'; index++) {
        if (strchr(argv[index], 's')) setting = 1;
        if (strchr(argv[index], 'u')) clearing = 1;
        if (strchr(argv[index], 'q')) quiet = 1;
    }

    if (index >= argc) {
        for (int i = 0; KNOWN[i]; i++) {
            int *slot = shopt_slot(KNOWN[i]);
            printf("%-12s%s\n", KNOWN[i], !slot || *slot ? "on" : "off");
        }
        return 0;
    }

    int status = 0;
    for (; index < argc; index++) {
        int known = 0;
        for (int i = 0; KNOWN[i]; i++)
            if (strcmp(KNOWN[i], argv[index]) == 0) known = 1;

        if (!known) {
            if (!quiet) shell_error("shopt: %s: unknown option", argv[index]);
            status = 1;
            continue;
        }

        int *slot = shopt_slot(argv[index]);
        if (setting && slot) *slot = 1;
        else if (clearing && slot) *slot = 0;
        else if (!setting && !clearing) {
            int on = !slot || *slot;
            if (!quiet) printf("%-12s%s\n", argv[index], on ? "on" : "off");
            if (!on) status = 1;
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

static int clipboard_open(void) {
    for (int attempt = 0; attempt < 10; attempt++) {
        if (OpenClipboard(NULL)) return 1;
        Sleep(20);
    }
    return 0;
}

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

    int status = 1;
    if (clipboard_open()) {
        EmptyClipboard();
        HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, text.len + 1);
        if (block) {
            memcpy(GlobalLock(block), text.data, text.len + 1);
            GlobalUnlock(block);
            SetClipboardData(CF_TEXT, block);
            status = 0;
        }
        CloseClipboard();
    }
    if (status) shell_error("copy: the clipboard would not open");
    sb_free(&text);
    return status;
}

static int builtin_paste(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (!clipboard_open()) {
        shell_error("paste: the clipboard would not open");
        return 1;
    }
    HANDLE data = GetClipboardData(CF_TEXT);
    if (data) {
        const char *text = GlobalLock(data);
        if (text) {
            printf("%s\n", text);
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
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
        sb_printf(&command,
                  "powershell.exe -NoLogo -NoProfile -Command \"Expand-Archive -LiteralPath '%s' "
                  "-DestinationPath '%s' -Force\"",
                  archive, into);
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
    if (argc < 3) {
        shell_error("getopts: usage: getopts optstring name [argument ...]");
        return 2;
    }

    const char *spec = argv[1];
    const char *name = argv[2];
    int silent = spec[0] == ':';
    if (silent) spec++;

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

    const char *inside = var_get("FRESH_OPTCHAR");
    long letter = inside ? atol(inside) : 1;
    if (letter < 1) letter = 1;

    int status = 1;
    var_set("OPTARG", "");

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

        const char *found = strchr(spec, option);
        char text[2] = {option, '\0'};

        if (!found || option == ':') {
            var_set(name, "?");
            if (silent) var_set("OPTARG", text);
            else shell_error("getopts: illegal option -- %c", option);
            status = 0;
            break;
        }

        if (found[1] == ':') {
            const char *value = NULL;
            if (word[letter]) {
                value = word + letter;
            } else if ((size_t)position < args.len) {
                position++;
                value = args.items[position - 1];
            }
            position++;
            letter = 1;

            if (!value) {
                var_set(name, silent ? ":" : "?");
                if (silent) var_set("OPTARG", text);
                else shell_error("getopts: option requires an argument -- %c", option);
                status = 0;
                break;
            }
            var_set("OPTARG", value);
        } else if (!word[letter]) {
            position++;
            letter = 1;
        }

        var_set(name, text);
        status = 0;
        break;
    }

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%ld", position);
    var_set("OPTIND", buffer);
    snprintf(buffer, sizeof(buffer), "%ld", letter);
    var_set("FRESH_OPTCHAR", buffer);

    sl_free(&args);
    return status;
}

static StrList directory_stack;

static int builtin_pushd(int argc, char **argv) {
    char cwd[PATH_BUF];
    if (!GetCurrentDirectoryA(sizeof(cwd), cwd)) return 1;
    path_to_slashes(cwd);

    const char *target = argc > 1 ? argv[1] : NULL;
    if (!target) {
        if (directory_stack.len == 0) {
            shell_error("pushd: no other directory");
            return 1;
        }
        char *previous = directory_stack.items[directory_stack.len - 1];
        char *swap = xstrdup(previous);
        free(previous);
        directory_stack.items[directory_stack.len - 1] = xstrdup(cwd);
        int failed = !SetCurrentDirectoryA(swap);
        if (failed) shell_error("pushd: %s: no such directory", swap);
        free(swap);
        return failed;
    }

    char native[PATH_BUF];
    snprintf(native, sizeof(native), "%s", target);
    path_to_backslashes(native);
    if (!SetCurrentDirectoryA(native)) {
        shell_error("pushd: %s: no such directory", target);
        return 1;
    }
    sl_push_copy(&directory_stack, cwd);
    return 0;
}

static int builtin_popd(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (directory_stack.len == 0) {
        shell_error("popd: the stack is empty");
        return 1;
    }
    char *target = directory_stack.items[--directory_stack.len];
    char native[PATH_BUF];
    snprintf(native, sizeof(native), "%s", target);
    path_to_backslashes(native);

    int failed = !SetCurrentDirectoryA(native);
    if (failed) shell_error("popd: %s: no such directory", target);
    free(target);
    return failed;
}

static int builtin_dirs(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "-c") == 0) {
        sl_clear(&directory_stack);
        return 0;
    }

    char cwd[PATH_BUF];
    if (GetCurrentDirectoryA(sizeof(cwd), cwd)) {
        path_to_slashes(cwd);
        printf("%s", cwd);
    }
    for (size_t i = directory_stack.len; i > 0; i--) printf(" %s", directory_stack.items[i - 1]);
    printf("\n");
    return 0;
}

static int builtin_mapfile(int argc, char **argv) {
    int strip = 0;
    int index = 1;
    for (; index < argc && argv[index][0] == '-'; index++) {
        if (strchr(argv[index], 't')) strip = 1;
    }
    const char *name = index < argc ? argv[index] : "MAPFILE";

    StrList rows;
    sl_init(&rows);
    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        if (strip) line[strcspn(line, "\r\n")] = '\0';
        sl_push_copy(&rows, line);
    }

    var_set_array(name, &rows, VAR_INDEXED);
    sl_free(&rows);
    return 0;
}

static int builtin_return(int argc, char **argv) {
    shell.returning = 1;
    return argc > 1 ? atoi(argv[1]) : shell.last_status;
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
    int count = argc > 1 ? atoi(argv[1]) : 1;
    for (int i = 0; i < count; i++) {
        if (shell.params.len <= 1) return 1;
        free(shell.params.items[1]);
        memmove(shell.params.items + 1, shell.params.items + 2,
                (shell.params.len - 2) * sizeof(char *));
        shell.params.len--;
    }
    return 0;
}

static int builtin_eval(int argc, char **argv) {
    StrBuf sb;
    sb_init(&sb);
    for (int i = 1; i < argc; i++) {
        if (i > 1) sb_putc(&sb, ' ');
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
    {"test", builtin_test},         {"theme", builtin_theme},
    {"plugin", builtin_plugin},     {"true", builtin_true},
    {"type", builtin_type},         {"unalias", builtin_unalias},
    {"unset", builtin_unset},       {"which", builtin_which},
    {"[", builtin_test},            {".", builtin_source},
    {":", builtin_true},
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
    for (size_t i = 0; i < sizeof(BUILTINS) / sizeof(BUILTINS[0]); i++)
        sl_push_copy(out, BUILTINS[i].name);
}
