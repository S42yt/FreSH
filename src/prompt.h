/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_PROMPT_H
#define FRESH_PROMPT_H

#include "util.h"

void prompt_build(StrBuf *out);
void prompt_set_title(void);
void prompt_build_continuation(StrBuf *out);
void prompt_expand(const char *format, StrBuf *out);
int display_width(const char *text);

#endif
