/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "prompt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "gitinfo.h"
#include "shell.h"
#include "term.h"
#include "vars.h"

#define COLOR_RESET "\x1b[0m"
#define COLOR_USER "\x1b[1;32m"
#define COLOR_PATH "\x1b[1;36m"
#define COLOR_GIT "\x1b[1;32m"
#define COLOR_BRANCH "\x1b[35m"
#define COLOR_DIRTY "\x1b[31m"
#define COLOR_PROMPT "\x1b[97m"
#define COLOR_ERROR "\x1b[31m"

int display_width(const char *text) {
    int width = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == 0x1b) {
            p++;
            if (*p == '[' || *p == ']') {
                p++;
                while (*p && (*p < 0x40 || *p > 0x7e)) p++;
            }
            if (!*p) break;
            continue;
        }
        if ((*p & 0xC0) == 0x80) continue;
        width++;
    }
    return width;
}

static int option_enabled(const char *name, int fallback) {
    const char *value = var_get(name);
    if (!value || !*value) return fallback;
    return !(strcmp(value, "0") == 0 || str_ieq(value, "false") || str_ieq(value, "off"));
}

static void short_path(StrBuf *out) {
    char cwd[PATH_BUF];
    if (!GetCurrentDirectoryA(sizeof(cwd), cwd)) {
        sb_puts(out, "?");
        return;
    }
    path_to_slashes(cwd);

    char home[PATH_BUF];
    snprintf(home, sizeof(home), "%s", home_dir());
    path_to_slashes(home);

    char display[PATH_BUF];
    size_t home_len = strlen(home);
    if (home_len > 1 && _strnicmp(cwd, home, home_len) == 0 &&
        (cwd[home_len] == '\0' || cwd[home_len] == '/')) {
        snprintf(display, sizeof(display), "~%s", cwd + home_len);
    } else {
        snprintf(display, sizeof(display), "%s", cwd);
    }

    const char *depth_value = var_get("FRESH_PATH_DEPTH");
    int depth = depth_value ? atoi(depth_value) : 3;
    if (depth <= 0) depth = 3;

    const char *start = display;
    int seen = 0;
    for (const char *p = display + strlen(display) - 1; p > display; p--) {
        if (*p != '/') continue;
        if (++seen == depth) {
            start = p + 1;
            break;
        }
    }
    sb_puts(out, start);
}

static void build_status(StrBuf *out) {
    if (!option_enabled("FRESH_SHOW_RPROMPT", 1) || shell.last_status == 0) return;
    sb_printf(out, COLOR_ERROR "%d \xe2\x86\xb5" COLOR_RESET, shell.last_status);
}

void prompt_build(StrBuf *out) {
    StrBuf left;
    sb_init(&left);

    if (option_enabled("FRESH_SHOW_USER", 1)) {
        char user[256];
        DWORD size = sizeof(user);
        if (GetUserNameA(user, &size)) {
            sb_puts(&left, COLOR_USER);
            sb_puts(&left, user);
            sb_puts(&left, COLOR_RESET " ");
        }
    }

    sb_puts(&left, COLOR_PATH);
    short_path(&left);
    sb_puts(&left, COLOR_RESET);

    if (option_enabled("FRESH_SHOW_GIT", 1)) {
        const char *branch = git_branch();
        if (branch) {
            sb_puts(&left, " " COLOR_GIT "(" COLOR_BRANCH);
            sb_puts(&left, branch);
            if (option_enabled("FRESH_SHOW_GIT_DIRTY", 1) && git_dirty())
                sb_puts(&left, COLOR_DIRTY "!");
            sb_puts(&left, COLOR_GIT ")" COLOR_RESET);
        }
    }

    StrBuf right;
    sb_init(&right);
    build_status(&right);

    sb_puts(out, left.data);
    int padding = term_width() - display_width(left.data) - display_width(right.data) - 1;
    if (right.len > 0 && padding > 0) {
        for (int i = 0; i < padding; i++) sb_putc(out, ' ');
        sb_puts(out, right.data);
    }
    sb_free(&left);
    sb_free(&right);

    const char *character = var_get("FRESH_PROMPT_CHAR");
    if (!character || !*character) character = "\xce\xbb";
    sb_puts(out, "\r\n" COLOR_PROMPT);
    sb_puts(out, character);
    sb_puts(out, COLOR_RESET " ");
}

void prompt_build_continuation(StrBuf *out) {
    sb_puts(out, COLOR_GIT "\xe2\x80\xa6" COLOR_RESET " ");
}
