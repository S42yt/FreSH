/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "coreutils.h"

#include <ctype.h>
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <windows.h>

#include "exec.h"
#include "moreutils.h"
#include "regex.h"
#include "shell.h"
#include "util.h"
#include "vars.h"

#define LINE_MAX_LEN 8192

static FILE *open_input(const char *path) {
    if (!path || strcmp(path, "-") == 0) return stdin;
    FILE *f = fopen(path, "rb");
    if (!f) shell_error("%s: no such file", path);
    return f;
}

static void close_input(FILE *f) {
    if (f && f != stdin) fclose(f);
}

static void strip_newline(char *line) {
    line[strcspn(line, "\r\n")] = '\0';
}

static int flag_set(int argc, char **argv, char flag) {
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || !argv[i][1] || argv[i][1] == '-') continue;
        if (strchr(argv[i] + 1, flag)) return 1;
    }
    return 0;
}

static int first_operand(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || !argv[i][1]) return i;
    }
    return argc;
}

static int core_cat(int argc, char **argv) {
    int start = first_operand(argc, argv);
    int numbered = flag_set(argc, argv, 'n');
    int status = 0;
    int line_number = 0;

    if (start >= argc) {
        char buffer[4096];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), stdin)) > 0) fwrite(buffer, 1, n, stdout);
        return 0;
    }

    for (int i = start; i < argc; i++) {
        FILE *f = open_input(argv[i]);
        if (!f) {
            status = 1;
            continue;
        }
        if (numbered) {
            char line[LINE_MAX_LEN];
            while (fgets(line, sizeof(line), f)) printf("%6d  %s", ++line_number, line);
        } else {
            char buffer[4096];
            size_t n;
            while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) fwrite(buffer, 1, n, stdout);
        }
        close_input(f);
    }
    return status;
}

static int core_head_tail(int argc, char **argv, int is_head) {
    int count = 10;
    int start = 1;

    while (start < argc) {
        if (strcmp(argv[start], "-n") == 0 && start + 1 < argc) {
            count = atoi(argv[start + 1]);
            start += 2;
            continue;
        }
        if (argv[start][0] == '-' && isdigit((unsigned char)argv[start][1])) {
            count = atoi(argv[start] + 1);
            start++;
            continue;
        }
        if (argv[start][0] == '-' && argv[start][1]) {
            start++;
            continue;
        }
        break;
    }
    if (count <= 0) count = 10;

    int multiple = argc - start > 1;
    int status = 0;
    int i = start;

    do {
        const char *name = i < argc ? argv[i] : NULL;
        FILE *f = open_input(name);
        if (!f) {
            status = 1;
            i++;
            continue;
        }
        if (multiple) printf("==> %s <==\n", name);

        if (is_head) {
            char line[LINE_MAX_LEN];
            int printed = 0;
            while (printed < count && fgets(line, sizeof(line), f)) {
                fputs(line, stdout);
                printed++;
            }
        } else {
            char **ring = xmalloc((size_t)count * sizeof(char *));
            for (int r = 0; r < count; r++) ring[r] = NULL;
            int total = 0;
            char line[LINE_MAX_LEN];
            while (fgets(line, sizeof(line), f)) {
                int slot = total % count;
                free(ring[slot]);
                ring[slot] = xstrdup(line);
                total++;
            }
            int available = total < count ? total : count;
            for (int r = 0; r < available; r++) {
                int slot = (total - available + r) % count;
                if (ring[slot]) fputs(ring[slot], stdout);
            }
            for (int r = 0; r < count; r++) free(ring[r]);
            free(ring);
        }
        close_input(f);
        i++;
    } while (i < argc);
    return status;
}

static int core_head(int argc, char **argv) {
    return core_head_tail(argc, argv, 1);
}

static int core_tail(int argc, char **argv) {
    return core_head_tail(argc, argv, 0);
}

