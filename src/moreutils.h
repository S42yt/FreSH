/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_MOREUTILS_H
#define FRESH_MOREUTILS_H

#include "builtins.h"

BuiltinFn moreutil_lookup(const char *name);
void moreutil_names(StrList *out);

#endif
