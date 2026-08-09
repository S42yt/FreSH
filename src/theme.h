/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_THEME_H
#define FRESH_THEME_H

#include "builtins.h"

void fresh_home_init(void);
void theme_load_configured(void);
void plugins_load_configured(void);

int builtin_theme(int argc, char **argv);
int builtin_plugin(int argc, char **argv);

#endif
