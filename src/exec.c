/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "exec.h"

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <windows.h>

#include <tlhelp32.h>

#include "builtins.h"
#include "coreutils.h"
#include "expand.h"
#include "foreign.h"
#include "parser.h"
#include "regex.h"
#include "style.h"
#include "table.h"
#include "shell.h"
#include "vars.h"

#define MAX_STAGES 32
#define MAX_TRACKED 128

#define EXTRA_FDS 8

typedef struct {
    int fd;
    HANDLE handle;
} ExtraFd;

typedef struct {
    HANDLE in;
    HANDLE out;
    HANDLE err;
    ExtraFd extra[EXTRA_FDS];
    int extra_count;
} IoSet;

typedef struct {
    int saved[3];
    int extra_fd[EXTRA_FDS];
    int extra_saved[EXTRA_FDS];
    int extra_count;
} FdSave;

static Table function_table;

static HANDLE tracked[MAX_TRACKED];
static int tracked_count = 0;

static StrList command_cache;
static int command_cache_valid = 0;

static int exec_command(Node *node, IoSet io, int background, HANDLE *async_out);
static int exec_pipeline(Node *node, int background);
static void job_add(HANDLE process, const char *command);
static void resolutions_release(void);
static int skip_functions = 0;
static int keep_redirections = 0;

void exec_keep_redirections(void) {
    keep_redirections = 1;
}

static StrList temp_free_list;
static StrList temp_created;

void exec_init(void) {
    sl_init(&command_cache);
    sl_init(&temp_free_list);
    sl_init(&temp_created);
}

static char *temp_acquire(void) {
    if (temp_free_list.len > 0) return temp_free_list.items[--temp_free_list.len];

    char directory[PATH_BUF];
    char file[PATH_BUF];
    if (!GetTempPathA(sizeof(directory), directory)) return NULL;
    if (!GetTempFileNameA(directory, "frsh", 0, file)) return NULL;

    sl_push_copy(&temp_created, file);
    return xstrdup(file);
}

static void temp_release(char *path) {
    if (path) sl_push(&temp_free_list, path);
}

static void temp_cleanup(void) {
    for (size_t i = 0; i < temp_created.len; i++) DeleteFileA(temp_created.items[i]);
    sl_free(&temp_created);
    sl_free(&temp_free_list);
}

void exec_cleanup(void) {
    table_free(&function_table);
    sl_free(&command_cache);
    resolutions_release();
    temp_cleanup();
}

static void function_release(void *body) {
    node_free(body);
}

static Node *function_find(const char *name) {
    return table_get(&function_table, name);
}

void function_define(const char *name, Node *body) {
    if (!function_table.buckets) table_init(&function_table, 32, 0, function_release);
    table_put(&function_table, name, body);
}

int function_defined(const char *name) {
    return function_find(name) != NULL;
}

int function_undefine(const char *name) {
    return table_remove(&function_table, name);
}

void function_names(StrList *out) {
    table_names(&function_table, out);
}

int function_name_prefix(const char *prefix, size_t length) {
    return table_has_prefix(&function_table, prefix, length);
}

static void pathext_list(StrList *out) {
    const char *pathext = var_get("PATHEXT");
    if (!pathext || !*pathext) pathext = ".COM;.EXE;.BAT;.CMD";
    char *copy = xstrdup(pathext);
    char *cursor = copy;
    char *token;
    while ((token = str_next_field(&cursor, ';')) != NULL) {
        if (*token) sl_push_copy(out, token);
    }
    free(copy);
    sl_push_copy(out, ".frsh");
    sl_push_copy(out, ".ps1");
    sl_push_copy(out, ".sh");
}

static int try_with_extensions(const char *base, char *out, size_t out_size, int with_extensions) {
    if (path_is_file(base)) {
        snprintf(out, out_size, "%s", base);
        return 1;
    }
    if (!with_extensions) return 0;

    StrList extensions;
    sl_init(&extensions);
    pathext_list(&extensions);
    int found = 0;
    for (size_t i = 0; i < extensions.len && !found; i++) {
        char candidate[PATH_BUF];
        snprintf(candidate, sizeof(candidate), "%s%s", base, extensions.items[i]);
        if (path_is_file(candidate)) {
            snprintf(out, out_size, "%s", candidate);
            found = 1;
        }
    }
    sl_free(&extensions);
    return found;
}

static Table resolution_table;

static void resolutions_forget(void) {
    table_free(&resolution_table);
}

static void resolutions_release(void) {
    table_free(&resolution_table);
}

static const char *resolution_find(const char *name) {
    return table_get(&resolution_table, name);
}

static void resolution_remember(const char *name, const char *path) {
    if (!resolution_table.buckets) table_init(&resolution_table, 64, 1, free);
    table_put(&resolution_table, name, xstrdup(path ? path : ""));
}

int resolve_command(const char *name, char *out, size_t out_size) {
    if (!name || !*name) return 0;

    if (strchr(name, '/') || strchr(name, '\\')) {
        char base[PATH_BUF];
        snprintf(base, sizeof(base), "%s", name);
        path_to_backslashes(base);
        return try_with_extensions(base, out, out_size, 1);
    }

    const char *remembered = resolution_find(name);
    if (remembered) {
        if (!*remembered) return 0;
        snprintf(out, out_size, "%s", remembered);
        return 1;
    }

    const char *path = var_get("PATH");
    if (!path) return 0;

    int with_extensions = path_command_exists(name);
    char *copy = xstrdup(path);
    char *cursor = copy;
    char *dir;
    int found = 0;
    while (!found && (dir = str_next_field(&cursor, ';')) != NULL) {
        if (!*dir) continue;
        char *base = path_join(dir, name);
        found = try_with_extensions(base, out, out_size, with_extensions);
        free(base);
    }
    free(copy);

    resolution_remember(name, found ? out : NULL);
    return found;
}

void path_rehash(void) {
    command_cache_valid = 0;
    resolutions_forget();
}

static char *read_registry_path(HKEY root, const char *subkey) {
    HKEY key;
    if (RegOpenKeyExA(root, subkey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return NULL;

    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExA(key, "Path", NULL, &type, NULL, &size) != ERROR_SUCCESS || size == 0) {
        RegCloseKey(key);
        return NULL;
    }
    char *raw = xmalloc(size + 1);
    if (RegQueryValueExA(key, "Path", NULL, &type, (BYTE *)raw, &size) != ERROR_SUCCESS) {
        free(raw);
        RegCloseKey(key);
        return NULL;
    }
    raw[size] = '\0';
    RegCloseKey(key);

    if (type == REG_EXPAND_SZ) {
        DWORD needed = ExpandEnvironmentStringsA(raw, NULL, 0);
        if (needed > 0) {
            char *expanded = xmalloc(needed + 1);
            if (ExpandEnvironmentStringsA(raw, expanded, needed + 1)) {
                free(raw);
                return expanded;
            }
            free(expanded);
        }
    }
    return raw;
}

static int path_listed(const char *list, const char *dir) {
    if (!list || !*list) return 0;
    size_t length = strlen(dir);
    const char *cursor = list;

    while (*cursor) {
        const char *end = strchr(cursor, ';');
        size_t span = end ? (size_t)(end - cursor) : strlen(cursor);
        if (span == length && _strnicmp(cursor, dir, length) == 0) return 1;
        if (!end) break;
        cursor = end + 1;
    }
    return 0;
}

void path_reload_environment(void) {
    StrBuf path;
    sb_init(&path);

    const char *home_bins[] = {"bin", ".local\\bin", "AppData\\Local\\bin", "scoop\\shims",
                               "AppData\\Roaming\\npm"};
    for (size_t i = 0; i < sizeof(home_bins) / sizeof(home_bins[0]); i++) {
        char *candidate = path_join(home_dir(), home_bins[i]);
        if (path_is_dir(candidate)) {
            sb_puts(&path, candidate);
            sb_putc(&path, ';');
        }
        free(candidate);
    }

    char *machine = read_registry_path(
        HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment");
    char *user = read_registry_path(HKEY_CURRENT_USER, "Environment");

    if (machine && *machine) {
        sb_puts(&path, machine);
        sb_putc(&path, ';');
    }
    if (user && *user) sb_puts(&path, user);
    free(machine);
    free(user);

    const char *current = var_get("PATH");
    if (current) {
        char *copy = xstrdup(current);
        char *cursor = copy;
        char *dir;
        while ((dir = str_next_field(&cursor, ';')) != NULL) {
            if (!*dir || path_listed(path.data, dir)) continue;
            if (path.len > 0) sb_putc(&path, ';');
            sb_puts(&path, dir);
        }
        free(copy);
    }

    if (path.len > 0) var_set_exported("PATH", path.data);
    sb_free(&path);
    command_cache_valid = 0;
    resolutions_forget();
}

static int is_command_extension(const StrList *extensions, const char *ext) {
    for (size_t i = 0; i < extensions->len; i++) {
        if (str_ieq(extensions->items[i], ext)) return 1;
    }
    return 0;
}

static int compare_names_fold(const void *a, const void *b) {
    return _stricmp(*(const char *const *)a, *(const char *const *)b);
}

static void scan_directory(const char *dir, const StrList *extensions, StrList *out) {
    char *pattern = path_join(dir, "*");
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    free(pattern);
    if (find == INVALID_HANDLE_VALUE) return;

    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const char *ext = path_ext(data.cFileName);
        if (!*ext || !is_command_extension(extensions, ext)) continue;
        sl_push(out, xstrndup(data.cFileName, strlen(data.cFileName) - strlen(ext)));
    } while (FindNextFileA(find, &data));
    FindClose(find);
}

static void drop_adjacent_duplicates(StrList *list) {
    size_t kept = 0;
    for (size_t i = 0; i < list->len; i++) {
        if (kept > 0 && _stricmp(list->items[kept - 1], list->items[i]) == 0) {
            free(list->items[i]);
            continue;
        }
        list->items[kept++] = list->items[i];
    }
    list->len = kept;
}

static void ensure_command_cache(void) {
    if (command_cache_valid) return;
    sl_clear(&command_cache);

    const char *path = var_get("PATH");
    if (path) {
        StrList extensions;
        sl_init(&extensions);
        pathext_list(&extensions);

        char *copy = xstrdup(path);
        char *cursor = copy;
        char *dir;
        while ((dir = str_next_field(&cursor, ';')) != NULL) {
            if (*dir) scan_directory(dir, &extensions, &command_cache);
        }
        free(copy);
        sl_free(&extensions);
    }

    if (command_cache.len > 1)
        qsort(command_cache.items, command_cache.len, sizeof(char *), compare_names_fold);
    drop_adjacent_duplicates(&command_cache);
    command_cache_valid = 1;
}

static size_t cache_lower_bound(const char *name, size_t prefix_length) {
    size_t low = 0;
    size_t high = command_cache.len;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int order = prefix_length ? _strnicmp(command_cache.items[mid], name, prefix_length)
                                  : _stricmp(command_cache.items[mid], name);
        if (order < 0) low = mid + 1;
        else high = mid;
    }
    return low;
}

