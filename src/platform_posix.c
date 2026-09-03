/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef _WIN32

#include "platform.h"

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

extern char **environ;

#define TAG_SHIFT 40
#define TAG_FD ((intptr_t)1 << TAG_SHIFT)
#define TAG_PROCESS ((intptr_t)2 << TAG_SHIFT)
#define TAG_THREAD ((intptr_t)3 << TAG_SHIFT)
#define TAG_MASK ((intptr_t)0xff << TAG_SHIFT)
#define VALUE_MASK (((intptr_t)1 << TAG_SHIFT) - 1)

#define EPOCH_DIFFERENCE 11644473600ULL
#define TICKS_PER_SECOND 10000000ULL

static HANDLE fd_handle(int fd) {
    return (HANDLE)(TAG_FD | (intptr_t)fd);
}

#define PRIVATE_FD_FLOOR 10

static int lift_fd(int fd, int inheritable) {
    if (fd < 0 || fd >= PRIVATE_FD_FLOOR) return fd;
    int lifted = fcntl(fd, inheritable ? F_DUPFD : F_DUPFD_CLOEXEC, PRIVATE_FD_FLOOR);
    if (lifted < 0) return fd;
    close(fd);
    return lifted;
}

int fresh_handle_fd(HANDLE handle) {
    intptr_t value = (intptr_t)handle;
    if ((value & TAG_MASK) != TAG_FD) return -1;
    return (int)(value & VALUE_MASK);
}

HANDLE fresh_process_handle(pid_t pid) {
    return (HANDLE)(TAG_PROCESS | (intptr_t)pid);
}

static pid_t handle_pid(HANDLE handle) {
    intptr_t value = (intptr_t)handle;
    if ((value & TAG_MASK) != TAG_PROCESS) return -1;
    return (pid_t)(value & VALUE_MASK);
}

static void native_path(const char *path, char *out, size_t size) {
    size_t i = 0;
    for (; path[i] && i + 1 < size; i++) out[i] = path[i] == '\\' ? '/' : path[i];
    out[i] = '\0';
    if (strcmp(out, "nul") == 0) snprintf(out, size, "%s", "/dev/null");
}

DWORD GetLastError(void) {
    if (errno == EACCES || errno == EPERM) return ERROR_ACCESS_DENIED;
    if (errno == ENOTEMPTY || errno == EEXIST) return ERROR_DIR_NOT_EMPTY;
    return (DWORD)errno;
}

static FILETIME filetime_from_time(time_t seconds, long nanoseconds) {
    ULONGLONG ticks = ((ULONGLONG)seconds + EPOCH_DIFFERENCE) * TICKS_PER_SECOND +
                      (ULONGLONG)nanoseconds / 100ULL;
    FILETIME out;
    out.dwLowDateTime = (DWORD)(ticks & 0xffffffffULL);
    out.dwHighDateTime = (DWORD)(ticks >> 32);
    return out;
}

static ULONGLONG filetime_ticks(const FILETIME *time) {
    return ((ULONGLONG)time->dwHighDateTime << 32) | (ULONGLONG)(time->dwLowDateTime & 0xffffffffULL);
}

static time_t filetime_seconds(const FILETIME *time) {
    return (time_t)(filetime_ticks(time) / TICKS_PER_SECOND - EPOCH_DIFFERENCE);
}

static DWORD attributes_from_stat(const struct stat *info) {
    DWORD attributes = 0;
    if (S_ISDIR(info->st_mode)) attributes |= FILE_ATTRIBUTE_DIRECTORY;
    if (!(info->st_mode & S_IWUSR)) attributes |= FILE_ATTRIBUTE_READONLY;
    if (info->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) attributes |= FRESH_ATTRIBUTE_EXECUTABLE;
    return attributes;
}

static int stat_native(const char *path, struct stat *info) {
    char native[MAX_PATH];
    native_path(path, native, sizeof(native));
    if (stat(native, info) == 0) return 1;
    return lstat(native, info) == 0;
}

