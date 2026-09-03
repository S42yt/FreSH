/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "commands.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "platform.h"

#ifndef _WIN32
#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <sys/statvfs.h>
#endif

#include "exec.h"
#include "regex.h"
#include "shell.h"
#include "vars.h"

#define END(list) (sizeof(list) / sizeof(list[0]))

static int parse_or_exit(int argc, char **argv, const OptSpec *specs, const char *tool, Args *args,
                         int failure) {
    int parsed = args_parse(argc, argv, specs, tool, args);
    if (parsed == ARGS_OK) return -1;
    return parsed == ARGS_DONE ? 0 : failure;
}

static void strip_trailing_separators(char *path) {
    size_t length = strlen(path);
    while (length > 1 && (path[length - 1] == '/' || path[length - 1] == '\\') &&
           path[length - 2] != ':')
        path[--length] = '\0';
}

static int exists(const char *path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

typedef int (*EntryVisitor)(const char *directory, const char *name, int is_dir, void *ctx);

static int list_directory(const char *directory, StrList *names) {
    char pattern[PATH_BUF];
    snprintf(pattern, sizeof(pattern), "%s" PATH_SEP_STR "*", directory);
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return 0;
    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        sl_push_copy(names, data.cFileName);
    } while (FindNextFileA(find, &data));
    FindClose(find);
    sl_sort(names);
    return 1;
}

#ifndef _WIN32

static int delete_entry(const char *path, int directory) {
    return (directory ? rmdir(path) : unlink(path)) == 0 ? 0 : 1;
}

#else

typedef struct {
    DWORD Flags;
} DispositionEx;

#define DISPOSE_DELETE 0x1
#define DISPOSE_POSIX 0x2
#define DISPOSE_IGNORE_READONLY 0x10
#define INFO_DISPOSITION_EX 21