void path_commands(StrList *out) {
    ensure_command_cache();
    for (size_t i = 0; i < command_cache.len; i++) sl_push_copy(out, command_cache.items[i]);
}

int path_command_exists(const char *name) {
    ensure_command_cache();
    size_t index = cache_lower_bound(name, 0);
    return index < command_cache.len && _stricmp(command_cache.items[index], name) == 0;
}

int path_command_prefix(const char *prefix) {
    ensure_command_cache();
    size_t length = strlen(prefix);
    if (length == 0) return 0;
    size_t index = cache_lower_bound(prefix, length);
    return index < command_cache.len && _strnicmp(command_cache.items[index], prefix, length) == 0;
}

void path_command_complete(const char *prefix, StrList *out) {
    ensure_command_cache();
    size_t length = strlen(prefix);

    for (size_t i = cache_lower_bound(prefix, length); i < command_cache.len; i++) {
        if (length && _strnicmp(command_cache.items[i], prefix, length) != 0) break;
        sl_push_copy(out, command_cache.items[i]);
    }
}

void function_complete(const char *prefix, size_t length, StrList *out) {
    table_collect_prefix(&function_table, prefix, length, out);
}

static void track_handle(HANDLE handle) {
    if (tracked_count < MAX_TRACKED) tracked[tracked_count++] = handle;
}

static void untrack_handle(HANDLE handle) {
    for (int i = 0; i < tracked_count; i++) {
        if (tracked[i] == handle) tracked[i] = NULL;
    }
}

static void release_tracked(int mark) {
    for (int i = mark; i < tracked_count; i++) {
        if (tracked[i]) CloseHandle(tracked[i]);
        tracked[i] = NULL;
    }
    if (tracked_count > mark) tracked_count = mark;
}

static void set_inherit(HANDLE handle, int enabled) {
    if (handle && handle != INVALID_HANDLE_VALUE)
        SetHandleInformation(handle, HANDLE_FLAG_INHERIT, enabled ? HANDLE_FLAG_INHERIT : 0);
}

static IoSet io_default(void) {
    IoSet io;
    memset(&io, 0, sizeof(io));
    io.in = GetStdHandle(STD_INPUT_HANDLE);
    io.out = GetStdHandle(STD_OUTPUT_HANDLE);
    io.err = GetStdHandle(STD_ERROR_HANDLE);
    return io;
}

static int io_is_default(const IoSet *io) {
    IoSet base = io_default();
    return io->in == base.in && io->out == base.out && io->err == base.err &&
           io->extra_count == 0;
}

static void discard_stdin_buffer(void) {
    fflush(stdin);
    setvbuf(stdin, NULL, _IOFBF, BUFSIZ);
}

static void fds_apply(const IoSet *io, FdSave *save) {
    IoSet base = io_default();
    save->saved[0] = save->saved[1] = save->saved[2] = -1;
    fflush(stdout);
    fflush(stderr);
    if (io->in != base.in) discard_stdin_buffer();

    HANDLE handles[3] = {io->in, io->out, io->err};
    HANDLE defaults[3] = {base.in, base.out, base.err};
    int flags[3] = {_O_RDONLY | _O_BINARY, _O_BINARY, _O_BINARY};

    for (int i = 0; i < 3; i++) {
        if (handles[i] == defaults[i]) continue;
        HANDLE copy;
        if (!DuplicateHandle(GetCurrentProcess(), handles[i], GetCurrentProcess(), &copy, 0, FALSE,
                             DUPLICATE_SAME_ACCESS))
            continue;
        save->saved[i] = _dup(i);
        int fd = _open_osfhandle((intptr_t)copy, flags[i]);
        if (fd < 0) {
            CloseHandle(copy);
            continue;
        }
        if (fd != i) {
            _dup2(fd, i);
            _close(fd);
        }
    }

    save->extra_count = 0;
    for (int i = 0; i < io->extra_count; i++) {
        int target = io->extra[i].fd;
        save->extra_fd[save->extra_count] = target;
        save->extra_saved[save->extra_count] = _dup(target);
        save->extra_count++;

        if (io->extra[i].handle == INVALID_HANDLE_VALUE) {
            _close(target);
            continue;
        }

        HANDLE copy;
        if (!DuplicateHandle(GetCurrentProcess(), io->extra[i].handle, GetCurrentProcess(), &copy,
                             0, FALSE, DUPLICATE_SAME_ACCESS))
            continue;

        int fd = _open_osfhandle((intptr_t)copy, _O_BINARY);
        if (fd < 0) {
            CloseHandle(copy);
            continue;
        }
        if (fd != target) {
            _dup2(fd, target);
            _close(fd);
        }
    }
}

static void fds_restore(FdSave *save) {
    fflush(stdout);
    fflush(stderr);
    if (save->saved[0] >= 0) discard_stdin_buffer();
    for (int i = 0; i < 3; i++) {
        if (save->saved[i] < 0) continue;
        _dup2(save->saved[i], i);
        _close(save->saved[i]);
    }

    for (int i = 0; i < save->extra_count; i++) {
        int target = save->extra_fd[i];
        if (save->extra_saved[i] < 0) {
            _close(target);
            continue;
        }
        _dup2(save->extra_saved[i], target);
        _close(save->extra_saved[i]);
    }
    save->extra_count = 0;
}

static int io_assign(IoSet *io, int fd, HANDLE handle) {
    if (fd == 0) {
        io->in = handle;
        return 1;
    }
    if (fd == 1) {
        io->out = handle;
        return 1;
    }
    if (fd == 2) {
        io->err = handle;
        return 1;
    }

    for (int i = 0; i < io->extra_count; i++) {
        if (io->extra[i].fd != fd) continue;
        io->extra[i].handle = handle;
        return 1;
    }

    if (io->extra_count >= EXTRA_FDS) {
        shell_error("a command cannot redirect more than %d extra descriptors", EXTRA_FDS);
        return 0;
    }
    io->extra[io->extra_count].fd = fd;
    io->extra[io->extra_count].handle = handle;
    io->extra_count++;
    return 1;
}

