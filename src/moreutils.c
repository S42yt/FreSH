/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "moreutils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <wincrypt.h>

#include "awk.h"
#include "exec.h"
#include "expand.h"
#include "regex.h"
#include "shell.h"
#include "table.h"
#include "update.h"
#include "style.h"
#include "util.h"

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

static int first_operand(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || !argv[i][1]) return i;
    }
    return argc;
}

static int flag_set(int argc, char **argv, char flag) {
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || !argv[i][1] || argv[i][1] == '-') continue;
        if (strchr(argv[i] + 1, flag)) return 1;
    }
    return 0;
}

static void read_lines(int argc, char **argv, int start, StrList *out) {
    int index = start;
    do {
        const char *name = index < argc ? argv[index] : NULL;
        FILE *f = open_input(name);
        if (f) {
            StrBuf line;
            sb_init(&line);
            while (read_line(f, &line) != 0) sl_push_copy(out, line.data);
            sb_free(&line);
            close_input(f);
        }
        index++;
    } while (index < argc);
}

static int more_nl(int argc, char **argv) {
    StrList lines;
    sl_init(&lines);
    read_lines(argc, argv, first_operand(argc, argv), &lines);
    for (size_t i = 0; i < lines.len; i++) printf("%6zu\t%s\n", i + 1, lines.items[i]);
    sl_free(&lines);
    return 0;
}

static int more_tac(int argc, char **argv) {
    StrList lines;
    sl_init(&lines);
    read_lines(argc, argv, first_operand(argc, argv), &lines);
    for (size_t i = lines.len; i > 0; i--) printf("%s\n", lines.items[i - 1]);
    sl_free(&lines);
    return 0;
}

static int more_rev(int argc, char **argv) {
    StrList lines;
    sl_init(&lines);
    read_lines(argc, argv, first_operand(argc, argv), &lines);
    for (size_t i = 0; i < lines.len; i++) {
        char *line = lines.items[i];
        size_t length = strlen(line);
        for (size_t c = length; c > 0; c--) putchar(line[c - 1]);
        putchar('\n');
    }
    sl_free(&lines);
    return 0;
}

static int more_yes(int argc, char **argv) {
    const char *text = argc > 1 ? argv[1] : "y";
    for (long i = 0; i < 100000; i++) {
        if (printf("%s\n", text) < 0) break;
    }
    return 0;
}

static int replace_once(const char *line, const char *pattern, const char *replacement, int global,
                        StrBuf *out) {
    const char *cursor = line;
    size_t pattern_length = strlen(pattern);
    int replaced = 0;

    while (*cursor) {
        const char *hit = pattern_length ? strstr(cursor, pattern) : NULL;
        if (!hit || (replaced && !global)) {
            sb_puts(out, cursor);
            break;
        }
        sb_putn(out, cursor, (size_t)(hit - cursor));
        sb_puts(out, replacement);
        cursor = hit + pattern_length;
        replaced = 1;
    }
    return replaced;
}

