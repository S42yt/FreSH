/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "rustcore.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifndef FRESH_RUST

static int compare_bytes(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

static int compare_folded(const void *a, const void *b) {
    const unsigned char *left = *(const unsigned char **)a;
    const unsigned char *right = *(const unsigned char **)b;

    for (;;) {
        int x = tolower(*left);
        int y = tolower(*right);
        if (x != y) return x < y ? -1 : 1;
        if (x == 0) return 0;
        left++;
        right++;
    }
}

static int compare_numeric(const void *a, const void *b) {
    const char *left = *(const char **)a;
    const char *right = *(const char **)b;
    double x = atof(left);
    double y = atof(right);
    if (x < y) return -1;
    if (x > y) return 1;
    return strcmp(left, right);
}

#endif

void core_sort_pointers(char **items, size_t len, unsigned int mode) {
    if (!items || len < 2) return;

#ifdef FRESH_RUST
    fresh_sort_pointers((const unsigned char **)(void *)items, len, mode);
#else
    int (*comparator)(const void *, const void *) = compare_bytes;
    if (mode == FRESH_SORT_FOLD) comparator = compare_folded;
    else if (mode == FRESH_SORT_NUMERIC) comparator = compare_numeric;
    qsort(items, len, sizeof(char *), comparator);
#endif
}

void core_count_block(const char *data, size_t len, FreshCounts *counts) {
    if (!data || !counts) return;

#ifdef FRESH_RUST
    fresh_count_block((const unsigned char *)data, len, counts);
#else
    unsigned long long lines = counts->lines;
    unsigned long long words = counts->words;
    int in_word = counts->in_word != 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char byte = (unsigned char)data[i];
        if (byte == '\n') lines++;
        if (isspace(byte)) {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }

    counts->lines = lines;
    counts->words = words;
    counts->bytes += (unsigned long long)len;
    counts->in_word = (unsigned int)in_word;
#endif
}
