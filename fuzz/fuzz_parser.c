/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 64 * 1024) return 0;

    char *text = malloc(size + 1);
    if (!text) return 0;
    memcpy(text, data, size);
    text[size] = '\0';

    int incomplete = 0;
    char *error = NULL;
    Node *node = parse_string(text, &incomplete, &error);

    node_free(node);
    free(error);
    free(text);
    return 0;
}
