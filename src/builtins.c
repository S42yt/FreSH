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
#include "history.h"
#include "prompt.h"
#include "shell.h"
#include "style.h"
#include "term.h"
#include "theme.h"
#include "update.h"
#include "vars.h"

static void sync_cwd(void) {
    char cwd[PATH_BUF];
    if (GetCurrentDirectoryA(sizeof(cwd), cwd)) var_set_exported("PWD", cwd);
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
    for (int i = 1; i < argc; i++) var_unset(argv[i]);
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

    for (int i = 1; i < argc; i++) {
        char sign = argv[i][0];
        if (sign != '-' && sign != '+') continue;
        for (const char *flag = argv[i] + 1; *flag; flag++) {
            if (*flag == 'e') shell.errexit = sign == '-';
            else if (*flag == 'x') shell.xtrace = sign == '-';
        }
    }
    return 0;
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

static int builtin_type(int argc, char **argv) {
    int status = 0;
    for (int i = 1; i < argc; i++) {
        if (describe_command(argv[i], 1) != 0) status = 1;
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
    path_rehash();
    foreign_forget();
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
    if (argc > 2 && strcmp(argv[1], "-p") == 0) {
        prompt = argv[2];
        index = 3;
    }
    if (prompt) {
        fputs(prompt, stdout);
        fflush(stdout);
    }

    char line[4096];
    if (!fgets(line, sizeof(line), stdin)) return 1;
    line[strcspn(line, "\r\n")] = '\0';

    if (index >= argc) {
        var_set("REPLY", line);
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
    (void)argc;
    (void)argv;
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

    printf("  %sLine editing%s\n", style(S_LABEL), style(S_RESET));
    printf("  Tab              complete commands, files and variables\n");
    printf("  Up / Down        history, filtered by what is already typed\n");
    printf("  Right / End      accept the inline history suggestion\n");
    printf("  Ctrl+R           search history\n");
    printf("  Ctrl+A / Ctrl+E  start and end of line\n");
    printf("  Ctrl+W / Backsp  delete the previous word\n");
    printf("  Ctrl+U / Ctrl+K  cut to start and to end\n");
    printf("  Ctrl+L           clear the screen\n\n");
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
    {"continue", builtin_continue}, {"die", builtin_die},
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
    {"test", builtin_test},         {"theme", builtin_theme},
    {"plugin", builtin_plugin},     {"true", builtin_true},
    {"type", builtin_type},         {"unalias", builtin_unalias},
    {"unset", builtin_unset},       {"which", builtin_which},
    {"[", builtin_test},            {".", builtin_source},
};

BuiltinFn builtin_lookup(const char *name) {
    for (size_t i = 0; i < sizeof(BUILTINS) / sizeof(BUILTINS[0]); i++) {
        if (strcmp(BUILTINS[i].name, name) == 0) return BUILTINS[i].fn;
    }
    return NULL;
}

void builtin_names(StrList *out) {
    for (size_t i = 0; i < sizeof(BUILTINS) / sizeof(BUILTINS[0]); i++)
        sl_push_copy(out, BUILTINS[i].name);
}
