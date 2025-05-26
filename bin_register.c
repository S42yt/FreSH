/*
 * Copyright (c) 2025 Musa
 * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#include "bin_register.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <io.h>

#define MAX_BIN_COMMANDS 100
#define MAX_COMMAND_NAME 64

typedef struct {
    char name[MAX_COMMAND_NAME];
    char path[MAX_PATH];
} BinCommand;

static BinCommand bin_commands[MAX_BIN_COMMANDS];
static int bin_command_count = 0;

void register_bin_commands() {
    char *home_dir = getenv("USERPROFILE");
    if (!home_dir) return;

    char bin_paths[][MAX_PATH] = {
        "%s\\bin",
        "%s\\.local\\bin",
        "%s\\AppData\\Local\\bin"
    };

    bin_command_count = 0;

    for (int p = 0; p < 3; p++) {
        char search_path[MAX_PATH];
        snprintf(search_path, MAX_PATH, bin_paths[p], home_dir);


        if (_access(search_path, 0) != 0) {
            continue;
        }

        WIN32_FIND_DATAA findFileData;
        HANDLE hFind;
        char searchPattern[MAX_PATH];
        snprintf(searchPattern, MAX_PATH, "%s\\*", search_path);

        hFind = FindFirstFileA(searchPattern, &findFileData);
        if (hFind == INVALID_HANDLE_VALUE) {
            continue;
        }

        do {
            if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char *ext = strrchr(findFileData.cFileName, '.');
                if (ext && (strcmp(ext, ".exe") == 0 || strcmp(ext, ".bat") == 0 ||
                            strcmp(ext, ".cmd") == 0 || strcmp(ext, ".com") == 0)) {

                    if (bin_command_count < MAX_BIN_COMMANDS) {

                        strncpy(bin_commands[bin_command_count].name, findFileData.cFileName, MAX_COMMAND_NAME - 1);
                        bin_commands[bin_command_count].name[MAX_COMMAND_NAME - 1] = '\0';

                        char *dot = strrchr(bin_commands[bin_command_count].name, '.');
                        if (dot) *dot = '\0';

                        snprintf(bin_commands[bin_command_count].path, MAX_PATH, "%s\\%s",
                                 search_path, findFileData.cFileName);
                        bin_command_count++;
                    }
                }
            }
        } while (FindNextFileA(hFind, &findFileData) != 0 && bin_command_count < MAX_BIN_COMMANDS);

        FindClose(hFind);
    }


    if (bin_command_count > 0) {
        printf("Registered %d bin commands\n", bin_command_count);
    }
}

int is_bin_command(const char *command) {
    for (int i = 0; i < bin_command_count; i++) {
        if (strcmp(bin_commands[i].name, command) == 0) {
            return 1;
        }
    }
    return 0;
}

const char* get_bin_command_path(const char *command) {
    for (int i = 0; i < bin_command_count; i++) {
        if (strcmp(bin_commands[i].name, command) == 0) {
            return bin_commands[i].path;
        }
    }
    return NULL;
}

void list_bin_commands() {
    printf("Registered bin commands (%d):\n", bin_command_count);
    if (bin_command_count == 0) {
        printf("  No bin commands found. Create ~/bin, ~/.local/bin, or ~/AppData/Local/bin directories\n");
        printf("  and place executable files there to register them.\n");
    } else {
        for (int i = 0; i < bin_command_count; i++) {
            printf("  %s -> %s\n", bin_commands[i].name, bin_commands[i].path);
        }
    }
}