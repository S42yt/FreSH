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

#include "builtins.h"
#include "coreutils.h"
#include "expand.h"
#include "foreign.h"
#include "regex.h"
#include "shell.h"
#include "vars.h"

#define MAX_STAGES 32
#define MAX_TRACKED 128

typedef struct {
    HANDLE in;
    HANDLE out;
    HANDLE err;
} IoSet;

typedef struct {
    int saved[3];
} FdSave;

typedef struct {
    char *name;
    Node *body;
} Function;

static Function *functions = NULL;
static size_t function_count = 0;
static size_t function_cap = 0;

static HANDLE tracked[MAX_TRACKED];
static int tracked_count = 0;

static StrList command_cache;
static int command_cache_valid = 0;

static int exec_command(Node *node, IoSet io, int background, HANDLE *async_out);
static int exec_pipeline(Node *node, int background);

void exec_init(void) {
    sl_init(&command_cache);
}

void exec_cleanup(void) {
    for (size_t i = 0; i < function_count; i++) {
        free(functions[i].name);
        node_free(functions[i].body);
    }
    free(functions);
    functions = NULL;
    function_count = function_cap = 0;
    sl_free(&command_cache);
}

static Function *function_find(const char *name) {
    for (size_t i = 0; i < function_count; i++) {
        if (strcmp(functions[i].name, name) == 0) return &functions[i];
    }
    return NULL;
}

void function_define(const char *name, Node *body) {
    Function *f = function_find(name);
    if (f) {
        node_free(f->body);
        f->body = body;
        return;
    }
    if (function_count + 1 >= function_cap) {
        function_cap = function_cap ? function_cap * 2 : 16;
        functions = xrealloc(functions, function_cap * sizeof(Function));
    }
    functions[function_count].name = xstrdup(name);
    functions[function_count].body = body;
    function_count++;
}

int function_defined(const char *name) {
    return function_find(name) != NULL;
}

