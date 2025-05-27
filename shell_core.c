/*
 * Copyright (c) 2025 Musa/S42
 * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#include "shell_core.h"
#include "error_handler.h"
#include "command_parser.h"
#include "builtins.h"
#include <string.h>
#include <direct.h>
#include <ctype.h>

static int shell_running = 0;

void shell_init() {
    SetConsoleOutputCP(CP_UTF8);
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

    shell_running = 1;

    error_handler_init();
    parser_init();
}

int shell_execute(const char *cmd) {
    if (!cmd || strlen(cmd) == 0) {
        return 0;
    }

    char command[MAX_CMD];
    strcpy(command, cmd);

    char *start = command;
    while (*start && isspace(*start)) start++;

    if (strlen(start) == 0) {
        return 0;
    }

    if (strcmp(start, "exit") == 0 || strcmp(start, "quit") == 0) {
        shell_running = 0;
        return 0;
    }

    if (strcmp(start, "cd") == 0) {
        const char *home = getenv("USERPROFILE");
        if (home) {
            _chdir(home);
        }
        return 0;
    } else if (strncmp(start, "cd ", 3) == 0) {
        char *path = start + 3;
        while (*path && isspace(*path)) path++;

        if (_chdir(path) != 0) {
            show_error(ERROR_DIRECTORY_NOT_FOUND_VAL, path);
            return 1;
        }
        return 0;
    }

    if (handle_builtin(start)) {
        return 0;
    }

    return execute_external_command(start);
}

void shell_cleanup() {
    error_handler_cleanup();
    parser_cleanup();
}

int is_shell_running() {
    return shell_running;
}