static int core_wc(int argc, char **argv) {
    int show_lines = flag_set(argc, argv, 'l');
    int show_words = flag_set(argc, argv, 'w');
    int show_bytes = flag_set(argc, argv, 'c');
    if (!show_lines && !show_words && !show_bytes) show_lines = show_words = show_bytes = 1;

    int start = first_operand(argc, argv);
    int status = 0;
    int index = start;

    do {
        const char *name = index < argc ? argv[index] : NULL;
        FILE *f = open_input(name);
        if (!f) {
            status = 1;
            index++;
            continue;
        }
        long lines = 0, words = 0, bytes = 0;
        int in_word = 0, c;
        while ((c = fgetc(f)) != EOF) {
            bytes++;
            if (c == '\n') lines++;
            if (isspace(c)) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
        close_input(f);

        if (show_lines) printf("%8ld", lines);
        if (show_words) printf("%8ld", words);
        if (show_bytes) printf("%8ld", bytes);
        if (name) printf(" %s", name);
        printf("\n");
        index++;
    } while (index < argc);
    return status;
}

static int line_matches(const char *line, const char *pattern, int ignore_case, int fixed) {
    if (fixed) {
        if (!ignore_case) return strstr(line, pattern) != NULL;
        size_t pattern_length = strlen(pattern);
        for (const char *p = line; *p; p++) {
            if (_strnicmp(p, pattern, pattern_length) == 0) return 1;
        }
        return 0;
    }

    if (!ignore_case) return regex_search(pattern, line, NULL);

    char lowered_line[LINE_MAX_LEN];
    char lowered_pattern[LINE_MAX_LEN];
    snprintf(lowered_line, sizeof(lowered_line), "%s", line);
    snprintf(lowered_pattern, sizeof(lowered_pattern), "%s", pattern);
    for (char *p = lowered_line; *p; p++) *p = (char)tolower((unsigned char)*p);
    for (char *p = lowered_pattern; *p; p++) *p = (char)tolower((unsigned char)*p);
    return regex_search(lowered_pattern, lowered_line, NULL);
}

static int core_grep(int argc, char **argv) {
    int ignore_case = flag_set(argc, argv, 'i');
    int invert = flag_set(argc, argv, 'v');
    int numbered = flag_set(argc, argv, 'n');
    int count_only = flag_set(argc, argv, 'c');
    int list_files = flag_set(argc, argv, 'l');
    int fixed = flag_set(argc, argv, 'F');

    int index = first_operand(argc, argv);
    if (index >= argc) {
        shell_error("grep: usage: grep [-ivncl] pattern [file...]");
        return 2;
    }
    const char *given = argv[index++];
    StrBuf expression;
    sb_init(&expression);
    if (fixed || flag_set(argc, argv, 'E')) sb_puts(&expression, given);
    else regex_bre_to_ere(given, &expression);
    const char *pattern = expression.data;
    int multiple = argc - index > 1;
    int found_any = 0;
    int status = 0;

    do {
        const char *name = index < argc ? argv[index] : NULL;
        FILE *f = open_input(name);
        if (!f) {
            status = 2;
            index++;
            continue;
        }
        char line[LINE_MAX_LEN];
        long number = 0;
        long matches = 0;
        while (fgets(line, sizeof(line), f)) {
            number++;
            strip_newline(line);
            int matched = line_matches(line, pattern, ignore_case, fixed);
            if (invert) matched = !matched;
            if (!matched) continue;
            matches++;
            found_any = 1;
            if (count_only || list_files) continue;
            if (multiple && name) printf("%s:", name);
            if (numbered) printf("%ld:", number);
            printf("%s\n", line);
        }
        close_input(f);

        if (count_only) {
            if (multiple && name) printf("%s:", name);
            printf("%ld\n", matches);
        }
        if (list_files && matches > 0 && name) printf("%s\n", name);
        index++;
    } while (index < argc);

    sb_free(&expression);
    return status ? status : (found_any ? 0 : 1);
}

static int compare_lines(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

static int compare_lines_numeric(const void *a, const void *b) {
    double left = atof(*(const char **)a);
    double right = atof(*(const char **)b);
    return left < right ? -1 : left > right ? 1 : 0;
}

static void read_all_lines(int argc, char **argv, int start, StrList *out) {
    int index = start;
    do {
        const char *name = index < argc ? argv[index] : NULL;
        FILE *f = open_input(name);
        if (f) {
            char line[LINE_MAX_LEN];
            while (fgets(line, sizeof(line), f)) {
                strip_newline(line);
                sl_push_copy(out, line);
            }
            close_input(f);
        }
        index++;
    } while (index < argc);
}

static int core_sort(int argc, char **argv) {
    int reverse = flag_set(argc, argv, 'r');
    int numeric = flag_set(argc, argv, 'n');
    int unique = flag_set(argc, argv, 'u');

    StrList lines;
    sl_init(&lines);
    read_all_lines(argc, argv, first_operand(argc, argv), &lines);

    if (lines.len > 1)
        qsort(lines.items, lines.len, sizeof(char *), numeric ? compare_lines_numeric : compare_lines);

    for (size_t i = 0; i < lines.len; i++) {
        size_t index = reverse ? lines.len - 1 - i : i;
        if (unique && i > 0) {
            size_t previous = reverse ? index + 1 : index - 1;
            if (strcmp(lines.items[index], lines.items[previous]) == 0) continue;
        }
        printf("%s\n", lines.items[index]);
    }
    sl_free(&lines);
    return 0;
}

static int core_uniq(int argc, char **argv) {
    int show_count = flag_set(argc, argv, 'c');
    StrList lines;
    sl_init(&lines);
    read_all_lines(argc, argv, first_operand(argc, argv), &lines);

    size_t i = 0;
    while (i < lines.len) {
        size_t run = 1;
        while (i + run < lines.len && strcmp(lines.items[i], lines.items[i + run]) == 0) run++;
        if (show_count) printf("%7zu %s\n", run, lines.items[i]);
        else printf("%s\n", lines.items[i]);
        i += run;
    }
    sl_free(&lines);
    return 0;
}

static int core_cut(int argc, char **argv) {
    char delimiter = '\t';
    int field = 0;
    int start = argc;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) delimiter = argv[++i][0];
        else if (strncmp(argv[i], "-d", 2) == 0 && argv[i][2]) delimiter = argv[i][2];
        else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) field = atoi(argv[++i]);
        else if (strncmp(argv[i], "-f", 2) == 0 && argv[i][2]) field = atoi(argv[i] + 2);
        else if (argv[i][0] != '-') {
            start = i;
            break;
        }
    }
    if (field < 1) {
        shell_error("cut: usage: cut -d C -f N [file...]");
        return 2;
    }

    StrList lines;
    sl_init(&lines);
    read_all_lines(argc, argv, start, &lines);

    for (size_t i = 0; i < lines.len; i++) {
        char *cursor = lines.items[i];
        char *piece = NULL;
        for (int f = 0; f < field; f++) {
            piece = str_next_field(&cursor, delimiter);
            if (!piece) break;
        }
        if (piece) printf("%s\n", piece);
    }
    sl_free(&lines);
    return 0;
}

