/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "prompt.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#include "gitinfo.h"
#include "shell.h"
#include "style.h"
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

typedef struct {
    const char *name;
    int code;
} ColorName;

static const ColorName COLORS[] = {
    {"black", 30},   {"red", 31},     {"green", 32},   {"yellow", 33},
    {"blue", 34},    {"magenta", 35}, {"cyan", 36},    {"white", 37},
    {"grey", 90},    {"gray", 90},    {"brightred", 91}, {"brightgreen", 92},
    {"brightyellow", 93}, {"brightblue", 94}, {"brightmagenta", 95}, {"brightcyan", 96},
    {"brightwhite", 97},
};

static void append_color(StrBuf *out, const char *spec, int background) {
    if (!style_enabled()) return;

    for (size_t i = 0; i < sizeof(COLORS) / sizeof(COLORS[0]); i++) {
        if (str_ieq(spec, COLORS[i].name)) {
            sb_printf(out, "\x1b[%dm", COLORS[i].code + (background ? 10 : 0));
            return;
        }
    }
    if (isdigit((unsigned char)spec[0]))
        sb_printf(out, "\x1b[%d;5;%dm", background ? 48 : 38, atoi(spec));
}

static const char *read_brace(const char *p, char *name, size_t size) {
    size_t index = 0;
    if (*p != '{') {
        name[0] = '\0';
        return p;
    }
    p++;
    while (*p && *p != '}' && index + 1 < size) name[index++] = *p++;
    name[index] = '\0';
    if (*p == '}') p++;
    return p;
}

static void append_git_segment(StrBuf *out) {
    if (!option_enabled("FRESH_SHOW_GIT", 1)) return;
    const char *branch = git_branch();
    if (!branch) return;

    sb_puts(out, style(COLOR_GIT));
    sb_puts(out, " (");
    sb_puts(out, style(COLOR_BRANCH));
    sb_puts(out, branch);
    if (option_enabled("FRESH_SHOW_GIT_DIRTY", 1) && git_dirty()) {
        sb_puts(out, style(COLOR_DIRTY));
        sb_puts(out, "!");
    }
    sb_puts(out, style(COLOR_GIT));
    sb_puts(out, ")");
    sb_puts(out, style(COLOR_RESET));
}

void prompt_expand(const char *format, StrBuf *out) {
    char name[64];

    for (const char *p = format; *p; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') {
                sb_puts(out, "\r\n");
            } else if (*p == 't') {
                sb_putc(out, '\t');
            } else if (*p == 'e') {
                sb_putc(out, '\x1b');
            } else if (*p == 'u' && isxdigit((unsigned char)p[1])) {
                unsigned int code = 0;
                int digits = 0;
                while (digits < 4 && isxdigit((unsigned char)p[1])) {
                    char digit = *++p;
                    code = code * 16 +
                           (unsigned int)(isdigit((unsigned char)digit)
                                              ? digit - '0'
                                              : tolower((unsigned char)digit) - 'a' + 10);
                    digits++;
                }
                if (code < 0x80) {
                    sb_putc(out, (char)code);
                } else if (code < 0x800) {
                    sb_putc(out, (char)(0xc0 | (code >> 6)));
                    sb_putc(out, (char)(0x80 | (code & 0x3f)));
                } else {
                    sb_putc(out, (char)(0xe0 | (code >> 12)));
                    sb_putc(out, (char)(0x80 | ((code >> 6) & 0x3f)));
                    sb_putc(out, (char)(0x80 | (code & 0x3f)));
                }
            } else {
                sb_putc(out, *p);
            }
            continue;
        }
        if (*p != '%') {
            sb_putc(out, *p);
            continue;
        }

        p++;
        switch (*p) {
        case 'n': {
            char user[256];
            DWORD size = sizeof(user);
            if (GetUserNameA(user, &size)) sb_puts(out, user);
            break;
        }
        case 'm': {
            char host[256];
            DWORD size = sizeof(host);
            if (GetComputerNameA(host, &size)) sb_puts(out, host);
            break;
        }
        case '~':
            short_path(out);
            break;
        case 'd': {
            char cwd[PATH_BUF];
            if (GetCurrentDirectoryA(sizeof(cwd), cwd)) {
                path_to_slashes(cwd);
                sb_puts(out, cwd);
            }
            break;
        }
        case 'g':
            append_git_segment(out);
            break;
        case 'b': {
            const char *branch = git_branch();
            if (branch) sb_puts(out, branch);
            break;
        }
        case 't':
        case 'D': {
            time_t now = time(NULL);
            struct tm *local = localtime(&now);
            char buffer[64];
            strftime(buffer, sizeof(buffer), *p == 't' ? "%H:%M" : "%Y-%m-%d", local);
            sb_puts(out, buffer);
            break;
        }
        case '?':
            sb_printf(out, "%d", shell.last_status);
            break;
        case 'e':
            if (shell.last_status != 0) {
                sb_puts(out, style(COLOR_ERROR));
                sb_printf(out, "%d \xe2\x86\xb5", shell.last_status);
                sb_puts(out, style(COLOR_RESET));
            }
            break;
        case '#': {
            const char *character = var_get("FRESH_PROMPT_CHAR");
            sb_puts(out, character && *character ? character : "\xce\xbb");
            break;
        }
        case 'F':
            p = read_brace(p + 1, name, sizeof(name)) - 1;
            append_color(out, name, 0);
            break;
        case 'K':
            p = read_brace(p + 1, name, sizeof(name)) - 1;
            append_color(out, name, 1);
            break;
        case 'f':
            sb_puts(out, style("\x1b[39m"));
            break;
        case 'k':
            sb_puts(out, style("\x1b[49m"));
            break;
        case 'S':
            sb_puts(out, style("\x1b[1m"));
            break;
        case 's':
            sb_puts(out, style(COLOR_RESET));
            break;
        case '%':
            sb_putc(out, '%');
            break;
        case '\0':
            return;
        default:
            sb_putc(out, *p);
            break;
        }
    }
}