static HANDLE null_device(void) {
    static HANDLE cached = NULL;
    if (!cached) {
        SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
        cached = CreateFileA("nul", GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    }
    return cached;
}

static int apply_redirs(Redir *redirs, IoSet *io) {
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

    for (Redir *r = redirs; r; r = r->next) {
        if (r->type == R_HEREDOC || r->type == R_HEREDOC_RAW || r->type == R_HERESTRING) {
            char directory[PATH_BUF];
            char file[PATH_BUF];
            if (!GetTempPathA(sizeof(directory), directory)) return 0;
            if (!GetTempFileNameA(directory, "frhd", 0, file)) return 0;

            char *body;
            if (r->type == R_HEREDOC_RAW) body = xstrdup(r->target);
            else if (r->type == R_HERESTRING) {
                char *value = expand_single(r->target);
                StrBuf sb;
                sb_init(&sb);
                sb_puts(&sb, value);
                sb_putc(&sb, '\n');
                free(value);
                body = sb_take(&sb);
            } else body = expand_heredoc(r->target);
            FILE *f = fopen(file, "wb");
            if (f) {
                fputs(body, f);
                fclose(f);
            }
            free(body);

            HANDLE handle =
                CreateFileA(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                            OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                            NULL);
            if (handle == INVALID_HANDLE_VALUE) return 0;
            track_handle(handle);
            io->in = handle;
            continue;
        }

        if (strncmp(r->target, "<(", 2) == 0 && r->target[strlen(r->target) - 1] == ')') {
            char directory[PATH_BUF];
            char file[PATH_BUF];
            if (GetTempPathA(sizeof(directory), directory) &&
                GetTempFileNameA(directory, "frps", 0, file)) {
                char *command = xstrndup(r->target + 2, strlen(r->target) - 3);
                StrBuf script;
                sb_init(&script);
                sb_printf(&script, "%s > \"%s\"", command, file);
                exec_text(script.data);
                sb_free(&script);
                free(command);

                HANDLE handle =
                    CreateFileA(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
                if (handle == INVALID_HANDLE_VALUE) return 0;
                track_handle(handle);
                io->in = handle;
                continue;
            }
        }

        char *target = expand_single(r->target);
        if (strcmp(target, "/dev/null") == 0) {
            free(target);
            target = xstrdup("nul");
        } else if (strcmp(target, "/dev/stdout") == 0) {
            free(target);
            target = xstrdup("1");
            r->type = R_DUP;
        } else if (strcmp(target, "/dev/stderr") == 0) {
            free(target);
            target = xstrdup("2");
            r->type = R_DUP;
        }

        if (r->type == R_DUP) {
            HANDLE source;
            if (strcmp(target, "-") == 0) {
                source = INVALID_HANDLE_VALUE;
            } else {
                int fd = atoi(target);
                source = fd == 0 ? io->in : (fd == 2 ? io->err : io->out);

                if (fd >= 3) {
                    intptr_t native = _get_osfhandle(fd);
                    source = native == -1 ? INVALID_HANDLE_VALUE : (HANDLE)native;
                }
                for (int i = 0; i < io->extra_count; i++) {
                    if (io->extra[i].fd == fd) source = io->extra[i].handle;
                }
                if (source == INVALID_HANDLE_VALUE) {
                    shell_error("%s: bad file descriptor", target);
                    free(target);
                    return 0;
                }
            }
            free(target);
            if (!io_assign(io, r->fd, source)) return 0;
            continue;
        }

        char native[PATH_BUF];
        if ((size_t)snprintf(native, sizeof(native), "%s", target) >= sizeof(native)) {
            shell_error("%s: path is too long to redirect to", target);
            free(target);
            return 0;
        }
        free(target);
        path_to_backslashes(native);

        if (_stricmp(native, "nul") == 0) {
            HANDLE empty = null_device();
            if (empty == INVALID_HANDLE_VALUE) return 0;
            if (!io_assign(io, r->fd, empty)) return 0;
            continue;
        }

        HANDLE handle;
        if (r->type == R_IN) {
            handle = CreateFileA(native, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        } else if (r->type == R_APPEND) {
            handle = CreateFileA(native, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                 OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        } else {
            handle = CreateFileA(native, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        }

        if (handle == INVALID_HANDLE_VALUE) {
            shell_error("%s: cannot open file", native);
            return 0;
        }
        track_handle(handle);
        if (!io_assign(io, r->fd, handle)) return 0;
    }
    return 1;
}

static void quote_argument(StrBuf *sb, const char *arg) {
    if (*arg && !strpbrk(arg, " \t\"")) {
        sb_puts(sb, arg);
        return;
    }
    sb_putc(sb, '"');
    for (const char *p = arg; *p; p++) {
        size_t backslashes = 0;
        while (*p == '\\') {
            backslashes++;
            p++;
        }
        if (!*p) {
            for (size_t i = 0; i < backslashes * 2; i++) sb_putc(sb, '\\');
            break;
        }
        size_t repeat = *p == '"' ? backslashes * 2 + 1 : backslashes;
        for (size_t i = 0; i < repeat; i++) sb_putc(sb, '\\');
        sb_putc(sb, *p);
    }
    sb_putc(sb, '"');
}

static char *build_command_line(const char *program, char **argv, int argc, const char *prefix) {
    StrBuf sb;
    sb_init(&sb);
    if (prefix) sb_puts(&sb, prefix);
    quote_argument(&sb, program);
    for (int i = 1; i < argc; i++) {
        sb_putc(&sb, ' ');
        quote_argument(&sb, argv[i]);
    }
    return sb_take(&sb);
}

static int is_shell_script(const char *path) {
    const char *ext = path_ext(path);
    if (str_ieq(ext, ".frsh") || str_ieq(ext, ".sh") || str_ieq(ext, ".fresh")) return 1;
    if (*ext) return 0;

    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char head[128] = {0};
    size_t n = fread(head, 1, sizeof(head) - 1, f);
    fclose(f);
    if (n < 3 || head[0] != '#' || head[1] != '!') return 0;
    char *newline = strpbrk(head, "\r\n");
    if (newline) *newline = '\0';
    return strstr(head, "sh") != NULL;
}

static HANDLE spawn_process(char *command_line, IoSet *io, int *status) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = io->in;
    si.hStdOutput = io->out;
    si.hStdError = io->err;

    for (int i = 0; i < tracked_count; i++) set_inherit(tracked[i], 0);
    set_inherit(io->in, 1);
    set_inherit(io->out, 1);
    set_inherit(io->err, 1);
    fflush(stdout);
    fflush(stderr);

    if (!CreateProcessA(NULL, command_line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED) shell_error("permission denied");
        else shell_error("failed to start process (error %lu)", error);
        *status = 126;
        return NULL;
    }
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

static char **argv_from_list(const StrList *list) {
    char **argv = xmalloc((list->len + 1) * sizeof(char *));
    for (size_t i = 0; i < list->len; i++) argv[i] = list->items[i];
    argv[list->len] = NULL;
    return argv;
}

static int is_assignment(const char *word) {
    if (!isalpha((unsigned char)*word) && *word != '_') return 0;
    const char *p = word;
    while (isalnum((unsigned char)*p) || *p == '_') p++;

    if (*p == '[') {
        const char *close = strchr(p, ']');
        if (!close) return 0;
        p = close + 1;
    }
    if (*p == '+') p++;
    return *p == '=';
}

static void assign_from_word(char *word) {
    char *eq = strchr(word, '=');
    if (!eq) return;

    const char *value = eq + 1;
    int append = eq > word && eq[-1] == '+';
    char *name_end = append ? eq - 1 : eq;

    char *open = memchr(word, '[', (size_t)(name_end - word));
    if (open) {
        char *name = xstrndup(word, (size_t)(open - word));
        char *close = memchr(open, ']', (size_t)(name_end - open));
        char *index = xstrndup(open + 1, close ? (size_t)(close - open - 1) : 0);
        char *resolved = expand_single(index);

        if (append) {
            const char *previous = var_get_element(name, resolved);
            StrBuf sb;
            sb_init(&sb);
            sb_puts(&sb, previous ? previous : "");
            sb_puts(&sb, value);
            var_set_element(name, resolved, sb.data);
            sb_free(&sb);
        } else {
            var_set_element(name, resolved, value);
        }
        free(resolved);
        free(index);
        free(name);
        return;
    }

    char *name = xstrndup(word, (size_t)(name_end - word));
    if (var_is_integer(name)) {
        int ok = 1;
        long number = eval_arith(value, &ok);
        if (append) number = eval_arith(var_get(name), &ok) + number;
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%ld", number);
        var_set(name, buffer);
    } else if (append) {
        var_append(name, value);
    } else {
        var_set(name, value);
    }
    free(name);
}

static int call_function(Node *body, const StrList *args) {
    StrList saved = shell.params;
    sl_init(&shell.params);
    for (size_t i = 0; i < args->len; i++) sl_push_copy(&shell.params, args->items[i]);

    const char *previous = var_get("FUNCNAME");
    char *restore = previous ? xstrdup(previous) : NULL;
    if (args->len > 0) var_set("FUNCNAME", args->items[0]);

    scope_push();
    shell.depth++;
    int status = exec_node(body);
    shell.depth--;
    scope_pop();

    if (shell.trap_return) {
        char *handler = shell.trap_return;
        shell.trap_return = NULL;
        run_trap(handler);
        shell.trap_return = handler;
    }
    if (shell.returning) {
        shell.returning = 0;
        status = shell.last_status;
    }

    if (restore) {
        var_set("FUNCNAME", restore);
        free(restore);
    } else {
        var_unset("FUNCNAME");
    }

    sl_free(&shell.params);
    shell.params = saved;
    return status;
}

static int run_program(const char *path, char **argv, int argc, IoSet *io, int background,
                       HANDLE *async_out) {
    const char *ext = path_ext(path);
    const char *prefix = NULL;
    char *command_line;

    if (str_ieq(ext, ".ps1")) {
        prefix = "powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File ";
    } else if (str_ieq(ext, ".bat") || str_ieq(ext, ".cmd")) {
        prefix = "cmd.exe /c ";
    }
    command_line = build_command_line(path, argv, argc, prefix);

    int status = 0;
    HANDLE process = spawn_process(command_line, io, &status);
    free(command_line);
    if (!process) return status;

    if (async_out) {
        *async_out = process;
        return 0;
    }
    if (background) {
        StrBuf label;
        sb_init(&label);
        for (int i = 0; i < argc; i++) {
            if (i > 0) sb_putc(&label, ' ');
            sb_puts(&label, argv[i]);
        }
        job_add(process, label.data);
        sb_free(&label);
        return 0;
    }

    WaitForSingleObject(process, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(process, &exit_code);
    CloseHandle(process);
    return (int)exit_code;
}

typedef struct {
    char path[PATH_BUF];
    char *command;
} Substitution;

static int substitute_processes(StrList *words, Substitution *pending, int max) {
    int count = 0;

    for (size_t i = 0; i < words->len; i++) {
        char *word = words->items[i];
        int output = strncmp(word, ">(", 2) == 0;
        int input = strncmp(word, "<(", 2) == 0;
        if (!input && !output) continue;

        size_t length = strlen(word);
        if (length < 3 || word[length - 1] != ')') continue;

        if (count >= max) {
            shell_error("%s: a command cannot have more than %d process substitutions", word, max);
            return count;
        }

        char *command = xstrndup(word + 2, length - 3);
        char directory[PATH_BUF];
        char file[PATH_BUF];
        if (!GetTempPathA(sizeof(directory), directory) ||
            !GetTempFileNameA(directory, "frps", 0, file)) {
            shell_error("%s: could not make a temporary file for the substitution", word);
            free(command);
            return count;
        }

        if (input) {
            StrBuf script;
            sb_init(&script);
            sb_printf(&script, "%s > \"%s\"", command, file);
            exec_text(script.data);
            sb_free(&script);
            free(command);
            command = NULL;
        }

        snprintf(pending[count].path, PATH_BUF, "%s", file);
        pending[count].command = command;
        count++;

        free(words->items[i]);
        words->items[i] = xstrdup(file);
    }
    return count;
}

static void finish_substitutions(Substitution *pending, int count) {
    for (int i = 0; i < count; i++) {
        if (pending[i].command) {
            StrBuf script;
            sb_init(&script);
            sb_printf(&script, "%s < \"%s\"", pending[i].command, pending[i].path);
            exec_text(script.data);
            sb_free(&script);
            free(pending[i].command);
        }
        DeleteFileA(pending[i].path);
    }
}

static int edit_distance(const char *a, const char *b, int limit) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    if (lb > 96 || la > lb + (size_t)limit || lb > la + (size_t)limit) return limit + 1;

    int before[98];
    int previous[98];
    int current[98];
    for (size_t j = 0; j <= lb; j++) previous[j] = (int)j;

    for (size_t i = 1; i <= la; i++) {
        current[0] = (int)i;
        for (size_t j = 1; j <= lb; j++) {
            int cost = tolower((unsigned char)a[i - 1]) == tolower((unsigned char)b[j - 1]) ? 0 : 1;
            int best = previous[j] + 1;
            if (current[j - 1] + 1 < best) best = current[j - 1] + 1;
            if (previous[j - 1] + cost < best) best = previous[j - 1] + cost;
            if (i > 1 && j > 1 && tolower((unsigned char)a[i - 1]) == tolower((unsigned char)b[j - 2]) &&
                tolower((unsigned char)a[i - 2]) == tolower((unsigned char)b[j - 1]) &&
                before[j - 2] + 1 < best)
                best = before[j - 2] + 1;
            current[j] = best;
        }
        memcpy(before, previous, sizeof(int) * (lb + 1));
        memcpy(previous, current, sizeof(int) * (lb + 1));
    }
    return previous[lb];
}

typedef struct {
    const char *name;
    int limit;
    int score;
    char *best;
} Nearest;

static void nearest_consider(Nearest *search, const char *candidate) {
    int score = edit_distance(search->name, candidate, search->limit);
    if (score >= search->score) return;

    search->score = score;
    free(search->best);
    search->best = xstrdup(candidate);
}

static char *nearest_command(const char *name) {
    Nearest search = {name, strlen(name) <= 3 ? 1 : 2, 0, NULL};
    search.score = search.limit + 1;

    StrList names;
    sl_init(&names);
    keyword_names(&names);
    builtin_names(&names);
    coreutil_names(&names);
    function_names(&names);
    alias_list(&names);

    for (size_t i = 0; i < names.len; i++) {
        char *quote = strchr(names.items[i], '=');
        if (quote) *quote = '\0';
        nearest_consider(&search, names.items[i]);
    }
    sl_free(&names);

    ensure_command_cache();
    for (size_t i = 0; i < command_cache.len; i++)
        nearest_consider(&search, command_cache.items[i]);

    return search.best;
}

static void report_not_found(const char *name) {
    char *closest = nearest_command(name);
    if (closest) shell_error("%s: command not found\n  did you mean %s", name, closest);
    else shell_error("%s: command not found\n  if it is installed, run rehash so FreSH sees it",
                     name);
    free(closest);
}

static int exec_simple(Node *node, IoSet io, int background, HANDLE *async_out) {
    StrList words;
    sl_init(&words);
    if (node->line > 0) shell.line = node->line;
    expand_forget_substitution_status();
    expand_words(&node->words, &words);

    Substitution pending[8];
    int substitutions = substitute_processes(&words, pending, 8);

    if (!apply_redirs(node->redirs, &io)) {
        sl_free(&words);
        return 1;
    }

    size_t first = 0;
    while (first < words.len && is_assignment(words.items[first])) first++;

    if (first == words.len) {
        for (size_t i = 0; i < words.len; i++) assign_from_word(words.items[i]);
        sl_free(&words);
        return expand_substitution_status();
    }

    StrList saved_names;
    StrList saved_values;
    sl_init(&saved_names);
    sl_init(&saved_values);
    for (size_t i = 0; i < first; i++) {
        char *eq = strchr(words.items[i], '=');
        *eq = '\0';
        const char *previous = var_get(words.items[i]);
        sl_push_copy(&saved_names, words.items[i]);
        sl_push_copy(&saved_values, previous ? previous : "\x01");
        var_set_exported(words.items[i], eq + 1);
        *eq = '=';
    }

    StrList args;
    sl_init(&args);
    for (size_t i = first; i < words.len; i++) sl_push_copy(&args, words.items[i]);

    int argc = (int)args.len;
    char **argv = argv_from_list(&args);
    int status = 0;

    if (shell.xtrace) {
        fflush(stdout);
        fprintf(stderr, "+ ");
        for (int i = 0; i < argc; i++) fprintf(stderr, "%s%s", i > 0 ? " " : "", argv[i]);
        fprintf(stderr, "\n");
        fflush(stderr);
    }

    if (shell.trap_debug) {
        char *handler = shell.trap_debug;
        shell.trap_debug = NULL;
        run_trap(handler);
        shell.trap_debug = handler;
    }

    Node *f = skip_functions ? NULL : function_find(argv[0]);
    if (skip_functions) skip_functions--;
    BuiltinFn builtin = f ? NULL : builtin_lookup(argv[0]);

    char path[PATH_BUF] = "";
    BuiltinFn fallback = NULL;
    int resolved = 0;

    if (!f && !builtin) {
        resolved = coreutil_preferred(argv[0]) ? 0 : resolve_command(argv[0], path, sizeof(path));
        fallback = resolved ? NULL : coreutil_lookup(argv[0]);
    }

    if (!f && !builtin && !fallback && !resolved && argc == 1 &&
        option_enabled("FRESH_AUTOCD", 1) && path_is_dir(argv[0])) {
        char *arguments[2] = {"cd", argv[0]};
        BuiltinFn enter = builtin_lookup("cd");
        status = enter(2, arguments);
    } else if (resolved && !is_shell_script(path)) {
        status = run_program(path, argv, argc, &io, background, async_out);
    } else {
        FdSave save;
        int redirected = !io_is_default(&io);
        if (redirected) fds_apply(&io, &save);

        if (f) {
            status = call_function(f, &args);
        } else if (builtin) {
            status = builtin(argc, argv);
        } else if (fallback) {
            status = fallback(argc, argv);
        } else if (resolved) {
            StrList script_args;
            sl_init(&script_args);
            for (int i = 1; i < argc; i++) sl_push_copy(&script_args, argv[i]);
            status = exec_script_file(path, &script_args);
            sl_free(&script_args);
        } else {
            report_not_found(argv[0]);
            status = 127;
        }

        if (redirected && !keep_redirections) fds_restore(&save);
        keep_redirections = 0;
    }

    for (size_t i = 0; i < saved_names.len; i++) {
        if (strcmp(saved_values.items[i], "\x01") == 0) var_unset(saved_names.items[i]);
        else var_set_exported(saved_names.items[i], saved_values.items[i]);
    }

    if (argc > 0) var_set("_", argv[argc - 1]);

    free(argv);
    sl_free(&args);
    sl_free(&saved_names);
    sl_free(&saved_values);
    sl_free(&words);
    finish_substitutions(pending, substitutions);
    return status;
}

static int exec_switch(Node *node);

static int exec_command(Node *node, IoSet io, int background, HANDLE *async_out) {
    if (node->kind == N_SIMPLE) return exec_simple(node, io, background, async_out);

    if (!apply_redirs(node->redirs, &io)) return 1;

    FdSave save;
    int redirected = !io_is_default(&io);
    if (redirected) fds_apply(&io, &save);
    int status = exec_switch(node);
    if (redirected) fds_restore(&save);
    return status;
}

static int stage_runs_in_process(Node *node) {
    if (node->kind != N_SIMPLE) return 1;
    if (node->words.len == 0) return 1;

    const char *word = node->words.items[0];
    if (strpbrk(word, "$`\"'\\*?")) return 0;
    if (function_find(word) || builtin_lookup(word)) return 1;

    char path[PATH_BUF];
    if (resolve_command(word, path, sizeof(path))) return is_shell_script(path);
    return coreutil_lookup(word) != NULL;
}

static HANDLE open_spool(char **path_out) {
    char *path = temp_acquire();
    if (!path) return INVALID_HANDLE_VALUE;

    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    HANDLE handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        temp_release(path);
        return INVALID_HANDLE_VALUE;
    }
    *path_out = path;
    return handle;
}

static HANDLE rewind_spool(HANDLE spool) {
    HANDLE reader = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), spool, GetCurrentProcess(), &reader, 0, TRUE,
                         DUPLICATE_SAME_ACCESS))
        return INVALID_HANDLE_VALUE;

    SetFilePointer(reader, 0, NULL, FILE_BEGIN);
    return reader;
}

static int flatten_pipeline(Node *node, Node **stages, int max) {
    if (node->kind != N_PIPE) {
        stages[0] = node;
        return 1;
    }
    int count = flatten_pipeline(node->left, stages, max);
    if (count < max) stages[count++] = node->right;
    return count;
}

static int pipeline_length(Node *node) {
    return node->kind == N_PIPE ? pipeline_length(node->left) + 1 : 1;
}

static int exec_pipeline(Node *node, int background) {
    if (pipeline_length(node) > MAX_STAGES) {
        shell_error("a pipeline cannot have more than %d stages", MAX_STAGES);
        return 1;
    }

    Node *stages[MAX_STAGES];
    int count = flatten_pipeline(node, stages, MAX_STAGES);

    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    HANDLE processes[MAX_STAGES];
    int process_count = 0;
    char *spools[MAX_STAGES];
    int spool_count = 0;
    HANDLE stage_input = NULL;
    int mark = tracked_count;
    int last_stage_spawned = 0;
    int status = 0;
    int stage_status[MAX_STAGES];
    int proc_stage[MAX_STAGES];
    for (int i = 0; i < count; i++) stage_status[i] = 0;

    for (int i = 0; i < count; i++) {
        IoSet io = io_default();
        HANDLE read_end = NULL;
        HANDLE write_end = NULL;
        HANDLE spool = INVALID_HANDLE_VALUE;
        char *spool_path = NULL;

        if (stage_input) io.in = stage_input;

        if (i < count - 1) {
            if (stage_runs_in_process(stages[i])) {
                spool = open_spool(&spool_path);
                if (spool == INVALID_HANDLE_VALUE) {
                    shell_error("cannot create pipeline buffer");
                    break;
                }
                track_handle(spool);
                io.out = spool;
            } else {
                if (!CreatePipe(&read_end, &write_end, &sa, 0)) {
                    shell_error("cannot create pipe");
                    break;
                }
                track_handle(read_end);
                track_handle(write_end);
                io.out = write_end;
            }
        }

        HANDLE spawned = NULL;
        status = exec_command(stages[i], io, 0, &spawned);
        stage_status[i] = status;
        if (spawned) {
            proc_stage[process_count] = i;
            processes[process_count++] = spawned;
            if (i == count - 1) last_stage_spawned = 1;
        }

        if (write_end) {
            untrack_handle(write_end);
            CloseHandle(write_end);
        }
        if (stage_input) {
            untrack_handle(stage_input);
            CloseHandle(stage_input);
            stage_input = NULL;
        }

        if (spool != INVALID_HANDLE_VALUE) {
            stage_input = rewind_spool(spool);
            untrack_handle(spool);
            CloseHandle(spool);
            spools[spool_count++] = spool_path;
            if (stage_input == INVALID_HANDLE_VALUE) stage_input = NULL;
            else track_handle(stage_input);
        } else {
            stage_input = read_end;
        }
    }

    if (stage_input) {
        untrack_handle(stage_input);
        CloseHandle(stage_input);
    }

    for (int i = 0; i < process_count; i++) {
        if (background) {
            CloseHandle(processes[i]);
            continue;
        }
        WaitForSingleObject(processes[i], INFINITE);
        DWORD exit_code = 0;
        GetExitCodeProcess(processes[i], &exit_code);
        stage_status[proc_stage[i]] = (int)exit_code;
        if (i == process_count - 1 && last_stage_spawned) status = (int)exit_code;
        CloseHandle(processes[i]);
    }

    for (int i = 0; i < spool_count; i++) temp_release(spools[i]);

    StrList reported;
    sl_init(&reported);
    for (int i = 0; i < count; i++) {
        char text[16];
        snprintf(text, sizeof(text), "%d", stage_status[i]);
        sl_push_copy(&reported, text);
    }
    var_set_array("PIPESTATUS", &reported, VAR_INDEXED);
    sl_free(&reported);

    if (!background && shell.pipefail) {
        status = 0;
        for (int i = 0; i < count; i++)
            if (stage_status[i] != 0) status = stage_status[i];
    }

    release_tracked(mark);
    return status;
}

typedef struct {
    int id;
    DWORD pid;
    HANDLE process;
    char *command;
    int stopped;
} Job;

static Job *jobs = NULL;
static int job_count = 0;
static int job_cap = 0;
static int next_job_id = 1;

static void job_add(HANDLE process, const char *command) {
    if (job_count + 1 > job_cap) {
        job_cap = job_cap ? job_cap * 2 : 16;
        jobs = xrealloc(jobs, (size_t)job_cap * sizeof(Job));
    }
    jobs[job_count].id = next_job_id++;
    jobs[job_count].pid = GetProcessId(process);
    jobs[job_count].process = process;
    jobs[job_count].command = xstrdup(command);
    printf("[%d] %lu\n", jobs[job_count].id, jobs[job_count].pid);
    job_count++;
}

static void job_remove(int index) {
    CloseHandle(jobs[index].process);
    free(jobs[index].command);
    memmove(jobs + index, jobs + index + 1, (size_t)(job_count - index - 1) * sizeof(Job));
    job_count--;
}

void jobs_list(StrList *out) {
    for (int i = job_count - 1; i >= 0; i--) {
        DWORD code = 0;
        int running = GetExitCodeProcess(jobs[i].process, &code) && code == STILL_ACTIVE;
        if (!running) {
            job_remove(i);
            continue;
        }
        StrBuf sb;
        sb_init(&sb);
        sb_printf(&sb, "[%d] %lu  %s  %s", jobs[i].id, jobs[i].pid,
                  jobs[i].stopped ? "stopped" : "running", jobs[i].command);
        sl_push(out, sb_take(&sb));
    }
    sl_sort(out);
}

int jobs_wait(int id) {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].id != id && (int)jobs[i].pid != id) continue;
        WaitForSingleObject(jobs[i].process, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(jobs[i].process, &code);
        job_remove(i);
        return (int)code;
    }
    return 127;
}