static void fill_attribute_data(const struct stat *info, WIN32_FILE_ATTRIBUTE_DATA *out) {
    out->dwFileAttributes = attributes_from_stat(info);
#ifdef __APPLE__
    out->ftCreationTime = filetime_from_time(info->st_birthtimespec.tv_sec, info->st_birthtimespec.tv_nsec);
    out->ftLastAccessTime = filetime_from_time(info->st_atimespec.tv_sec, info->st_atimespec.tv_nsec);
    out->ftLastWriteTime = filetime_from_time(info->st_mtimespec.tv_sec, info->st_mtimespec.tv_nsec);
#else
    out->ftCreationTime = filetime_from_time(info->st_ctim.tv_sec, info->st_ctim.tv_nsec);
    out->ftLastAccessTime = filetime_from_time(info->st_atim.tv_sec, info->st_atim.tv_nsec);
    out->ftLastWriteTime = filetime_from_time(info->st_mtim.tv_sec, info->st_mtim.tv_nsec);
#endif
    out->nFileSizeHigh = (DWORD)((ULONGLONG)info->st_size >> 32);
    out->nFileSizeLow = (DWORD)((ULONGLONG)info->st_size & 0xffffffffULL);
}

DWORD GetFileAttributesA(const char *path) {
    struct stat info;
    if (!stat_native(path, &info)) return INVALID_FILE_ATTRIBUTES;
    return attributes_from_stat(&info);
}

BOOL GetFileAttributesExA(const char *path, int level, WIN32_FILE_ATTRIBUTE_DATA *out) {
    (void)level;
    struct stat info;
    if (!stat_native(path, &info)) return FALSE;
    fill_attribute_data(&info, out);
    return TRUE;
}

BOOL SetFileAttributesA(const char *path, DWORD attributes) {
    char native[MAX_PATH];
    native_path(path, native, sizeof(native));
    struct stat info;
    if (stat(native, &info) != 0) return FALSE;

    mode_t mode = info.st_mode & 07777;
    if (attributes & FILE_ATTRIBUTE_READONLY) mode &= (mode_t)~(S_IWUSR | S_IWGRP | S_IWOTH);
    else mode |= S_IWUSR;
    return chmod(native, mode) == 0;
}

typedef struct {
    DIR *directory;
    char base[MAX_PATH];
    char leaf[MAX_PATH];
    int single;
} Finder;

static int wildcard_match(const char *pattern, const char *text) {
    while (*pattern) {
        if (*pattern == '*') {
            while (*pattern == '*') pattern++;
            if (!*pattern) return 1;
            for (const char *p = text; *p; p++) {
                if (wildcard_match(pattern, p)) return 1;
            }
            return 0;
        }
        if (!*text) return 0;
        if (*pattern != '?' && *pattern != *text) return 0;
        pattern++;
        text++;
    }
    return *text == '\0';
}

static void fill_find_data(const char *base, const char *name, WIN32_FIND_DATAA *data) {
    char full[MAX_PATH * 2];
    snprintf(full, sizeof(full), "%s%s", base, name);

    struct stat info;
    memset(data, 0, sizeof(*data));
    if (stat(full, &info) == 0 || lstat(full, &info) == 0) {
        WIN32_FILE_ATTRIBUTE_DATA attributes;
        fill_attribute_data(&info, &attributes);
        data->dwFileAttributes = attributes.dwFileAttributes;
        data->ftCreationTime = attributes.ftCreationTime;
        data->ftLastAccessTime = attributes.ftLastAccessTime;
        data->ftLastWriteTime = attributes.ftLastWriteTime;
        data->nFileSizeHigh = attributes.nFileSizeHigh;
        data->nFileSizeLow = attributes.nFileSizeLow;
    }
    snprintf(data->cFileName, sizeof(data->cFileName), "%s", name);
}

HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *data) {
    char native[MAX_PATH];
    native_path(pattern, native, sizeof(native));

    Finder *finder = calloc(1, sizeof(Finder));
    if (!finder) return INVALID_HANDLE_VALUE;

    const char *slash = strrchr(native, '/');
    if (slash) {
        size_t length = (size_t)(slash - native) + 1;
        memcpy(finder->base, native, length);
        finder->base[length] = '\0';
        snprintf(finder->leaf, sizeof(finder->leaf), "%s", slash + 1);
    } else {
        snprintf(finder->leaf, sizeof(finder->leaf), "%s", native);
    }

    if (!strpbrk(finder->leaf, "*?")) {
        struct stat info;
        if (stat(native, &info) != 0 && lstat(native, &info) != 0) {
            free(finder);
            return INVALID_HANDLE_VALUE;
        }
        finder->single = 1;
        fill_find_data(finder->base, finder->leaf, data);
        return finder;
    }

    finder->directory = opendir(finder->base[0] ? finder->base : ".");
    if (!finder->directory) {
        free(finder);
        return INVALID_HANDLE_VALUE;
    }
    if (!FindNextFileA(finder, data)) {
        FindClose(finder);
        return INVALID_HANDLE_VALUE;
    }
    return finder;
}

