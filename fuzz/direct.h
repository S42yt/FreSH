/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_FUZZ_DIRECT_H
#define FRESH_FUZZ_DIRECT_H

#include <sys/stat.h>
#include <sys/types.h>

static inline int _mkdir(const char *path) {
    return mkdir(path, 0777);
}

#endif