static int more_sed(int argc, char **argv) {
    int quiet = flag_set(argc, argv, 'n');
    int extended = flag_set(argc, argv, 'E') || flag_set(argc, argv, 'r');
    int index = first_operand(argc, argv);
    if (index >= argc) {
        shell_error("sed: usage: sed [-n] '[/address/]s/pattern/replacement/[g]' [file...]");
        return 2;
    }

    char *script = xstrdup(argv[index++]);

    StrList lines;
    sl_init(&lines);
    read_lines(argc, argv, index, &lines);

    char *cursor = script;
    StrBuf address;
    sb_init(&address);
    long address_line = 0;
    int addressed = 0;

    if (*cursor == '/') {
        char *close = strchr(cursor + 1, '/');
        if (!close) {
            shell_error("sed: unterminated address");
            sb_free(&address);
            sl_free(&lines);
            free(script);
            return 2;
        }
        *close = '\0';
        if (extended) sb_puts(&address, cursor + 1);
        else regex_bre_to_ere(cursor + 1, &address);
        addressed = 1;
        cursor = close + 1;
    } else if (isdigit((unsigned char)*cursor)) {
        address_line = strtol(cursor, &cursor, 10);
        addressed = 1;
    }

    char command = *cursor;
    char *pattern = NULL;
    char *replacement = NULL;
    int global = 0;

    if (command == 's' && cursor[1]) {
        char separator = cursor[1];
        pattern = cursor + 2;
        char *middle = strchr(pattern, separator);
        if (!middle) {
            shell_error("sed: unterminated s command");
            sb_free(&address);
            sl_free(&lines);
            free(script);
            return 2;
        }
        *middle = '\0';
        replacement = middle + 1;
        char *end = strchr(replacement, separator);
        if (end) {
            *end = '\0';
            global = strchr(end + 1, 'g') != NULL;
        }
    } else if (command != 'd' && command != 'p') {
        shell_error("sed: %c: only s, d and p are supported, with an optional /address/", command);
        sb_free(&address);
        sl_free(&lines);
        free(script);
        return 2;
    }

    StrBuf expression;
    sb_init(&expression);
    if (pattern) {
        if (extended) sb_puts(&expression, pattern);
        else regex_bre_to_ere(pattern, &expression);
    }

    for (size_t i = 0; i < lines.len; i++) {
        const char *line = lines.items[i];
        int selected = 1;
        if (addressed) {
            selected = address_line ? (long)(i + 1) == address_line
                                    : regex_search(address.data, line, NULL);
        }

        if (command == 'd') {
            if (!selected && !quiet) printf("%s\n", line);
            continue;
        }
        if (command == 'p') {
            if (!quiet) printf("%s\n", line);
            if (selected) printf("%s\n", line);
            continue;
        }

        if (!selected) {
            if (!quiet) printf("%s\n", line);
            continue;
        }

        StrBuf out;
        sb_init(&out);
        int replaced = regex_replace(expression.data, replacement, line, global, &out);
        if (!quiet || replaced) printf("%s\n", out.data);
        sb_free(&out);
    }

    sb_free(&expression);
    sb_free(&address);
    sl_free(&lines);
    free(script);
    return 0;
}

static int more_paste(int argc, char **argv) {
    int start = first_operand(argc, argv);
    int count = argc - start;
    if (count < 1) return 0;

    FILE **files = xmalloc((size_t)count * sizeof(FILE *));
    for (int i = 0; i < count; i++) files[i] = open_input(argv[start + i]);

    int active = 1;
    while (active) {
        active = 0;
        StrBuf row;
        sb_init(&row);
        StrBuf line;
        sb_init(&line);
        for (int i = 0; i < count; i++) {
            sb_clear(&line);
            if (files[i] && read_line(files[i], &line) != 0) active = 1;
            if (i > 0) sb_putc(&row, '\t');
            sb_puts(&row, line.data);
        }
        sb_free(&line);
        if (active) printf("%s\n", row.data);
        sb_free(&row);
    }

    for (int i = 0; i < count; i++) close_input(files[i]);
    free(files);
    return 0;
}

static int more_comm(int argc, char **argv) {
    int start = first_operand(argc, argv);
    if (argc - start < 2) {
        shell_error("comm: usage: comm file1 file2");
        return 2;
    }
    StrList left, right;
    sl_init(&left);
    sl_init(&right);
    char *names[2] = {argv[start], argv[start + 1]};
    for (int side = 0; side < 2; side++) {
        FILE *f = open_input(names[side]);
        if (!f) continue;
        StrBuf line;
        sb_init(&line);
        while (read_line(f, &line) != 0)
            sl_push_copy(side == 0 ? &left : &right, line.data);
        sb_free(&line);
        close_input(f);
    }

    size_t i = 0, j = 0;
    while (i < left.len || j < right.len) {
        int compared = i >= left.len ? 1
                       : j >= right.len ? -1
                                        : strcmp(left.items[i], right.items[j]);
        if (compared < 0) printf("%s\n", left.items[i++]);
        else if (compared > 0) printf("\t%s\n", right.items[j++]);
        else {
            printf("\t\t%s\n", left.items[i]);
            i++;
            j++;
        }
    }
    sl_free(&left);
    sl_free(&right);
    return 0;
}

