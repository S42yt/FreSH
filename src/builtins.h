/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_BUILTINS_H
#define FRESH_BUILTINS_H

#include "util.h"

typedef int (*BuiltinFn)(int argc, char **argv);

BuiltinFn builtin_lookup(const char *name);
void builtin_names(StrList *out);

#endif
