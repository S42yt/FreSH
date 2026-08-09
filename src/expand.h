/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_EXPAND_H
#define FRESH_EXPAND_H

#include "util.h"

void expand_words(const StrList *in, StrList *out);
char *expand_single(const char *word);
long eval_arith(const char *expr, int *ok);
int glob_expand(const char *pattern, StrList *out);

#endif
