/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_RUSTCORE_H
#define FRESH_RUSTCORE_H

#include <stddef.h>

#define FRESH_SORT_BYTES 0u
#define FRESH_SORT_FOLD 1u
#define FRESH_SORT_NUMERIC 2u

typedef struct {
    unsigned long long lines;
    unsigned long long words;
    unsigned long long bytes;
    unsigned int in_word;
} FreshCounts;

#ifdef FRESH_RUST

void fresh_sort_pointers(const unsigned char **items, size_t len, unsigned int mode);
void fresh_count_block(const unsigned char *data, size_t len, FreshCounts *counts);
unsigned long long fresh_count_newlines(const unsigned char *data, size_t len);
size_t fresh_path_merge(const unsigned char *const *parts, size_t count, unsigned char *out,
                        size_t cap);

#define FRESH_CORE "rust"

#else

#define FRESH_CORE "c"

#endif

void core_sort_pointers(char **items, size_t len, unsigned int mode);
void core_count_block(const char *data, size_t len, FreshCounts *counts);
size_t core_path_merge(const char *const *parts, size_t count, char *out, size_t cap);

#endif
