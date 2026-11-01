/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_TASK_H
#define FRESH_TASK_H

int builtin_task(int argc, char **argv);
void task_finished(unsigned long pid, int status);
void task_cleanup(void);

#endif
