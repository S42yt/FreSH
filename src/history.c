/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "vars.h"

#define HISTORY_MAX 5000

static char *entries[HISTORY_MAX];
static int entry_count = 0;

static char *history_file(void) {
    const char *custom = var_get("HISTFILE");
    if (custom && *custom) return xstrdup(custom);
    return config_path(".fresh_history");
}

static int history_limit(void) {
    const char *size = var_get("HISTSIZE");
    int n = size ? atoi(size) : 0;
    if (n <= 0 || n > HISTORY_MAX) n = HISTORY_MAX;
    return n;
}

void history_clear(void) {
    for (int i = 0; i < entry_count; i++) free(entries[i]);
    entry_count = 0;
}

void history_add(const char *line) {
    if (!line || !*line) return;
    if (line[0] == ' ') return;
    if (entry_count > 0 && strcmp(entries[entry_count - 1], line) == 0) return;

    int limit = history_limit();
    if (entry_count >= limit) {
        int drop = entry_count - limit + 1;
        for (int i = 0; i < drop; i++) free(entries[i]);
        memmove(entries, entries + drop, (size_t)(entry_count - drop) * sizeof(char *));
        entry_count -= drop;
    }
    entries[entry_count++] = xstrdup(line);
}

int history_count(void) {
    return entry_count;
}

const char *history_get(int index) {
    if (index < 0 || index >= entry_count) return NULL;
    return entries[index];
}

int history_search_prefix(const char *prefix, int from, int direction) {
    size_t n = strlen(prefix);
    for (int i = from; i >= 0 && i < entry_count; i += direction) {
        if (strncmp(entries[i], prefix, n) == 0) return i;
    }
    return -1;
}

int history_search_substring(const char *needle, int from) {
    if (!*needle) return -1;
    for (int i = from; i >= 0 && i < entry_count; i--) {
        if (strstr(entries[i], needle)) return i;
    }
    return -1;
}

void history_load(void) {
    char *path = history_file();
    FILE *f = fopen(path, "r");
    free(path);
    if (!f) return;

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (*line) history_add(line);
    }
    fclose(f);
}

void history_save(void) {
    char *path = history_file();
    FILE *f = fopen(path, "w");
    free(path);
    if (!f) return;

    for (int i = 0; i < entry_count; i++) fprintf(f, "%s\n", entries[i]);
    fclose(f);
}