static int core_tr(int argc, char **argv) {
    int deleting = flag_set(argc, argv, 'd');
    int index = first_operand(argc, argv);
    if (index >= argc) {
        shell_error("tr: usage: tr [-d] SET1 [SET2]");
        return 2;
    }
    const char *set1 = argv[index];
    const char *set2 = index + 1 < argc ? argv[index + 1] : "";
    size_t set2_length = strlen(set2);

    int c;
    while ((c = fgetc(stdin)) != EOF) {
        const char *hit = strchr(set1, c);
        if (!hit) {
            fputc(c, stdout);
            continue;
        }
        if (deleting) continue;
        size_t position = (size_t)(hit - set1);
        if (set2_length == 0) continue;
        fputc(set2[position < set2_length ? position : set2_length - 1], stdout);
    }
    return 0;
}

static int core_tee(int argc, char **argv) {
    int append = flag_set(argc, argv, 'a');
    int start = first_operand(argc, argv);
    int count = argc - start;

    FILE **files = count > 0 ? xmalloc((size_t)count * sizeof(FILE *)) : NULL;
    for (int i = 0; i < count; i++) {
        files[i] = fopen(argv[start + i], append ? "ab" : "wb");
        if (!files[i]) shell_error("tee: %s: cannot open", argv[start + i]);
    }

    int c;
    while ((c = fgetc(stdin)) != EOF) {
        fputc(c, stdout);
        for (int i = 0; i < count; i++)
            if (files[i]) fputc(c, files[i]);
    }
    for (int i = 0; i < count; i++)
        if (files[i]) fclose(files[i]);
    free(files);
    return 0;
}

