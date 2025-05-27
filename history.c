/*
 * Copyright (c) 2025 Musa/S42
 * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#include "history.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

#ifndef MAX_PATH_SIZE
#define MAX_PATH_SIZE 260
#endif

static char history[HISTORY_MAX][MAX_CMD];
static int history_size = 0;

void history_add(const char *cmd) {
    if (!cmd || strlen(cmd) == 0) {
        return;
    }

    for (int i = 0; i < history_size; i++) {
        if (strcmp(history[i], cmd) == 0) {
            return;
        }
    }

    if (history_size < HISTORY_MAX) {
        strcpy(history[history_size++], cmd);
    } else {
        for (int i = 0; i < HISTORY_MAX - 1; i++) {
            strcpy(history[i], history[i + 1]);
        }
        strcpy(history[HISTORY_MAX - 1], cmd);
    }
}

const char *history_get(int index) {
    if (index < 0 || index >= history_size) {
        return NULL;
    }

    return history[index];
}

int history_count() {
    return history_size;
}

void history_load() {
    char path[MAX_PATH_SIZE];
    char *home = getenv("USERPROFILE");

    if (!home) {
        return;
    }

    snprintf(path, MAX_PATH_SIZE, "%s\\.fresh_history", home);

    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }

    char line[MAX_CMD];
    while (fgets(line, MAX_CMD, file) && history_size < HISTORY_MAX) {
        line[strcspn(line, "\r\n")] = 0;
        if (strlen(line) > 0) {
            strcpy(history[history_size++], line);
        }
    }

    fclose(file);
}

void history_save() {
    char path[MAX_PATH_SIZE];
    char *home = getenv("USERPROFILE");

    if (!home) {
        return;
    }

    snprintf(path, MAX_PATH_SIZE, "%s\\.FreSH_history", home);

    FILE *file = fopen(path, "w");
    if (!file) {
        return;
    }

    for (int i = 0; i < history_size; i++) {
        fprintf(file, "%s\n", history[i]);
    }

    fclose(file);
}
