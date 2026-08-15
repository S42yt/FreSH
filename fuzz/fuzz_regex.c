/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "regex.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2 || size > 256) return 0;

    size_t split = data[0] % (size - 1) + 1;
    char *pattern = malloc(split + 1);
    char *text = malloc(size - split + 1);
    if (!pattern || !text) {
        free(pattern);
        free(text);
        return 0;
    }

    memcpy(pattern, data + 1, split - 1);
    pattern[split - 1] = '\0';
    memcpy(text, data + split, size - split);
    text[size - split] = '\0';

    RegexMatch match;
    regex_search(pattern, text, &match);

    StrBuf replaced;
    sb_init(&replaced);
    regex_replace(pattern, "<&>", text, 1, &replaced);
    sb_free(&replaced);

    StrBuf converted;
    sb_init(&converted);
    regex_bre_to_ere(pattern, &converted);
    sb_free(&converted);

    free(pattern);
    free(text);
    return 0;
}