static int remove_recursive(const char *path) {
    if (!path_is_dir(path)) return DeleteFileA(path) ? 0 : 1;

    char pattern[PATH_BUF];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
            char *child = path_join(path, data.cFileName);
            remove_recursive(child);
            free(child);
        } while (FindNextFileA(find, &data));
        FindClose(find);
    }
    return RemoveDirectoryA(path) ? 0 : 1;
}

static int core_rm(int argc, char **argv) {
    int recursive = flag_set(argc, argv, 'r') || flag_set(argc, argv, 'R');
    int force = flag_set(argc, argv, 'f');
    int status = 0;

    for (int i = first_operand(argc, argv); i < argc; i++) {
        char native[PATH_BUF];
        snprintf(native, sizeof(native), "%s", argv[i]);
        path_to_backslashes(native);

        if (path_is_dir(native)) {
            if (!recursive) {
                shell_error("rm: %s: is a directory", argv[i]);
                status = 1;
                continue;
            }
            if (remove_recursive(native) != 0 && !force) {
                shell_error("rm: %s: cannot remove", argv[i]);
                status = 1;
            }
            continue;
        }
        if (!DeleteFileA(native) && !force) {
            shell_error("rm: %s: no such file", argv[i]);
            status = 1;
        }
    }
    return status;
}

static int copy_recursive(const char *source, const char *destination) {
    if (!path_is_dir(source)) return CopyFileA(source, destination, FALSE) ? 0 : 1;

    path_mkdirs(destination);
    char pattern[PATH_BUF];
    snprintf(pattern, sizeof(pattern), "%s\\*", source);
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return 1;

    int status = 0;
    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        char *from = path_join(source, data.cFileName);
        char *to = path_join(destination, data.cFileName);
        status |= copy_recursive(from, to);
        free(from);
        free(to);
    } while (FindNextFileA(find, &data));
    FindClose(find);
    return status;
}

static void resolve_destination(const char *destination, const char *source, char *out,
                                size_t out_size) {
    if (!path_is_dir(destination)) {
        snprintf(out, out_size, "%s", destination);
        path_to_backslashes(out);
        return;
    }
    char native[PATH_BUF];
    snprintf(native, sizeof(native), "%s", source);
    path_to_backslashes(native);
    const char *leaf = strrchr(native, '\\');
    char *joined = path_join(destination, leaf ? leaf + 1 : native);
    snprintf(out, out_size, "%s", joined);
    free(joined);
    path_to_backslashes(out);
}

static int core_cp(int argc, char **argv) {
    int recursive = flag_set(argc, argv, 'r') || flag_set(argc, argv, 'R');
    int start = first_operand(argc, argv);
    if (argc - start < 2) {
        shell_error("cp: usage: cp [-r] source... destination");
        return 2;
    }
    const char *destination = argv[argc - 1];
    int status = 0;

    for (int i = start; i < argc - 1; i++) {
        char target[PATH_BUF];
        resolve_destination(destination, argv[i], target, sizeof(target));
        char source[PATH_BUF];
        snprintf(source, sizeof(source), "%s", argv[i]);
        path_to_backslashes(source);

        if (path_is_dir(source)) {
            if (!recursive) {
                shell_error("cp: %s: is a directory", argv[i]);
                status = 1;
                continue;
            }
            status |= copy_recursive(source, target);
        } else if (!CopyFileA(source, target, FALSE)) {
            shell_error("cp: %s: cannot copy", argv[i]);
            status = 1;
        }
    }
    return status;
}

