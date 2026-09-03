/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "gitinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"

#include "util.h"

#define DIRTY_REFRESH_MS 1500

static char cached_cwd[PATH_BUF];
static char cached_root[PATH_BUF];
static int cached_is_repo = 0;

static char cached_branch[256];
static char cached_repo[256];
static char cached_user[256];
static int user_loaded = 0;

static char dirty_root[PATH_BUF];
static volatile LONG dirty_running = 0;
static volatile LONG dirty_state = 0;
static DWORD dirty_stamp = 0;

void git_invalidate(void) {
    cached_cwd[0] = '\0';
    cached_root[0] = '\0';
    cached_is_repo = 0;
    cached_branch[0] = '\0';
    cached_repo[0] = '\0';
}

static int find_root(const char *start, char *out, size_t out_size) {
    char dir[PATH_BUF];
    snprintf(dir, sizeof(dir), "%s", start);

    while (1) {
        char marker[PATH_BUF];
        snprintf(marker, sizeof(marker), "%s" PATH_SEP_STR ".git", dir);
        if (GetFileAttributesA(marker) != INVALID_FILE_ATTRIBUTES) {
            snprintf(out, out_size, "%s", dir);
            return 1;
        }
        char *slash = path_last_sep(dir);
        if (!slash || slash == dir) return 0;
        *slash = '\0';
    }
}

int git_repo_root(char *out, size_t out_size) {
    char cwd[PATH_BUF];
    if (!GetCurrentDirectoryA(sizeof(cwd), cwd)) return 0;

    if (strcmp(cwd, cached_cwd) != 0) {
        snprintf(cached_cwd, sizeof(cached_cwd), "%s", cwd);
        cached_is_repo = find_root(cwd, cached_root, sizeof(cached_root));
        cached_branch[0] = '\0';
        cached_repo[0] = '\0';
    }
    if (!cached_is_repo) return 0;
    snprintf(out, out_size, "%s", cached_root);
    return 1;
}

#ifndef _WIN32

static int run_git(const char *arguments, const char *working_dir, char *out, size_t out_size) {
    StrBuf command;
    sb_init(&command);
    sb_puts(&command, "git ");
    if (working_dir) {
        sb_puts(&command, "-C ");
        sb_put_quoted(&command, working_dir);
        sb_putc(&command, ' ');
    }
    sb_puts(&command, arguments);
    sb_puts(&command, " 2>/dev/null");

    FILE *pipe = popen(command.data, "r");
    sb_free(&command);
    if (!pipe) return 0;

    size_t total = 0;
    while (total + 1 < out_size) {
        size_t n = fread(out + total, 1, out_size - total - 1, pipe);
        if (n == 0) break;
        total += n;
    }
    out[total] = '\0';
    int status = pclose(pipe);

    char *end = out + strlen(out);
    while (end > out && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) *--end = '\0';
    return status == 0;
}

#else

static int run_git(const char *arguments, const char *working_dir, char *out, size_t out_size) {
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    HANDLE read_end, write_end;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) return 0;
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = write_end;
    si.hStdError = write_end;
    si.hStdInput = NULL;

    char command_line[512];
    snprintf(command_line, sizeof(command_line), "git.exe %s", arguments);

    if (!CreateProcessA(NULL, command_line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, working_dir,
                        &si, &pi)) {
        CloseHandle(read_end);
        CloseHandle(write_end);
        return 0;
    }
    CloseHandle(write_end);

    size_t total = 0;
    DWORD n = 0;
    while (total + 1 < out_size &&
           ReadFile(read_end, out + total, (DWORD)(out_size - total - 1), &n, NULL) && n > 0)
        total += n;
    out[total] = '\0';
    CloseHandle(read_end);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    char *end = out + strlen(out);
    while (end > out && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) *--end = '\0';
    return exit_code == 0;
}

#endif

static int read_head_file(const char *root, char *out, size_t out_size) {
    char head_path[PATH_BUF];
    snprintf(head_path, sizeof(head_path), "%s" PATH_SEP_STR ".git", root);

    if (path_is_file(head_path)) {
        FILE *f = fopen(head_path, "r");
        if (!f) return 0;
        char line[PATH_BUF];
        char *read = fgets(line, sizeof(line), f);
        fclose(f);
        if (!read) return 0;
        char *gitdir = strstr(line, "gitdir:");
        if (!gitdir) return 0;
        snprintf(head_path, sizeof(head_path), "%s" PATH_SEP_STR "HEAD", str_trim(gitdir + 7));
    } else {
        snprintf(head_path, sizeof(head_path), "%s" PATH_SEP_STR ".git" PATH_SEP_STR "HEAD", root);
    }

    FILE *f = fopen(head_path, "r");
    if (!f) return 0;
    char line[PATH_BUF];
    char *read = fgets(line, sizeof(line), f);
    fclose(f);
    if (!read) return 0;

    char *value = str_trim(line);
    if (strncmp(value, "ref:", 4) == 0) {
        char *ref = str_trim(value + 4);
        char *name = strrchr(ref, '/');
        snprintf(out, out_size, "%s", name ? name + 1 : ref);
    } else {
        snprintf(out, out_size, "%.7s", value);
    }
    return 1;
}

const char *git_branch(void) {
    char root[PATH_BUF];
    if (!git_repo_root(root, sizeof(root))) return NULL;
    if (!read_head_file(root, cached_branch, sizeof(cached_branch))) return NULL;
    return cached_branch[0] ? cached_branch : NULL;
}

const char *git_repo_name(void) {
    char root[PATH_BUF];
    if (!git_repo_root(root, sizeof(root))) return NULL;
    if (!cached_repo[0]) {
        char *slash = path_last_sep(root);
        snprintf(cached_repo, sizeof(cached_repo), "%s", slash ? slash + 1 : root);
    }
    return cached_repo;
}

const char *git_user(void) {
    if (!user_loaded) {
        user_loaded = 1;
        if (!run_git("config user.name", NULL, cached_user, sizeof(cached_user)))
            cached_user[0] = '\0';
    }
    return cached_user[0] ? cached_user : NULL;
}

static DWORD WINAPI dirty_worker(LPVOID parameter) {
    char *root = (char *)parameter;
    char output[512];
    run_git("status --porcelain --untracked-files=no", root, output, sizeof(output));
    InterlockedExchange(&dirty_state, output[0] != '\0');
    dirty_stamp = GetTickCount();
    free(root);
    InterlockedExchange(&dirty_running, 0);
    return 0;
}

int git_dirty(void) {
    char root[PATH_BUF];
    if (!git_repo_root(root, sizeof(root))) {
        dirty_root[0] = '\0';
        InterlockedExchange(&dirty_state, 0);
        return 0;
    }

    if (strcmp(root, dirty_root) != 0) {
        snprintf(dirty_root, sizeof(dirty_root), "%s", root);
        InterlockedExchange(&dirty_state, 0);
        dirty_stamp = 0;
    }

    DWORD now = GetTickCount();
    int stale = dirty_stamp == 0 || now - dirty_stamp > DIRTY_REFRESH_MS;

    if (stale && InterlockedCompareExchange(&dirty_running, 1, 0) == 0) {
        char *copy = xstrdup(root);
        HANDLE thread = CreateThread(NULL, 0, dirty_worker, copy, 0, NULL);
        if (thread) {
            CloseHandle(thread);
        } else {
            free(copy);
            InterlockedExchange(&dirty_running, 0);
        }
    }
    return (int)dirty_state;
}
