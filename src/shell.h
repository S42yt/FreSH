/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_SHELL_H
#define FRESH_SHELL_H

#include "util.h"

#define FRESH_VERSION "2.0.0"

typedef struct {
    int running;
    int interactive;
    int last_status;
    int break_level;
    int continue_level;
    int returning;
    int depth;
    int errexit;
    int xtrace;
    int nounset;
    int condition_depth;
    StrList params;
    char *script_name;
} ShellState;

extern ShellState shell;

void shell_init(int interactive);
void shell_cleanup(void);
void shell_error(const char *fmt, ...);

#endif
