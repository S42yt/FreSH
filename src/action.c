/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "action.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "platform.h"

#ifndef _WIN32
#include <pthread.h>
#endif

#include "exec.h"
#include "shell.h"
#include "style.h"
#include "vars.h"

#define ACTION_STACK (16u * 1024u * 1024u)

#ifdef _WIN32
typedef HANDLE Signal;

static Signal signal_new(void) { return CreateSemaphoreA(NULL, 0, 1, NULL); }
static void signal_post(Signal s) { ReleaseSemaphore(s, 1, NULL); }
static void signal_wait(Signal s) { WaitForSingleObject(s, INFINITE); }
static void signal_free(Signal s) { CloseHandle(s); }
#else
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int posted;
} *Signal;

static Signal signal_new(void) {
    Signal s = xmalloc(sizeof(*s));
    pthread_mutex_init(&s->lock, NULL);
    pthread_cond_init(&s->cond, NULL);
    s->posted = 0;
    return s;
}

static void signal_post(Signal s) {
    pthread_mutex_lock(&s->lock);
    s->posted = 1;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->lock);
}

static void signal_wait(Signal s) {
    pthread_mutex_lock(&s->lock);
    while (!s->posted) pthread_cond_wait(&s->cond, &s->lock);
    s->posted = 0;
    pthread_mutex_unlock(&s->lock);
}

static void signal_free(Signal s) {
    pthread_mutex_destroy(&s->lock);
    pthread_cond_destroy(&s->cond);
    free(s);
}
#endif

typedef enum { WAIT_START, WAIT_CD, WAIT_PROMPT, WAIT_COMMAND, WAIT_AFTER, WAIT_CHANGE } Wait;

typedef struct {
    StrList params;
    int depth;
    int returnable;
    int returning;
    int break_level;
    int continue_level;
    int condition_depth;
    int line;
    int errexit;
    int xtrace;
    int nounset;
    int pipefail;
    int last_status;
    int running;
    char *funcname;
} Frame;

typedef struct {
    int id;
    char *name;
    char *origin;
    Node *body;
    StrList args;
    Node **adopted;
    size_t adopted_len;
    Signal go;
    Signal done;
    Frame frame;
    void *scopes;
    size_t scope_base;
    int isolation;
    Wait wait;
    char *argument;
    unsigned long since;
    unsigned long duration;
    unsigned long armed;
    long long stamp;
    const char *event;
    char *event_arg;
    int event_status;
    int stop;
    int finished;
    int status;
} Action;

static Action **actions;
static size_t action_count;
static size_t action_cap;
static int next_id = 1;
static Action *running_action;
static _Thread_local Action *self;
static StrList *collecting;
static const char *loading_origin;
static char *last_cwd;
static unsigned long serial;

static void swap_int(int *a, int *b) {
    int t = *a;
    *a = *b;
    *b = t;
}

static void frame_swap(Frame *f) {
    StrList params = shell.params;
    shell.params = f->params;
    f->params = params;
    swap_int(&shell.depth, &f->depth);
    swap_int(&shell.returnable, &f->returnable);
    swap_int(&shell.returning, &f->returning);
    swap_int(&shell.break_level, &f->break_level);
    swap_int(&shell.continue_level, &f->continue_level);
    swap_int(&shell.condition_depth, &f->condition_depth);
    swap_int(&shell.line, &f->line);
    swap_int(&shell.errexit, &f->errexit);
    swap_int(&shell.xtrace, &f->xtrace);
    swap_int(&shell.nounset, &f->nounset);
    swap_int(&shell.pipefail, &f->pipefail);
    swap_int(&shell.last_status, &f->last_status);
    swap_int(&shell.running, &f->running);

    const char *current = var_get("FUNCNAME");
    char *outgoing = current ? xstrdup(current) : NULL;
    if (f->funcname) var_set("FUNCNAME", f->funcname);
    else var_unset("FUNCNAME");
    free(f->funcname);
    f->funcname = outgoing;
}

