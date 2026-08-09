/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_HISTORY_H
#define FRESH_HISTORY_H

void history_load(void);
void history_save(void);
void history_add(const char *line);
void history_clear(void);
int history_count(void);
const char *history_get(int index);
int history_search_prefix(const char *prefix, int from, int direction);
int history_search_substring(const char *needle, int from);

#endif