BOOL FindNextFileA(HANDLE find, WIN32_FIND_DATAA *data) {
    Finder *finder = find;
    if (!finder || finder->single || !finder->directory) return FALSE;

    struct dirent *entry;
    while ((entry = readdir(finder->directory)) != NULL) {
        if (!wildcard_match(finder->leaf, entry->d_name)) continue;
        fill_find_data(finder->base, entry->d_name, data);
        return TRUE;
    }
    return FALSE;
}

BOOL FindClose(HANDLE find) {
    Finder *finder = find;
    if (!finder) return FALSE;
    if (finder->directory) closedir(finder->directory);
    free(finder);
    return TRUE;
}

HANDLE CreateFileA(const char *path, DWORD access, DWORD share, SECURITY_ATTRIBUTES *sa,
                   DWORD disposition, DWORD flags, HANDLE template_file) {
    (void)share;
    (void)template_file;
    char native[MAX_PATH];
    native_path(path, native, sizeof(native));

    int wants_read = (access & GENERIC_READ) != 0;
    int wants_write = (access & (GENERIC_WRITE | FILE_APPEND_DATA | FILE_WRITE_ATTRIBUTES)) != 0;
    int mode = wants_read && wants_write ? O_RDWR : wants_write ? O_WRONLY : O_RDONLY;
    if (access & FILE_APPEND_DATA) mode |= O_APPEND;
    if (disposition == CREATE_ALWAYS) mode |= O_CREAT | O_TRUNC;
    else if (disposition == OPEN_ALWAYS) mode |= O_CREAT;
    if (!sa || !sa->bInheritHandle) mode |= O_CLOEXEC;

    int fd = open(native, mode, 0666);
    if (fd < 0) return INVALID_HANDLE_VALUE;
    fd = lift_fd(fd, sa && sa->bInheritHandle);
    if (flags & FILE_FLAG_DELETE_ON_CLOSE) unlink(native);
    return fd_handle(fd);
}

typedef struct {
    pid_t pid;
    int done;
    int status;
} Child;

static Child children[256];
static int child_count = 0;

static Child *child_find(pid_t pid) {
    for (int i = 0; i < child_count; i++) {
        if (children[i].pid == pid) return &children[i];
    }
    return NULL;
}

static Child *child_register(pid_t pid) {
    Child *child = child_find(pid);
    if (child) {
        child->done = 0;
        return child;
    }
    if (child_count == (int)(sizeof(children) / sizeof(children[0]))) {
        memmove(children, children + 1, sizeof(children) - sizeof(children[0]));
        child_count--;
    }
    child = &children[child_count++];
    child->pid = pid;
    child->done = 0;
    child->status = 0;
    return child;
}

static void child_forget(pid_t pid) {
    Child *child = child_find(pid);
    if (!child) return;
    size_t index = (size_t)(child - children);
    memmove(child, child + 1, (child_count - index - 1) * sizeof(Child));
    child_count--;
}

static int exit_status(int raw) {
    if (WIFEXITED(raw)) return WEXITSTATUS(raw);
    if (WIFSIGNALED(raw)) return 128 + WTERMSIG(raw);
    return raw;
}

static int child_reap(Child *child, int block) {
    if (child->done) return 1;
    int raw = 0;
    pid_t reaped = waitpid(child->pid, &raw, block ? 0 : WNOHANG);
    if (reaped == child->pid) {
        child->done = 1;
        child->status = exit_status(raw);
        return 1;
    }
    if (reaped < 0 && errno != EINTR) {
        child->done = 1;
        child->status = 127;
        return 1;
    }
    if (reaped < 0 && block) return child_reap(child, block);
    return 0;
}

typedef struct {
    pthread_t thread;
    LPTHREAD_START_ROUTINE start;
    LPVOID parameter;
    int used;
    int joined;
} Worker;

static Worker workers[32];

static void *worker_main(void *parameter) {
    Worker *worker = parameter;
    worker->start(worker->parameter);
    return NULL;
}