#ifdef _WIN32
static DWORD WINAPI action_main(LPVOID parameter) {
#else
static void *action_main(void *parameter) {
#endif
    Action *a = parameter;
    self = a;
    signal_wait(a->go);
    a->status = exec_function_body(a->body, &a->args);
    a->finished = 1;
    signal_post(a->done);
    return 0;
}

static int thread_start(Action *a) {
#ifdef _WIN32
    HANDLE thread = CreateThread(NULL, ACTION_STACK, action_main, a,
                                 STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!thread) return 0;
    CloseHandle(thread);
    return 1;
#else
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, ACTION_STACK);
    pthread_t thread;
    int failed = pthread_create(&thread, &attributes, action_main, a);
    pthread_attr_destroy(&attributes);
    if (failed) return 0;
    pthread_detach(thread);
    return 1;
#endif
}

static void resume(Action *a) {
    Action *previous = running_action;
    a->scope_base = scope_level();
    a->isolation = exec_isolated();
    frame_swap(&a->frame);
    scope_attach(a->scopes);
    a->scopes = NULL;

    running_action = a;
    signal_post(a->go);
    signal_wait(a->done);
    running_action = previous;

    a->scopes = scope_detach(a->scope_base);
    frame_swap(&a->frame);
    var_unset("FRESH_EVENT");
    var_unset("FRESH_EVENT_ARG");
}

static void action_free(Action *a) {
    free(a->name);
    free(a->origin);
    sl_free(&a->args);
    for (size_t i = 0; i < a->adopted_len; i++) node_free(a->adopted[i]);
    free(a->adopted);
    signal_free(a->go);
    signal_free(a->done);
    sl_free(&a->frame.params);
    free(a->frame.funcname);
    free(a->argument);
    free(a->event_arg);
    free(a);
}

static void reap(void) {
    size_t kept = 0;
    for (size_t i = 0; i < action_count; i++) {
        if (actions[i]->finished && actions[i] != running_action) action_free(actions[i]);
        else actions[kept++] = actions[i];
    }
    action_count = kept;
}

int action_start(const char *name, Node *body, const StrList *args) {
    Action *a = xmalloc(sizeof(Action));
    memset(a, 0, sizeof(Action));
    a->id = next_id++;
    a->name = xstrdup(name);
    a->origin = loading_origin ? xstrdup(loading_origin) : NULL;
    a->body = body;
    sl_init(&a->args);
    for (size_t i = 0; i < args->len; i++) sl_push_copy(&a->args, args->items[i]);
    sl_init(&a->frame.params);
    a->frame.running = 1;
    a->wait = WAIT_START;
    a->go = signal_new();
    a->done = signal_new();

    if (!thread_start(a)) {
        shell_error("%s: could not start the action", name);
        action_free(a);
        return 1;
    }

    if (action_count == action_cap) {
        action_cap = action_cap ? action_cap * 2 : 8;
        actions = xrealloc(actions, action_cap * sizeof(Action *));
    }
    actions[action_count++] = a;

    if (!last_cwd) {
        char cwd[PATH_BUF];
        if (GetCurrentDirectoryA(sizeof(cwd), cwd)) last_cwd = xstrdup(cwd);
    }

    resume(a);
    int status = a->finished ? a->status : 0;
    reap();
    return status;
}

int action_adopt(Node *body) {
    for (size_t i = 0; i < action_count; i++) {
        Action *a = actions[i];
        if (a->body != body || a->finished) continue;
        a->adopted = xrealloc(a->adopted, (a->adopted_len + 1) * sizeof(Node *));
        a->adopted[a->adopted_len++] = body;
        return 1;
    }
    return 0;
}

void action_note_function(const char *name) {
    if (collecting && !sl_contains(collecting, name)) sl_push_copy(collecting, name);
}

static long long file_stamp(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) return -1;
    return (long long)info.st_mtime * 1000003LL + (long long)info.st_size;
}

static const char *first_word(const char *line, char *buffer, size_t size) {
    while (*line == ' ' || *line == '\t') line++;
    size_t length = 0;
    while (line[length] && !isspace((unsigned char)line[length]) && length + 1 < size) length++;
    memcpy(buffer, line, length);
    buffer[length] = '\0';
    return buffer;
}