static int job_index(int id) {
    if (job_count == 0) return -1;
    if (id == 0) return job_count - 1;
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].id == id || (int)jobs[i].pid == id) return i;
    }
    return -1;
}

static int walk_threads(DWORD pid, int suspend) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    THREADENTRY32 entry;
    entry.dwSize = sizeof(entry);
    int touched = 0;

    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != pid) continue;
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
            if (!thread) continue;
            if (suspend) SuspendThread(thread);
            else ResumeThread(thread);
            CloseHandle(thread);
            touched++;
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return touched;
}

int jobs_suspend(int id) {
    int index = job_index(id);
    if (index < 0) {
        shell_error("stop: no such job");
        return 1;
    }
    if (jobs[index].stopped) return 0;
    if (!walk_threads(jobs[index].pid, 1)) {
        shell_error("stop: cannot suspend %lu", jobs[index].pid);
        return 1;
    }
    jobs[index].stopped = 1;
    printf("[%d] stopped  %s\n", jobs[index].id, jobs[index].command);
    return 0;
}

int jobs_resume(int id) {
    int index = job_index(id);
    if (index < 0) {
        shell_error("bg: no such job");
        return 1;
    }
    if (jobs[index].stopped) {
        walk_threads(jobs[index].pid, 0);
        jobs[index].stopped = 0;
    }
    printf("[%d] %s &\n", jobs[index].id, jobs[index].command);
    return 0;
}