static int more_shuf(int argc, char **argv) {
    StrList lines;
    sl_init(&lines);
    read_lines(argc, argv, first_operand(argc, argv), &lines);

    srand((unsigned)GetTickCount());
    for (size_t i = lines.len; i > 1; i--) {
        size_t j = (size_t)rand() % i;
        char *swap = lines.items[i - 1];
        lines.items[i - 1] = lines.items[j];
        lines.items[j] = swap;
    }
    for (size_t i = 0; i < lines.len; i++) printf("%s\n", lines.items[i]);
    sl_free(&lines);
    return 0;
}

static int more_fold(int argc, char **argv) {
    int width = 80;
    int start = 1;
    while (start < argc) {
        if (strcmp(argv[start], "-w") == 0 && start + 1 < argc) {
            width = atoi(argv[start + 1]);
            start += 2;
            continue;
        }
        if (argv[start][0] == '-' && isdigit((unsigned char)argv[start][1])) {
            width = atoi(argv[start] + 1);
            start++;
            continue;
        }
        if (argv[start][0] == '-' && argv[start][1]) {
            start++;
            continue;
        }
        break;
    }
    if (width < 1) width = 80;

    StrList lines;
    sl_init(&lines);
    read_lines(argc, argv, start, &lines);
    for (size_t i = 0; i < lines.len; i++) {
        const char *line = lines.items[i];
        size_t length = strlen(line);
        if (length == 0) {
            printf("\n");
            continue;
        }
        for (size_t offset = 0; offset < length; offset += (size_t)width)
            printf("%.*s\n", width, line + offset);
    }
    sl_free(&lines);
    return 0;
}

static int more_column(int argc, char **argv) {
    StrList lines;
    sl_init(&lines);
    read_lines(argc, argv, first_operand(argc, argv), &lines);

    size_t longest = 0;
    for (size_t i = 0; i < lines.len; i++) {
        size_t length = strlen(lines.items[i]);
        if (length > longest) longest = length;
    }
    int column_width = (int)longest + 2;
    int columns = column_width > 0 ? 80 / column_width : 1;
    if (columns < 1) columns = 1;

    for (size_t i = 0; i < lines.len; i++) {
        printf("%-*s", column_width, lines.items[i]);
        if ((i + 1) % (size_t)columns == 0) printf("\n");
    }
    if (lines.len % (size_t)columns) printf("\n");
    sl_free(&lines);
    return 0;
}

static int more_cmp(int argc, char **argv) {
    int start = first_operand(argc, argv);
    if (argc - start < 2) {
        shell_error("cmp: usage: cmp file1 file2");
        return 2;
    }
    FILE *a = open_input(argv[start]);
    FILE *b = open_input(argv[start + 1]);
    if (!a || !b) {
        close_input(a);
        close_input(b);
        return 2;
    }

    long position = 1;
    long line = 1;
    int status = 0;
    while (1) {
        int ca = fgetc(a);
        int cb = fgetc(b);
        if (ca == EOF && cb == EOF) break;
        if (ca != cb) {
            printf("%s %s differ: byte %ld, line %ld\n", argv[start], argv[start + 1], position,
                   line);
            status = 1;
            break;
        }
        if (ca == '\n') line++;
        position++;
    }
    close_input(a);
    close_input(b);
    return status;
}

