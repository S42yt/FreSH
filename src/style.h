/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_STYLE_H
#define FRESH_STYLE_H

#define S_RESET "\x1b[0m"
#define S_DIM "\x1b[90m"
#define S_ACCENT "\x1b[1;32m"
#define S_HEADING "\x1b[1;97m"
#define S_LABEL "\x1b[36m"
#define S_VALUE "\x1b[97m"
#define S_WARN "\x1b[33m"
#define S_ERROR "\x1b[31m"

#define S_DIR "\x1b[1;34m"
#define S_EXEC "\x1b[1;32m"
#define S_ARCHIVE "\x1b[35m"
#define S_IMAGE "\x1b[36m"
#define S_MEDIA "\x1b[95m"
#define S_CODE "\x1b[33m"
#define S_CONFIG "\x1b[93m"
#define S_HIDDEN "\x1b[90m"

#define S_LAMBDA "\xce\xbb"

int style_enabled(void);
const char *style(const char *code);

#endif