static void build_status(StrBuf *out) {
    if (!option_enabled("FRESH_SHOW_RPROMPT", 1) || shell.last_status == 0) return;
    sb_printf(out, COLOR_ERROR "%d \xe2\x86\xb5" COLOR_RESET, shell.last_status);
}

static const char *default_prompt(void) {
    if (option_enabled("FRESH_SHOW_USER", 1))
        return "%F{green}%n%f %F{cyan}%~%f%g\\n%F{brightwhite}%#%f ";
    return "%F{cyan}%~%f%g\\n%F{brightwhite}%#%f ";
}

void prompt_build(StrBuf *out) {
    const char *format = var_get("FRESH_PROMPT");
    if (!format || !*format) format = default_prompt();

    StrBuf body;
    sb_init(&body);
    prompt_expand(format, &body);

    StrBuf right;
    sb_init(&right);
    const char *right_format = var_get("FRESH_RPROMPT");
    if (right_format && *right_format) prompt_expand(right_format, &right);
    else build_status(&right);

    char *first_break = strstr(body.data, "\r\n");
    if (right.len == 0) {
        sb_puts(out, body.data);
    } else {
        size_t head_length = first_break ? (size_t)(first_break - body.data) : body.len;
        char *head = xstrndup(body.data, head_length);
        int padding = term_width() - display_width(head) - display_width(right.data) - 1;

        sb_puts(out, head);
        if (padding > 0) {
            for (int i = 0; i < padding; i++) sb_putc(out, ' ');
            sb_puts(out, right.data);
        }
        if (first_break) sb_puts(out, first_break);
        free(head);
    }

    sb_free(&body);
    sb_free(&right);
}

void prompt_set_title(void) {
    if (!option_enabled("FRESH_TITLE", 1)) return;

    StrBuf path;
    sb_init(&path);
    short_path(&path);

    StrBuf title;
    sb_init(&title);
    sb_printf(&title, "FreSH  %s", path.data);
    term_set_title(title.data);

    sb_free(&path);
    sb_free(&title);
}

void prompt_build_continuation(StrBuf *out) {
    const char *format = var_get("FRESH_PROMPT2");
    if (format && *format) {
        prompt_expand(format, out);
        return;
    }
    sb_puts(out, style(COLOR_GIT));
    sb_puts(out, "\xe2\x80\xa6");
    sb_puts(out, style(COLOR_RESET));
    sb_puts(out, " ");
}