static int core_mv(int argc, char **argv) {
    int start = first_operand(argc, argv);
    if (argc - start < 2) {
        shell_error("mv: usage: mv source... destination");
        return 2;
    }
    const char *destination = argv[argc - 1];
    int status = 0;

    for (int i = start; i < argc - 1; i++) {
        char target[PATH_BUF];
        resolve_destination(destination, argv[i], target, sizeof(target));
        char source[PATH_BUF];
        snprintf(source, sizeof(source), "%s", argv[i]);
        path_to_backslashes(source);

        if (!MoveFileExA(source, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            shell_error("mv: %s: cannot move", argv[i]);
            status = 1;
        }
    }
    return status;
}

static int core_mkdir(int argc, char **argv) {
    int parents = flag_set(argc, argv, 'p');
    int status = 0;

    for (int i = first_operand(argc, argv); i < argc; i++) {
        char native[PATH_BUF];
        snprintf(native, sizeof(native), "%s", argv[i]);
        path_to_backslashes(native);
        if (parents) {
            if (!path_mkdirs(native)) status = 1;
        } else if (_mkdir(native) != 0) {
            shell_error("mkdir: %s: cannot create", argv[i]);
            status = 1;
        }
    }
    return status;
}

static int core_rmdir(int argc, char **argv) {
    int status = 0;
    for (int i = first_operand(argc, argv); i < argc; i++) {
        char native[PATH_BUF];
        snprintf(native, sizeof(native), "%s", argv[i]);
        path_to_backslashes(native);
        if (!RemoveDirectoryA(native)) {
            shell_error("rmdir: %s: cannot remove", argv[i]);
            status = 1;
        }
    }
    return status;
}

static int core_touch(int argc, char **argv) {
    int status = 0;
    for (int i = first_operand(argc, argv); i < argc; i++) {
        char native[PATH_BUF];
        snprintf(native, sizeof(native), "%s", argv[i]);
        path_to_backslashes(native);

        HANDLE handle = CreateFileA(native, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            shell_error("touch: %s: cannot create", argv[i]);
            status = 1;
            continue;
        }
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        SetFileTime(handle, NULL, &now, &now);
        CloseHandle(handle);
    }
    return status;
}

static int core_basename(int argc, char **argv) {
    if (argc < 2) {
        shell_error("basename: usage: basename path [suffix]");
        return 2;
    }
    char native[PATH_BUF];
    snprintf(native, sizeof(native), "%s", argv[1]);
    path_to_backslashes(native);

    size_t length = strlen(native);
    while (length > 1 && native[length - 1] == '\\') native[--length] = '\0';

    const char *leaf = strrchr(native, '\\');
    leaf = leaf ? leaf + 1 : native;

    char result[PATH_BUF];
    snprintf(result, sizeof(result), "%s", leaf);
    if (argc > 2 && str_has_suffix_i(result, argv[2]))
        result[strlen(result) - strlen(argv[2])] = '\0';
    printf("%s\n", result);
    return 0;
}

static int core_dirname(int argc, char **argv) {
    if (argc < 2) {
        shell_error("dirname: usage: dirname path");
        return 2;
    }
    char native[PATH_BUF];
    snprintf(native, sizeof(native), "%s", argv[1]);
    path_to_backslashes(native);

    size_t length = strlen(native);
    while (length > 1 && native[length - 1] == '\\') native[--length] = '\0';

    char *leaf = strrchr(native, '\\');
    if (!leaf) {
        printf(".\n");
        return 0;
    }
    if (leaf == native) leaf[1] = '\0';
    else *leaf = '\0';
    path_to_slashes(native);
    printf("%s\n", native);
    return 0;
}

static int core_seq(int argc, char **argv) {
    double first = 1, increment = 1, last = 0;
    int start = first_operand(argc, argv);
    int count = argc - start;

    if (count == 1) last = atof(argv[start]);
    else if (count == 2) {
        first = atof(argv[start]);
        last = atof(argv[start + 1]);
    } else if (count >= 3) {
        first = atof(argv[start]);
        increment = atof(argv[start + 1]);
        last = atof(argv[start + 2]);
    } else {
        shell_error("seq: usage: seq [first [increment]] last");
        return 2;
    }
    if (increment == 0) return 1;

    for (double value = first; increment > 0 ? value <= last : value >= last; value += increment) {
        if (value == (long long)value) printf("%lld\n", (long long)value);
        else printf("%g\n", value);
    }
    return 0;
}

static int core_env(int argc, char **argv) {
    int start = first_operand(argc, argv);
    if (start >= argc) {
        StrList list;
        sl_init(&list);
        vars_list(&list);
        for (size_t i = 0; i < list.len; i++) printf("%s\n", list.items[i]);
        sl_free(&list);
        return 0;
    }

    StrBuf command;
    sb_init(&command);
    for (int i = start; i < argc; i++) {
        if (i > start) sb_putc(&command, ' ');
        sb_puts(&command, argv[i]);
    }
    int status = exec_text(command.data);
    sb_free(&command);
    return status;
}

static int core_date(int argc, char **argv) {
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    const char *format = "%a %b %d %H:%M:%S %Y";
    if (argc > 1 && argv[1][0] == '+') format = argv[1] + 1;

    char buffer[512];
    strftime(buffer, sizeof(buffer), format, local);
    printf("%s\n", buffer);
    return 0;
}

static int core_sleep(int argc, char **argv) {
    if (argc < 2) return 0;
    double seconds = atof(argv[1]);
    if (seconds > 0) Sleep((DWORD)(seconds * 1000));
    return 0;
}

static int core_whoami(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char user[256];
    DWORD size = sizeof(user);
    if (!GetUserNameA(user, &size)) return 1;
    printf("%s\n", user);
    return 0;
}

static int core_hostname(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char host[256];
    DWORD size = sizeof(host);
    if (!GetComputerNameA(host, &size)) return 1;
    printf("%s\n", host);
    return 0;
}

static int core_uname(int argc, char **argv) {
    int all = flag_set(argc, argv, 'a');
    if (!all) {
        printf("Windows\n");
        return 0;
    }
    char host[256];
    DWORD size = sizeof(host);
    if (!GetComputerNameA(host, &size)) snprintf(host, sizeof(host), "unknown");

    SYSTEM_INFO system;
    GetNativeSystemInfo(&system);
    const char *architecture =
        system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x86_64"
        : system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "arm64"
                                                                        : "x86";
    printf("Windows %s %s\n", host, architecture);
    return 0;
}

static ULONGLONG directory_size(const char *path) {
    char pattern[PATH_BUF];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return 0;

    ULONGLONG total = 0;
    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char *child = path_join(path, data.cFileName);
            total += directory_size(child);
            free(child);
        } else {
            total += ((ULONGLONG)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        }
    } while (FindNextFileA(find, &data));
    FindClose(find);
    return total;
}

