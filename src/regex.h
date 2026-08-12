/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_REGEX_H
#define FRESH_REGEX_H

#include "util.h"

#define REGEX_GROUPS 10

typedef struct {
    int start[REGEX_GROUPS];
    int end[REGEX_GROUPS];
    int count;
} RegexMatch;

int regex_search(const char *pattern, const char *text, RegexMatch *match);
int regex_replace(const char *pattern, const char *replacement, const char *text, int global,
                  StrBuf *out);

#endif