static int due(const Action *a, Wait kind, const char *arg) {
    if (a->finished || a->wait != kind) return 0;
    switch (kind) {
    case WAIT_COMMAND: {
        if (a->armed >= serial) return 0;
        if (!a->argument) return 1;
        char word[256];
        return pattern_match(a->argument, first_word(arg ? arg : "", word, sizeof(word)));
    }
    case WAIT_AFTER: return (unsigned long)(GetTickCount() - a->since) >= a->duration;
    case WAIT_CHANGE: return file_stamp(a->argument) != a->stamp;
    default: return 1;
    }
}

static void fire(Wait kind, const char *event, const char *arg, int status) {
    int *ids = NULL;
    size_t count = 0;
    for (size_t i = 0; i < action_count; i++) {
        if (!due(actions[i], kind, arg)) continue;
        ids = xrealloc(ids, (count + 1) * sizeof(int));
        ids[count++] = actions[i]->id;
    }

    for (size_t n = 0; n < count; n++) {
        for (size_t i = 0; i < action_count; i++) {
            Action *a = actions[i];
            if (a->id != ids[n] || !due(a, kind, arg)) continue;
            a->event = event;
            free(a->event_arg);
            a->event_arg = xstrdup(kind == WAIT_CHANGE ? a->argument : arg ? arg : "");
            a->event_status = kind == WAIT_COMMAND ? status : 0;
            resume(a);
            break;
        }
    }
    free(ids);
    reap();
}

static int safe(void) {
    return action_count > 0 && !running_action && !self && !exec_isolated();
}

static int anyone_waits(Wait kind) {
    for (size_t i = 0; i < action_count; i++) {
        if (actions[i]->wait == kind && !actions[i]->finished) return 1;
    }
    return 0;
}

static void checkpoint(const char *line, int status, int prompt) {
    if (line) fire(WAIT_COMMAND, "command", line, status);

    char cwd[PATH_BUF];
    if (GetCurrentDirectoryA(sizeof(cwd), cwd)) {
        if (last_cwd && strcmp(last_cwd, cwd) != 0) {
            free(last_cwd);
            last_cwd = xstrdup(cwd);
            char *shown = xstrdup(cwd);
            path_to_slashes(shown);
            fire(WAIT_CD, "cd", shown, 0);
            free(shown);
        } else if (!last_cwd) {
            last_cwd = xstrdup(cwd);
        }
    }

    fire(WAIT_AFTER, "after", "", 0);
    fire(WAIT_CHANGE, "change", "", 0);
    if (prompt) fire(WAIT_PROMPT, "prompt", "", 0);
}

void actions_begin(void) {
    if (!self && scope_level() == 0 && !exec_isolated()) serial++;
}

void actions_statement(const Node *node, int status) {
    if (!safe() || shell.interactive || scope_level() != 0 || node->kind == N_FUNC) return;
    StrBuf line;
    sb_init(&line);
    if (anyone_waits(WAIT_COMMAND)) node_source(node, &line);
    checkpoint(line.data, status, 0);
    sb_free(&line);
}

void actions_after_line(const char *line, int status) {
    if (safe()) checkpoint(line, status, 0);
}

void actions_before_prompt(void) {
    if (safe()) checkpoint(NULL, shell.last_status, 1);
}

int actions_idle_waiting(void) {
    return safe() && (anyone_waits(WAIT_AFTER) || anyone_waits(WAIT_CHANGE));
}

int actions_idle_due(void) {
    for (size_t i = 0; i < action_count; i++) {
        if (due(actions[i], WAIT_AFTER, "") || due(actions[i], WAIT_CHANGE, "")) return 1;
    }
    return 0;
}

void actions_idle_fire(void) {
    if (safe()) checkpoint(NULL, shell.last_status, 0);
}

static int parse_duration(const char *text, unsigned long *out) {
    char *end = NULL;
    double value = strtod(text, &end);
    if (end == text || value < 0) return 0;
    double unit = 1000;
    if (strcmp(end, "ms") == 0) unit = 1;
    else if (strcmp(end, "s") == 0 || *end == '\0') unit = 1000;
    else if (strcmp(end, "m") == 0) unit = 60000;
    else if (strcmp(end, "h") == 0) unit = 3600000;
    else return 0;
    *out = (unsigned long)(value * unit);
    return 1;
}