static int core_du(int argc, char **argv) {
    int human = flag_set(argc, argv, 'h');
    int start = first_operand(argc, argv);
    const char *target = start < argc ? argv[start] : ".";

    char native[PATH_BUF];
    snprintf(native, sizeof(native), "%s", target);
    path_to_backslashes(native);

    ULONGLONG total = path_is_dir(native) ? directory_size(native) : 0;
    if (!path_is_dir(native)) {
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (GetFileAttributesExA(native, GetFileExInfoStandard, &info))
            total = ((ULONGLONG)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    }

    if (human) {
        const char *units[] = {"B", "K", "M", "G", "T"};
        double value = (double)total;
        int unit = 0;
        while (value >= 1024 && unit < 4) {
            value /= 1024;
            unit++;
        }
        printf("%.1f%s\t%s\n", value, units[unit], target);
    } else {
        printf("%llu\t%s\n", total / 1024, target);
    }
    return 0;
}

static void find_walk(const char *directory, const char *name_pattern, char type) {
    char pattern[PATH_BUF];
    snprintf(pattern, sizeof(pattern), "%s\\*", directory);
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return;

    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        char *child = path_join(directory, data.cFileName);
        int is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        int type_ok = type == 0 || (type == 'd' && is_dir) || (type == 'f' && !is_dir);
        int name_ok = !name_pattern || pattern_match(name_pattern, data.cFileName);
        if (type_ok && name_ok) {
            char shown[PATH_BUF];
            snprintf(shown, sizeof(shown), "%s", child);
            path_to_slashes(shown);
            printf("%s\n", shown);
        }
        if (is_dir) find_walk(child, name_pattern, type);
        free(child);
    } while (FindNextFileA(find, &data));
    FindClose(find);
}

