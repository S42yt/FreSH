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

#define FRESH_CORE "rust"

typedef struct {
    unsigned long long lines;
    unsigned long long words;
    unsigned long long bytes;
    unsigned int in_word;
} FreshCounts;

void fresh_sort_pointers(const unsigned char **items, size_t len, unsigned int mode);
void fresh_count_block(const unsigned char *data, size_t len, FreshCounts *counts);
size_t fresh_path_merge(const unsigned char *const *parts, size_t count, unsigned char *out,
                        size_t cap, unsigned char separator);

#define core_sort_pointers(items, len, mode) \
    fresh_sort_pointers((const unsigned char **)(void *)(items), (len), (mode))
#define core_count_block(data, len, counts) \
    fresh_count_block((const unsigned char *)(data), (len), (counts))
#define core_path_merge(parts, count, out, cap, separator) \
    fresh_path_merge((const unsigned char *const *)(const void *)(parts), (count), \
                     (unsigned char *)(out), (cap), (unsigned char)(separator))

#endif