static int more_diff(int argc, char **argv) {
    int start = first_operand(argc, argv);
    if (argc - start < 2) {
        shell_error("diff: usage: diff file1 file2");
        return 2;
    }

    StrList left, right;
    sl_init(&left);
    sl_init(&right);
    char *names[2] = {argv[start], argv[start + 1]};
    for (int side = 0; side < 2; side++) {
        FILE *f = open_input(names[side]);
        if (!f) {
            sl_free(&left);
            sl_free(&right);
            return 2;
        }
        StrBuf line;
        sb_init(&line);
        while (read_line(f, &line) != 0)
            sl_push_copy(side == 0 ? &left : &right, line.data);
        sb_free(&line);
        close_input(f);
    }

    int status = 0;
    size_t i = 0;
    while (i < left.len || i < right.len) {
        const char *a = i < left.len ? left.items[i] : NULL;
        const char *b = i < right.len ? right.items[i] : NULL;
        if (a && b && strcmp(a, b) == 0) {
            i++;
            continue;
        }
        status = 1;
        printf("%s%zu%s\n", style(S_DIM), i + 1, style(S_RESET));
        if (a) printf("%s< %s%s\n", style(S_ERROR), a, style(S_RESET));
        if (b) printf("%s> %s%s\n", style(S_ACCENT), b, style(S_RESET));
        i++;
    }
    sl_free(&left);
    sl_free(&right);
    return status;
}

static int more_expr(int argc, char **argv) {
    StrBuf expression;
    sb_init(&expression);
    for (int i = 1; i < argc; i++) {
        if (i > 1) sb_putc(&expression, ' ');
        sb_puts(&expression, argv[i]);
    }
    int ok = 1;
    long value = eval_arith(expression.data, &ok);
    sb_free(&expression);
    if (!ok) {
        shell_error("expr: invalid expression");
        return 2;
    }
    printf("%ld\n", value);
    return value != 0 ? 0 : 1;
}