HANDLE CreateThread(SECURITY_ATTRIBUTES *sa, size_t stack, LPTHREAD_START_ROUTINE start,
                    LPVOID parameter, DWORD flags, DWORD *id) {
    (void)sa;
    (void)stack;
    (void)flags;
    if (id) *id = 0;

    for (size_t i = 0; i < sizeof(workers) / sizeof(workers[0]); i++) {
        if (workers[i].used) continue;
        workers[i].used = 1;
        workers[i].joined = 0;
        workers[i].start = start;
        workers[i].parameter = parameter;
        if (pthread_create(&workers[i].thread, NULL, worker_main, &workers[i]) != 0) {
            workers[i].used = 0;
            return NULL;
        }
        return (HANDLE)(TAG_THREAD | (intptr_t)i);
    }
    return NULL;
}

static Worker *handle_worker(HANDLE handle) {
    intptr_t value = (intptr_t)handle;
    if ((value & TAG_MASK) != TAG_THREAD) return NULL;
    return &workers[value & VALUE_MASK];
}

BOOL CloseHandle(HANDLE handle) {
    int fd = fresh_handle_fd(handle);
    if (fd >= 0) return close(fd) == 0;

    pid_t pid = handle_pid(handle);
    if (pid > 0) {
        Child *child = child_find(pid);
        if (child) {
            child_reap(child, 0);
            child_forget(pid);
        }
        return TRUE;
    }

    Worker *worker = handle_worker(handle);
    if (worker && worker->used) {
        if (!worker->joined) pthread_detach(worker->thread);
        worker->used = 0;
        return TRUE;
    }
    return FALSE;
}

BOOL ReadFile(HANDLE handle, void *buffer, DWORD size, DWORD *read_out, void *overlapped) {
    (void)overlapped;
    int fd = fresh_handle_fd(handle);
    if (fd < 0) return FALSE;
    ssize_t n;
    do {
        n = read(fd, buffer, size);
    } while (n < 0 && errno == EINTR);
    if (n < 0) return FALSE;
    *read_out = (DWORD)n;
    return TRUE;
}

BOOL WriteFile(HANDLE handle, const void *buffer, DWORD size, DWORD *written, void *overlapped) {
    (void)overlapped;
    int fd = fresh_handle_fd(handle);
    if (fd < 0) return FALSE;
    ssize_t n = write(fd, buffer, size);
    if (n < 0) return FALSE;
    if (written) *written = (DWORD)n;
    return TRUE;
}

DWORD SetFilePointer(HANDLE handle, LONG distance, LONG *high, DWORD method) {
    (void)high;
    int fd = fresh_handle_fd(handle);
    if (fd < 0) return (DWORD)-1;
    int whence = method == FILE_BEGIN ? SEEK_SET : SEEK_CUR;
    off_t where = lseek(fd, distance, whence);
    return where < 0 ? (DWORD)-1 : (DWORD)where;
}

BOOL SetEndOfFile(HANDLE handle) {
    int fd = fresh_handle_fd(handle);
    if (fd < 0) return FALSE;
    off_t where = lseek(fd, 0, SEEK_CUR);
    return where >= 0 && ftruncate(fd, where) == 0;
}

BOOL SetFileTime(HANDLE handle, const FILETIME *created, const FILETIME *accessed,
                 const FILETIME *written) {
    (void)created;
    int fd = fresh_handle_fd(handle);
    if (fd < 0) return FALSE;

    struct timespec times[2];
    times[0].tv_sec = accessed ? filetime_seconds(accessed) : 0;
    times[0].tv_nsec = accessed ? 0 : UTIME_NOW;
    times[1].tv_sec = written ? filetime_seconds(written) : 0;
    times[1].tv_nsec = written ? 0 : UTIME_NOW;
    return futimens(fd, times) == 0;
}

BOOL DeleteFileA(const char *path) {
    char native[MAX_PATH];
    native_path(path, native, sizeof(native));
    return unlink(native) == 0;
}

BOOL RemoveDirectoryA(const char *path) {
    char native[MAX_PATH];
    native_path(path, native, sizeof(native));
    return rmdir(native) == 0;
}