int builtin_yield(int argc, char **argv) {
    Action *a = self;
    if (!a || a != running_action) {
        shell_error("yield: only works inside a *function");
        return 2;
    }

    const char *kind = argc > 1 ? argv[1] : "";
    Wait wait;
    char *argument = NULL;
    unsigned long duration = 0;
    if (strcmp(kind, "cd") == 0 && argc == 2) {
        wait = WAIT_CD;
    } else if (strcmp(kind, "prompt") == 0 && argc == 2) {
        wait = WAIT_PROMPT;
    } else if (strcmp(kind, "command") == 0 && argc <= 3) {
        wait = WAIT_COMMAND;
        if (argc == 3) argument = xstrdup(argv[2]);
    } else if (strcmp(kind, "after") == 0 && argc == 3 && parse_duration(argv[2], &duration)) {
        wait = WAIT_AFTER;
    } else if (strcmp(kind, "change") == 0 && argc == 3) {
        wait = WAIT_CHANGE;
        argument = xstrdup(argv[2]);
    } else {
        shell_error("yield: usage: yield cd | prompt | command [<pattern>] | after <time> | "
                    "change <file>");
        return 2;
    }

    if (exec_isolated() != a->isolation) {
        free(argument);
        shell_error("yield: cannot wait inside a redirection, a pipe or $( )");
        return 1;
    }

    free(a->argument);
    a->argument = argument;
    a->wait = wait;
    a->armed = serial;
    a->since = GetTickCount();
    a->duration = duration;
    if (wait == WAIT_CHANGE) a->stamp = file_stamp(argument);

    signal_post(a->done);
    signal_wait(a->go);

    if (a->stop) {
        shell.running = 0;
        return 1;
    }
    var_set("FRESH_EVENT", a->event ? a->event : "");
    var_set("FRESH_EVENT_ARG", a->event_arg ? a->event_arg : "");
    return a->event_status;
}

static void stop_action(Action *a) {
    if (a->finished) return;
    if (a == self) {
        shell.running = 0;
        return;
    }
    if (a == running_action) return;
    a->stop = 1;
    resume(a);
}

static void describe_wait(const Action *a, StrBuf *out) {
    switch (a->wait) {
    case WAIT_START: sb_puts(out, "running"); break;
    case WAIT_CD: sb_puts(out, "waiting for cd"); break;
    case WAIT_PROMPT: sb_puts(out, "waiting for the prompt"); break;
    case WAIT_COMMAND:
        sb_printf(out, "waiting for %s%s", a->argument ? "command " : "a command",
                  a->argument ? a->argument : "");
        break;
    case WAIT_AFTER: {
        unsigned long passed = (unsigned long)(GetTickCount() - a->since);
        unsigned long left = passed >= a->duration ? 0 : (a->duration - passed + 999) / 1000;
        sb_puts(out, "waiting ");
        if (left >= 3600) sb_printf(out, "%luh %lum", left / 3600, left % 3600 / 60);
        else if (left >= 60) sb_printf(out, "%lum %lus", left / 60, left % 60);
        else sb_printf(out, "%lus", left);
        break;
    }
    case WAIT_CHANGE: sb_printf(out, "waiting for %s to change", a->argument); break;
    }
}

static void list_actions(void) {
    if (action_count == 0) {
        printf("  %sno actions running%s\n", style(S_DIM), style(S_RESET));
        return;
    }
    for (size_t i = 0; i < action_count; i++) {
        Action *a = actions[i];
        if (a->finished) continue;
        StrBuf state;
        sb_init(&state);
        describe_wait(a, &state);
        const char *origin = a->origin ? a->origin : "";
        char *home = xstrdup(var_get("HOME") ? var_get("HOME") : "");
        path_to_slashes(home);
        size_t length = strlen(home);
        int under_home = length && strncmp(origin, home, length) == 0 && origin[length] == '/';
        printf("  %s%-3d%s %-20s %-30s %s%s%s%s\n", style(S_DIM), a->id, style(S_RESET), a->name,
               state.data, style(S_DIM), under_home ? "~" : "", under_home ? origin + length : origin,
               style(S_RESET));
        free(home);
        sb_free(&state);
    }
}