static int core_find(int argc, char **argv) {
    const char *root = ".";
    const char *name_pattern = NULL;
    char type = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-name") == 0 && i + 1 < argc) name_pattern = argv[++i];
        else if (strcmp(argv[i], "-type") == 0 && i + 1 < argc) type = argv[++i][0];
        else if (argv[i][0] != '-') root = argv[i];
    }

    char native[PATH_BUF];
    snprintf(native, sizeof(native), "%s", root);
    path_to_backslashes(native);
    find_walk(native, name_pattern, type);
    return 0;
}

static void printf_escapes(const char *s) {
    for (const char *p = s; *p; p++) {
        if (*p != '\\' || !p[1]) {
            putchar(*p);
            continue;
        }
        p++;
        switch (*p) {
        case 'n': putchar('\n'); break;
        case 't': putchar('\t'); break;
        case 'r': putchar('\r'); break;
        case 'a': putchar(7); break;
        case 'b': putchar(8); break;
        case 'f': putchar(12); break;
        case 'v': putchar(11); break;
        case 'e': putchar(27); break;
        case '\\': putchar('\\'); break;
        default: putchar('\\'); putchar(*p); break;
        }
    }
}

static int core_printf(int argc, char **argv) {
    if (argc < 2) return 2;
    const char *format = argv[1];
    int next = 2;
    int has_conversion = 0;

    do {
        for (const char *p = format; *p; p++) {
            if (*p == '\\' && p[1]) {
                p++;
                switch (*p) {
                case 'n': putchar('\n'); break;
                case 't': putchar('\t'); break;
                case 'r': putchar('\r'); break;
                case 'a': putchar(7); break;
                case 'b': putchar(8); break;
                case 'f': putchar(12); break;
                case 'v': putchar(11); break;
                case 'e': putchar(27); break;
                case '\\': putchar('\\'); break;
                default: putchar(*p); break;
                }
                continue;
            }
            if (*p != '%') {
                putchar(*p);
                continue;
            }
            if (p[1] == '%') {
                putchar('%');
                p++;
                continue;
            }

            char spec[64];
            size_t s = 0;
            spec[s++] = '%';
            p++;
            while (*p && strchr("-+ #0", *p) && s < sizeof(spec) - 4) spec[s++] = *p++;
            while (isdigit((unsigned char)*p) && s < sizeof(spec) - 4) spec[s++] = *p++;
            if (*p == '.') {
                spec[s++] = *p++;
                while (isdigit((unsigned char)*p) && s < sizeof(spec) - 4) spec[s++] = *p++;
            }
            char conv = *p;
            if (!conv) break;
            has_conversion = 1;
            const char *value = next < argc ? argv[next++] : "";

            switch (conv) {
            case 'd':
            case 'i':
                spec[s++] = 'l';
                spec[s++] = 'd';
                spec[s] = '\0';
                printf(spec, strtol(value, NULL, 0));
                break;
            case 'o':
            case 'u':
            case 'x':
            case 'X':
                spec[s++] = 'l';
                spec[s++] = conv;
                spec[s] = '\0';
                printf(spec, strtoul(value, NULL, 0));
                break;
            case 'f':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
                spec[s++] = conv;
                spec[s] = '\0';
                printf(spec, atof(value));
                break;
            case 'c':
                spec[s++] = 'c';
                spec[s] = '\0';
                printf(spec, value[0]);
                break;
            case 'b':
                printf_escapes(value);
                break;
            case 's':
            default:
                spec[s++] = 's';
                spec[s] = '\0';
                printf(spec, value);
                break;
            }
        }
    } while (next < argc && has_conversion);
    return 0;
}

