/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "style.h"

#include <stdio.h>
#include <string.h>

#include "platform.h"

#include "util.h"
#include "vars.h"

int option_enabled(const char *name, int fallback) {
    const char *value = var_get(name);
    if (!value || !*value) return fallback;
    return !(strcmp(value, "0") == 0 || str_ieq(value, "false") || str_ieq(value, "off") ||
             str_ieq(value, "no"));
}

int style_enabled(void) {
    return _isatty(_fileno(stdout)) && option_enabled("FRESH_COLOR", 1);
}

const char *style(const char *code) {
    return style_enabled() ? code : "";
}