static int more_stat(int argc, char **argv) {
    int status = 0;
    for (int i = first_operand(argc, argv); i < argc; i++) {
        char native[PATH_BUF];
        snprintf(native, sizeof(native), "%s", argv[i]);
        path_to_backslashes(native);

        WIN32_FILE_ATTRIBUTE_DATA info;
        if (!GetFileAttributesExA(native, GetFileExInfoStandard, &info)) {
            shell_error("stat: %s: no such file", argv[i]);
            status = 1;
            continue;
        }
        ULONGLONG size = ((ULONGLONG)info.nFileSizeHigh << 32) | info.nFileSizeLow;
        SYSTEMTIME modified;
        FILETIME local;
        FileTimeToLocalFileTime(&info.ftLastWriteTime, &local);
        FileTimeToSystemTime(&local, &modified);

        printf("  %sfile%s     %s\n", style(S_LABEL), style(S_RESET), argv[i]);
        printf("  %ssize%s     %llu\n", style(S_LABEL), style(S_RESET), size);
        printf("  %stype%s     %s\n", style(S_LABEL), style(S_RESET),
               (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "directory" : "file");
        printf("  %smodified%s %04d-%02d-%02d %02d:%02d:%02d\n", style(S_LABEL), style(S_RESET),
               modified.wYear, modified.wMonth, modified.wDay, modified.wHour, modified.wMinute,
               modified.wSecond);
    }
    return status;
}

static int more_df(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char drives[512];
    DWORD length = GetLogicalDriveStringsA(sizeof(drives), drives);
    if (!length) return 1;

    printf("  %s%-6s %10s %10s %10s%s\n", style(S_LABEL), "drive", "size", "used", "free",
           style(S_RESET));
    for (char *drive = drives; *drive; drive += strlen(drive) + 1) {
        ULARGE_INTEGER available, total, free_bytes;
        if (!GetDiskFreeSpaceExA(drive, &available, &total, &free_bytes)) continue;
        double gigabyte = 1024.0 * 1024.0 * 1024.0;
        printf("  %-6s %9.1fG %9.1fG %9.1fG\n", drive, total.QuadPart / gigabyte,
               (total.QuadPart - free_bytes.QuadPart) / gigabyte, free_bytes.QuadPart / gigabyte);
    }
    return 0;
}

static int more_ps(int argc, char **argv) {
    (void)argc;
    (void)argv;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 1;

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);
    printf("  %s%8s %8s  %s%s\n", style(S_LABEL), "pid", "threads", "name", style(S_RESET));

    if (Process32First(snapshot, &entry)) {
        do {
            printf("  %8lu %8lu  %s\n", entry.th32ProcessID, entry.cntThreads, entry.szExeFile);
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return 0;
}

static int more_pkill(int argc, char **argv) {
    int index = first_operand(argc, argv);
    if (index >= argc) {
        shell_error("pkill: usage: pkill name");
        return 2;
    }
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 1;

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);
    int killed = 0;

    if (Process32First(snapshot, &entry)) {
        do {
            if (!pattern_match(argv[index], entry.szExeFile) &&
                _stricmp(argv[index], entry.szExeFile) != 0)
                continue;
            HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
            if (!process) continue;
            if (TerminateProcess(process, 1)) killed++;
            CloseHandle(process);
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return killed > 0 ? 0 : 1;
}

static int more_id(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char user[256];
    DWORD size = sizeof(user);
    if (!win_user_name(user, &size)) return 1;

    char host[256];
    DWORD host_size = sizeof(host);
    if (!GetComputerNameA(host, &host_size)) snprintf(host, sizeof(host), "?");

    printf("user=%s host=%s admin=%s\n", user, host, IsUserAnAdmin() ? "yes" : "no");
    return 0;
}

static int more_groups(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("%s\n", IsUserAnAdmin() ? "users administrators" : "users");
    return 0;
}

static int more_mktemp(int argc, char **argv) {
    int directory_wanted = flag_set(argc, argv, 'd');
    char directory[PATH_BUF];
    if (!GetTempPathA(sizeof(directory), directory)) return 1;

    char file[PATH_BUF];
    if (!GetTempFileNameA(directory, "frsh", 0, file)) return 1;

    if (directory_wanted) {
        DeleteFileA(file);
        if (!path_mkdirs(file)) return 1;
    }
    path_to_slashes(file);
    printf("%s\n", file);
    return 0;
}

static int more_realpath(int argc, char **argv) {
    int status = 0;
    for (int i = first_operand(argc, argv); i < argc; i++) {
        char full[PATH_BUF];
        if (!GetFullPathNameA(argv[i], sizeof(full), full, NULL)) {
            shell_error("realpath: %s: cannot resolve", argv[i]);
            status = 1;
            continue;
        }
        path_to_slashes(full);
        printf("%s\n", full);
    }
    return status;
}

static int more_chmod(int argc, char **argv) {
    int index = first_operand(argc, argv);
    if (argc - index < 2) {
        shell_error("chmod: usage: chmod [+w|-w] file...");
        return 2;
    }
    const char *mode = argv[index++];
    int writable = strchr(mode, 'w') != NULL ? (mode[0] != '-') : 1;
    int status = 0;

    for (int i = index; i < argc; i++) {
        char native[PATH_BUF];
        snprintf(native, sizeof(native), "%s", argv[i]);
        path_to_backslashes(native);

        DWORD attributes = GetFileAttributesA(native);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            shell_error("chmod: %s: no such file", argv[i]);
            status = 1;
            continue;
        }
        if (writable) attributes &= ~(DWORD)FILE_ATTRIBUTE_READONLY;
        else attributes |= FILE_ATTRIBUTE_READONLY;
        if (!SetFileAttributesA(native, attributes)) status = 1;
    }
    return status;
}

static int more_ln(int argc, char **argv) {
    int symbolic = flag_set(argc, argv, 's');
    int index = first_operand(argc, argv);
    if (argc - index < 2) {
        shell_error("ln: usage: ln [-s] target link");
        return 2;
    }

    char target[PATH_BUF];
    char link[PATH_BUF];
    snprintf(target, sizeof(target), "%s", argv[index]);
    snprintf(link, sizeof(link), "%s", argv[index + 1]);
    path_to_backslashes(target);
    path_to_backslashes(link);

    if (symbolic) {
        DWORD flags = path_is_dir(target) ? 1 : 0;
        if (!CreateSymbolicLinkA(link, target, flags | 0x2)) {
            shell_error("ln: cannot create symlink (developer mode or admin rights needed)");
            return 1;
        }
        return 0;
    }
    if (!CreateHardLinkA(link, target, NULL)) {
        shell_error("ln: cannot create link");
        return 1;
    }
    return 0;
}

static int more_open(int argc, char **argv) {
    const char *target = argc > 1 ? argv[1] : ".";

    typedef HINSTANCE(WINAPI * OpenFn)(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT);
    HMODULE library = LoadLibraryA("shell32.dll");
    OpenFn shell_open =
        library ? (OpenFn)(void *)GetProcAddress(library, "ShellExecuteA") : NULL;
    if (!shell_open) {
        shell_error("open: %s: cannot open", target);
        return 1;
    }

    HINSTANCE result = shell_open(NULL, "open", target, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        shell_error("open: %s: cannot open", target);
        return 1;
    }
    return 0;
}

static int hash_file(const char *path, ALG_ID algorithm, char *out, size_t out_size) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    FILE *f = open_input(path);
    if (!f) return 0;

    if (!CryptAcquireContextA(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        close_input(f);
        return 0;
    }
    if (!CryptCreateHash(provider, algorithm, 0, 0, &hash)) {
        CryptReleaseContext(provider, 0);
        close_input(f);
        return 0;
    }

    unsigned char buffer[8192];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) CryptHashData(hash, buffer, (DWORD)n, 0);
    close_input(f);

    unsigned char digest[64];
    DWORD digest_size = sizeof(digest);
    int ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &digest_size, 0) != 0;

    if (ok) {
        size_t offset = 0;
        for (DWORD i = 0; i < digest_size && offset + 2 < out_size; i++)
            offset += (size_t)snprintf(out + offset, out_size - offset, "%02x", digest[i]);
    }
    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    return ok;
}