void function_names(StrList *out) {
    for (size_t i = 0; i < function_count; i++) sl_push_copy(out, functions[i].name);
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

static int try_with_extensions(const char *base, char *out, size_t out_size) {
    if (path_is_file(base)) {
        snprintf(out, out_size, "%s", base);
        return 1;
    }
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

int resolve_command(const char *name, char *out, size_t out_size) {
    if (!name || !*name) return 0;

    if (strchr(name, '/') || strchr(name, '\\')) {
        char base[PATH_BUF];
        snprintf(base, sizeof(base), "%s", name);
        path_to_backslashes(base);
        return try_with_extensions(base, out, out_size);
    }

    const char *path = var_get("PATH");
    if (!path) return 0;

    char *copy = xstrdup(path);
    char *cursor = copy;
    char *dir;
    int found = 0;
    while (!found && (dir = str_next_field(&cursor, ';')) != NULL) {
        if (!*dir) continue;
        char *base = path_join(dir, name);
        found = try_with_extensions(base, out, out_size);
        free(base);
    }
    free(copy);
    return found;
}

void path_rehash(void) {
    command_cache_valid = 0;
}

static void ensure_command_cache(void) {
    if (!command_cache_valid) {
        sl_clear(&command_cache);
        const char *path = var_get("PATH");
        if (path) {
            char *copy = xstrdup(path);
            char *cursor = copy;
            char *dir;
            while ((dir = str_next_field(&cursor, ';')) != NULL) {
                if (*dir) {
                    char *pattern = path_join(dir, "*");
                    WIN32_FIND_DATAA data;
                    HANDLE find = FindFirstFileA(pattern, &data);
                    free(pattern);
                    if (find != INVALID_HANDLE_VALUE) {
                        do {
                            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                            const char *ext = path_ext(data.cFileName);
                            if (!*ext) continue;
                            if (!str_ieq(ext, ".exe") && !str_ieq(ext, ".com") &&
                                !str_ieq(ext, ".bat") && !str_ieq(ext, ".cmd") &&
                                !str_ieq(ext, ".ps1") && !str_ieq(ext, ".sh") &&
                                !str_ieq(ext, ".frsh"))
                                continue;
                            char name[PATH_BUF];
                            snprintf(name, sizeof(name), "%s", data.cFileName);
                            name[strlen(name) - strlen(ext)] = '\0';
                            if (!sl_contains(&command_cache, name)) sl_push_copy(&command_cache, name);
                        } while (FindNextFileA(find, &data));
                        FindClose(find);
                    }
                }
            }
            free(copy);
        }
        sl_sort(&command_cache);
        command_cache_valid = 1;
    }
}

void path_commands(StrList *out) {
    ensure_command_cache();
    for (size_t i = 0; i < command_cache.len; i++) sl_push_copy(out, command_cache.items[i]);
}

int path_command_exists(const char *name) {
    ensure_command_cache();
    for (size_t i = 0; i < command_cache.len; i++) {
        if (_stricmp(command_cache.items[i], name) == 0) return 1;
    }
    return 0;
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
    io.in = GetStdHandle(STD_INPUT_HANDLE);
    io.out = GetStdHandle(STD_OUTPUT_HANDLE);
    io.err = GetStdHandle(STD_ERROR_HANDLE);
    return io;
}

static int io_is_default(const IoSet *io) {
    IoSet base = io_default();
    return io->in == base.in && io->out == base.out && io->err == base.err;
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
    int flags[3] = {_O_RDONLY, 0, 0};

    for (int i = 0; i < 3; i++) {
        if (handles[i] == defaults[i]) continue;
        HANDLE copy;
        if (!DuplicateHandle(GetCurrentProcess(), handles[i], GetCurrentProcess(), &copy, 0, FALSE,
                             DUPLICATE_SAME_ACCESS))
            continue;
        save->saved[i] = _dup(i);
        int fd = _open_osfhandle((intptr_t)copy, flags[i]);
        if (fd >= 0) {
            _dup2(fd, i);
            _close(fd);
        } else {
            CloseHandle(copy);
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
}

static int apply_redirs(Redir *redirs, IoSet *io) {
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

    for (Redir *r = redirs; r; r = r->next) {
        if (r->type == R_HEREDOC || r->type == R_HEREDOC_RAW) {
            char directory[PATH_BUF];
            char file[PATH_BUF];
            if (!GetTempPathA(sizeof(directory), directory)) return 0;
            if (!GetTempFileNameA(directory, "frhd", 0, file)) return 0;

            char *body = r->type == R_HEREDOC ? expand_heredoc(r->target) : xstrdup(r->target);
            FILE *f = fopen(file, "wb");
            if (f) {
                fputs(body, f);
                fclose(f);
            }
            free(body);

            HANDLE handle = CreateFileA(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY, NULL);
            if (handle == INVALID_HANDLE_VALUE) return 0;
            track_handle(handle);
            io->in = handle;
            continue;
        }

        char *target = expand_single(r->target);

        if (r->type == R_DUP) {
            int fd = atoi(target);
            HANDLE source = fd == 0 ? io->in : (fd == 2 ? io->err : io->out);
            if (r->fd == 0) io->in = source;
            else if (r->fd == 2) io->err = source;
            else io->out = source;
            free(target);
            continue;
        }

        char native[PATH_BUF];
        snprintf(native, sizeof(native), "%s", target);
        free(target);
        path_to_backslashes(native);

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
        if (r->fd == 0) io->in = handle;
        else if (r->fd == 2) io->err = handle;
        else io->out = handle;
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
    return *p == '=';
}

static int call_function(Function *f, const StrList *args) {
    StrList saved = shell.params;
    sl_init(&shell.params);
    for (size_t i = 0; i < args->len; i++) sl_push_copy(&shell.params, args->items[i]);

    scope_push();
    shell.depth++;
    int status = exec_node(f->body);
    shell.depth--;
    scope_pop();
    if (shell.returning) {
        shell.returning = 0;
        status = shell.last_status;
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
        printf("[background] %lu\n", GetProcessId(process));
        CloseHandle(process);
        return 0;
    }

    WaitForSingleObject(process, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(process, &exit_code);
    CloseHandle(process);
    return (int)exit_code;
}

static int exec_simple(Node *node, IoSet io, int background, HANDLE *async_out) {
    StrList words;
    sl_init(&words);
    expand_words(&node->words, &words);

    if (!apply_redirs(node->redirs, &io)) {
        sl_free(&words);
        return 1;
    }

    size_t first = 0;
    while (first < words.len && is_assignment(words.items[first])) first++;

    if (first == words.len) {
        for (size_t i = 0; i < words.len; i++) {
            char *eq = strchr(words.items[i], '=');
            *eq = '\0';
            var_set(words.items[i], eq + 1);
            *eq = '=';
        }
        sl_free(&words);
        return 0;
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

    Function *f = function_find(argv[0]);
    BuiltinFn builtin = f ? NULL : builtin_lookup(argv[0]);

    if (f || builtin) {
        FdSave save;
        int redirected = !io_is_default(&io);
        if (redirected) fds_apply(&io, &save);
        status = f ? call_function(f, &args) : builtin(argc, argv);
        if (redirected) fds_restore(&save);
    } else {
        char path[PATH_BUF] = "";
        int preferred = coreutil_preferred(argv[0]);
        int resolved = preferred ? 0 : resolve_command(argv[0], path, sizeof(path));
        BuiltinFn fallback = resolved ? NULL : coreutil_lookup(argv[0]);

        if (fallback) {
            FdSave save;
            int redirected = !io_is_default(&io);
            if (redirected) fds_apply(&io, &save);
            status = fallback(argc, argv);
            if (redirected) fds_restore(&save);
        } else if (!resolved) {
            FdSave save;
            int redirected = !io_is_default(&io);
            if (redirected) fds_apply(&io, &save);
            shell_error("%s: command not found", argv[0]);
            if (redirected) fds_restore(&save);
            status = 127;
        } else if (is_shell_script(path)) {
            FdSave save;
            int redirected = !io_is_default(&io);
            if (redirected) fds_apply(&io, &save);
            StrList script_args;
            sl_init(&script_args);
            for (int i = 1; i < argc; i++) sl_push_copy(&script_args, argv[i]);
            status = exec_script_file(path, &script_args);
            sl_free(&script_args);
            if (redirected) fds_restore(&save);
        } else {
            status = run_program(path, argv, argc, &io, background, async_out);
        }
    }

    for (size_t i = 0; i < saved_names.len; i++) {
        if (strcmp(saved_values.items[i], "\x01") == 0) var_unset(saved_names.items[i]);
        else var_set_exported(saved_names.items[i], saved_values.items[i]);
    }

    free(argv);
    sl_free(&args);
    sl_free(&saved_names);
    sl_free(&saved_values);
    sl_free(&words);
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

static HANDLE open_spool(char *path, size_t path_size) {
    char directory[PATH_BUF];
    if (!GetTempPathA(sizeof(directory), directory)) return INVALID_HANDLE_VALUE;

    char file[PATH_BUF];
    if (!GetTempFileNameA(directory, "frsp", 0, file)) return INVALID_HANDLE_VALUE;
    snprintf(path, path_size, "%s", file);

    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    return CreateFileA(file, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_TEMPORARY, NULL);
}

static HANDLE reopen_spool(const char *path) {
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    return CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                       FILE_ATTRIBUTE_TEMPORARY, NULL);
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

static int exec_pipeline(Node *node, int background) {
    Node *stages[MAX_STAGES];
    int count = flatten_pipeline(node, stages, MAX_STAGES);

    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    HANDLE processes[MAX_STAGES];
    int process_count = 0;
    char spools[MAX_STAGES][PATH_BUF];
    int spool_count = 0;
    HANDLE stage_input = NULL;
    int mark = tracked_count;
    int last_stage_spawned = 0;
    int status = 0;

    for (int i = 0; i < count; i++) {
        IoSet io = io_default();
        HANDLE read_end = NULL;
        HANDLE write_end = NULL;
        HANDLE spool = INVALID_HANDLE_VALUE;
        char spool_path[PATH_BUF] = "";

        if (stage_input) io.in = stage_input;

        if (i < count - 1) {
            if (stage_runs_in_process(stages[i])) {
                spool = open_spool(spool_path, sizeof(spool_path));
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
        if (spawned) {
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
            untrack_handle(spool);
            CloseHandle(spool);
            snprintf(spools[spool_count++], PATH_BUF, "%s", spool_path);
            stage_input = reopen_spool(spool_path);
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
        if (i == process_count - 1 && last_stage_spawned) status = (int)exit_code;
        CloseHandle(processes[i]);
    }

    for (int i = 0; i < spool_count; i++) DeleteFileA(spools[i]);

    release_tracked(mark);
    return status;
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
    if (strcmp(op, "=~") == 0) return regex_search(right, word, NULL);
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
    return value ? 0 : 1;
}

int pattern_match(const char *pattern, const char *text) {
    while (*pattern) {
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
                    if (*text >= pattern[0] && *text <= pattern[2]) matched = 1;
                    pattern += 3;
                } else {
                    if (*text == *pattern) matched = 1;
                    pattern++;
                }
            }
            if (*pattern == ']') pattern++;
            if (matched == negate || !*text) return 0;
            text++;
            continue;
        }
        if (*pattern != *text) return 0;
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
        for (Node *item = node->right; item; item = item->extra) {
            int matched = 0;
            for (size_t i = 0; i < item->words.len && !matched; i++) {
                char *pattern = expand_single(item->words.items[i]);
                matched = pattern_match(pattern, subject);
                free(pattern);
            }
            if (matched) {
                status = exec_node(item->right);
                break;
            }
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

    while (*p) {
        if (*p == ' ' || *p == '\t') {
            sb_putc(&out, *p++);
            continue;
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
            const char *value = alias_get(word);
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
            shell_error("%s", error);
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

    int was_running = shell.running;
    shell.depth++;
    int status = exec_text(text);
    shell.depth--;
    shell.returning = 0;

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

int capture_command(const char *command, StrBuf *out) {
    char temp_dir[PATH_BUF];
    char temp_file[PATH_BUF];
    if (!GetTempPathA(sizeof(temp_dir), temp_dir)) return 1;
    if (!GetTempFileNameA(temp_dir, "frsh", 0, temp_file)) return 1;

    fflush(stdout);
    int saved_out = _dup(1);
    int fd = _open(temp_file, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) {
        _close(saved_out);
        DeleteFileA(temp_file);
        return 1;
    }
    _dup2(fd, 1);
    _close(fd);

    int status = exec_text(command);

    fflush(stdout);
    _dup2(saved_out, 1);
    _close(saved_out);

    FILE *f = fopen(temp_file, "rb");
    if (f) {
        char buffer[4096];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) sb_putn(out, buffer, n);
        fclose(f);
    }
    DeleteFileA(temp_file);
    return status;
}