BOOL CopyFileA(const char *source, const char *destination, BOOL fail_if_exists) {
    char from[MAX_PATH];
    char to[MAX_PATH];
    native_path(source, from, sizeof(from));
    native_path(destination, to, sizeof(to));

    int in = open(from, O_RDONLY | O_CLOEXEC);
    if (in < 0) return FALSE;
    struct stat info;
    if (fstat(in, &info) != 0) {
        close(in);
        return FALSE;
    }

    int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (fail_if_exists ? O_EXCL : O_TRUNC);
    int out = open(to, flags, info.st_mode & 07777);
    if (out < 0) {
        close(in);
        return FALSE;
    }

    char buffer[65536];
    ssize_t n;
    int ok = 1;
    while ((n = read(in, buffer, sizeof(buffer))) > 0) {
        ssize_t done = 0;
        while (done < n) {
            ssize_t w = write(out, buffer + done, (size_t)(n - done));
            if (w < 0) {
                ok = 0;
                break;
            }
            done += w;
        }
        if (!ok) break;
    }
    if (n < 0) ok = 0;
    close(in);
    if (close(out) != 0) ok = 0;
    return ok;
}

BOOL MoveFileExA(const char *source, const char *destination, DWORD flags) {
    char from[MAX_PATH];
    char to[MAX_PATH];
    native_path(source, from, sizeof(from));
    native_path(destination, to, sizeof(to));

    if (rename(from, to) == 0) return TRUE;
    if (errno != EXDEV || !(flags & MOVEFILE_COPY_ALLOWED)) return FALSE;
    if (!CopyFileA(from, to, FALSE)) return FALSE;
    return unlink(from) == 0;
}

BOOL CreateHardLinkA(const char *link_path, const char *target, SECURITY_ATTRIBUTES *sa) {
    (void)sa;
    char from[MAX_PATH];
    char to[MAX_PATH];
    native_path(target, from, sizeof(from));
    native_path(link_path, to, sizeof(to));
    return link(from, to) == 0;
}

BOOL CreateSymbolicLinkA(const char *link_path, const char *target, DWORD flags) {
    (void)flags;
    char from[MAX_PATH];
    char to[MAX_PATH];
    native_path(target, from, sizeof(from));
    native_path(link_path, to, sizeof(to));
    return symlink(from, to) == 0;
}

DWORD GetTempPathA(DWORD size, char *out) {
    const char *directory = getenv("TMPDIR");
    if (!directory || !*directory) directory = "/tmp";
    size_t length = strlen(directory);
    int slash = length > 0 && directory[length - 1] != '/';
    int written = snprintf(out, size, "%s%s", directory, slash ? "/" : "");
    return written > 0 && (DWORD)written < size ? (DWORD)written : 0;
}

UINT GetTempFileNameA(const char *directory, const char *prefix, UINT unique, char *out) {
    (void)unique;
    char base[MAX_PATH];
    native_path(directory, base, sizeof(base));
    size_t length = strlen(base);
    int slash = length > 0 && base[length - 1] != '/';

    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s%s%sXXXXXX", base, slash ? "/" : "", prefix);
    int fd = mkstemp(pattern);
    if (fd < 0) return 0;
    close(fd);
    snprintf(out, MAX_PATH, "%s", pattern);
    return 1;
}

DWORD GetCurrentDirectoryA(DWORD size, char *out) {
    if (!getcwd(out, size)) return 0;
    return (DWORD)strlen(out);
}

BOOL SetCurrentDirectoryA(const char *path) {
    char native[MAX_PATH];
    native_path(path, native, sizeof(native));
    return chdir(native) == 0;
}

static void collapse_path(char *path) {
    char *segments[MAX_PATH / 2];
    size_t count = 0;
    char copy[MAX_PATH];
    snprintf(copy, sizeof(copy), "%s", path);

    char *cursor = copy;
    char *segment;
    while ((segment = strsep(&cursor, "/")) != NULL) {
        if (!*segment || strcmp(segment, ".") == 0) continue;
        if (strcmp(segment, "..") == 0) {
            if (count > 0) count--;
            continue;
        }
        segments[count++] = segment;
    }

    size_t offset = 0;
    path[0] = '\0';
    for (size_t i = 0; i < count; i++)
        offset += (size_t)snprintf(path + offset, MAX_PATH - offset, "/%s", segments[i]);
    if (count == 0) snprintf(path, MAX_PATH, "/");
}

DWORD GetFullPathNameA(const char *path, DWORD size, char *out, char **file_part) {
    if (file_part) *file_part = NULL;
    char native[MAX_PATH];
    native_path(path, native, sizeof(native));

    char full[MAX_PATH];
    if (native[0] == '/') {
        snprintf(full, sizeof(full), "%s", native);
    } else {
        char cwd[MAX_PATH];
        if (!getcwd(cwd, sizeof(cwd))) return 0;
        snprintf(full, sizeof(full), "%s/%s", cwd, native);
    }
    collapse_path(full);
    int written = snprintf(out, size, "%s", full);
    return written > 0 && (DWORD)written < size ? (DWORD)written : 0;
}