static int hash_command(int argc, char **argv, ALG_ID algorithm) {
    int index = first_operand(argc, argv);
    if (index >= argc) {
        shell_error("%s: a file name is required", argv[0]);
        return 2;
    }
    int status = 0;
    for (int i = index; i < argc; i++) {
        char digest[160] = "";
        if (!hash_file(argv[i], algorithm, digest, sizeof(digest))) {
            status = 1;
            continue;
        }
        printf("%s  %s\n", digest, argv[i]);
    }
    return status;
}

static int more_md5sum(int argc, char **argv) {
    return hash_command(argc, argv, CALG_MD5);
}

static int more_sha1sum(int argc, char **argv) {
    return hash_command(argc, argv, CALG_SHA1);
}

static int more_sha256sum(int argc, char **argv) {
    return hash_command(argc, argv, CALG_SHA_256);
}

static int more_file(int argc, char **argv) {
    int status = 0;
    for (int i = first_operand(argc, argv); i < argc; i++) {
        if (path_is_dir(argv[i])) {
            printf("%s: directory\n", argv[i]);
            continue;
        }
        FILE *f = fopen(argv[i], "rb");
        if (!f) {
            shell_error("file: %s: no such file", argv[i]);
            status = 1;
            continue;
        }
        unsigned char head[8] = {0};
        size_t n = fread(head, 1, sizeof(head), f);
        fclose(f);

        const char *kind = "data";
        if (n >= 2 && head[0] == 'M' && head[1] == 'Z') kind = "Windows executable";
        else if (n >= 4 && memcmp(head, "\x7f" "ELF", 4) == 0) kind = "ELF executable";
        else if (n >= 8 && memcmp(head, "\x89PNG", 4) == 0) kind = "PNG image";
        else if (n >= 3 && head[0] == 0xff && head[1] == 0xd8) kind = "JPEG image";
        else if (n >= 2 && head[0] == 'P' && head[1] == 'K') kind = "zip archive";
        else if (n >= 2 && head[0] == '#' && head[1] == '!') kind = "script";
        else {
            int text = 1;
            for (size_t c = 0; c < n; c++) {
                if (head[c] == 0) text = 0;
            }
            kind = text ? "text" : "binary data";
        }
        printf("%s: %s\n", argv[i], kind);
    }
    return status;
}

