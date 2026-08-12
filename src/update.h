/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_UPDATE_H
#define FRESH_UPDATE_H

#include "builtins.h"

int builtin_fresh(int argc, char **argv);
int http_download(const char *url, const char *path);

#endif
