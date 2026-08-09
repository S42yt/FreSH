/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_PROMPT_H
#define FRESH_PROMPT_H

#include "util.h"

void prompt_build(StrBuf *out, int *visible_width);
void prompt_build_right(StrBuf *out, int *visible_width);
void prompt_build_continuation(StrBuf *out, int *visible_width);
int display_width(const char *text);

#endif
