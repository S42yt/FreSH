/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "style.h"

#include <io.h>
#include <stdio.h>

int style_enabled(void) {
    return _isatty(_fileno(stdout));
}

const char *style(const char *code) {
    return style_enabled() ? code : "";
}