int jobs_foreground(int id) {
    int index = job_index(id);
    if (index < 0) {
        shell_error("fg: no such job");
        return 1;
    }
    if (jobs[index].stopped) {
        walk_threads(jobs[index].pid, 0);
        jobs[index].stopped = 0;
    }
    printf("%s\n", jobs[index].command);

    WaitForSingleObject(jobs[index].process, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(jobs[index].process, &code);
    job_remove(index);
    return (int)code;
}

int jobs_wait_all(void) {
    int status = 0;
    while (job_count > 0) {
        WaitForSingleObject(jobs[0].process, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(jobs[0].process, &code);
        status = (int)code;
        job_remove(0);
    }
    return status;
}

void jobs_cleanup(void) {
    while (job_count > 0) job_remove(0);
}

void run_trap(const char *command) {
    if (!command || !*command) return;
    char *copy = xstrdup(command);
    int saved = shell.last_status;
    exec_text(copy);
    shell.last_status = saved;
    free(copy);
}

static int truthy(int status) {
    return status == 0;
}

typedef struct {
    StrList words;
    size_t pos;
} Bracket;

static int bracket_or(Bracket *b);

static const char *bracket_peek(Bracket *b) {
    return b->pos < b->words.len ? b->words.items[b->pos] : NULL;
}

static int file_check(char flag, const char *path) {
    DWORD attributes = GetFileAttributesA(path);
    int exists = attributes != INVALID_FILE_ATTRIBUTES;

    switch (flag) {
    case 'e': return exists;
    case 'f': return exists && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    case 'd': return exists && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    case 'r':
    case 'x': return exists;
    case 'w': return exists && !(attributes & FILE_ATTRIBUTE_READONLY);
    case 's': {
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info)) return 0;
        return info.nFileSizeLow > 0 || info.nFileSizeHigh > 0;
    }
    default: return 0;
    }
}

