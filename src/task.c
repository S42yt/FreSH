/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "task.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

#include "commands.h"
#include "exec.h"
#include "parser.h"
#include "shell.h"
#include "style.h"
#include "util.h"

typedef struct {
    char *name;
    char *command;
    char *log;
    unsigned long pid;
    DWORD started;
    DWORD ended;
    int status;
    int finished;
    int stopped;
} Task;

static Task *tasks = NULL;
static int task_count = 0;
static int task_cap = 0;

static const char *TASK_USAGE =
    "task: usage: task [list] | run <name> <command> ... | log <name> | wait [<name> ...] | "
    "stop <name> ... | clear";

static Task *task_find(const char *name) {
    for (int i = 0; i < task_count; i++)
        if (strcmp(tasks[i].name, name) == 0) return &tasks[i];
    return NULL;
}

static Task *task_named(const char *name) {
    Task *task = task_find(name);
    if (!task) shell_error("task: %s: no such task", name);
    return task;
}

static void task_forget(Task *task) {
    DeleteFileA(task->log);
    free(task->name);
    free(task->command);
    free(task->log);
    int index = (int)(task - tasks);
    memmove(task, task + 1, (size_t)(task_count - index - 1) * sizeof(Task));
    task_count--;
}

void task_finished(unsigned long pid, int status) {
    for (int i = 0; i < task_count; i++) {
        if (tasks[i].finished || tasks[i].pid != pid) continue;
        tasks[i].finished = 1;
        tasks[i].status = status;
        tasks[i].ended = GetTickCount();
    }
}

static int task_name_valid(const char *name) {
    if (!isalpha((unsigned char)name[0]) && name[0] != '_') return 0;
    for (const char *p = name; *p; p++)
        if (!isalnum((unsigned char)*p) && !strchr("_.-", *p)) return 0;
    return 1;
}

static char *quoted(const char *text) {
    StrBuf out;
    sb_init(&out);
    sb_putc(&out, '\'');
    for (const char *p = text; *p; p++) {
        if (*p == '\'') sb_puts(&out, "'\\''");
        else sb_putc(&out, *p);
    }
    sb_putc(&out, '\'');
    return sb_take(&out);
}

static Redir *redir_new(int fd, RedirType type, char *target, Redir *next) {
    Redir *redir = xmalloc(sizeof(Redir));
    redir->fd = fd;
    redir->type = type;
    redir->target = target;
    redir->next = next;
    return redir;
}

static unsigned long task_start(const char *text, const char *log) {
    int incomplete = 0;
    char *error = NULL;
    Node *node = parse_string(text, &incomplete, &error);
    if (!node) {
        shell_error("task: %s", error ? error : "unexpected end of input");
        free(error);
        return 0;
    }
    free(error);
    if (node->kind != N_SIMPLE) {
        Node *group = xmalloc(sizeof(Node));
        memset(group, 0, sizeof(Node));
        group->kind = N_GROUP;
        group->right = node;
        node = group;
    }
    node->redirs = redir_new(1, R_OUT, quoted(log), redir_new(2, R_DUP, xstrdup("1"), node->redirs));
    node->background = 1;

    unsigned long before = shell.last_background_pid;
    shell.last_background_pid = 0;
    exec_node(node);
    node_free(node);
    unsigned long pid = shell.last_background_pid;
    if (!pid) shell.last_background_pid = before;
    return pid;
}

static int task_run(int argc, char **argv) {
    int first = argc > 3 && strcmp(argv[3], "--") == 0 ? 4 : 3;
    if (first >= argc) {
        fprintf(stderr, "%s\n", TASK_USAGE);
        return 2;
    }
    const char *name = argv[2];
    if (!task_name_valid(name)) {
        shell_error("task: %s: a name is letters, digits, _ . and -, starting with a letter", name);
        return 2;
    }
    Task *old = task_find(name);
    if (old && !old->finished) {
        shell_error("task: %s is still running", name);
        return 1;
    }

    char directory[PATH_BUF];
    char log[PATH_BUF];
    if (!GetTempPathA(sizeof(directory), directory) || !GetTempFileNameA(directory, "task", 0, log)) {
        shell_error("task: cannot create a log file for %s", name);
        return 1;
    }
    StrBuf text;
    sb_init(&text);
    for (int i = first; i < argc; i++) {
        if (i > first) sb_putc(&text, ' ');
        sb_puts(&text, argv[i]);
    }

    unsigned long pid = task_start(text.data, log);
    if (!pid) {
        shell_error("task: %s did not start", name);
        DeleteFileA(log);
        sb_free(&text);
        return 1;
    }
    jobs_name(pid, name, text.data);
    if (old) task_forget(old);
    if (task_count == task_cap) {
        task_cap = task_cap ? task_cap * 2 : 8;
        tasks = xrealloc(tasks, (size_t)task_cap * sizeof(Task));
    }
    Task *task = &tasks[task_count++];
    memset(task, 0, sizeof(Task));
    task->name = xstrdup(name);
    task->command = sb_take(&text);
    task->log = xstrdup(log);
    task->pid = pid;
    task->started = GetTickCount();
    return 0;
}