static int delete_entry(const char *path, int directory) {
    HANDLE handle = CreateFileA(path, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (handle != INVALID_HANDLE_VALUE) {
        DispositionEx ex = {DISPOSE_DELETE | DISPOSE_POSIX | DISPOSE_IGNORE_READONLY};
        BOOL done = SetFileInformationByHandle(handle, (FILE_INFO_BY_HANDLE_CLASS)INFO_DISPOSITION_EX,
                                               &ex, sizeof(ex));
        CloseHandle(handle);
        if (done) return 0;
    }

    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    if (directory) {
        for (int attempt = 0; attempt < 10; attempt++) {
            if (RemoveDirectoryA(path)) return 0;
            if (GetLastError() != ERROR_DIR_NOT_EMPTY) break;
            Sleep(10);
        }
        return 1;
    }
    return DeleteFileA(path) ? 0 : 1;
}

#endif

typedef struct {
    int force;
    int verbose;
    int recursive;
    int empty_dirs;
    int status;
} RmState;

static void rm_path(RmState *rm, const char *path, const char *shown) {
    if (!exists(path)) {
        if (!rm->force) {
            cmd_error("rm", "cannot remove '%s': No such file or directory", shown);
            rm->status = 1;
        }
        return;
    }
    if (path_is_dir(path)) {
        if (rm->recursive) {
            StrList names;
            sl_init(&names);
            list_directory(path, &names);
            for (size_t i = 0; i < names.len; i++) {
                char *child = path_join(path, names.items[i]);
                char child_shown[PATH_BUF];
                snprintf(child_shown, sizeof(child_shown), "%s/%s", shown, names.items[i]);
                rm_path(rm, child, child_shown);
                free(child);
            }
            sl_free(&names);
        } else if (!rm->empty_dirs) {
            cmd_error("rm", "cannot remove '%s': Is a directory", shown);
            rm->status = 1;
            return;
        }
        if (delete_entry(path, 1) != 0) {
            cmd_error("rm", "cannot remove '%s': %s", shown,
                      rm->empty_dirs && !rm->recursive ? "Directory not empty" : strerror(errno));
            rm->status = 1;
            return;
        }
        if (rm->verbose) printf("removed directory '%s'\n", shown);
        return;
    }
    if (delete_entry(path, 0) != 0) {
        cmd_error("rm", "cannot remove '%s': %s", shown, strerror(errno));
        rm->status = 1;
        return;
    }
    if (rm->verbose) printf("removed '%s'\n", shown);
}

static int cmd_rm(int argc, char **argv) {
    static const OptSpec specs[] = {{'f', "force", 0},    {'i', NULL, 0},        {'I', NULL, 0},
                                    {'r', "recursive", 0}, {'R', NULL, 0},        {'d', "dir", 0},
                                    {'v', "verbose", 0},  {'1', "one-file-system", 0},
                                    {'2', "no-preserve-root", 0}, {'3', "preserve-root", 2},
                                    {'4', "interactive", 2}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "rm", &args, 1);
    if (early >= 0) return early;
    RmState rm = {args_has(&args, 'f'), args_has(&args, 'v'), args_has(&args, 'r') || args_has(&args, 'R'),
                  args_has(&args, 'd'), 0};
    if (args.operand_count == 0) {
        if (!rm.force) {
            cmd_error("rm", "missing operand");
            fprintf(stderr, "Try 'rm --help' for more information.\n");
            args_free(&args);
            return 1;
        }
        args_free(&args);
        return 0;
    }
    for (int i = 0; i < args.operand_count; i++) {
        char native[PATH_BUF];
        snprintf(native, sizeof(native), "%s", args.operands[i]);
        strip_trailing_separators(native);
        if (strcmp(native, "/") == 0 || strcmp(native, ".") == 0 || strcmp(native, "..") == 0) {
            cmd_error("rm", "refusing to remove '.' or '..' directory: skipping '%s'", args.operands[i]);
            rm.status = 1;
            continue;
        }
        path_to_backslashes(native);
        rm_path(&rm, native, args.operands[i]);
    }
    args_free(&args);
    return rm.status;
}

static int copy_file(const char *source, const char *destination, int preserve) {
    if (!CopyFileA(source, destination, FALSE)) return 0;
    if (preserve) {
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (GetFileAttributesExA(source, GetFileExInfoStandard, &info)) {
            HANDLE handle = CreateFileA(destination, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (handle != INVALID_HANDLE_VALUE) {
                SetFileTime(handle, NULL, &info.ftLastAccessTime, &info.ftLastWriteTime);
                CloseHandle(handle);
            }
        }
#ifndef _WIN32
        struct stat st;
        if (stat(source, &st) == 0) chmod(destination, st.st_mode & 07777);
#endif
    }
    return 1;
}

typedef struct {
    int recursive;
    int force;
    int no_clobber;
    int verbose;
    int preserve;
    int update;
    int status;
} CpState;

static int newer_than(const char *a, const char *b) {
    WIN32_FILE_ATTRIBUTE_DATA ia, ib;
    if (!GetFileAttributesExA(a, GetFileExInfoStandard, &ia)) return 0;
    if (!GetFileAttributesExA(b, GetFileExInfoStandard, &ib)) return 1;
    return CompareFileTime(&ia.ftLastWriteTime, &ib.ftLastWriteTime) > 0;
}

static void cp_one(CpState *cp, const char *source, const char *destination) {
    if (path_is_dir(source)) {
        if (!cp->recursive) {
            cmd_error("cp", "-r not specified; omitting directory '%s'", source);
            cp->status = 1;
            return;
        }
        if (!path_is_dir(destination) && _mkdir(destination) != 0) {
            cmd_error("cp", "cannot create directory '%s': %s", destination, strerror(errno));
            cp->status = 1;
            return;
        }
        if (cp->verbose) printf("'%s' -> '%s'\n", source, destination);
        StrList names;
        sl_init(&names);
        list_directory(source, &names);
        for (size_t i = 0; i < names.len; i++) {
            char *from = path_join(source, names.items[i]);
            char *to = path_join(destination, names.items[i]);
            path_to_slashes(from);
            path_to_slashes(to);
            cp_one(cp, from, to);
            free(from);
            free(to);
        }
        sl_free(&names);
        return;
    }
    if (!exists(source)) {
        cmd_error("cp", "cannot stat '%s': No such file or directory", source);
        cp->status = 1;
        return;
    }
    if (exists(destination)) {
        if (cp->no_clobber) return;
        if (cp->update && !newer_than(source, destination)) return;
        if (cp->force) DeleteFileA(destination);
    }
    if (!copy_file(source, destination, cp->preserve)) {
        cmd_error("cp", "cannot create regular file '%s': %s", destination, strerror(errno));
        cp->status = 1;
        return;
    }
    if (cp->verbose) printf("'%s' -> '%s'\n", source, destination);
}

static void destination_for(const char *directory, const char *source, char *out, size_t size) {
    char copy[PATH_BUF];
    snprintf(copy, sizeof(copy), "%s", source);
    strip_trailing_separators(copy);
    const char *leaf = path_last_sep(copy);
    snprintf(out, size, "%s/%s", directory, leaf ? leaf + 1 : copy);
}

static int cmd_cp(int argc, char **argv) {
    static const OptSpec specs[] = {{'a', "archive", 0},     {'f', "force", 0},      {'i', "interactive", 0},
                                    {'n', "no-clobber", 0},  {'p', NULL, 0},         {'P', "no-dereference", 0},
                                    {'r', "recursive", 0},   {'R', NULL, 0},         {'t', "target-directory", 1},
                                    {'T', "no-target-directory", 0}, {'u', "update", 2}, {'v', "verbose", 0},
                                    {'L', "dereference", 0}, {'d', NULL, 0},         {'l', "link", 0},
                                    {'s', "symbolic-link", 0}, {'x', "one-file-system", 0},
                                    {'1', "preserve", 2},    {'2', "no-preserve", 1}, {'3', "parents", 0},
                                    {'4', "backup", 2},      {'5', "strip-trailing-slashes", 0},
                                    {'6', "reflink", 2},     {'7', "sparse", 1},     {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "cp", &args, 1);
    if (early >= 0) return early;
    CpState cp;
    memset(&cp, 0, sizeof(cp));
    cp.recursive = args_has(&args, 'r') || args_has(&args, 'R') || args_has(&args, 'a');
    cp.force = args_has(&args, 'f');
    cp.no_clobber = args_has(&args, 'n');
    cp.verbose = args_has(&args, 'v');
    cp.preserve = args_has(&args, 'p') || args_has(&args, 'a') || args_has(&args, '1');
    cp.update = args_has(&args, 'u');

    const char *target = args_value(&args, 't');
    int sources = args.operand_count - (target ? 0 : 1);
    if (args.operand_count == 0 || (!target && args.operand_count == 1)) {
        if (args.operand_count == 1) cmd_error("cp", "missing destination file operand after '%s'", args.operands[0]);
        else cmd_error("cp", "missing file operand");
        fprintf(stderr, "Try 'cp --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    const char *destination = target ? target : args.operands[args.operand_count - 1];
    int into_directory = path_is_dir(destination) && !args_has(&args, 'T');
    if (sources > 1 && !into_directory) {
        cmd_error("cp", "target '%s' is not a directory", destination);
        args_free(&args);
        return 1;
    }
    for (int i = 0; i < sources; i++) {
        char resolved[PATH_BUF];
        if (into_directory) destination_for(destination, args.operands[i], resolved, sizeof(resolved));
        else snprintf(resolved, sizeof(resolved), "%s", destination);
        cp_one(&cp, args.operands[i], resolved);
    }
    args_free(&args);
    return cp.status;
}

static int cmd_mv(int argc, char **argv) {
    static const OptSpec specs[] = {{'f', "force", 0},      {'i', "interactive", 0}, {'n', "no-clobber", 0},
                                    {'t', "target-directory", 1}, {'T', "no-target-directory", 0},
                                    {'u', "update", 2},     {'v', "verbose", 0},     {'b', "backup", 2},
                                    {'1', "strip-trailing-slashes", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "mv", &args, 1);
    if (early >= 0) return early;
    const char *target = args_value(&args, 't');
    int sources = args.operand_count - (target ? 0 : 1);
    if (args.operand_count == 0 || (!target && args.operand_count == 1)) {
        if (args.operand_count == 1) cmd_error("mv", "missing destination file operand after '%s'", args.operands[0]);
        else cmd_error("mv", "missing file operand");
        fprintf(stderr, "Try 'mv --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    const char *destination = target ? target : args.operands[args.operand_count - 1];
    int into_directory = path_is_dir(destination) && !args_has(&args, 'T');
    if (sources > 1 && !into_directory) {
        cmd_error("mv", "target '%s' is not a directory", destination);
        args_free(&args);
        return 1;
    }
    int status = 0;
    for (int i = 0; i < sources; i++) {
        char resolved[PATH_BUF];
        if (into_directory) destination_for(destination, args.operands[i], resolved, sizeof(resolved));
        else snprintf(resolved, sizeof(resolved), "%s", destination);
        if (!exists(args.operands[i])) {
            cmd_error("mv", "cannot stat '%s': No such file or directory", args.operands[i]);
            status = 1;
            continue;
        }
        if (exists(resolved)) {
            if (args_has(&args, 'n')) continue;
            if (args_has(&args, 'u') && !newer_than(args.operands[i], resolved)) continue;
        }
        if (!MoveFileExA(args.operands[i], resolved, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
            cmd_error("mv", "cannot move '%s' to '%s': %s", args.operands[i], resolved, strerror(errno));
            status = 1;
            continue;
        }
        if (args_has(&args, 'v')) printf("renamed '%s' -> '%s'\n", args.operands[i], resolved);
    }
    args_free(&args);
    return status;
}

static int parse_mode(const char *text, unsigned current, unsigned *out) {
    if (isdigit((unsigned char)*text)) {
        char *end;
        unsigned long value = strtoul(text, &end, 8);
        if (*end || value > 07777) return 0;
        *out = (unsigned)value;
        return 1;
    }
    unsigned mode = current;
    const char *p = text;
    while (*p) {
        unsigned who = 0;
        while (strchr("ugoa", *p) && *p) {
            if (*p == 'u') who |= 04700;
            else if (*p == 'g') who |= 02070;
            else if (*p == 'o') who |= 01007;
            else who |= 07777;
            p++;
        }
        if (!who) who = 07777;
        if (!strchr("+-=", *p) || !*p) return 0;
        while (*p && strchr("+-=", *p)) {
            char op = *p++;
            unsigned perm = 0;
            while (*p && strchr("rwxXst", *p)) {
                if (*p == 'r') perm |= 0444;
                else if (*p == 'w') perm |= 0222;
                else if (*p == 'x') perm |= 0111;
                else if (*p == 'X') perm |= (current & 0111) || (current & 040000) ? 0111 : 0;
                else if (*p == 's') perm |= 06000;
                else if (*p == 't') perm |= 01000;
                p++;
            }
            if (*p && strchr("ugo", *p)) {
                unsigned source = *p == 'u' ? (current >> 6) & 7 : *p == 'g' ? (current >> 3) & 7 : current & 7;
                perm = source | (source << 3) | (source << 6);
                p++;
            }
            perm &= who;
            if (op == '+') mode |= perm;
            else if (op == '-') mode &= ~perm;
            else mode = (mode & ~who) | perm;
        }
        if (*p == ',') p++;
        else if (*p) return 0;
    }
    *out = mode & 07777;
    return 1;
}

static unsigned current_mode(const char *path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return 0;
    unsigned mode = (attributes & FILE_ATTRIBUTE_READONLY) ? 0444 : 0666;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) mode |= 0111 | 040000;
    return mode;
#else
    struct stat info;
    if (stat(path, &info) != 0) return 0;
    return (unsigned)(info.st_mode & 047777);
#endif
}

static int apply_mode(const char *path, unsigned mode) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return 0;
    if (mode & 0200) attributes &= ~(DWORD)FILE_ATTRIBUTE_READONLY;
    else attributes |= FILE_ATTRIBUTE_READONLY;
    if (attributes == 0) attributes = FILE_ATTRIBUTE_NORMAL;
    return SetFileAttributesA(path, attributes) != 0;
#else
    return chmod(path, mode) == 0;
#endif
}

static void mode_bits(unsigned mode, char *out) {
    const char *bits = "rwxrwxrwx";
    for (int i = 0; i < 9; i++) out[i] = (mode & (0400 >> i)) ? bits[i] : '-';
    out[9] = '\0';
}

typedef struct {
    const char *mode_text;
    int recursive;
    int verbose;
    int changes;
    int quiet;
    int status;
} ChmodState;

static void chmod_path(ChmodState *ch, const char *path) {
    if (!exists(path)) {
        if (!ch->quiet) cmd_error("chmod", "cannot access '%s': No such file or directory", path);
        ch->status = 1;
        return;
    }
    unsigned before = current_mode(path);
    unsigned wanted;
    if (!parse_mode(ch->mode_text, before, &wanted)) {
        cmd_error("chmod", "invalid mode: '%s'", ch->mode_text);
        ch->status = 1;
        return;
    }
    if (!apply_mode(path, wanted)) {
        if (!ch->quiet) cmd_error("chmod", "changing permissions of '%s': %s", path, strerror(errno));
        ch->status = 1;
        return;
    }
    unsigned after = current_mode(path) & 07777;
    unsigned old = before & 07777;
    if (ch->verbose || (ch->changes && after != old)) {
        char old_bits[16], new_bits[16];
        mode_bits(old, old_bits);
        mode_bits(after, new_bits);
        if (after == old) printf("mode of '%s' retained as %04o (%s)\n", path, after, new_bits);
        else printf("mode of '%s' changed from %04o (%s) to %04o (%s)\n", path, old, old_bits, after, new_bits);
    }
    if (ch->recursive && path_is_dir(path)) {
        StrList names;
        sl_init(&names);
        list_directory(path, &names);
        for (size_t i = 0; i < names.len; i++) {
            char *child = path_join(path, names.items[i]);
            path_to_slashes(child);
            chmod_path(ch, child);
            free(child);
        }
        sl_free(&names);
    }
}

static int cmd_chmod(int argc, char **argv) {
    static const OptSpec specs[] = {{'R', "recursive", 0}, {'v', "verbose", 0}, {'c', "changes", 0},
                                    {'f', "silent", 0},    {'f', "quiet", 0},   {'1', "reference", 1},
                                    {'2', "preserve-root", 0}, {'3', "no-preserve-root", 0}, {0, NULL, 0}};
    Args args;
    memset(&args, 0, sizeof(args));
    args.stop_at_operand = 1;
    int parsed = args_parse(argc, argv, specs, "chmod", &args);
    if (parsed != ARGS_OK) return parsed == ARGS_DONE ? 0 : 1;
    if (args.operand_count < 2 && !args_has(&args, '1')) {
        cmd_error("chmod", args.operand_count ? "missing operand after '%s'" : "missing operand",
                  args.operand_count ? args.operands[0] : "");
        fprintf(stderr, "Try 'chmod --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    ChmodState ch = {args.operands[0], args_has(&args, 'R'), args_has(&args, 'v'), args_has(&args, 'c'),
                     args_has(&args, 'f'), 0};
    int first = 1;
    static char reference_mode[16];
    if (args_has(&args, '1')) {
        snprintf(reference_mode, sizeof(reference_mode), "%o", current_mode(args_value(&args, '1')) & 07777);
        ch.mode_text = reference_mode;
        first = 0;
    }
    for (int i = first; i < args.operand_count; i++) chmod_path(&ch, args.operands[i]);
    args_free(&args);
    return ch.status;
}

static int cmd_mkdir(int argc, char **argv) {
    static const OptSpec specs[] = {{'p', "parents", 0}, {'v', "verbose", 0}, {'m', "mode", 1},
                                    {'Z', "context", 2}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "mkdir", &args, 1);
    if (early >= 0) return early;
    if (args.operand_count == 0) {
        cmd_error("mkdir", "missing operand");
        fprintf(stderr, "Try 'mkdir --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    int status = 0;
    for (int i = 0; i < args.operand_count; i++) {
        const char *path = args.operands[i];
        if (args_has(&args, 'p')) {
            if (path_is_dir(path)) continue;
            char partial[PATH_BUF];
            snprintf(partial, sizeof(partial), "%s", path);
            for (char *p = partial + 1; *p; p++) {
                if (*p != '/' && *p != '\\') continue;
                *p = '\0';
                if (!path_is_dir(partial) && *partial) {
                    if (_mkdir(partial) == 0 && args_has(&args, 'v')) printf("mkdir: created directory '%s'\n", partial);
                }
                *p = '/';
            }
            if (!path_is_dir(path) && _mkdir(path) != 0) {
                cmd_error("mkdir", "cannot create directory '%s': %s", path, strerror(errno));
                status = 1;
                continue;
            }
            if (args_has(&args, 'v')) printf("mkdir: created directory '%s'\n", path);
        } else if (_mkdir(path) != 0) {
            cmd_error("mkdir", "cannot create directory '%s': %s", path, strerror(errno));
            status = 1;
            continue;
        } else if (args_has(&args, 'v')) {
            printf("mkdir: created directory '%s'\n", path);
        }
        if (args_has(&args, 'm')) {
            unsigned mode;
            if (parse_mode(args_value(&args, 'm'), 0777, &mode)) apply_mode(path, mode);
        }
    }
    args_free(&args);
    return status;
}

static int cmd_rmdir(int argc, char **argv) {
    static const OptSpec specs[] = {{'p', "parents", 0}, {'v', "verbose", 0},
                                    {'1', "ignore-fail-on-non-empty", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "rmdir", &args, 1);
    if (early >= 0) return early;
    if (args.operand_count == 0) {
        cmd_error("rmdir", "missing operand");
        fprintf(stderr, "Try 'rmdir --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    int status = 0;
    for (int i = 0; i < args.operand_count; i++) {
        char path[PATH_BUF];
        snprintf(path, sizeof(path), "%s", args.operands[i]);
        strip_trailing_separators(path);
        for (;;) {
            if (args_has(&args, 'v')) printf("rmdir: removing directory, '%s'\n", path);
            if (!RemoveDirectoryA(path)) {
                int not_empty = GetLastError() == ERROR_DIR_NOT_EMPTY;
                if (!(not_empty && args_has(&args, '1'))) {
                    cmd_error("rmdir", "failed to remove '%s': %s", path,
                              not_empty ? "Directory not empty" : !exists(path) ? "No such file or directory" : "Not a directory");
                    status = 1;
                }
                break;
            }
            if (!args_has(&args, 'p')) break;
            char *sep = path_last_sep(path);
            if (!sep || sep == path) break;
            *sep = '\0';
        }
    }
    args_free(&args);
    return status;
}

static int parse_iso_time(const char *text, time_t *out) {
    struct tm parts;
    memset(&parts, 0, sizeof(parts));
    int year, month, day, hour = 0, minute = 0, second = 0;
    int fields = sscanf(text, "%d-%d-%d%*[T ]%d:%d:%d", &year, &month, &day, &hour, &minute, &second);
    if (fields < 3) return 0;
    parts.tm_year = year - 1900;
    parts.tm_mon = month - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    parts.tm_sec = second;
    parts.tm_isdst = -1;
    *out = mktime(&parts);
    return *out != (time_t)-1;
}

static int cmd_touch(int argc, char **argv) {
    static const OptSpec specs[] = {{'a', NULL, 0},          {'c', "no-create", 0}, {'d', "date", 1},
                                    {'f', NULL, 0},          {'h', "no-dereference", 0}, {'m', NULL, 0},
                                    {'r', "reference", 1},   {'t', NULL, 1},        {'1', "time", 1},
                                    {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "touch", &args, 1);
    if (early >= 0) return early;
    if (args.operand_count == 0) {
        cmd_error("touch", "missing file operand");
        fprintf(stderr, "Try 'touch --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    FILETIME stamp;
    GetSystemTimeAsFileTime(&stamp);
    if (args_has(&args, 'r')) {
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (!GetFileAttributesExA(args_value(&args, 'r'), GetFileExInfoStandard, &info)) {
            cmd_error("touch", "failed to get attributes of '%s': No such file or directory", args_value(&args, 'r'));
            args_free(&args);
            return 1;
        }
        stamp = info.ftLastWriteTime;
    } else if (args_has(&args, 'd')) {
        time_t when;
        const char *text = args_value(&args, 'd');
        if (*text == '@') when = (time_t)atoll(text + 1);
        else if (!parse_iso_time(text, &when)) {
            cmd_error("touch", "invalid date format '%s'", text);
            args_free(&args);
            return 1;
        }
        unsigned long long ticks = ((unsigned long long)when + 11644473600ULL) * 10000000ULL;
        stamp.dwLowDateTime = (DWORD)(ticks & 0xffffffffULL);
        stamp.dwHighDateTime = (DWORD)(ticks >> 32);
    }
    int only_access = args_has(&args, 'a') && !args_has(&args, 'm');
    int only_modify = args_has(&args, 'm') && !args_has(&args, 'a');
    int status = 0;
    for (int i = 0; i < args.operand_count; i++) {
        const char *path = args.operands[i];
        if (!exists(path) && args_has(&args, 'c')) continue;
        HANDLE handle = CreateFileA(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            cmd_error("touch", "cannot touch '%s': %s", path, strerror(errno));
            status = 1;
            continue;
        }
        SetFileTime(handle, NULL, only_modify ? NULL : &stamp, only_access ? NULL : &stamp);
        CloseHandle(handle);
    }
    args_free(&args);
    return status;
}

static int cmd_ln(int argc, char **argv) {
    static const OptSpec specs[] = {{'s', "symbolic", 0},  {'f', "force", 0},   {'n', "no-dereference", 0},
                                    {'v', "verbose", 0},   {'r', "relative", 0}, {'t', "target-directory", 1},
                                    {'T', "no-target-directory", 0}, {'i', "interactive", 0},
                                    {'b', "backup", 2},    {'L', "logical", 0}, {'P', "physical", 0},
                                    {'d', "directory", 0}, {'F', NULL, 0},      {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "ln", &args, 1);
    if (early >= 0) return early;
    if (args.operand_count == 0) {
        cmd_error("ln", "missing file operand");
        fprintf(stderr, "Try 'ln --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    const char *target_dir = args_value(&args, 't');
    int sources = args.operand_count;
    const char *destination = NULL;
    if (!target_dir) {
        if (args.operand_count == 1) destination = ".";
        else {
            destination = args.operands[args.operand_count - 1];
            sources--;
        }
    } else destination = target_dir;
    int into_directory = path_is_dir(destination) && !args_has(&args, 'T');
    if (sources > 1 && !into_directory) {
        cmd_error("ln", "target '%s' is not a directory", destination);
        args_free(&args);
        return 1;
    }
    int status = 0;
    for (int i = 0; i < sources; i++) {
        char link[PATH_BUF];
        if (into_directory) destination_for(destination, args.operands[i], link, sizeof(link));
        else snprintf(link, sizeof(link), "%s", destination);
        if (exists(link)) {
            if (args_has(&args, 'f')) DeleteFileA(link);
            else {
                cmd_error("ln", "failed to create %s link '%s': File exists", args_has(&args, 's') ? "symbolic" : "hard", link);
                status = 1;
                continue;
            }
        }
        int ok;
        if (args_has(&args, 's')) ok = CreateSymbolicLinkA(link, args.operands[i], path_is_dir(args.operands[i]) ? 3 : 2);
        else {
            if (!exists(args.operands[i])) {
                cmd_error("ln", "failed to access '%s': No such file or directory", args.operands[i]);
                status = 1;
                continue;
            }
            ok = CreateHardLinkA(link, args.operands[i], NULL);
        }
        if (!ok) {
            cmd_error("ln", "failed to create %s link '%s': %s", args_has(&args, 's') ? "symbolic" : "hard", link,
#ifdef _WIN32
                      "Operation not permitted (developer mode or administrator rights are needed)"
#else
                      strerror(errno)
#endif
            );
            status = 1;
            continue;
        }
        if (args_has(&args, 'v')) printf("'%s' %s '%s'\n", link, args_has(&args, 's') ? "->" : "=>", args.operands[i]);
    }
    args_free(&args);
    return status;
}

static const char *file_type_name(const char *path, WIN32_FILE_ATTRIBUTE_DATA *info) {
    (void)path;
#ifndef _WIN32
    struct stat st;
    if (lstat(path, &st) == 0) {
        if (S_ISLNK(st.st_mode)) return "symbolic link";
        if (S_ISDIR(st.st_mode)) return "directory";
        if (S_ISCHR(st.st_mode)) return "character special file";
        if (S_ISBLK(st.st_mode)) return "block special file";
        if (S_ISFIFO(st.st_mode)) return "fifo";
        if (S_ISSOCK(st.st_mode)) return "socket";
        if (st.st_size == 0) return "regular empty file";
        return "regular file";
    }
#endif
    if (info->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return "directory";
    if (info->nFileSizeHigh == 0 && info->nFileSizeLow == 0) return "regular empty file";
    return "regular file";
}

static void mode_string(unsigned mode, int is_dir, int is_link, char *out) {
    out[0] = is_link ? 'l' : is_dir ? 'd' : '-';
    const char *bits = "rwxrwxrwx";
    for (int i = 0; i < 9; i++) out[i + 1] = (mode & (0400 >> i)) ? bits[i] : '-';
    if (mode & 04000) out[3] = (mode & 0100) ? 's' : 'S';
    if (mode & 02000) out[6] = (mode & 010) ? 's' : 'S';
    if (mode & 01000) out[9] = (mode & 01) ? 't' : 'T';
    out[10] = '\0';
}

static void format_time(const FILETIME *time, char *out, size_t size) {
    FILETIME local;
    SYSTEMTIME parts;
    FileTimeToLocalFileTime(time, &local);
    FileTimeToSystemTime(&local, &parts);
    unsigned long long ticks = ((unsigned long long)time->dwHighDateTime << 32) | time->dwLowDateTime;
    unsigned long long fraction = ticks % 10000000ULL;
    long offset_minutes = 0;
    {
        unsigned long long utc = ticks;
        unsigned long long loc = ((unsigned long long)local.dwHighDateTime << 32) | local.dwLowDateTime;
        offset_minutes = (long)(((long long)loc - (long long)utc) / 600000000LL);
    }
    snprintf(out, size, "%04d-%02d-%02d %02d:%02d:%02d.%09llu %c%02ld%02ld", parts.wYear, parts.wMonth,
             parts.wDay, parts.wHour, parts.wMinute, parts.wSecond, fraction * 100,
             offset_minutes < 0 ? '-' : '+', labs(offset_minutes) / 60, labs(offset_minutes) % 60);
}

typedef struct {
    const char *path;
    WIN32_FILE_ATTRIBUTE_DATA info;
    unsigned mode;
    int is_dir;
    int is_link;
    unsigned long long size;
    unsigned long long inode, device, links, blocks, block_size, uid, gid;
    char user[64], group[64];
} StatInfo;

static int stat_collect(const char *path, int follow, StatInfo *st) {
    memset(st, 0, sizeof(*st));
    st->path = path;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &st->info)) return 0;
    st->is_dir = (st->info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    st->size = ((unsigned long long)st->info.nFileSizeHigh << 32) | st->info.nFileSizeLow;
    st->mode = current_mode(path) & 07777;
    st->block_size = 4096;
    st->blocks = (st->size + 511) / 512;
    st->links = 1;
    snprintf(st->user, sizeof(st->user), "%s", "user");
    snprintf(st->group, sizeof(st->group), "%s", "users");
#ifndef _WIN32
    struct stat native;
    if ((follow ? stat(path, &native) : lstat(path, &native)) == 0) {
        st->is_link = S_ISLNK(native.st_mode);
        st->mode = (unsigned)(native.st_mode & 07777);
        st->inode = (unsigned long long)native.st_ino;
        st->device = (unsigned long long)native.st_dev;
        st->links = (unsigned long long)native.st_nlink;
        st->blocks = (unsigned long long)native.st_blocks;
        st->block_size = (unsigned long long)native.st_blksize;
        st->uid = native.st_uid;
        st->gid = native.st_gid;
        st->size = (unsigned long long)native.st_size;
        struct passwd *pw = getpwuid(native.st_uid);
        struct group *gr = getgrgid(native.st_gid);
        snprintf(st->user, sizeof(st->user), "%s", pw ? pw->pw_name : "UNKNOWN");
        snprintf(st->group, sizeof(st->group), "%s", gr ? gr->gr_name : "UNKNOWN");
    }
#else
    (void)follow;
    char user[64];
    DWORD user_size = sizeof(user);
    if (win_user_name(user, &user_size)) snprintf(st->user, sizeof(st->user), "%s", user);
#endif
    return 1;
}

static void stat_format(const StatInfo *st, const char *format, StrBuf *out) {
    char text[128];
    char modes[16];
    for (const char *p = format; *p; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            sb_putc(out, *p == 'n' ? '\n' : *p == 't' ? '\t' : *p);
            continue;
        }
        if (*p != '%') {
            sb_putc(out, *p);
            continue;
        }
        p++;
        char spec[16] = "%";
        size_t s = 1;
        while (*p && strchr("-+ #0123456789.", *p) && s < 12) spec[s++] = *p++;
        spec[s] = '\0';
        char conversion = *p;
        if (!conversion) break;
        int numeric = 1;
        unsigned long long number = 0;
        const char *string = NULL;
        switch (conversion) {
        case '%': sb_putc(out, '%'); continue;
        case 'n': string = st->path; break;
        case 'N':
            snprintf(text, sizeof(text), "'%s'", st->path);
            string = text;
            break;
        case 's': number = st->size; break;
        case 'F': string = file_type_name(st->path, (WIN32_FILE_ATTRIBUTE_DATA *)&st->info); break;
        case 'a':
            snprintf(text, sizeof(text), "%o", st->mode);
            string = text;
            break;
        case 'A':
            mode_string(st->mode, st->is_dir, st->is_link, modes);
            string = modes;
            break;
        case 'U': string = st->user; break;
        case 'G': string = st->group; break;
        case 'u': number = st->uid; break;
        case 'g': number = st->gid; break;
        case 'i': number = st->inode; break;
        case 'd': number = st->device; break;
        case 'h': number = st->links; break;
        case 'b': number = st->blocks; break;
        case 'B': number = 512; break;
        case 'o': number = st->block_size; break;
        case 'f':
            snprintf(text, sizeof(text), "%x", st->mode | (st->is_dir ? 040000 : 0100000));
            string = text;
            break;
        case 'x': format_time(&st->info.ftLastAccessTime, text, sizeof(text)); string = text; break;
        case 'y': format_time(&st->info.ftLastWriteTime, text, sizeof(text)); string = text; break;
        case 'z': format_time(&st->info.ftLastWriteTime, text, sizeof(text)); string = text; break;
        case 'w': format_time(&st->info.ftCreationTime, text, sizeof(text)); string = text; break;
        case 'X': number = (((unsigned long long)st->info.ftLastAccessTime.dwHighDateTime << 32) | st->info.ftLastAccessTime.dwLowDateTime) / 10000000ULL - 11644473600ULL; break;
        case 'Y':
        case 'Z': number = (((unsigned long long)st->info.ftLastWriteTime.dwHighDateTime << 32) | st->info.ftLastWriteTime.dwLowDateTime) / 10000000ULL - 11644473600ULL; break;
        case 'W': number = (((unsigned long long)st->info.ftCreationTime.dwHighDateTime << 32) | st->info.ftCreationTime.dwLowDateTime) / 10000000ULL - 11644473600ULL; break;
        case 't': string = "0"; break;
        case 'T': string = "0"; break;
        case 'm': string = "/"; break;
        default:
            sb_putc(out, '%');
            sb_putc(out, conversion);
            continue;
        }
        if (string) {
            numeric = 0;
            strcat(spec, "s");
            sb_printf(out, spec, string);
        }
        if (numeric) {
            strcat(spec, "llu");
            sb_printf(out, spec, number);
        }
    }
}

static int cmd_stat(int argc, char **argv) {
    static const OptSpec specs[] = {{'c', "format", 1}, {'L', "dereference", 0}, {'t', "terse", 0},
                                    {'f', "file-system", 0}, {'1', "printf", 1}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "stat", &args, 1);
    if (early >= 0) return early;
    if (args.operand_count == 0) {
        cmd_error("stat", "missing operand");
        fprintf(stderr, "Try 'stat --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    int status = 0;
    const char *format = args_value(&args, 'c');
    const char *printf_format = args_value(&args, '1');
    int terse = args_has(&args, 't');
    for (int i = 0; i < args.operand_count; i++) {
        StatInfo st;
        if (!stat_collect(args.operands[i], args_has(&args, 'L'), &st)) {
            cmd_error("stat", "cannot statx '%s': No such file or directory", args.operands[i]);
            status = 1;
            continue;
        }
        StrBuf out;
        sb_init(&out);
        if (printf_format) stat_format(&st, printf_format, &out);
        else if (format) {
            stat_format(&st, format, &out);
            sb_putc(&out, '\n');
        } else if (terse) {
            stat_format(&st, "%n %s %b %f %u %g %d %i %h %t %T %X %Y %Z %W %o\n", &out);
        } else {
            stat_format(&st, "  File: %N\n  Size: %-10s\tBlocks: %-10b IO Block: %-6o %F\n", &out);
            stat_format(&st, "Device: %d\tInode: %-10i  Links: %h\n", &out);
            stat_format(&st, "Access: (%04a/%A)  Uid: (%5u/%8U)   Gid: (%5g/%8G)\n", &out);
            stat_format(&st, "Access: %x\nModify: %y\nChange: %z\n Birth: %w\n", &out);
        }
        fputs(out.data, stdout);
        sb_free(&out);
    }
    args_free(&args);
    return status;
}

typedef struct {
    int all;
    int summarize;
    int total;
    int human;
    int si;
    int apparent;
    int max_depth;
    unsigned long long unit;
    unsigned long long grand;
    int status;
} DuState;

static void du_print(const DuState *du, unsigned long long bytes, const char *path) {
    if (du->human || du->si) {
        unsigned long long base = du->si ? 1000 : 1024;
        const char *units = du->si ? "kMGTPE" : "KMGTPE";
        double value = (double)bytes;
        int unit = -1;
        while (value >= (double)base && unit < 5) {
            value /= (double)base;
            unit++;
        }
        if (unit < 0) printf("%llu\t%s\n", bytes, path);
        else if (value < 10) printf("%.1f%c\t%s\n", (double)((unsigned long long)(value * 10 + 0.999)) / 10.0, units[unit], path);
        else printf("%llu%c\t%s\n", (unsigned long long)(value + 0.999), units[unit], path);
        return;
    }
    printf("%llu\t%s\n", (bytes + du->unit - 1) / du->unit, path);
}

static unsigned long long du_size_of(const char *path, int apparent) {
#ifndef _WIN32
    struct stat st;
    if (lstat(path, &st) != 0) return 0;
    if (!apparent) return (unsigned long long)st.st_blocks * 512ULL;
    if (S_ISDIR(st.st_mode)) return 0;
    return (unsigned long long)st.st_size;
#else
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) return 0;
    unsigned long long size = ((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    if (apparent) return size;
    if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 0;
    return ((size + 4095) / 4096) * 4096;
#endif
}

static unsigned long long du_walk(DuState *du, const char *path, int depth) {
    unsigned long long total = du_size_of(path, du->apparent);
    if (path_is_dir(path)) {
        StrList names;
        sl_init(&names);
        list_directory(path, &names);
        for (size_t i = 0; i < names.len; i++) {
            char child[PATH_BUF];
            snprintf(child, sizeof(child), "%s/%s", path, names.items[i]);
            if (path_is_dir(child)) total += du_walk(du, child, depth + 1);
            else {
                unsigned long long size = du_size_of(child, du->apparent);
                total += size;
                if (du->all && !du->summarize && (du->max_depth < 0 || depth + 1 <= du->max_depth)) du_print(du, size, child);
            }
        }
        sl_free(&names);
    }
    int show = !du->summarize && (du->max_depth < 0 || depth <= du->max_depth);
    if (depth == 0 || (show && path_is_dir(path))) du_print(du, total, path);
    return total;
}

static int cmd_du(int argc, char **argv) {
    static const OptSpec specs[] = {{'a', "all", 0},         {'b', "bytes", 0},      {'c', "total", 0},
                                    {'d', "max-depth", 1},   {'h', "human-readable", 0}, {'k', NULL, 0},
                                    {'m', NULL, 0},          {'s', "summarize", 0},  {'x', "one-file-system", 0},
                                    {'L', "dereference", 0}, {'P', "no-dereference", 0}, {'S', "separate-dirs", 0},
                                    {'1', "apparent-size", 0}, {'2', "si", 0},       {'B', "block-size", 1},
                                    {'0', "null", 0},        {'3', "time", 2},       {'4', "exclude", 1},
                                    {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "du", &args, 1);
    if (early >= 0) return early;
    DuState du;
    memset(&du, 0, sizeof(du));
    du.all = args_has(&args, 'a');
    du.summarize = args_has(&args, 's');
    du.total = args_has(&args, 'c');
    du.human = args_has(&args, 'h');
    du.si = args_has(&args, '2');
    du.apparent = args_has(&args, '1') || args_has(&args, 'b');
    du.max_depth = args_has(&args, 'd') ? atoi(args_value(&args, 'd')) : -1;
    if (du.summarize) du.max_depth = 0;
    du.unit = 1024;
    if (args_has(&args, 'b')) du.unit = 1;
    if (args_has(&args, 'm')) du.unit = 1024 * 1024;
    if (args_has(&args, 'B')) {
        int ok;
        long long value = cmd_parse_size(args_value(&args, 'B'), &ok);
        if (ok && value > 0) du.unit = (unsigned long long)value;
    }
    const char *env_block = getenv("DU_BLOCK_SIZE");
    if (env_block && !args_has(&args, 'B') && !args_has(&args, 'b') && !args_has(&args, 'k') && !args_has(&args, 'm')) {
        int ok;
        long long value = cmd_parse_size(env_block, &ok);
        if (ok && value > 0) du.unit = (unsigned long long)value;
    }
    int count = args.operand_count ? args.operand_count : 1;
    for (int i = 0; i < count; i++) {
        const char *path = args.operand_count ? args.operands[i] : ".";
        if (!exists(path)) {
            cmd_error("du", "cannot access '%s': No such file or directory", path);
            du.status = 1;
            continue;
        }
        char clean[PATH_BUF];
        snprintf(clean, sizeof(clean), "%s", path);
        strip_trailing_separators(clean);
        du.grand += du_walk(&du, clean, 0);
    }
    if (du.total) du_print(&du, du.grand, "total");
    args_free(&args);
    return du.status;
}

static void human_size(unsigned long long bytes, int si, char *out, size_t size) {
    unsigned long long base = si ? 1000 : 1024;
    const char *units = si ? "kMGTPE" : "KMGTPE";
    double value = (double)bytes;
    int unit = -1;
    while (value >= (double)base && unit < 5) {
        value /= (double)base;
        unit++;
    }
    if (unit < 0) snprintf(out, size, "%llu", bytes);
    else if (value < 10) snprintf(out, size, "%.1f%c", (double)((unsigned long long)(value * 10 + 0.999)) / 10.0, units[unit]);
    else snprintf(out, size, "%llu%c", (unsigned long long)(value + 0.999), units[unit]);
}

typedef struct {
    char name[PATH_BUF];
    char mount[PATH_BUF];
    char type[64];
    unsigned long long total, used, available;
} Volume;

#ifndef _WIN32

static int df_volume(const char *path, Volume *volume) {
    struct statvfs space;
    if (statvfs(path, &space) != 0) return 0;
    volume->total = (unsigned long long)space.f_blocks * space.f_frsize;
    volume->available = (unsigned long long)space.f_bavail * space.f_frsize;
    volume->used = volume->total - (unsigned long long)space.f_bfree * space.f_frsize;
    snprintf(volume->name, sizeof(volume->name), "%s", "-");
    snprintf(volume->mount, sizeof(volume->mount), "%s", path);
    snprintf(volume->type, sizeof(volume->type), "%s", "-");
#ifdef __linux__
    FILE *mounts = fopen("/proc/self/mounts", "r");
    if (mounts) {
        char line[PATH_BUF * 2];
        size_t best = 0;
        char resolved[PATH_BUF];
        if (!realpath(path, resolved)) snprintf(resolved, sizeof(resolved), "%s", path);
        while (fgets(line, sizeof(line), mounts)) {
            char device[PATH_BUF], where[PATH_BUF], kind[64];
            if (sscanf(line, "%1023s %1023s %63s", device, where, kind) != 3) continue;
            size_t length = strlen(where);
            if (strncmp(resolved, where, length) == 0 && (resolved[length] == '/' || resolved[length] == '\0' || length == 1) && length >= best) {
                best = length;
                snprintf(volume->name, sizeof(volume->name), "%s", device);
                snprintf(volume->mount, sizeof(volume->mount), "%s", where);
                snprintf(volume->type, sizeof(volume->type), "%s", kind);
            }
        }
        fclose(mounts);
    }
#endif
    return 1;
}

static int df_all(Volume *volumes, int max) {
    int count = 0;
#ifdef __linux__
    FILE *mounts = fopen("/proc/self/mounts", "r");
    if (!mounts) return 0;
    char line[PATH_BUF * 2];
    while (fgets(line, sizeof(line), mounts) && count < max) {
        char device[PATH_BUF], where[PATH_BUF], kind[64];
        if (sscanf(line, "%1023s %1023s %63s", device, where, kind) != 3) continue;
        if (device[0] != '/') continue;
        Volume *volume = &volumes[count];
        if (!df_volume(where, volume)) continue;
        snprintf(volume->name, sizeof(volume->name), "%s", device);
        snprintf(volume->mount, sizeof(volume->mount), "%s", where);
        snprintf(volume->type, sizeof(volume->type), "%s", kind);
        count++;
    }
    fclose(mounts);
#else
    if (df_volume("/", &volumes[0])) count = 1;
#endif
    return count;
}

#else

static int df_volume(const char *path, Volume *volume) {
    char root[8];
    if (isalpha((unsigned char)path[0]) && path[1] == ':') snprintf(root, sizeof(root), "%c:\\", path[0]);
    else {
        char full[PATH_BUF];
        if (!GetFullPathNameA(path, sizeof(full), full, NULL)) return 0;
        snprintf(root, sizeof(root), "%c:\\", full[0]);
    }
    ULARGE_INTEGER available, total, free_bytes;
    if (!GetDiskFreeSpaceExA(root, &available, &total, &free_bytes)) return 0;
    volume->total = total.QuadPart;
    volume->available = available.QuadPart;
    volume->used = total.QuadPart - free_bytes.QuadPart;
    snprintf(volume->name, sizeof(volume->name), "%c:", root[0]);
    snprintf(volume->mount, sizeof(volume->mount), "%c:", root[0]);
    char kind[64] = "-";
    GetVolumeInformationA(root, NULL, 0, NULL, NULL, NULL, kind, sizeof(kind));
    snprintf(volume->type, sizeof(volume->type), "%s", kind);
    return 1;
}

static int df_all(Volume *volumes, int max) {
    char drives[512];
    DWORD length = GetLogicalDriveStringsA(sizeof(drives), drives);
    if (!length) return 0;
    int count = 0;
    for (char *drive = drives; *drive && count < max; drive += strlen(drive) + 1) {
        if (df_volume(drive, &volumes[count])) count++;
    }
    return count;
}

#endif

static int cmd_df(int argc, char **argv) {
    static const OptSpec specs[] = {{'a', "all", 0},       {'h', "human-readable", 0}, {'H', "si", 0},
                                    {'k', NULL, 0},        {'m', NULL, 0},          {'l', "local", 0},
                                    {'P', "portability", 0}, {'T', "print-type", 0}, {'i', "inodes", 0},
                                    {'B', "block-size", 1}, {'t', "type", 1},        {'x', "exclude-type", 1},
                                    {'1', "total", 0},     {'2', "output", 2},      {'3', "sync", 0},
                                    {'4', "no-sync", 0},   {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "df", &args, 1);
    if (early >= 0) return early;
    Volume volumes[64];
    int count = 0;
    int status = 0;
    if (args.operand_count == 0) count = df_all(volumes, 64);
    else {
        for (int i = 0; i < args.operand_count && count < 64; i++) {
            if (!exists(args.operands[i])) {
                cmd_error("df", "%s: No such file or directory", args.operands[i]);
                status = 1;
                continue;
            }
            if (df_volume(args.operands[i], &volumes[count])) count++;
        }
    }
    int human = args_has(&args, 'h'), si = args_has(&args, 'H');
    unsigned long long unit = 1024;
    if (args_has(&args, 'm')) unit = 1024 * 1024;
    if (args_has(&args, 'B')) {
        int ok;
        long long value = cmd_parse_size(args_value(&args, 'B'), &ok);
        if (ok && value > 0) unit = (unsigned long long)value;
    }
    int show_type = args_has(&args, 'T');
    char unit_label[32];
    if (human || si) snprintf(unit_label, sizeof(unit_label), "%s", "Size");
    else if (unit == 1024) snprintf(unit_label, sizeof(unit_label), "%s", "1K-blocks");
    else if (unit == 1024 * 1024) snprintf(unit_label, sizeof(unit_label), "%s", "1M-blocks");
    else snprintf(unit_label, sizeof(unit_label), "%llu-blocks", unit);
    printf("%-14s ", "Filesystem");
    if (show_type) printf("%-9s ", "Type");
    printf("%9s %9s %9s %4s Mounted on\n", unit_label, "Used", "Avail", "Use%");
    for (int i = 0; i < count; i++) {
        Volume *v = &volumes[i];
        char total[32], used[32], avail[32];
        if (human || si) {
            human_size(v->total, si, total, sizeof(total));
            human_size(v->used, si, used, sizeof(used));
            human_size(v->available, si, avail, sizeof(avail));
        } else {
            snprintf(total, sizeof(total), "%llu", (v->total + unit - 1) / unit);
            snprintf(used, sizeof(used), "%llu", (v->used + unit - 1) / unit);
            snprintf(avail, sizeof(avail), "%llu", (v->available + unit - 1) / unit);
        }
        unsigned long long denominator = v->used + v->available;
        int percent = denominator ? (int)((v->used * 100 + denominator - 1) / denominator) : 0;
        printf("%-14s ", v->name);
        if (show_type) printf("%-9s ", v->type);
        printf("%9s %9s %9s %3d%% %s\n", total, used, avail, percent, v->mount);
    }
    args_free(&args);
    return status;
}

typedef enum {
    F_NAME, F_INAME, F_PATH, F_IPATH, F_TYPE, F_SIZE, F_EMPTY, F_MTIME, F_MMIN, F_NEWER, F_REGEX,
    F_PRINT, F_PRINT0, F_DELETE, F_EXEC, F_PRUNE, F_TRUE, F_FALSE, F_NOT, F_AND, F_OR, F_QUIT,
    F_MAXDEPTH, F_MINDEPTH, F_PERM, F_READABLE, F_WRITABLE, F_EXECUTABLE
} FindOp;

typedef struct {
    FindOp op;
    char *text;
    char sign;
    long long number;
    char type;
    StrList exec_args;
    int exec_plus;
    int left, right;
} FindNode;

typedef struct {
    FindNode nodes[256];
    int count;
    int root;
    int max_depth;
    int min_depth;
    int has_action;
    int status;
    int quit;
    int print0;
} FindProgram;

static int find_node(FindProgram *program, FindOp op) {
    if (program->count >= 256) return -1;
    FindNode *node = &program->nodes[program->count];
    memset(node, 0, sizeof(*node));
    node->op = op;
    node->left = node->right = -1;
    sl_init(&node->exec_args);
    return program->count++;
}

static int find_parse_or(FindProgram *program, char **argv, int *index, int argc);

static int find_parse_primary(FindProgram *program, char **argv, int *index, int argc) {
    if (*index >= argc) return -1;
    const char *word = argv[*index];
    (*index)++;
    if (strcmp(word, "(") == 0) {
        int inner = find_parse_or(program, argv, index, argc);
        if (*index >= argc || strcmp(argv[*index], ")") != 0) {
            cmd_error("find", "expected ')' but reached end of expression");
            program->status = 1;
            return -1;
        }
        (*index)++;
        return inner;
    }
    if (strcmp(word, "!") == 0 || strcmp(word, "-not") == 0) {
        int node = find_node(program, F_NOT);
        program->nodes[node].left = find_parse_primary(program, argv, index, argc);
        return node;
    }
    FindOp op;
    int needs_value = 1;
    if (strcmp(word, "-name") == 0) op = F_NAME;
    else if (strcmp(word, "-iname") == 0) op = F_INAME;
    else if (strcmp(word, "-path") == 0 || strcmp(word, "-wholename") == 0) op = F_PATH;
    else if (strcmp(word, "-ipath") == 0 || strcmp(word, "-iwholename") == 0) op = F_IPATH;
    else if (strcmp(word, "-type") == 0) op = F_TYPE;
    else if (strcmp(word, "-size") == 0) op = F_SIZE;
    else if (strcmp(word, "-mtime") == 0) op = F_MTIME;
    else if (strcmp(word, "-mmin") == 0) op = F_MMIN;
    else if (strcmp(word, "-newer") == 0) op = F_NEWER;
    else if (strcmp(word, "-regex") == 0) op = F_REGEX;
    else if (strcmp(word, "-perm") == 0) op = F_PERM;
    else if (strcmp(word, "-maxdepth") == 0) op = F_MAXDEPTH;
    else if (strcmp(word, "-mindepth") == 0) op = F_MINDEPTH;
    else if (strcmp(word, "-exec") == 0 || strcmp(word, "-execdir") == 0 || strcmp(word, "-ok") == 0) op = F_EXEC;
    else {
        needs_value = 0;
        if (strcmp(word, "-empty") == 0) op = F_EMPTY;
        else if (strcmp(word, "-print") == 0) op = F_PRINT;
        else if (strcmp(word, "-print0") == 0) op = F_PRINT0;
        else if (strcmp(word, "-delete") == 0) op = F_DELETE;
        else if (strcmp(word, "-prune") == 0) op = F_PRUNE;
        else if (strcmp(word, "-true") == 0) op = F_TRUE;
        else if (strcmp(word, "-false") == 0) op = F_FALSE;
        else if (strcmp(word, "-quit") == 0) op = F_QUIT;
        else if (strcmp(word, "-readable") == 0) op = F_READABLE;
        else if (strcmp(word, "-writable") == 0) op = F_WRITABLE;
        else if (strcmp(word, "-executable") == 0) op = F_EXECUTABLE;
        else if (strcmp(word, "-depth") == 0 || strcmp(word, "-mount") == 0 || strcmp(word, "-xdev") == 0 ||
                 strcmp(word, "-noleaf") == 0 || strcmp(word, "-follow") == 0 || strcmp(word, "-nowarn") == 0)
            op = F_TRUE;
        else {
            cmd_error("find", "unknown predicate `%s'", word);
            program->status = 1;
            return -1;
        }
    }
    int node = find_node(program, op);
    if (node < 0) return -1;
    FindNode *n = &program->nodes[node];
    if (op == F_PRINT || op == F_PRINT0 || op == F_DELETE || op == F_EXEC || op == F_QUIT) program->has_action = 1;
    if (op == F_PRINT0) program->print0 = 1;
    if (!needs_value) return node;
    if (op == F_EXEC) {
        while (*index < argc) {
            const char *arg = argv[*index];
            (*index)++;
            if (strcmp(arg, ";") == 0) break;
            if (strcmp(arg, "+") == 0 && n->exec_args.len > 0 && strcmp(n->exec_args.items[n->exec_args.len - 1], "{}") == 0) {
                n->exec_plus = 1;
                break;
            }
            sl_push_copy(&n->exec_args, arg);
        }
        if (n->exec_args.len == 0) {
            cmd_error("find", "missing argument to `-exec'");
            program->status = 1;
            return -1;
        }
        return node;
    }
    if (*index >= argc) {
        cmd_error("find", "missing argument to `%s'", word);
        program->status = 1;
        return -1;
    }
    const char *value = argv[*index];
    (*index)++;
    n->text = xstrdup(value);
    if (op == F_TYPE) n->type = value[0];
    if (op == F_SIZE || op == F_MTIME || op == F_MMIN || op == F_MAXDEPTH || op == F_MINDEPTH || op == F_PERM) {
        const char *digits = value;
        if (*digits == '+' || *digits == '-' || *digits == '/') {
            n->sign = *digits;
            digits++;
        }
        n->number = op == F_PERM ? strtoll(digits, NULL, 8) : atoll(digits);
        if (op == F_SIZE) {
            const char *suffix = digits;
            while (isdigit((unsigned char)*suffix)) suffix++;
            long long unit = 512;
            switch (*suffix) {
            case 'c': unit = 1; break;
            case 'w': unit = 2; break;
            case 'k': unit = 1024; break;
            case 'M': unit = 1024 * 1024; break;
            case 'G': unit = 1024LL * 1024 * 1024; break;
            default: unit = 512; break;
            }
            n->number *= unit;
            n->type = *suffix ? *suffix : 'b';
        }
        if (op == F_MAXDEPTH) program->max_depth = (int)n->number;
        if (op == F_MINDEPTH) program->min_depth = (int)n->number;
        if (op == F_MAXDEPTH || op == F_MINDEPTH) n->op = F_TRUE;
    }
    return node;
}

static int find_parse_and(FindProgram *program, char **argv, int *index, int argc) {
    int left = find_parse_primary(program, argv, index, argc);
    while (left >= 0 && *index < argc) {
        const char *word = argv[*index];
        if (strcmp(word, ")") == 0 || strcmp(word, "-o") == 0 || strcmp(word, "-or") == 0) break;
        if (strcmp(word, "-a") == 0 || strcmp(word, "-and") == 0 || strcmp(word, ",") == 0) (*index)++;
        int right = find_parse_primary(program, argv, index, argc);
        if (right < 0) return -1;
        int node = find_node(program, F_AND);
        program->nodes[node].left = left;
        program->nodes[node].right = right;
        left = node;
    }
    return left;
}

static int find_parse_or(FindProgram *program, char **argv, int *index, int argc) {
    int left = find_parse_and(program, argv, index, argc);
    while (left >= 0 && *index < argc && (strcmp(argv[*index], "-o") == 0 || strcmp(argv[*index], "-or") == 0)) {
        (*index)++;
        int right = find_parse_and(program, argv, index, argc);
        if (right < 0) return -1;
        int node = find_node(program, F_OR);
        program->nodes[node].left = left;
        program->nodes[node].right = right;
        left = node;
    }
    return left;
}

typedef struct {
    const char *path;
    const char *name;
    WIN32_FIND_DATAA *data;
    int is_dir;
    int prune;
} FindEntry;

static int glob_match_case(const char *pattern, const char *text, int icase) {
    int saved = shell.nocasematch;
    shell.nocasematch = icase;
    int matched = pattern_match(pattern, text);
    shell.nocasematch = saved;
    return matched;
}

static int find_eval(FindProgram *program, int index, FindEntry *entry) {
    if (index < 0) return 1;
    FindNode *node = &program->nodes[index];
    switch (node->op) {
    case F_TRUE: return 1;
    case F_FALSE: return 0;
    case F_NOT: return !find_eval(program, node->left, entry);
    case F_AND: return find_eval(program, node->left, entry) && find_eval(program, node->right, entry);
    case F_OR: return find_eval(program, node->left, entry) || find_eval(program, node->right, entry);
    case F_NAME: return glob_match_case(node->text, entry->name, 0);
    case F_INAME: return glob_match_case(node->text, entry->name, 1);
    case F_PATH: return glob_match_case(node->text, entry->path, 0);
    case F_IPATH: return glob_match_case(node->text, entry->path, 1);
    case F_REGEX: {
        RegexMatch match;
        return regex_search(node->text, entry->path, &match) && match.start[0] == 0 && entry->path[match.end[0]] == '\0';
    }
    case F_TYPE: {
        int is_link = 0;
#ifndef _WIN32
        struct stat st;
        if (lstat(entry->path, &st) == 0) is_link = S_ISLNK(st.st_mode);
#endif
        if (node->type == 'd') return entry->is_dir;
        if (node->type == 'f') return !entry->is_dir && !is_link;
        if (node->type == 'l') return is_link;
        return 0;
    }
    case F_EMPTY:
        if (entry->is_dir) {
            StrList names;
            sl_init(&names);
            list_directory(entry->path, &names);
            int empty = names.len == 0;
            sl_free(&names);
            return empty;
        }
        return entry->data->nFileSizeHigh == 0 && entry->data->nFileSizeLow == 0;
    case F_SIZE: {
        long long size = (long long)(((unsigned long long)entry->data->nFileSizeHigh << 32) | entry->data->nFileSizeLow);
        long long unit = node->type == 'c' ? 1 : node->type == 'w' ? 2 : node->type == 'k' ? 1024 : node->type == 'M' ? 1024 * 1024 : node->type == 'G' ? 1024LL * 1024 * 1024 : 512;
        long long rounded = (size + unit - 1) / unit * unit;
        if (node->sign == '+') return rounded > node->number;
        if (node->sign == '-') return rounded < node->number;
        return rounded == node->number;
    }
    case F_MTIME:
    case F_MMIN: {
        unsigned long long ticks = ((unsigned long long)entry->data->ftLastWriteTime.dwHighDateTime << 32) | entry->data->ftLastWriteTime.dwLowDateTime;
        time_t modified = (time_t)(ticks / 10000000ULL - 11644473600ULL);
        double age = difftime(time(NULL), modified);
        long long units = node->op == F_MTIME ? (long long)(age / 86400) : (long long)(age / 60);
        if (node->sign == '+') return units > node->number;
        if (node->sign == '-') return units < node->number;
        return units == node->number;
    }
    case F_NEWER: {
        WIN32_FILE_ATTRIBUTE_DATA reference;
        if (!GetFileAttributesExA(node->text, GetFileExInfoStandard, &reference)) return 0;
        return CompareFileTime(&entry->data->ftLastWriteTime, &reference.ftLastWriteTime) > 0;
    }
    case F_PERM: {
        unsigned mode = current_mode(entry->path) & 07777;
        if (node->sign == '-') return (mode & (unsigned)node->number) == (unsigned)node->number;
        if (node->sign == '/') return (mode & (unsigned)node->number) != 0;
        return mode == (unsigned)node->number;
    }
    case F_READABLE: return 1;
    case F_WRITABLE: return !(entry->data->dwFileAttributes & FILE_ATTRIBUTE_READONLY);
    case F_EXECUTABLE:
#ifdef _WIN32
        return entry->is_dir || str_ieq(path_ext(entry->name), ".exe") || str_ieq(path_ext(entry->name), ".bat") || str_ieq(path_ext(entry->name), ".cmd");
#else
        return access(entry->path, X_OK) == 0;
#endif
    case F_PRINT:
        printf("%s\n", entry->path);
        return 1;
    case F_PRINT0:
        printf("%s%c", entry->path, '\0');
        return 1;
    case F_DELETE:
        if (delete_entry(entry->path, entry->is_dir) != 0) {
            cmd_error("find", "cannot delete '%s': %s", entry->path, strerror(errno));
            program->status = 1;
            return 0;
        }
        return 1;
    case F_PRUNE:
        entry->prune = 1;
        return 1;
    case F_QUIT:
        program->quit = 1;
        return 1;
    case F_EXEC: {
        StrBuf command;
        sb_init(&command);
        for (size_t i = 0; i < node->exec_args.len; i++) {
            if (i > 0) sb_putc(&command, ' ');
            const char *arg = node->exec_args.items[i];
            const char *brace = strstr(arg, "{}");
            if (brace) {
                StrBuf expanded;
                sb_init(&expanded);
                for (const char *p = arg; *p;) {
                    if (p[0] == '{' && p[1] == '}') {
                        sb_puts(&expanded, entry->path);
                        p += 2;
                    } else sb_putc(&expanded, *p++);
                }
                sb_put_quoted(&command, expanded.data);
                sb_free(&expanded);
            } else sb_put_quoted(&command, arg);
        }
        int status = exec_text(command.data);
        sb_free(&command);
        return status == 0;
    }
    default: return 0;
    }
}

static void find_walk(FindProgram *program, const char *path, const char *name, WIN32_FIND_DATAA *data, int depth) {
    if (program->quit) return;
    FindEntry entry = {path, name, data, (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, 0};
    if (depth >= program->min_depth) {
        int matched = find_eval(program, program->root, &entry);
        if (matched && !program->has_action) printf("%s\n", path);
    }
    if (!entry.is_dir || entry.prune || program->quit) return;
    if (program->max_depth >= 0 && depth >= program->max_depth) return;
    StrList names;
    sl_init(&names);
    list_directory(path, &names);
    for (size_t i = 0; i < names.len && !program->quit; i++) {
        char child[PATH_BUF];
        snprintf(child, sizeof(child), "%s/%s", path, names.items[i]);
        WIN32_FIND_DATAA child_data;
        HANDLE find = FindFirstFileA(child, &child_data);
        if (find == INVALID_HANDLE_VALUE) continue;
        FindClose(find);
        find_walk(program, child, names.items[i], &child_data, depth + 1);
    }
    sl_free(&names);
}

static int cmd_find(int argc, char **argv) {
    FindProgram program;
    memset(&program, 0, sizeof(program));
    program.max_depth = -1;
    program.root = -1;

    int index = 1;
    StrList roots;
    sl_init(&roots);
    while (index < argc && argv[index][0] != '-' && strcmp(argv[index], "(") != 0 && strcmp(argv[index], "!") != 0) {
        if (strcmp(argv[index], "--help") == 0) break;
        sl_push_copy(&roots, argv[index]);
        index++;
    }
    while (index < argc && (strcmp(argv[index], "-H") == 0 || strcmp(argv[index], "-L") == 0 || strcmp(argv[index], "-P") == 0)) index++;
    if (index < argc && strcmp(argv[index], "--help") == 0) {
        printf("Usage: find [path...] [expression]\n");
        sl_free(&roots);
        return 0;
    }
    if (roots.len == 0) sl_push_copy(&roots, ".");
    if (index < argc) {
        program.root = find_parse_or(&program, argv, &index, argc);
        if (program.status) {
            sl_free(&roots);
            return 1;
        }
        if (index < argc) {
            cmd_error("find", "paths must precede expression: `%s'", argv[index]);
            sl_free(&roots);
            return 1;
        }
    }
    for (size_t r = 0; r < roots.len && !program.quit; r++) {
        char root[PATH_BUF];
        snprintf(root, sizeof(root), "%s", roots.items[r]);
        if (strlen(root) > 1) {
            size_t length = strlen(root);
            while (length > 1 && (root[length - 1] == '/' || root[length - 1] == '\\')) root[--length] = '\0';
        }
        WIN32_FIND_DATAA data;
        HANDLE find = FindFirstFileA(root, &data);
        if (find == INVALID_HANDLE_VALUE) {
            cmd_error("find", "'%s': No such file or directory", roots.items[r]);
            program.status = 1;
            continue;
        }
        FindClose(find);
        const char *name = path_last_sep(root);
        find_walk(&program, root, name ? name + 1 : root, &data, 0);
    }
    for (int i = 0; i < program.count; i++) {
        free(program.nodes[i].text);
        sl_free(&program.nodes[i].exec_args);
    }
    sl_free(&roots);
    return program.status;
}

static int cmd_basename(int argc, char **argv) {
    static const OptSpec specs[] = {{'a', "multiple", 0}, {'s', "suffix", 1}, {'z', "zero", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "basename", &args, 1);
    if (early >= 0) return early;
    int multiple = args_has(&args, 'a') || args_has(&args, 's');
    const char *suffix = args_value(&args, 's');
    if (args.operand_count == 0) {
        cmd_error("basename", "missing operand");
        fprintf(stderr, "Try 'basename --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    if (!multiple && args.operand_count > 2) {
        cmd_error("basename", "extra operand '%s'", args.operands[2]);
        fprintf(stderr, "Try 'basename --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    int count = multiple ? args.operand_count : 1;
    if (!multiple && args.operand_count == 2) suffix = args.operands[1];
    char terminator = args_has(&args, 'z') ? '\0' : '\n';
    for (int i = 0; i < count; i++) {
        char native[PATH_BUF];
        snprintf(native, sizeof(native), "%s", args.operands[i]);
        size_t length = strlen(native);
        while (length > 1 && (native[length - 1] == '/' || native[length - 1] == '\\')) native[--length] = '\0';
        const char *leaf = path_last_sep(native);
        leaf = leaf && leaf[1] ? leaf + 1 : (leaf ? leaf : native);
        char result[PATH_BUF];
        snprintf(result, sizeof(result), "%s", leaf);
        if (suffix && *suffix && strlen(result) > strlen(suffix) && str_has_suffix_i(result, suffix) &&
            strcmp(result + strlen(result) - strlen(suffix), suffix) == 0)
            result[strlen(result) - strlen(suffix)] = '\0';
        printf("%s%c", result, terminator);
    }
    args_free(&args);
    return 0;
}

static int cmd_dirname(int argc, char **argv) {
    static const OptSpec specs[] = {{'z', "zero", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "dirname", &args, 1);
    if (early >= 0) return early;
    if (args.operand_count == 0) {
        cmd_error("dirname", "missing operand");
        fprintf(stderr, "Try 'dirname --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    char terminator = args_has(&args, 'z') ? '\0' : '\n';
    for (int i = 0; i < args.operand_count; i++) {
        char native[PATH_BUF];
        snprintf(native, sizeof(native), "%s", args.operands[i]);
        size_t length = strlen(native);
        while (length > 1 && (native[length - 1] == '/' || native[length - 1] == '\\')) native[--length] = '\0';
        char *leaf = path_last_sep(native);
        if (!leaf) {
            printf(".%c", terminator);
            continue;
        }
        while (leaf > native && (leaf[-1] == '/' || leaf[-1] == '\\')) leaf--;
        if (leaf == native) leaf[1] = '\0';
        else *leaf = '\0';
        printf("%s%c", native, terminator);
    }
    args_free(&args);
    return 0;
}

static int resolve_path(const char *path, int must_exist, int logical, char *out, size_t size) {
    (void)logical;
#ifndef _WIN32
    char resolved[4096];
    if (realpath(path, resolved)) {
        snprintf(out, size, "%s", resolved);
        return 1;
    }
    if (must_exist) return 0;
    char parent[PATH_BUF];
    snprintf(parent, sizeof(parent), "%s", path);
    char *sep = strrchr(parent, '/');
    const char *leaf = sep ? sep + 1 : parent;
    char resolved_parent[4096];
    if (sep) {
        if (sep == parent) snprintf(resolved_parent, sizeof(resolved_parent), "/");
        else {
            *sep = '\0';
            if (!realpath(parent, resolved_parent)) return 0;
        }
    } else if (!getcwd(resolved_parent, sizeof(resolved_parent))) return 0;
    snprintf(out, size, "%s/%s", strcmp(resolved_parent, "/") == 0 ? "" : resolved_parent, leaf);
    return 1;
#else
    if (must_exist && !exists(path)) return 0;
    if (!GetFullPathNameA(path, (DWORD)size, out, NULL)) return 0;
    path_to_slashes(out);
    return 1;
#endif
}

static size_t split_components(const char *path, char **parts, size_t max, char *storage, size_t storage_size) {
    snprintf(storage, storage_size, "%s", path);
    size_t count = 0;
    char *cursor = storage;
    char *piece;
    while ((piece = str_next_field(&cursor, '/')) != NULL) {
        if (*piece && strcmp(piece, ".") != 0 && count < max) parts[count++] = piece;
    }
    return count;
}

static void relative_path(const char *from, const char *to, char *out, size_t size) {
    char from_store[PATH_BUF], to_store[PATH_BUF];
    char *from_parts[128], *to_parts[128];
    size_t from_count = split_components(from, from_parts, 128, from_store, sizeof(from_store));
    size_t to_count = split_components(to, to_parts, 128, to_store, sizeof(to_store));
    size_t common = 0;
    while (common < from_count && common < to_count && strcmp(from_parts[common], to_parts[common]) == 0) common++;
    StrBuf result;
    sb_init(&result);
    for (size_t i = common; i < from_count; i++) {
        if (result.len) sb_putc(&result, '/');
        sb_puts(&result, "..");
    }
    for (size_t i = common; i < to_count; i++) {
        if (result.len) sb_putc(&result, '/');
        sb_puts(&result, to_parts[i]);
    }
    if (result.len == 0) sb_puts(&result, ".");
    snprintf(out, size, "%s", result.data);
    sb_free(&result);
}

static int cmd_realpath(int argc, char **argv) {
    static const OptSpec specs[] = {{'e', "canonicalize-existing", 0}, {'m', "canonicalize-missing", 0},
                                    {'L', "logical", 0}, {'P', "physical", 0}, {'q', "quiet", 0},
                                    {'s', "strip", 0}, {'s', "no-symlinks", 0}, {'z', "zero", 0},
                                    {'1', "relative-to", 1}, {'2', "relative-base", 1}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "realpath", &args, 1);
    if (early >= 0) return early;
    if (args.operand_count == 0) {
        cmd_error("realpath", "missing operand");
        fprintf(stderr, "Try 'realpath --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    int status = 0;
    char terminator = args_has(&args, 'z') ? '\0' : '\n';
    char base[PATH_BUF] = "";
    if (args_has(&args, '1')) resolve_path(args_value(&args, '1'), 0, 0, base, sizeof(base));
    for (int i = 0; i < args.operand_count; i++) {
        char full[PATH_BUF];
        if (!resolve_path(args.operands[i], args_has(&args, 'e'), args_has(&args, 'L'), full, sizeof(full))) {
            if (!args_has(&args, 'q')) cmd_error("realpath", "%s: No such file or directory", args.operands[i]);
            status = 1;
            continue;
        }
        if (*base) {
            char relative[PATH_BUF];
            relative_path(base, full, relative, sizeof(relative));
            printf("%s%c", relative, terminator);
        } else printf("%s%c", full, terminator);
    }
    args_free(&args);
    return status;
}

static int cmd_readlink(int argc, char **argv) {
    static const OptSpec specs[] = {{'f', "canonicalize", 0}, {'e', "canonicalize-existing", 0},
                                    {'m', "canonicalize-missing", 0}, {'n', "no-newline", 0},
                                    {'q', "quiet", 0}, {'s', "silent", 0}, {'v', "verbose", 0},
                                    {'z', "zero", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "readlink", &args, 1);
    if (early >= 0) return early;
    if (args.operand_count == 0) {
        cmd_error("readlink", "missing operand");
        fprintf(stderr, "Try 'readlink --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    int canonical = args_has(&args, 'f') || args_has(&args, 'e') || args_has(&args, 'm');
    int status = 0;
    for (int i = 0; i < args.operand_count; i++) {
        char full[PATH_BUF];
        if (canonical) {
            if (!resolve_path(args.operands[i], args_has(&args, 'e'), 0, full, sizeof(full))) {
                if (args_has(&args, 'v')) cmd_error("readlink", "%s: No such file or directory", args.operands[i]);
                status = 1;
                continue;
            }
        } else {
#ifndef _WIN32
            ssize_t length = readlink(args.operands[i], full, sizeof(full) - 1);
            if (length < 0) {
                if (args_has(&args, 'v')) cmd_error("readlink", "%s: %s", args.operands[i], strerror(errno));
                status = 1;
                continue;
            }
            full[length] = '\0';
#else
            status = 1;
            continue;
#endif
        }
        printf("%s", full);
        if (!args_has(&args, 'n')) putchar(args_has(&args, 'z') ? '\0' : '\n');
    }
    args_free(&args);
    return status;
}

static int cmd_mktemp(int argc, char **argv) {
    static const OptSpec specs[] = {{'d', "directory", 0}, {'u', "dry-run", 0}, {'q', "quiet", 0},
                                    {'p', NULL, 1}, {'P', "tmpdir", 2}, {'t', NULL, 0}, {'1', "suffix", 1}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "mktemp", &args, 1);
    if (early >= 0) return early;
    if (args.operand_count > 1) {
        cmd_error("mktemp", "too many templates");
        args_free(&args);
        return 1;
    }
    const char *template = args.operand_count ? args.operands[0] : "tmp.XXXXXXXXXX";
    const char *suffix = args_has(&args, '1') ? args_value(&args, '1') : "";
    char directory[PATH_BUF] = "";
    int use_tmpdir = args_has(&args, 't') || args.operand_count == 0 || args_has(&args, 'p') || args_has(&args, 'P');
    const char *chosen = args_has(&args, 'p') ? args_value(&args, 'p') : args_value(&args, 'P');
    if (chosen && *chosen) snprintf(directory, sizeof(directory), "%s", chosen);
    else if (use_tmpdir && !path_last_sep(template)) {
        const char *env = getenv("TMPDIR");
        if (env && *env) snprintf(directory, sizeof(directory), "%s", env);
        else {
            GetTempPathA(sizeof(directory), directory);
            size_t length = strlen(directory);
            while (length > 1 && (directory[length - 1] == '\\' || directory[length - 1] == '/')) directory[--length] = '\0';
        }
    }
    const char *run = strstr(template, "XXX");
    if (!run) {
        cmd_error("mktemp", "too few X's in template '%s'", template);
        args_free(&args);
        return 1;
    }
    size_t x_count = 0;
    while (run[x_count] == 'X') x_count++;
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned)GetTickCount() ^ (unsigned)GetCurrentProcessId());
        seeded = 1;
    }
    for (int attempt = 0; attempt < 100; attempt++) {
        char name[PATH_BUF];
        snprintf(name, sizeof(name), "%s", template);
        char *xs = strstr(name, "XXX");
        for (size_t i = 0; i < x_count; i++) xs[i] = alphabet[rand() % (int)(sizeof(alphabet) - 1)];
        char full[PATH_BUF];
        if (*directory) snprintf(full, sizeof(full), "%s/%s%s", directory, name, suffix);
        else snprintf(full, sizeof(full), "%s%s", name, suffix);
        path_to_slashes(full);
        if (exists(full)) continue;
        if (args_has(&args, 'u')) {
            printf("%s\n", full);
            args_free(&args);
            return 0;
        }
        if (args_has(&args, 'd')) {
            if (_mkdir(full) != 0) continue;
        } else {
            FILE *f = fopen(full, "wb");
            if (!f) continue;
            fclose(f);
#ifndef _WIN32
            chmod(full, 0600);
#endif
        }
        printf("%s\n", full);
        args_free(&args);
        return 0;
    }
    if (!args_has(&args, 'q')) cmd_error("mktemp", "failed to create %s via template '%s'", args_has(&args, 'd') ? "directory" : "file", template);
    args_free(&args);
    return 1;
}

static int cmd_file(int argc, char **argv) {
    static const OptSpec specs[] = {{'b', "brief", 0}, {'i', "mime", 0}, {'L', "dereference", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "file", &args, 1);
    if (early >= 0) return early;
    int status = 0;
    for (int i = 0; i < args.operand_count; i++) {
        const char *kind;
        if (path_is_dir(args.operands[i])) kind = "directory";
        else {
            FILE *f = fopen(args.operands[i], "rb");
            if (!f) {
                static char missing[PATH_BUF + 64];
                snprintf(missing, sizeof(missing), "cannot open `%s' (No such file or directory)", args.operands[i]);
                kind = missing;
            } else {
                unsigned char head[512] = {0};
                size_t n = fread(head, 1, sizeof(head), f);
                fclose(f);
                if (n == 0) kind = "empty";
                else if (n >= 2 && head[0] == 'M' && head[1] == 'Z') kind = "PE32+ executable (Windows)";
                else if (n >= 4 && memcmp(head, "\x7f" "ELF", 4) == 0) kind = "ELF executable";
                else if (n >= 4 && (memcmp(head, "\xcf\xfa\xed\xfe", 4) == 0 || memcmp(head, "\xca\xfe\xba\xbe", 4) == 0)) kind = "Mach-O executable";
                else if (n >= 8 && memcmp(head, "\x89PNG", 4) == 0) kind = "PNG image data";
                else if (n >= 3 && head[0] == 0xff && head[1] == 0xd8) kind = "JPEG image data";
                else if (n >= 4 && memcmp(head, "GIF8", 4) == 0) kind = "GIF image data";
                else if (n >= 2 && head[0] == 'P' && head[1] == 'K') kind = "Zip archive data";
                else if (n >= 2 && head[0] == 0x1f && head[1] == 0x8b) kind = "gzip compressed data";
                else if (n >= 4 && memcmp(head, "%PDF", 4) == 0) kind = "PDF document";
                else if (n >= 2 && head[0] == '#' && head[1] == '!') kind = "script text executable";
                else {
                    int text = 1;
                    for (size_t c = 0; c < n; c++) {
                        if (head[c] == 0) text = 0;
                    }
                    kind = text ? "ASCII text" : "data";
                    if (text) {
                        for (size_t c = 0; c < n; c++)
                            if (head[c] >= 128) kind = "UTF-8 Unicode text";
                    }
                }
            }
        }
        if (args_has(&args, 'b')) printf("%s\n", kind);
        else printf("%s: %s\n", args.operands[i], kind);
    }
    args_free(&args);
    return status;
}

static int cmd_truncate(int argc, char **argv) {
    static const OptSpec specs[] = {{'s', "size", 1}, {'c', "no-create", 0}, {'o', "io-blocks", 0},
                                    {'r', "reference", 1}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "truncate", &args, 1);
    if (early >= 0) return early;
    if (!args_has(&args, 's') && !args_has(&args, 'r')) {
        cmd_error("truncate", "you must specify either '--size' or '--reference'");
        args_free(&args);
        return 1;
    }
    if (args.operand_count == 0) {
        cmd_error("truncate", "missing file operand");
        args_free(&args);
        return 1;
    }
    const char *size_text = args_value(&args, 's');
    char mode = 0;
    long long wanted = 0;
    if (size_text) {
        if (*size_text == '+' || *size_text == '-' || *size_text == '<' || *size_text == '>' || *size_text == '/' || *size_text == '%') mode = *size_text++;
        int ok;
        wanted = cmd_parse_size(size_text, &ok);
        if (!ok) {
            cmd_error("truncate", "invalid number: '%s'", args_value(&args, 's'));
            args_free(&args);
            return 1;
        }
    } else {
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (!GetFileAttributesExA(args_value(&args, 'r'), GetFileExInfoStandard, &info)) {
            cmd_error("truncate", "cannot stat '%s': No such file or directory", args_value(&args, 'r'));
            args_free(&args);
            return 1;
        }
        wanted = (long long)(((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow);
    }
    int status = 0;
    for (int i = 0; i < args.operand_count; i++) {
        const char *path = args.operands[i];
        if (!exists(path) && args_has(&args, 'c')) continue;
        HANDLE handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            cmd_error("truncate", "cannot open '%s' for writing: %s", path, strerror(errno));
            status = 1;
            continue;
        }
        WIN32_FILE_ATTRIBUTE_DATA info;
        long long current = 0;
        if (GetFileAttributesExA(path, GetFileExInfoStandard, &info)) current = (long long)(((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow);
        long long target = wanted;
        if (mode == '+') target = current + wanted;
        else if (mode == '-') target = current - wanted;
        else if (mode == '<') target = current < wanted ? current : wanted;
        else if (mode == '>') target = current > wanted ? current : wanted;
        else if (mode == '/') target = wanted ? (current / wanted) * wanted : current;
        else if (mode == '%') target = wanted ? ((current + wanted - 1) / wanted) * wanted : current;
        if (target < 0) target = 0;
        SetFilePointer(handle, (LONG)target, NULL, FILE_BEGIN);
        SetEndOfFile(handle);
        CloseHandle(handle);
    }
    args_free(&args);
    return status;
}

const Command FILE_COMMANDS[] = {
    {"basename", cmd_basename, "NAME [SUFFIX] | -a [-s SUFFIX] NAME..."},
    {"chmod", cmd_chmod, "[-Rvc] MODE FILE..."},
    {"cp", cmd_cp, "[-rfnpuv] [-t DIR] SOURCE... DEST"},
    {"df", cmd_df, "[-hHkmT] [FILE]..."},
    {"dirname", cmd_dirname, "NAME..."},
    {"du", cmd_du, "[-abchkms] [-d DEPTH] [FILE]..."},
    {"file", cmd_file, "[-b] FILE..."},
    {"find", cmd_find, "[path...] [expression]"},
    {"ln", cmd_ln, "[-sfnv] [-t DIR] TARGET... LINK"},
    {"mkdir", cmd_mkdir, "[-pv] [-m MODE] DIRECTORY..."},
    {"mktemp", cmd_mktemp, "[-duq] [-p DIR] [--suffix=SUF] [TEMPLATE]"},
    {"mv", cmd_mv, "[-fnuv] [-t DIR] SOURCE... DEST"},
    {"readlink", cmd_readlink, "[-femnqsvz] FILE..."},
    {"realpath", cmd_realpath, "[-emqsz] [--relative-to=DIR] FILE..."},
    {"rm", cmd_rm, "[-rfdv] FILE..."},
    {"rmdir", cmd_rmdir, "[-pv] [--ignore-fail-on-non-empty] DIRECTORY..."},
    {"stat", cmd_stat, "[-Lt] [-c FORMAT] FILE..."},
    {"touch", cmd_touch, "[-acm] [-d DATE] [-r FILE] FILE..."},
    {"truncate", cmd_truncate, "-s SIZE | -r FILE FILE..."},
};

const size_t FILE_COMMAND_COUNT = END(FILE_COMMANDS);