static int bracket_primary(Bracket *b) {
    const char *word = bracket_peek(b);
    if (!word) return 0;

    if (strcmp(word, "!") == 0) {
        b->pos++;
        return !bracket_primary(b);
    }
    if (strcmp(word, "(") == 0) {
        b->pos++;
        int value = bracket_or(b);
        if (bracket_peek(b) && strcmp(bracket_peek(b), ")") == 0) b->pos++;
        return value;
    }

    if (word[0] == '-' && word[1] && !word[2] && strchr("efdrwxsznv", word[1])) {
        char flag = word[1];
        b->pos++;
        const char *operand = bracket_peek(b);
        if (!operand) return 0;
        b->pos++;

        if (flag == 'z') return *operand == '\0';
        if (flag == 'n') return *operand != '\0';
        if (flag == 'v') return var_exists(operand);
        return file_check(flag, operand);
    }

    b->pos++;
    const char *op = bracket_peek(b);
    if (!op) return *word != '\0';

    if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0 || strcmp(op, ")") == 0)
        return *word != '\0';

    b->pos++;
    const char *right = bracket_peek(b);
    if (!right) return *word != '\0';
    b->pos++;

    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) return pattern_match(right, word);
    if (strcmp(op, "!=") == 0) return !pattern_match(right, word);
    if (strcmp(op, "=~") == 0) {
        RegexMatch found;
        if (!regex_search(right, word, &found)) return 0;

        StrList groups;
        sl_init(&groups);
        for (int i = 0; i < found.count && i < REGEX_GROUPS; i++) {
            if (found.start[i] < 0 || found.end[i] < found.start[i]) sl_push_copy(&groups, "");
            else sl_push(&groups, xstrndup(word + found.start[i],
                                           (size_t)(found.end[i] - found.start[i])));
        }
        var_set_array("BASH_REMATCH", &groups, VAR_INDEXED);
        sl_free(&groups);
        return 1;
    }
    if (strcmp(op, "<") == 0) return strcmp(word, right) < 0;
    if (strcmp(op, ">") == 0) return strcmp(word, right) > 0;
    if (strcmp(op, "-eq") == 0) return atol(word) == atol(right);
    if (strcmp(op, "-ne") == 0) return atol(word) != atol(right);
    if (strcmp(op, "-lt") == 0) return atol(word) < atol(right);
    if (strcmp(op, "-le") == 0) return atol(word) <= atol(right);
    if (strcmp(op, "-gt") == 0) return atol(word) > atol(right);
    if (strcmp(op, "-ge") == 0) return atol(word) >= atol(right);
    if (strcmp(op, "-nt") == 0 || strcmp(op, "-ot") == 0) {
        WIN32_FILE_ATTRIBUTE_DATA left_info;
        WIN32_FILE_ATTRIBUTE_DATA right_info;
        if (!GetFileAttributesExA(word, GetFileExInfoStandard, &left_info)) return 0;
        if (!GetFileAttributesExA(right, GetFileExInfoStandard, &right_info)) return 0;
        LONG compared = CompareFileTime(&left_info.ftLastWriteTime, &right_info.ftLastWriteTime);
        return strcmp(op, "-nt") == 0 ? compared > 0 : compared < 0;
    }
    return *word != '\0';
}

static int bracket_and(Bracket *b) {
    int value = bracket_primary(b);
    while (bracket_peek(b) && strcmp(bracket_peek(b), "&&") == 0) {
        b->pos++;
        int right = bracket_primary(b);
        value = value && right;
    }
    return value;
}

static int bracket_or(Bracket *b) {
    int value = bracket_and(b);
    while (bracket_peek(b) && strcmp(bracket_peek(b), "||") == 0) {
        b->pos++;
        int right = bracket_and(b);
        value = value || right;
    }
    return value;
}

static int evaluate_bracket(const StrList *words) {
    StrList merged;
    sl_init(&merged);

    for (size_t i = 0; i < words->len; i++) {
        sl_push_copy(&merged, words->items[i]);
        if (strcmp(words->items[i], "=~") != 0) continue;

        StrBuf pattern;
        sb_init(&pattern);
        size_t j = i + 1;
        for (; j < words->len; j++) {
            if (strcmp(words->items[j], "&&") == 0 || strcmp(words->items[j], "||") == 0) break;
            sb_puts(&pattern, words->items[j]);
        }
        sl_push(&merged, sb_take(&pattern));
        i = j - 1;
    }
    words = &merged;

    Bracket b;
    sl_init(&b.words);
    b.pos = 0;

    for (size_t i = 0; i < words->len; i++) {
        const char *word = words->items[i];
        if (strcmp(word, "&&") == 0 || strcmp(word, "||") == 0 || strcmp(word, "(") == 0 ||
            strcmp(word, ")") == 0 || strcmp(word, "!") == 0) {
            sl_push_copy(&b.words, word);
            continue;
        }
        char *expanded = expand_single(word);
        sl_push(&b.words, expanded);
    }

    int value = bracket_or(&b);
    sl_free(&b.words);
    sl_free(&merged);
    return value ? 0 : 1;
}

