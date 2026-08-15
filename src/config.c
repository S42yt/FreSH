/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "shell.h"
#include "style.h"
#include "vars.h"

static char *read_whole_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) {
        fclose(f);
        return NULL;
    }

    char *text = xmalloc((size_t)size + 1);
    size_t read = fread(text, 1, (size_t)size, f);
    text[read] = '\0';
    fclose(f);
    return text;
}

static void check_syntax(const char *path, StrList *problems) {
    char *text = read_whole_file(path);
    if (!text) return;

    int incomplete = 0;
    char *error = NULL;
    Node *node = parse_string(text, &incomplete, &error);
    node_free(node);
    free(text);

    if (error) {
        char *newline = strchr(error, '\n');
        if (newline) *newline = '\0';

        const char *leaf = strrchr(path, '\\');
        const char *slash = strrchr(path, '/');
        if (slash > leaf) leaf = slash;

        StrBuf problem;
        sb_init(&problem);
        sb_printf(&problem, "%s: %s", leaf ? leaf + 1 : path, error);
        sl_push(problems, sb_take(&problem));
    }
    free(error);
}

static void check_bundle(const char *setting, const char *kind, const char *extension,
                         StrList *problems) {
    const char *value = var_get(setting);
    if (!value || !*value || strcmp(value, "none") == 0) return;

    char *home = config_path(".fresh");
    char *copy = xstrdup(value);
    char *cursor = copy;
    char *name;

    while ((name = str_next_field(&cursor, ' ')) != NULL) {
        if (!*name) continue;

        char file[PATH_BUF];
        snprintf(file, sizeof(file), "%s\\%s\\%s%s", home, kind, name, extension);
        if (path_is_file(file)) continue;

        StrBuf problem;
        sb_init(&problem);
        sb_printf(&problem, "%s names %s, which is not in ~/.fresh/%s", setting, name, kind);
        sl_push(problems, sb_take(&problem));
    }
    free(copy);
    free(home);
}

int config_check(StrList *problems) {
    char *rc = config_path(".freshrc");
    check_syntax(rc, problems);
    free(rc);

    check_bundle("FRESH_THEME", "themes", ".theme", problems);
    check_bundle("FRESH_PLUGINS", "plugins", ".plugin", problems);
    return (int)problems->len;
}

void config_report(int found_problems, const StrList *problems) {
    const char *art[5] = {
        "     __", "    /\\ \\", "   /  \\ \\", "  / /\\ \\ \\", " /_/  \\_\\_\\",
    };

    char *rc = config_path(".freshrc");
    path_to_slashes(rc);

    const char *tone = found_problems ? S_ERROR : S_ACCENT;
    printf("\n");
    for (int i = 0; i < 5; i++) {
        printf("  %s%-12s%s   ", style(tone), art[i], style(S_RESET));

        if (i == 0) {
            printf("%sFreSH%s %s%s%s\n", style(S_HEADING), style(S_RESET), style(S_DIM),
                   FRESH_VERSION, style(S_RESET));
        } else if (i == 1) {
            printf("%s%s%s\n", style(S_DIM), rc, style(S_RESET));
        } else if (i == 3 && !found_problems) {
            printf("%s\xe2\x9c\x93%s  the configuration is valid\n", style(S_ACCENT),
                   style(S_RESET));
        } else if (i == 3) {
            printf("%s\xe2\x9c\x97%s  %d problem%s in the configuration\n", style(S_ERROR),
                   style(S_RESET), found_problems, found_problems == 1 ? "" : "s");
        } else {
            printf("\n");
        }
    }

    for (size_t i = 0; i < problems->len; i++)
        printf("  %s%s%s\n", style(S_WARN), problems->items[i], style(S_RESET));

    if (found_problems)
        printf("\n  %sedit %s, then run fresh doctor again%s\n", style(S_DIM), rc, style(S_RESET));
    printf("\n");
    free(rc);
}
