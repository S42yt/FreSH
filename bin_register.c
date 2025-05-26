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

#define MAX_BIN_COMMANDS 200
#define MAX_COMMAND_NAME 64

typedef struct {
    char name[MAX_COMMAND_NAME];
    char path[MAX_PATH];
} BinCommand;

static BinCommand bin_commands[MAX_BIN_COMMANDS];
static int bin_command_count = 0;

void register_bin_commands() {
    char *home_dir = getenv("USERPROFILE");
    if (!home_dir) {
        return;
    }


    char bin_paths[][MAX_PATH] = {
        "%s\\bin",
        "%s\\.local\\bin",
        "%s\\AppData\\Local\\bin",
        "%s\\AppData\\Roaming\\npm",
        "%s\\scoop\\shims",
        "%s\\AppData\\Local\\Microsoft\\WindowsApps",
        "C:\\tools\\bin",
        "C:\\bin"
    };

    bin_command_count = 0;

    for (int p = 0; p < 8; p++) {
        char search_path[MAX_PATH];
        snprintf(search_path, MAX_PATH, bin_paths[p], home_dir);


        DWORD attributes = GetFileAttributesA(search_path);
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
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
                int is_executable = 0;


                if (ext && (strcmp(ext, ".exe") == 0 || strcmp(ext, ".bat") == 0 ||
                            strcmp(ext, ".cmd") == 0 || strcmp(ext, ".com") == 0 ||
                            strcmp(ext, ".ps1") == 0 || strcmp(ext, ".vbs") == 0 ||
                            strcmp(ext, ".wsf") == 0 || strcmp(ext, ".msi") == 0 ||
                            strcmp(ext, ".sh") == 0)) {
                    is_executable = 1;
                } else if (!ext) {

                    is_executable = 1;
                }

                if (is_executable && bin_command_count < MAX_BIN_COMMANDS) {

                    strncpy(bin_commands[bin_command_count].name, findFileData.cFileName, MAX_COMMAND_NAME - 1);
                    bin_commands[bin_command_count].name[MAX_COMMAND_NAME - 1] = '\0';

                    char *dot = strrchr(bin_commands[bin_command_count].name, '.');
                    if (dot) *dot = '\0';

                    snprintf(bin_commands[bin_command_count].path, MAX_PATH, "%s\\%s",
                             search_path, findFileData.cFileName);

                    bin_command_count++;
                }
            }
        } while (FindNextFileA(hFind, &findFileData) != 0 && bin_command_count < MAX_BIN_COMMANDS);

        FindClose(hFind);
    }
}

int is_bin_command(const char *command) {
    if (!command) return 0;

    for (int i = 0; i < bin_command_count; i++) {
        if (strcmp(bin_commands[i].name, command) == 0) {
            return 1;
        }
    }
    return 0;
}

const char* get_bin_command_path(const char *command) {
    if (!command) return NULL;

    for (int i = 0; i < bin_command_count; i++) {
        if (strcmp(bin_commands[i].name, command) == 0) {
            return bin_commands[i].path;
        }
    }
    return NULL;
}

void list_bin_commands() {
    char *home_dir = getenv("USERPROFILE");

    printf("Bin Command Registration Status:\n");
    if (home_dir) {
        printf("Home directory: %s\n\n", home_dir);

        char bin_paths[][MAX_PATH] = {
            "%s\\bin",
            "%s\\.local\\bin",
            "%s\\AppData\\Local\\bin",
            "%s\\AppData\\Roaming\\npm",
            "%s\\scoop\\shims",
            "%s\\AppData\\Local\\Microsoft\\WindowsApps",
            "C:\\tools\\bin",
            "C:\\bin"
        };

        printf("Checked directories:\n");
        for (int p = 0; p < 8; p++) {
            char search_path[MAX_PATH];
            snprintf(search_path, MAX_PATH, bin_paths[p], home_dir);

            DWORD attributes = GetFileAttributesA(search_path);
            if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                printf("  %s - NOT FOUND\n", search_path);
            } else {
                printf("  %s - EXISTS\n", search_path);
            }
        }
        printf("\n");
    }

    printf("Registered bin commands (%d):\n", bin_command_count);
    if (bin_command_count == 0) {
        printf("  No bin commands found.\n");
        printf("  Create one of the following directories and place executable files there:\n");
        printf("  - %%USERPROFILE%%\\bin (recommended)\n");
        printf("  - %%USERPROFILE%%\\.local\\bin\n");
        printf("  - %%USERPROFILE%%\\AppData\\Local\\bin\n");
        printf("  - C:\\bin (system-wide)\n");
        printf("  - C:\\tools\\bin\n");
        printf("\n  Supported file types: .exe, .bat, .cmd, .com, .ps1, .vbs, .wsf, .msi, .sh\n");
        printf("\n  Example: Create %%USERPROFILE%%\\bin\\l.bat with content:\n");
        printf("    @echo off\n");
        printf("    dir /B %%*\n");
        printf("\n  Or create %%USERPROFILE%%\\bin\\ll.ps1 with content:\n");
        printf("    Get-ChildItem @args | Format-Table Name, Length, LastWriteTime\n");
        printf("\n  Or create %%USERPROFILE%%\\bin\\test.sh with content:\n");
        printf("    #!/bin/bash\n");
        printf("    echo \"Hello from shell script!\"\n");
    } else {
        for (int i = 0; i < bin_command_count; i++) {
            printf("  %s -> %s\n", bin_commands[i].name, bin_commands[i].path);
        }
    }
}