DWORD GetModuleFileNameA(void *module, char *out, DWORD size) {
    (void)module;
#ifdef __APPLE__
    char raw[MAX_PATH];
    uint32_t raw_size = sizeof(raw);
    if (_NSGetExecutablePath(raw, &raw_size) != 0) return 0;
    char resolved[MAX_PATH];
    const char *chosen = realpath(raw, resolved) ? resolved : raw;
    int written = snprintf(out, size, "%s", chosen);
#else
    ssize_t length = readlink("/proc/self/exe", out, size - 1);
    if (length < 0) return 0;
    out[length] = '\0';
    int written = (int)length;
#endif
    return written > 0 && (DWORD)written < size ? (DWORD)written : 0;
}

BOOL GetComputerNameA(char *out, DWORD *size) {
    if (gethostname(out, *size) != 0) return FALSE;
    out[*size - 1] = '\0';
    *size = (DWORD)strlen(out);
    return TRUE;
}

char *GetEnvironmentStringsA(void) {
    size_t total = 1;
    for (char **entry = environ; entry && *entry; entry++) total += strlen(*entry) + 1;

    char *block = malloc(total + 1);
    if (!block) return NULL;
    size_t offset = 0;
    for (char **entry = environ; entry && *entry; entry++) {
        size_t length = strlen(*entry) + 1;
        memcpy(block + offset, *entry, length);
        offset += length;
    }
    block[offset] = '\0';
    block[offset + 1] = '\0';
    return block;
}

BOOL FreeEnvironmentStringsA(char *block) {
    free(block);
    return TRUE;
}

BOOL SetEnvironmentVariableA(const char *name, const char *value) {
    if (!value) return unsetenv(name) == 0;
    return setenv(name, value, 1) == 0;
}

int _putenv_s(const char *name, const char *value) {
    if (!value || !*value) return unsetenv(name);
    return setenv(name, value, 1);
}

HANDLE GetStdHandle(DWORD which) {
    if (which == STD_INPUT_HANDLE) return fd_handle(0);
    if (which == STD_OUTPUT_HANDLE) return fd_handle(1);
    return fd_handle(2);
}

HANDLE GetCurrentProcess(void) {
    return fresh_process_handle(getpid());
}

DWORD GetCurrentProcessId(void) {
    return (DWORD)getpid();
}

BOOL DuplicateHandle(HANDLE source_process, HANDLE source, HANDLE target_process, HANDLE *out,
                     DWORD access, BOOL inherit, DWORD options) {
    (void)source_process;
    (void)target_process;
    (void)access;
    (void)options;
    int fd = fresh_handle_fd(source);
    if (fd < 0) return FALSE;
    int copy = fcntl(fd, inherit ? F_DUPFD : F_DUPFD_CLOEXEC, PRIVATE_FD_FLOOR);
    if (copy < 0) return FALSE;
    *out = fd_handle(copy);
    return TRUE;
}

BOOL SetHandleInformation(HANDLE handle, DWORD mask, DWORD flags) {
    int fd = fresh_handle_fd(handle);
    if (fd < 0 || !(mask & HANDLE_FLAG_INHERIT)) return FALSE;
    int current = fcntl(fd, F_GETFD);
    if (current < 0) return FALSE;
    int wanted = (flags & HANDLE_FLAG_INHERIT) ? (current & ~FD_CLOEXEC) : (current | FD_CLOEXEC);
    return fcntl(fd, F_SETFD, wanted) == 0;
}

BOOL CreatePipe(HANDLE *read_end, HANDLE *write_end, SECURITY_ATTRIBUTES *sa, DWORD size) {
    (void)sa;
    (void)size;
    int fds[2];
    if (pipe(fds) != 0) return FALSE;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    fds[0] = lift_fd(fds[0], 0);
    fds[1] = lift_fd(fds[1], 0);
    *read_end = fd_handle(fds[0]);
    *write_end = fd_handle(fds[1]);
    return TRUE;
}

int _open_osfhandle(intptr_t handle, int flags) {
    (void)flags;
    return fresh_handle_fd((HANDLE)handle);
}