static int core_kill(int argc, char **argv) {
    int status = 0;
    for (int i = first_operand(argc, argv); i < argc; i++) {
        DWORD pid = (DWORD)atoi(argv[i]);
        HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (!process) {
            shell_error("kill: %s: no such process", argv[i]);
            status = 1;
            continue;
        }
        if (!TerminateProcess(process, 1)) {
            shell_error("kill: %s: cannot terminate", argv[i]);
            status = 1;
        }
        CloseHandle(process);
    }
    return status;
}

static int core_xargs(int argc, char **argv) {
    StrBuf command;
    sb_init(&command);
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (i > 1) sb_putc(&command, ' ');
            sb_puts(&command, argv[i]);
        }
    } else {
        sb_puts(&command, "echo");
    }

    char line[LINE_MAX_LEN];
    while (fgets(line, sizeof(line), stdin)) {
        strip_newline(line);
        if (!*line) continue;
        sb_putc(&command, ' ');
        sb_putc(&command, '"');
        sb_puts(&command, line);
        sb_putc(&command, '"');
    }

    int status = exec_text(command.data);
    sb_free(&command);
    return status;
}

typedef struct {
    const char *name;
    BuiltinFn fn;
} Coreutil;

static const Coreutil COREUTILS[] = {
    {"basename", core_basename}, {"cat", core_cat},         {"cp", core_cp},
    {"cut", core_cut},           {"date", core_date},       {"dirname", core_dirname},
    {"du", core_du},             {"env", core_env},         {"find", core_find},
    {"grep", core_grep},         {"head", core_head},       {"hostname", core_hostname},
    {"kill", core_kill},         {"mkdir", core_mkdir},     {"mv", core_mv},
    {"printf", core_printf},     {"rm", core_rm},           {"rmdir", core_rmdir},
    {"seq", core_seq},           {"sleep", core_sleep},     {"sort", core_sort},
    {"tail", core_tail},         {"tee", core_tee},         {"touch", core_touch},
    {"tr", core_tr},             {"uname", core_uname},     {"uniq", core_uniq},
    {"wc", core_wc},             {"whoami", core_whoami},   {"xargs", core_xargs},
};

int coreutil_preferred(const char *name) {
    static const char *SHADOWED[] = {"find", "sort", "more", "where", NULL};
    for (int i = 0; SHADOWED[i]; i++) {
        if (strcmp(SHADOWED[i], name) == 0) return coreutil_lookup(name) != NULL;
    }
    return 0;
}

BuiltinFn coreutil_lookup(const char *name) {
    for (size_t i = 0; i < sizeof(COREUTILS) / sizeof(COREUTILS[0]); i++) {
        if (strcmp(COREUTILS[i].name, name) == 0) return COREUTILS[i].fn;
    }
    return moreutil_lookup(name);
}

void coreutil_names(StrList *out) {
    for (size_t i = 0; i < sizeof(COREUTILS) / sizeof(COREUTILS[0]); i++)
        sl_push_copy(out, COREUTILS[i].name);
    moreutil_names(out);
}
