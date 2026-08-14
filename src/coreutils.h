/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_COREUTILS_H
#define FRESH_COREUTILS_H

#include "builtins.h"

BuiltinFn coreutil_lookup(const char *name);
int coreutil_preferred(const char *name);
void coreutil_names(StrList *out);
int coreutil_name_prefix(const char *prefix, size_t length);

#endif