static const char *group_end(const char *open) {
    int depth = 0;
    for (const char *p = open; *p; p++) {
        if (*p == '(') depth++;
        else if (*p == ')' && --depth == 0) return p;
    }
    return NULL;
}

static int choice_matches(const StrList *choices, const char *text, size_t length) {
    char *piece = xstrndup(text, length);
    int matched = 0;
    for (size_t i = 0; i < choices->len && !matched; i++)
        matched = pattern_match(choices->items[i], piece);
    free(piece);
    return matched;
}

static int match_repeats(const StrList *choices, const char *tail, const char *text) {
    if (pattern_match(tail, text)) return 1;

    size_t length = strlen(text);
    for (size_t take = 1; take <= length; take++) {
        if (!choice_matches(choices, text, take)) continue;
        if (match_repeats(choices, tail, text + take)) return 1;
    }
    return 0;
}

static int match_extended(const char *pattern, const char *text) {
    char kind = pattern[0];
    const char *close = group_end(pattern + 1);
    if (!close) return -1;

    const char *tail = close + 1;
    StrList choices;
    sl_init(&choices);

    const char *start = pattern + 2;
    int depth = 0;
    for (const char *p = start; p <= close; p++) {
        if (*p == '(') depth++;
        else if (*p == ')' && p != close) depth--;
        if ((*p == '|' && depth == 0) || p == close) {
            sl_push(&choices, xstrndup(start, (size_t)(p - start)));
            start = p + 1;
        }
    }

    size_t length = strlen(text);
    int matched = 0;

    if (kind == '*') {
        matched = match_repeats(&choices, tail, text);
    } else if (kind == '+') {
        for (size_t take = 1; take <= length && !matched; take++) {
            if (!choice_matches(&choices, text, take)) continue;
            matched = match_repeats(&choices, tail, text + take);
        }
    } else if (kind == '@' || kind == '?') {
        if (kind == '?' && pattern_match(tail, text)) matched = 1;
        for (size_t take = kind == '?' ? 1 : 0; take <= length && !matched; take++) {
            if (!choice_matches(&choices, text, take)) continue;
            matched = pattern_match(tail, text + take);
        }
    } else {
        for (size_t take = 0; take <= length && !matched; take++) {
            if (choice_matches(&choices, text, take)) continue;
            matched = pattern_match(tail, text + take);
        }
    }

    sl_free(&choices);
    return matched;
}

static int same_char(char a, char b) {
    if (a == b) return 1;
    return shell.nocasematch && tolower((unsigned char)a) == tolower((unsigned char)b);
}

int pattern_match(const char *pattern, const char *text) {
    while (*pattern) {
        if (strchr("?*+@!", *pattern) && pattern[1] == '(') {
            int result = match_extended(pattern, text);
            if (result >= 0) return result;
        }
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return 1;
            for (const char *p = text; *p || !*pattern; p++) {
                if (pattern_match(pattern, p)) return 1;
                if (!*p) break;
            }
            return 0;
        }
        if (*pattern == '?') {
            if (!*text) return 0;
            pattern++;
            text++;
            continue;
        }
        if (*pattern == '[') {
            const char *class_start = ++pattern;
            int negate = *pattern == '!' || *pattern == '^';
            if (negate) pattern++;
            int matched = 0;
            while (*pattern && (*pattern != ']' || pattern == class_start)) {
                if (pattern[1] == '-' && pattern[2] && pattern[2] != ']') {
                    char low = pattern[0];
                    char high = pattern[2];
                    if (*text >= low && *text <= high) matched = 1;
                    if (shell.nocasematch) {
                        char folded = (char)(isupper((unsigned char)*text)
                                                 ? tolower((unsigned char)*text)
                                                 : toupper((unsigned char)*text));
                        if (folded >= low && folded <= high) matched = 1;
                    }
                    pattern += 3;
                } else {
                    if (*text == *pattern || same_char(*pattern, *text)) matched = 1;
                    pattern++;
                }
            }
            if (*pattern == ']') pattern++;
            if (matched == negate || !*text) return 0;
            text++;
            continue;
        }
        if (!same_char(*pattern, *text)) return 0;
        pattern++;
        text++;
    }
    return *text == '\0';
}

int exec_node(Node *node) {
    if (!node || !shell.running) return shell.last_status;
    if (shell.returning || shell.break_level || shell.continue_level) return shell.last_status;

    int mark = tracked_count;
    int status;

    if (node->kind != N_SIMPLE && node->kind != N_PIPE && node->redirs)
        status = exec_command(node, io_default(), node->background, NULL);
    else status = exec_switch(node);

    release_tracked(mark);
    shell.last_status = status;

    if (status != 0 && shell.condition_depth == 0 && shell.trap_err &&
        (node->kind == N_SIMPLE || node->kind == N_PIPE)) {
        char *handler = shell.trap_err;
        shell.trap_err = NULL;
        run_trap(handler);
        shell.trap_err = handler;
    }

    if (shell.errexit && status != 0 && shell.condition_depth == 0 &&
        (node->kind == N_SIMPLE || node->kind == N_PIPE))
        shell.running = 0;

    return status;
}

static int exec_condition(Node *node) {
    shell.condition_depth++;
    int status = exec_node(node);
    shell.condition_depth--;
    return status;
}

static int exec_switch(Node *node) {
    int status = 0;

    switch (node->kind) {
    case N_SIMPLE:
        status = exec_simple(node, io_default(), node->background, NULL);
        break;

    case N_GROUP:
        status = exec_node(node->right);
        break;

    case N_SUBSHELL: {
        char cwd[PATH_BUF];
        GetCurrentDirectoryA(sizeof(cwd), cwd);
        void *snapshot = vars_snapshot();
        int was_running = shell.running;
        status = exec_node(node->right);
        if (!shell.running) {
            shell.running = was_running;
            status = shell.last_status;
        }
        vars_restore(snapshot);
        SetCurrentDirectoryA(cwd);
        break;
    }

    case N_ARITH: {
        int ok = 1;
        long value = eval_arith(node->name, &ok);
        if (!ok) shell_error("((%s)): bad arithmetic expression", node->name);
        status = ok && value != 0 ? 0 : 1;
        break;
    }

    case N_TIME: {
        LARGE_INTEGER frequency;
        LARGE_INTEGER before;
        LARGE_INTEGER after;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&before);

        status = exec_node(node->right);

        QueryPerformanceCounter(&after);
        double seconds = (double)(after.QuadPart - before.QuadPart) / (double)frequency.QuadPart;
        fflush(stdout);
        fprintf(stderr, "\nreal\t%dm%.3fs\n", (int)(seconds / 60), seconds - (int)(seconds / 60) * 60);
        fflush(stderr);
        break;
    }

    case N_FOR_C: {
        int ok = 1;
        const char *init = node->words.items[0];
        const char *cond = node->words.items[1];
        const char *step = node->words.items[2];
        if (*init) eval_arith(init, &ok);
        while (shell.running && !shell.returning) {
            if (*cond && eval_arith(cond, &ok) == 0) break;
            status = exec_node(node->right);
            if (shell.continue_level) shell.continue_level--;
            if (shell.break_level) {
                shell.break_level--;
                break;
            }
            if (*step) eval_arith(step, &ok);
        }
        break;
    }

    case N_PIPE:
        status = exec_pipeline(node, node->background);
        break;

    case N_SEQ:
        exec_node(node->left);
        status = exec_node(node->right);
        break;

    case N_AND:
        status = exec_condition(node->left);
        shell.last_status = status;
        if (truthy(status)) status = exec_node(node->right);
        break;

    case N_OR:
        status = exec_condition(node->left);
        shell.last_status = status;
        if (!truthy(status)) status = exec_node(node->right);
        break;

    case N_NOT:
        status = exec_condition(node->right) == 0 ? 1 : 0;
        break;

    case N_IF:
        status = exec_condition(node->left);
        shell.last_status = status;
        if (truthy(status)) status = exec_node(node->right);
        else if (node->extra) status = exec_node(node->extra);
        else status = 0;
        break;

    case N_WHILE:
    case N_UNTIL:
        while (shell.running && !shell.returning) {
            int condition = exec_condition(node->left);
            shell.last_status = condition;
            int keep_going = node->kind == N_WHILE ? truthy(condition) : !truthy(condition);
            if (!keep_going) break;
            status = exec_node(node->right);
            if (shell.continue_level) shell.continue_level--;
            if (shell.break_level) {
                shell.break_level--;
                break;
            }
        }
        break;

    case N_FOR: {
        StrList values;
        sl_init(&values);
        expand_words(&node->words, &values);
        for (size_t i = 0; i < values.len && shell.running && !shell.returning; i++) {
            var_set(node->name, values.items[i]);
            status = exec_node(node->right);
            if (shell.continue_level) shell.continue_level--;
            if (shell.break_level) {
                shell.break_level--;
                break;
            }
        }
        sl_free(&values);
        break;
    }

    case N_CASE: {
        char *subject = expand_single(node->name);
        int running_on = 0;
        for (Node *item = node->right; item; item = item->extra) {
            int matched = running_on;
            for (size_t i = 0; i < item->words.len && !matched; i++) {
                char *pattern = expand_single(item->words.items[i]);
                matched = pattern_match(pattern, subject);
                free(pattern);
            }
            if (!matched) continue;

            status = exec_node(item->right);
            if (!item->background) break;
            running_on = 1;
        }
        free(subject);
        break;
    }

    case N_CASE_ITEM:
        status = exec_node(node->right);
        break;

    case N_ASSIGN_ARRAY: {
        StrList values;
        sl_init(&values);
        expand_words(&node->words, &values);

        size_t length = strlen(node->name);
        if (length > 0 && node->name[length - 1] == '+') {
            char *name = xstrndup(node->name, length - 1);
            for (size_t i = 0; i < values.len; i++) var_append(name, values.items[i]);
            free(name);
        } else {
            VarKind kind = var_kind(node->name) == VAR_ASSOC ? VAR_ASSOC : VAR_INDEXED;
            var_set_array(node->name, &values, kind);
        }
        sl_free(&values);
        break;
    }

    case N_SELECT: {
        StrList values;
        sl_init(&values);
        expand_words(&node->words, &values);

        while (shell.running && !shell.returning) {
            for (size_t i = 0; i < values.len; i++)
                printf("%2zu) %s\n", i + 1, values.items[i]);
            printf("#? ");
            fflush(stdout);

            char line[256];
            if (!fgets(line, sizeof(line), stdin)) break;
            line[strcspn(line, "\r\n")] = '\0';
            if (!*line) continue;

            int choice = atoi(line);
            if (choice < 1 || (size_t)choice > values.len) continue;
            var_set(node->name, values.items[choice - 1]);
            var_set("REPLY", line);

            status = exec_node(node->right);
            if (shell.continue_level) shell.continue_level--;
            if (shell.break_level) {
                shell.break_level--;
                break;
            }
        }
        sl_free(&values);
        break;
    }

    case N_TEST:
        status = evaluate_bracket(&node->words);
        break;

    case N_FUNC:
        function_define(node->name, node->right);
        node->right = NULL;
        break;
    }

    shell.last_status = status;
    return status;
}

