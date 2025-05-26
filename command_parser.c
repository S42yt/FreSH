/*
 * Copyright (c) 2025 Musa
  * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#include "command_parser.h"
#include "bin_register.h"
#include "error_handler.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define MAX_CMD_LEN 1024
#define MAX_ARGS 64

int file_exists(const char *path) {
    DWORD attributes = GetFileAttributesA(path);
    return (attributes != INVALID_FILE_ATTRIBUTES);
}

int find_in_user_bin(const char *command, char *command_path) {

    if (is_bin_command(command)) {
        const char *path = get_bin_command_path(command);
        if (path) {
            strcpy(command_path, path);
            return 1;
        }
    }


    char *home_dir = getenv("USERPROFILE");
    char bin_path[MAX_PATH];

    if (!home_dir) return 0;


    char *bin_dirs[] = {
        "\\bin",
        "\\.local\\bin",
        "\\AppData\\Local\\bin",
        "\\AppData\\Roaming\\npm",
        "\\scoop\\shims",
        "\\AppData\\Local\\Microsoft\\WindowsApps"
    };
    char *extensions[] = {".exe", ".bat", ".cmd", ".com", ".ps1", ".vbs", ".wsf", ""};


    for (int d = 0; d < 6; d++) {
        for (int e = 0; e < 8; e++) {
            snprintf(bin_path, MAX_PATH, "%s%s\\%s%s", home_dir, bin_dirs[d], command, extensions[e]);
            if (file_exists(bin_path)) {
                strcpy(command_path, bin_path);
                return 1;
            }
        }
    }


    char *system_bins[] = {"C:\\bin", "C:\\tools\\bin"};
    for (int d = 0; d < 2; d++) {
        for (int e = 0; e < 8; e++) {
            snprintf(bin_path, MAX_PATH, "%s\\%s%s", system_bins[d], command, extensions[e]);
            if (file_exists(bin_path)) {
                strcpy(command_path, bin_path);
                return 1;
            }
        }
    }

    return 0;
}

static void parse_arguments(const char *cmd, char *args[], int *argc) {
    char cmd_copy[MAX_CMD_LEN];
    strncpy(cmd_copy, cmd, MAX_CMD_LEN);
    cmd_copy[MAX_CMD_LEN - 1] = '\0';

    *argc = 0;
    char *token = strtok(cmd_copy, " \t");

    while (token != NULL && *argc < MAX_ARGS - 1) {
        args[(*argc)++] = token;
        token = strtok(NULL, " \t");
    }

    args[*argc] = NULL;
}

int execute_external_command(const char *cmd) {
    if (!cmd || strlen(cmd) == 0) {
        return 0;
    }

    char cmd_copy[MAX_CMD_LEN];
    strncpy(cmd_copy, cmd, MAX_CMD_LEN - 1);
    cmd_copy[MAX_CMD_LEN - 1] = '\0';

    char *args[MAX_ARGS];
    int argc = 0;

    parse_arguments(cmd_copy, args, &argc);

    if (argc == 0) {
        return 0;
    }

    char command_path[MAX_PATH];
    char *command_name = args[0];


    if (!command_name || strlen(command_name) == 0) {
        show_error(ERROR_COMMAND_NOT_FOUND_VAL, "empty command");
        return 1;
    }

    char *path_extensions[] = {"", ".exe", ".bat", ".cmd", ".com", ".ps1", ".vbs", ".wsf", NULL};
    int found = 0;


    if (find_in_user_bin(command_name, command_path)) {
        found = 1;
    } else if (strchr(command_name, '\\') || strchr(command_name, '/')) {

        strcpy(command_path, command_name);
        if (GetFileAttributesA(command_path) != INVALID_FILE_ATTRIBUTES) {
            found = 1;
        }
    } else {

        char *path_env = getenv("PATH");
        if (path_env) {
            char path_copy[32768];
            strncpy(path_copy, path_env, sizeof(path_copy) - 1);
            path_copy[sizeof(path_copy) - 1] = '\0';

            char *path_token = strtok(path_copy, ";");
            while (path_token != NULL && !found) {
                for (int i = 0; path_extensions[i] != NULL && !found; i++) {
                    snprintf(command_path, MAX_PATH, "%s\\%s%s",
                             path_token, command_name, path_extensions[i]);

                    if (GetFileAttributesA(command_path) != INVALID_FILE_ATTRIBUTES) {
                        found = 1;
                        break;
                    }
                }
                path_token = strtok(NULL, ";");
            }
        }


        if (!found) {
            for (int i = 0; path_extensions[i] != NULL && !found; i++) {
                snprintf(command_path, MAX_PATH, "%s%s", command_name, path_extensions[i]);
                if (GetFileAttributesA(command_path) != INVALID_FILE_ATTRIBUTES) {
                    found = 1;
                }
            }
        }
    }

    if (!found) {
        show_error(ERROR_COMMAND_NOT_FOUND_VAL, command_name);
        return 1;
    }


    char command_line[MAX_CMD_LEN];


    char *ext = strrchr(command_path, '.');
    if (ext && strcmp(ext, ".ps1") == 0) {
        snprintf(command_line, MAX_CMD_LEN, "powershell.exe -ExecutionPolicy Bypass -File \"%s\"", command_path);
    } else {
        snprintf(command_line, MAX_CMD_LEN, "\"%s\"", command_path);
    }


    for (int i = 1; i < argc; i++) {
        strcat(command_line, " ");

        if (strchr(args[i], ' ') != NULL) {
            strcat(command_line, "\"");
            strcat(command_line, args[i]);
            strcat(command_line, "\"");
        } else {
            strcat(command_line, args[i]);
        }
    }

    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};

    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    BOOL result = CreateProcessA(
                      NULL,
                      command_line,
                      NULL,
                      NULL,
                      TRUE,
                      0,
                      NULL,
                      NULL,
                      &si,
                      &pi
                  );

    if (!result) {
        DWORD error = GetLastError();

        switch (error) {
        case ERROR_FILE_NOT_FOUND:
            show_error(ERROR_COMMAND_NOT_FOUND_VAL, command_name);
            break;
        case ERROR_ACCESS_DENIED:
            show_error(ERROR_PERMISSION_DENIED_VAL, command_name);
            break;
        default: {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), "%s (Error code: %lu)", command_name, error);
            show_error(ERROR_PROCESS_CREATION_FAILED_VAL, error_msg);
        }
        break;
        }
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exit_code;
}

void parser_init() {
}

void parser_cleanup() {
}