static const char *task_state(const Task *task, char *buffer, size_t size, const char **color) {
    *color = S_ERROR;
    if (!task->finished) {
        int slot = jobs_find_pid(task->pid);
        *color = S_VALUE;
        if (slot < 0) return "disowned";
        if (!jobs_stopped(slot)) return "running";
        *color = S_WARN;
        return "suspended";
    }
    if (task->stopped) {
        *color = S_WARN;
        return "stopped";
    }
    if (task->status == 0) {
        *color = S_ACCENT;
        return "done";
    }
    snprintf(buffer, size, "failed %d", task->status);
    return buffer;
}

static void task_elapsed(const Task *task, char *buffer, size_t size) {
    unsigned long seconds = (unsigned long)((task->finished ? task->ended : GetTickCount()) - task->started) / 1000;
    if (seconds < 60) snprintf(buffer, size, "%lus", seconds);
    else if (seconds < 3600) snprintf(buffer, size, "%lum%02lus", seconds / 60, seconds % 60);
    else snprintf(buffer, size, "%luh%02lum", seconds / 3600, seconds / 60 % 60);
}

static int task_list(void) {
    jobs_refresh();
    if (task_count == 0) return 0;
    int width = 4;
    for (int i = 0; i < task_count; i++)
        if ((int)strlen(tasks[i].name) > width) width = (int)strlen(tasks[i].name);

    printf("  %s%-*s  %-10s  %-7s  %s%s\n", style(S_LABEL), width, "name", "state", "time", "command",
           style(S_RESET));
    for (int i = 0; i < task_count; i++) {
        char state_buffer[32];
        char elapsed[32];
        const char *color;
        const char *state = task_state(&tasks[i], state_buffer, sizeof(state_buffer), &color);
        task_elapsed(&tasks[i], elapsed, sizeof(elapsed));
        printf("  %-*s  %s%-10s%s  %-7s  %s%s%s\n", width, tasks[i].name, style(color), state,
               style(S_RESET), elapsed, style(S_DIM), tasks[i].command, style(S_RESET));
    }
    return 0;
}

static int task_wait_one(Task *task) {
    int slot = task->finished ? -1 : jobs_find_pid(task->pid);
    if (slot >= 0 && jobs_stopped(slot)) {
        shell_error("task: %s is suspended, bg %%%s resumes it", task->name, task->name);
        return 1;
    }
    if (slot >= 0) jobs_wait(slot);
    if (!task->finished) {
        shell_error("task: %s is no longer a job of this shell", task->name);
        return 127;
    }
    return task->status;
}

static int task_wait(int argc, char **argv) {
    int status = 0;
    for (int i = 0; argc == 0 && i < task_count; i++) {
        int result = task_wait_one(&tasks[i]);
        if (result) status = result;
    }
    for (int i = 0; i < argc; i++) {
        Task *task = task_named(argv[i]);
        int result = task ? task_wait_one(task) : 127;
        if (result) status = result;
    }
    return status;
}

static int task_stop(int argc, char **argv) {
    int status = 0;
    BuiltinFn kill_command = coreutil_lookup("kill");
    for (int i = 0; i < argc; i++) {
        Task *task = task_named(argv[i]);
        if (!task) {
            status = 1;
            continue;
        }
        if (task->finished) continue;
        char pid[32];
        snprintf(pid, sizeof(pid), "%lu", task->pid);
        char *arguments[] = {"kill", pid, NULL};
        if (kill_command(2, arguments) != 0) status = 1;
        else task->stopped = 1;
    }
    return status;
}

static int task_log(const char *name) {
    Task *task = task_named(name);
    if (!task) return 1;
    fflush(stdout);
    char *arguments[] = {"cat", task->log, NULL};
    return coreutil_lookup("cat")(2, arguments);
}

static int task_clear(void) {
    jobs_refresh();
    for (int i = task_count - 1; i >= 0; i--)
        if (tasks[i].finished) task_forget(&tasks[i]);
    return 0;
}

void task_cleanup(void) {
    while (task_count > 0) task_forget(&tasks[task_count - 1]);
    free(tasks);
    tasks = NULL;
    task_cap = 0;
}

int builtin_task(int argc, char **argv) {
    const char *action = argc > 1 ? argv[1] : "list";
    if (strcmp(action, "list") == 0 && argc <= 2) return task_list();
    if (strcmp(action, "run") == 0) return task_run(argc, argv);
    if (strcmp(action, "log") == 0 && argc == 3) return task_log(argv[2]);
    if (strcmp(action, "wait") == 0) return task_wait(argc - 2, argv + 2);
    if (strcmp(action, "stop") == 0 && argc > 2) return task_stop(argc - 2, argv + 2);
    if (strcmp(action, "clear") == 0 && argc == 2) return task_clear();
    fprintf(stderr, "%s\n", TASK_USAGE);
    return 2;
}
