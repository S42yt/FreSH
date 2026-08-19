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
    int pipefail;
    int line;
    unsigned long started;
    int globstar;
    int nullglob;
    int dotglob;
    int nocasematch;
    int condition_depth;
    unsigned long last_background_pid;
    StrList params;
    char *script_name;
    char *trap_exit;
    char *trap_int;
    char *trap_err;
    char *trap_term;
    char *trap_hup;
    char *trap_quit;
    char *trap_debug;
    char *trap_return;
    int interrupted;
} ShellState;

extern ShellState shell;

enum { SIGNAL_INT, SIGNAL_QUIT, SIGNAL_CLOSE };

void shell_init(int interactive);
void shell_cleanup(void);
void shell_error(const char *fmt, ...);
void shell_handle_signal(int which);

#endif
