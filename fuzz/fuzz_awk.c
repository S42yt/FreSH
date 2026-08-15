/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "awk.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void quieten(void) {
    static int done = 0;
    if (done) return;
    done = 1;
    if (!freopen("/dev/null", "w", stdout)) return;
    if (!freopen("/dev/null", "r", stdin)) return;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size > 8 * 1024) return 0;
    quieten();

    char *program = malloc(size + 1);
    if (!program) return 0;
    memcpy(program, data, size);
    program[size] = '\0';

    char name[] = "awk";
    char *argv[] = {name, program, NULL};
    awk_main(2, argv);

    free(program);
    return 0;
}
