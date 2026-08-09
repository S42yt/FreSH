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
#include "vars.h"

#define COLOR_RESET "\x1b[0m"
#define COLOR_PATH "\x1b[37m"
#define COLOR_GIT "\x1b[1;32m"
#define COLOR_BRANCH "\x1b[35m"
#define COLOR_DIRTY "\x1b[31m"
#define COLOR_PROMPT "\x1b[97m"
#define COLOR_ERROR "\x1b[31m"
#define COLOR_USER "\x1b[36m"

int display_width(const char *text) {
    int width = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == 0x1b) {
            while (*p && *p != 'm' && *p != 'G' && *p != 'K') p++;
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

    int slashes = 0;
    const char *start = display;
    for (const char *p = display + strlen(display); p > display; p--) {
        if (*p == '/') {
            slashes++;
            if (slashes == depth) {
                start = p + 1;
                break;
            }
        }
    }
    sb_puts(out, start);
}

void prompt_build(StrBuf *out, int *visible_width) {
    if (option_enabled("FRESH_SHOW_USER", 0)) {
        char user[256];
        DWORD size = sizeof(user);
        if (GetUserNameA(user, &size)) {
            sb_puts(out, COLOR_USER);
            sb_puts(out, user);
            sb_puts(out, COLOR_RESET " ");
        }
    }

    sb_puts(out, COLOR_PATH);
    short_path(out);
    sb_puts(out, COLOR_RESET);

    if (option_enabled("FRESH_SHOW_GIT", 1)) {
        const char *branch = git_branch();
        if (branch) {
            sb_puts(out, " " COLOR_GIT "(" COLOR_BRANCH);
            sb_puts(out, branch);
            if (option_enabled("FRESH_SHOW_GIT_DIRTY", 1) && git_dirty())
                sb_puts(out, COLOR_DIRTY "!");
            sb_puts(out, COLOR_GIT ")" COLOR_RESET);
        }
    }

    const char *character = var_get("FRESH_PROMPT_CHAR");
    if (!character || !*character) character = "\xce\xbb";
    sb_puts(out, " " COLOR_PROMPT);
    sb_puts(out, character);
    sb_puts(out, COLOR_RESET " ");

    if (visible_width) *visible_width = display_width(out->data);
}

void prompt_build_right(StrBuf *out, int *visible_width) {
    if (!option_enabled("FRESH_SHOW_RPROMPT", 1) || shell.last_status == 0) {
        if (visible_width) *visible_width = 0;
        return;
    }
    sb_printf(out, COLOR_ERROR "%d \xe2\x86\xb5" COLOR_RESET, shell.last_status);
    if (visible_width) *visible_width = display_width(out->data);
}

void prompt_build_continuation(StrBuf *out, int *visible_width) {
    sb_puts(out, COLOR_GIT "\xe2\x80\xa6 " COLOR_RESET);
    if (visible_width) *visible_width = display_width(out->data);
}
