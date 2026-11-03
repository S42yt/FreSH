/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_COMPLETE_H
#define FRESH_COMPLETE_H

#include "util.h"

void complete_at(const char *buffer, size_t cursor, StrList *matches, size_t *replace_start);
int complete_nospace(void);
void complete_forget(void);

int builtin_complete_spec(int argc, char **argv);
int builtin_compgen(int argc, char **argv);
int builtin_compopt(int argc, char **argv);
int builtin_comp_words_by_ref(int argc, char **argv);
int builtin_filedir(int argc, char **argv);

#endif