intptr_t _get_osfhandle(int fd) {
    if (fcntl(fd, F_GETFD) < 0) return -1;
    return (intptr_t)fd_handle(fd);
}

HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD pid) {
    (void)access;
    (void)inherit;
    if (kill((pid_t)pid, 0) != 0 && errno != EPERM) return NULL;
    return fresh_process_handle((pid_t)pid);
}

BOOL TerminateProcess(HANDLE process, UINT code) {
    (void)code;
    pid_t pid = handle_pid(process);
    if (pid <= 0) return FALSE;
    return kill(pid, SIGTERM) == 0;
}

DWORD GetProcessId(HANDLE process) {
    pid_t pid = handle_pid(process);
    return pid > 0 ? (DWORD)pid : 0;
}

BOOL GetExitCodeProcess(HANDLE process, DWORD *code) {
    pid_t pid = handle_pid(process);
    if (pid <= 0) return FALSE;
    Child *child = child_find(pid);
    if (!child) child = child_register(pid);
    if (child_reap(child, 0)) {
        *code = (DWORD)child->status;
    } else {
        *code = STILL_ACTIVE;
    }
    return TRUE;
}

DWORD WaitForSingleObject(HANDLE handle, DWORD timeout) {
    (void)timeout;
    pid_t pid = handle_pid(handle);
    if (pid > 0) {
        Child *child = child_find(pid);
        if (!child) child = child_register(pid);
        child_reap(child, 1);
        return 0;
    }

    Worker *worker = handle_worker(handle);
    if (worker && worker->used && !worker->joined) {
        pthread_join(worker->thread, NULL);
        worker->joined = 1;
        return 0;
    }
    return (DWORD)-1;
}

LONG InterlockedExchange(volatile LONG *target, LONG value) {
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

LONG InterlockedCompareExchange(volatile LONG *target, LONG exchange, LONG comparand) {
    LONG expected = comparand;
    __atomic_compare_exchange_n(target, &expected, exchange, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}

DWORD GetTickCount(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (DWORD)((unsigned long long)now.tv_sec * 1000ULL + (unsigned long long)now.tv_nsec / 1000000ULL);
}

void Sleep(DWORD milliseconds) {
    struct timespec wait;
    wait.tv_sec = milliseconds / 1000;
    wait.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    while (nanosleep(&wait, &wait) != 0 && errno == EINTR) continue;
}

BOOL QueryPerformanceCounter(LARGE_INTEGER *out) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    out->QuadPart = (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
    return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER *out) {
    out->QuadPart = 1000000000LL;
    return TRUE;
}

void GetSystemTimeAsFileTime(FILETIME *out) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    *out = filetime_from_time(now.tv_sec, now.tv_nsec);
}

BOOL FileTimeToLocalFileTime(const FILETIME *utc, FILETIME *local) {
    time_t seconds = filetime_seconds(utc);
    struct tm parts;
    localtime_r(&seconds, &parts);
    long offset = parts.tm_gmtoff;
    ULONGLONG ticks = filetime_ticks(utc) + (ULONGLONG)((long long)offset * (long long)TICKS_PER_SECOND);
    local->dwLowDateTime = (DWORD)(ticks & 0xffffffffULL);
    local->dwHighDateTime = (DWORD)(ticks >> 32);
    return TRUE;
}

BOOL FileTimeToSystemTime(const FILETIME *time, SYSTEMTIME *out) {
    time_t seconds = filetime_seconds(time);
    struct tm parts;
    gmtime_r(&seconds, &parts);
    out->wYear = (unsigned short)(parts.tm_year + 1900);
    out->wMonth = (unsigned short)(parts.tm_mon + 1);
    out->wDayOfWeek = (unsigned short)parts.tm_wday;
    out->wDay = (unsigned short)parts.tm_mday;
    out->wHour = (unsigned short)parts.tm_hour;
    out->wMinute = (unsigned short)parts.tm_min;
    out->wSecond = (unsigned short)parts.tm_sec;
    out->wMilliseconds = (unsigned short)((filetime_ticks(time) / 10000ULL) % 1000ULL);
    return TRUE;
}

LONG CompareFileTime(const FILETIME *a, const FILETIME *b) {
    ULONGLONG left = filetime_ticks(a);
    ULONGLONG right = filetime_ticks(b);
    return left < right ? -1 : left > right ? 1 : 0;
}

#endif
