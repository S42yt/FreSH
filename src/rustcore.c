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

#ifndef FRESH_RUST

#define CORE_MAX_ENTRIES 512

static unsigned long long folded_hash(const char *entry, size_t len) {
    unsigned long long hash = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < len; i++) {
        hash ^= (unsigned long long)tolower((unsigned char)entry[i]);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

static int same_folded(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    }
    return 1;
}

#endif

size_t core_path_merge(const char *const *parts, size_t count, char *out, size_t cap) {
    if (!parts || !out || cap == 0) return 0;

#ifdef FRESH_RUST
    return fresh_path_merge((const unsigned char *const *)(const void *)parts, count,
                            (unsigned char *)out, cap);
#else
    unsigned long long hashes[CORE_MAX_ENTRIES];
    size_t starts[CORE_MAX_ENTRIES];
    size_t lengths[CORE_MAX_ENTRIES];
    size_t kept = 0;
    size_t written = 0;

    for (size_t p = 0; p < count; p++) {
        const char *text = parts[p];
        if (!text) continue;

        while (*text) {
            const char *end = strchr(text, ';');
            size_t len = end ? (size_t)(end - text) : strlen(text);

            if (len > 0) {
                unsigned long long hash = folded_hash(text, len);
                int seen = 0;
                for (size_t i = 0; i < kept; i++) {
                    if (hashes[i] != hash || lengths[i] != len) continue;
                    if (same_folded(out + starts[i], text, len)) {
                        seen = 1;
                        break;
                    }
                }

                if (!seen) {
                    size_t separator = written > 0 ? 1 : 0;
                    if (written + separator + len + 1 > cap) {
                        out[written] = '\0';
                        return written;
                    }
                    if (separator) out[written++] = ';';

                    size_t start = written;
                    memcpy(out + written, text, len);
                    written += len;

                    if (kept < CORE_MAX_ENTRIES) {
                        hashes[kept] = hash;
                        starts[kept] = start;
                        lengths[kept] = len;
                        kept++;
                    }
                }
            }

            if (!end) break;
            text = end + 1;
        }
    }

    out[written] = '\0';
    return written;
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
