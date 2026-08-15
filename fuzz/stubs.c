/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "exec.h"
#include "shell.h"

ShellState shell;

void shell_error(const char *fmt, ...) {
    (void)fmt;
}

int exec_subshell(const char *text) {
    (void)text;
    return 0;
}

int exec_line(const char *line) {
    (void)line;
    return 0;
}