static int more_wget(int argc, char **argv) {
    int index = first_operand(argc, argv);
    if (index >= argc) {
        shell_error("wget: usage: wget [-O file] url");
        return 2;
    }

    const char *output = NULL;
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "-O") == 0 || strcmp(argv[i], "-o") == 0) output = argv[i + 1];
    }

    const char *url = argv[argc - 1];
    char derived[PATH_BUF];
    if (!output) {
        const char *slash = strrchr(url, '/');
        snprintf(derived, sizeof(derived), "%s", slash && slash[1] ? slash + 1 : "index.html");
        output = derived;
    }

    printf("%s%s%s -> %s\n", style(S_DIM), url, style(S_RESET), output);
    if (!http_download(url, output)) {
        shell_error("wget: could not fetch %s", url);
        return 1;
    }
    return 0;
}

typedef struct {
    const char *name;
    BuiltinFn fn;
} MoreUtil;

static const MoreUtil MOREUTILS[] = {
    {"chmod", more_chmod},   {"cmp", more_cmp},         {"column", more_column},
    {"comm", more_comm},     {"df", more_df},           {"diff", more_diff},
    {"expr", more_expr},     {"file", more_file},       {"fold", more_fold},
    {"groups", more_groups}, {"id", more_id},           {"ln", more_ln},
    {"md5sum", more_md5sum}, {"mktemp", more_mktemp},   {"nl", more_nl},
    {"open", more_open},     {"paste", more_paste},     {"pkill", more_pkill},
    {"ps", more_ps},         {"realpath", more_realpath}, {"rev", more_rev},
    {"sed", more_sed},       {"sha1sum", more_sha1sum}, {"sha256sum", more_sha256sum},
    {"shuf", more_shuf},     {"stat", more_stat},       {"tac", more_tac},
    {"wget", more_wget},     {"awk", awk_main},
    {"yes", more_yes},
};

static Table moreutil_table;

BuiltinFn moreutil_lookup(const char *name) {
    if (!moreutil_table.buckets) {
        table_init(&moreutil_table, sizeof(MOREUTILS) / sizeof(MOREUTILS[0]), 0, NULL);
        for (size_t i = 0; i < sizeof(MOREUTILS) / sizeof(MOREUTILS[0]); i++)
            table_put(&moreutil_table, MOREUTILS[i].name, (void *)&MOREUTILS[i]);
    }

    const MoreUtil *entry = table_get(&moreutil_table, name);
    return entry ? entry->fn : NULL;
}

void moreutil_names(StrList *out) {
    for (size_t i = 0; i < sizeof(MOREUTILS) / sizeof(MOREUTILS[0]); i++)
        sl_push_copy(out, MOREUTILS[i].name);
}

int moreutil_name_prefix(const char *prefix, size_t length) {
    for (size_t i = 0; i < sizeof(MOREUTILS) / sizeof(MOREUTILS[0]); i++) {
        if (_strnicmp(MOREUTILS[i].name, prefix, length) == 0) return 1;
    }
    return 0;
}

void moreutil_complete(const char *prefix, size_t length, StrList *out) {
    for (size_t i = 0; i < sizeof(MOREUTILS) / sizeof(MOREUTILS[0]); i++) {
        if (_strnicmp(MOREUTILS[i].name, prefix, length) == 0) sl_push_copy(out, MOREUTILS[i].name);
    }
}
