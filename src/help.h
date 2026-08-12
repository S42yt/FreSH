/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_HELP_H
#define FRESH_HELP_H

#include "util.h"

int help_show(const char *name);
const char *help_summary(const char *name);
void help_describe(const char *name, const char *summary, const char *usage, const char *detail);
void help_described_names(StrList *out);
int builtin_describe(int argc, char **argv);

#endif
