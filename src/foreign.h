/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_FOREIGN_H
#define FRESH_FOREIGN_H

#include "builtins.h"

int foreign_route(const char *line, int *status);
int foreign_run_powershell(const char *command);
int foreign_run_cmd(const char *command);
void foreign_names(StrList *out);
int foreign_name_prefix(const char *prefix, size_t length);
void foreign_complete(const char *prefix, size_t length, StrList *out);
int foreign_known(const char *name);
void foreign_forget(void);

int builtin_ps1(int argc, char **argv);
int builtin_cmd(int argc, char **argv);

#endif
