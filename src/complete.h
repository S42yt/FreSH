/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_COMPLETE_H
#define FRESH_COMPLETE_H

#include "util.h"

void complete_at(const char *buffer, size_t cursor, StrList *matches, size_t *replace_start);

#endif
