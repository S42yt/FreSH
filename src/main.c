/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "exec.h"
#include "history.h"
#include "line.h"
#include "parser.h"
#include "shell.h"
#include "term.h"
#include "util.h"
#include "vars.h"

Shell shell;

static const char *DEFAULT_RC =
    "# FreSH configuration - sourced on every start\n"
    "\n"
    "# prompt character shown before the cursor\n"
    "FRESH_PROMPT_CHAR=\"\xce\xbb\"\n"
    "# how many trailing path components to show\n"
    "FRESH_PATH_DEPTH=3\n"
    "# git branch, dirty marker, user name in prompt\n"
    "FRESH_SHOW_GIT=1\n"
    "FRESH_SHOW_GIT_DIRTY=1\n"
    "FRESH_SHOW_USER=1\n"
    "# right aligned exit code of the last command\n"
    "FRESH_SHOW_RPROMPT=1\n"
    "HISTSIZE=5000\n"
    "\n"
    "alias ll='ls -l'\n"
    "alias la='ls -la'\n"
    "alias ..='cd ..'\n"
    "alias ...='cd ../..'\n"
    "alias g='git'\n"
    "alias gs='git status'\n"
    "alias gp='git pull'\n";

void shell_error(const char *fmt, ...) {
    fflush(stdout);
    fputs("\x1b[31mFreSH: ", stderr);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs("\x1b[0m\n", stderr);
    fflush(stderr);
}

static void prepend_user_bins(void) {
    const char *directories[] = {"bin", ".local\\bin", "AppData\\Local\\bin", "scoop\\shims",
                                 "AppData\\Roaming\\npm"};
    StrBuf path;
    sb_init(&path);

    for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); i++) {
        char *candidate = path_join(home_dir(), directories[i]);
        if (path_is_dir(candidate)) {
            sb_puts(&path, candidate);
            sb_putc(&path, ';');
        }
        free(candidate);
    }

    const char *current = var_get("PATH");
    sb_puts(&path, current ? current : "");
    var_set_exported("PATH", path.data);
    sb_free(&path);
    path_rehash();
}

static void load_rc(void) {
    char *rc = config_path(".freshrc");
    if (!path_is_file(rc)) {
        FILE *f = fopen(rc, "w");
        if (f) {
            fputs(DEFAULT_RC, f);
            fclose(f);
        }
    }
    if (path_is_file(rc)) exec_script_file(rc, NULL);
    free(rc);
}

void shell_init(int interactive) {
    memset(&shell, 0, sizeof(shell));
    shell.running = 1;
    shell.interactive = interactive;
    sl_init(&shell.params);

    char exe[PATH_BUF];
    if (GetModuleFileNameA(NULL, exe, sizeof(exe))) sl_push_copy(&shell.params, exe);
    else sl_push_copy(&shell.params, "FreSH");

    vars_init();
    exec_init();
    prepend_user_bins();
}

void shell_cleanup(void) {
    exec_cleanup();
    vars_cleanup();
    sl_free(&shell.params);
}

static int needs_more_input(const char *text) {
    int incomplete = 0;
    char *error = NULL;
    Node *node = parse_string(text, &incomplete, &error);
    node_free(node);
    free(error);
    return incomplete;
}

static void interactive_loop(void) {
    printf("\x1b[1;32mFreSH\x1b[0m %s  \x1b[90mtype help for the basics\x1b[0m\n\n", FRESH_VERSION);

    while (shell.running) {
        char *line = line_read(0);
        if (!line) break;

        StrBuf command;
        sb_init(&command);
        sb_puts(&command, line);
        free(line);

        while (command.len > 0 && needs_more_input(command.data)) {
            char *more = line_read(1);
            if (!more) break;
            sb_putc(&command, '\n');
            sb_puts(&command, more);
            free(more);
        }

        char *trimmed = str_trim(command.data);
        if (*trimmed) {
            history_add(trimmed);
            exec_text(trimmed);
        }
        sb_free(&command);
    }
}

int main(int argc, char *argv[]) {
    int interactive = 1;
    const char *command = NULL;
    const char *script = NULL;
    int script_arg = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            command = argv[++i];
            interactive = 0;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("FreSH %s\n", FRESH_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: FreSH [-c command] [script [args...]]\n");
            return 0;
        } else {
            script = argv[i];
            script_arg = i + 1;
            interactive = 0;
            break;
        }
    }

    term_init();
    shell_init(interactive);

    if (script && path_is_dir(script)) {
        SetCurrentDirectoryA(script);
        script = NULL;
        shell.interactive = 1;
    }

    int status = 0;
    if (command) {
        load_rc();
        status = exec_text(command);
    } else if (script) {
        load_rc();
        StrList args;
        sl_init(&args);
        for (int i = script_arg; i < argc; i++) sl_push_copy(&args, argv[i]);
        status = exec_script_file(script, &args);
        sl_free(&args);
    } else {
        load_rc();
        history_load();
        interactive_loop();
        history_save();
        status = shell.last_status;
    }

    shell_cleanup();
    term_cleanup();
    return status;
}