char *apply_aliases(const char *line) {
    StrBuf out;
    sb_init(&out);
    const char *p = line;
    int at_command_start = 1;
    int expansions = 0;
    int case_depth = 0;
    int in_pattern = 0;

    while (*p) {
        if (*p == ' ' || *p == '\t') {
            sb_putc(&out, *p++);
            continue;
        }
        if (str_word_at(p, line, "case")) case_depth++;
        else if (case_depth > 0 && str_word_at(p, line, "esac")) case_depth--;
        else if (case_depth > 0 && str_word_at(p, line, "in")) in_pattern = 1;

        if (case_depth > 0) {
            if (*p == ')') in_pattern = 0;
            else if (*p == ';') in_pattern = 1;
        }
        if (*p == '\'' || *p == '"') {
            char quote = *p;
            sb_putc(&out, *p++);
            while (*p && *p != quote) {
                if (*p == '\\' && p[1]) sb_putc(&out, *p++);
                sb_putc(&out, *p++);
            }
            if (*p) sb_putc(&out, *p++);
            at_command_start = 0;
            continue;
        }
        if (strchr("|&;\n(", *p)) {
            sb_putc(&out, *p++);
            at_command_start = 1;
            continue;
        }
        if (at_command_start && (isalnum((unsigned char)*p) || *p == '_' || *p == '.')) {
            const char *start = p;
            while (*p && !strchr(" \t|&;<>()\n", *p)) p++;
            char *word = xstrndup(start, (size_t)(p - start));
            const char *value = in_pattern ? NULL : alias_get(word);
            if (value && expansions < 16) {
                sb_puts(&out, value);
                expansions++;
            } else {
                sb_puts(&out, word);
            }
            free(word);
            at_command_start = 0;
            continue;
        }
        sb_putc(&out, *p++);
        at_command_start = 0;
    }
    return sb_take(&out);
}

int exec_bypassing_functions(const char *text) {
    skip_functions++;
    int status = exec_text(text);
    if (skip_functions > 0) skip_functions--;
    return status;
}

int exec_text(const char *text) {
    int foreign_status = 0;
    if (foreign_route(text, &foreign_status)) {
        shell.last_status = foreign_status;
        return foreign_status;
    }

    int incomplete = 0;
    char *error = NULL;
    char *aliased = apply_aliases(text);
    Node *node = parse_string(aliased, &incomplete, &error);
    free(aliased);

    if (!node) {
        if (error) {
            if (shell.script_name) shell_error("%s: %s", shell.script_name, error);
            else shell_error("%s", error);
            free(error);
            shell.last_status = 2;
            return 2;
        }
        if (incomplete) {
            shell_error("unexpected end of input");
            shell.last_status = 2;
            return 2;
        }
        return shell.last_status;
    }
    free(error);
    int status = exec_node(node);
    node_free(node);
    return status;
}

int exec_line(const char *line) {
    return exec_text(line);
}

int exec_subshell(const char *text) {
    char cwd[PATH_BUF];
    GetCurrentDirectoryA(sizeof(cwd), cwd);
    void *snapshot = vars_snapshot();
    int was_running = shell.running;

    int status = exec_text(text);
    if (!shell.running) {
        shell.running = was_running;
        status = shell.last_status;
    }

    vars_restore(snapshot);
    SetCurrentDirectoryA(cwd);
    return status;
}

int exec_script_file(const char *path, const StrList *args) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        shell_error("%s: cannot open script", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return 1;
    }
    char *text = xmalloc((size_t)size + 1);
    size_t read = fread(text, 1, (size_t)size, f);
    text[read] = '\0';
    fclose(f);

    StrList saved = shell.params;
    if (args) {
        sl_init(&shell.params);
        sl_push_copy(&shell.params, path);
        for (size_t i = 0; i < args->len; i++) sl_push_copy(&shell.params, args->items[i]);
    }

    const char *leaf = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    if (back > leaf) leaf = back;
    char *saved_name = shell.script_name;
    shell.script_name = xstrdup(leaf ? leaf + 1 : path);

    int was_running = shell.running;
    shell.depth++;
    int status = exec_text(text);
    shell.depth--;
    shell.returning = 0;

    free(shell.script_name);
    shell.script_name = saved_name;

    if (args) {
        if (!shell.running) {
            shell.running = was_running;
            status = shell.last_status;
        }
        sl_free(&shell.params);
        shell.params = saved;
    }
    free(text);
    return status;
}

typedef struct {
    HANDLE source;
    StrBuf *out;
} Drain;

static DWORD WINAPI drain_pipe(LPVOID parameter) {
    Drain *drain = parameter;
    char buffer[8192];
    DWORD read = 0;

    while (ReadFile(drain->source, buffer, sizeof(buffer), &read, NULL) && read > 0)
        sb_putn(drain->out, buffer, read);
    return 0;
}

int capture_command(const char *command, StrBuf *out) {
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    HANDLE read_end = NULL;
    HANDLE write_end = NULL;
    if (!CreatePipe(&read_end, &write_end, &sa, 65536)) return 1;

    int fd = _open_osfhandle((intptr_t)write_end, _O_WRONLY | _O_BINARY);
    if (fd < 0) {
        CloseHandle(read_end);
        CloseHandle(write_end);
        return 1;
    }

    fflush(stdout);
    int saved_out = _dup(1);
    _dup2(fd, 1);
    _close(fd);

    Drain drain = {read_end, out};
    HANDLE reader = CreateThread(NULL, 0, drain_pipe, &drain, 0, NULL);

    char cwd[PATH_BUF];
    GetCurrentDirectoryA(sizeof(cwd), cwd);
    void *snapshot = vars_snapshot();
    int was_running = shell.running;

    int status = exec_text(command);

    if (!shell.running) {
        shell.running = was_running;
        status = shell.last_status;
    }
    vars_restore(snapshot);
    SetCurrentDirectoryA(cwd);

    fflush(stdout);
    _dup2(saved_out, 1);
    _close(saved_out);

    if (reader) {
        WaitForSingleObject(reader, INFINITE);
        CloseHandle(reader);
    }
    CloseHandle(read_end);
    return status;
}