static int header_ok(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char line[128] = "";
    if (!fgets(line, sizeof(line), f)) line[0] = '\0';
    fclose(f);
    if (strncmp(line, "#!", 2) != 0) return 0;
    char *p = line + 2;
    while (*p == ' ' || *p == '\t') p++;
    size_t length = strlen("fresh_action");
    if (strncmp(p, "fresh_action", length) != 0) return 0;
    p += length;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return *p == '\0';
}

static int load_file(const char *path) {
    if (!path_is_file(path)) {
        shell_error("action: %s: no such file", path);
        return 1;
    }
    if (!header_ok(path)) {
        shell_error("action: %s: the first line has to be #! fresh_action", path);
        return 1;
    }

    char *shown = xstrdup(path);
    path_to_slashes(shown);

    StrList names;
    sl_init(&names);
    StrList *outer = collecting;
    const char *outer_origin = loading_origin;
    collecting = &names;
    loading_origin = shown;
    int status = exec_script_file(path, NULL);
    collecting = outer;

    for (size_t i = 0; i < names.len; i++) {
        Node *body = function_body(names.items[i]);
        if (!body || !function_is_star(names.items[i])) continue;
        StrList args;
        sl_init(&args);
        sl_push_copy(&args, names.items[i]);
        action_start(names.items[i], body, &args);
        sl_free(&args);
    }
    loading_origin = outer_origin;
    sl_free(&names);
    free(shown);
    return status;
}

static char *actions_dir(void) {
    const char *custom = var_get("FRESH_HOME");
    char *home = custom && *custom ? xstrdup(custom) : config_path(".fresh");
    char *directory = path_join(home, "actions");
    free(home);
    return directory;
}

static void folder_files(StrList *out) {
    char *directory = actions_dir();
    char *pattern = path_join(directory, "*.frsh");
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            sl_push(out, path_join(directory, data.cFileName));
        } while (FindNextFileA(find, &data));
        FindClose(find);
    }
    sl_sort(out);
    free(pattern);
    free(directory);
}

void actions_load_folder(void) {
    StrList files;
    sl_init(&files);
    folder_files(&files);
    for (size_t i = 0; i < files.len; i++) load_file(files.items[i]);
    sl_free(&files);
}

static Action *find_action(const char *who) {
    char *end = NULL;
    long id = strtol(who, &end, 10);
    for (size_t i = 0; i < action_count; i++) {
        Action *a = actions[i];
        if (a->finished) continue;
        if ((*end == '\0' && a->id == id) || strcmp(a->name, who) == 0) return a;
    }
    return NULL;
}

static void stop_where(int from_folder) {
    char *directory = actions_dir();
    path_to_slashes(directory);
    size_t length = strlen(directory);
    for (size_t i = 0; i < action_count; i++) {
        Action *a = actions[i];
        if (from_folder && (!a->origin || strncmp(a->origin, directory, length) != 0)) continue;
        stop_action(a);
    }
    free(directory);
    reap();
}

int builtin_action(int argc, char **argv) {
    const char *command = argc > 1 ? argv[1] : "list";

    if (strcmp(command, "list") == 0) {
        list_actions();
        return 0;
    }

    if (strcmp(command, "run") == 0) {
        if (argc < 3) {
            shell_error("action: usage: action run <file> ...");
            return 2;
        }
        int status = 0;
        for (int i = 2; i < argc; i++) status |= load_file(argv[i]);
        return status;
    }

    if (strcmp(command, "stop") == 0) {
        if (argc < 3) {
            shell_error("action: usage: action stop <name|id|all> ...");
            return 2;
        }
        int status = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "all") == 0) {
                stop_where(0);
                continue;
            }
            Action *a = find_action(argv[i]);
            if (!a) {
                shell_error("action: %s: no such action", argv[i]);
                status = 1;
                continue;
            }
            stop_action(a);
        }
        reap();
        return status;
    }

    if (strcmp(command, "reload") == 0) {
        stop_where(1);
        actions_load_folder();
        return 0;
    }

    shell_error("action: usage: action [list | run <file> ... | stop <name|id|all> ... | reload]");
    return 2;
}
