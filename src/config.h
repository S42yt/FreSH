/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_CONFIG_H
#define FRESH_CONFIG_H

#include "util.h"

int config_check(StrList *problems);
void config_report(int found_problems, const StrList *problems);

#endif
