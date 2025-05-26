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
    return (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY));
}

int find_in_user_bin(const char *command, char *command_path) {
    if (!command || strlen(command) == 0) return 0;


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
    char *extensions[] = {".exe", ".bat", ".cmd", ".com", ".ps1", ".vbs", ".wsf", ".sh", ""};


    for (int d = 0; d < 6; d++) {
        for (int e = 0; e < 9; e++) {
            snprintf(bin_path, MAX_PATH, "%s%s\\%s%s", home_dir, bin_dirs[d], command, extensions[e]);
            if (file_exists(bin_path)) {
                strcpy(command_path, bin_path);
                return 1;
            }
        }
    }


    char *system_bins[] = {"C:\\bin", "C:\\tools\\bin"};
    for (int d = 0; d < 2; d++) {
        for (int e = 0; e < 9; e++) {
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
    if (!cmd || strlen(cmd) == 0) {
        *argc = 0;
        return;
    }

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


    char cmd_trimmed[MAX_CMD_LEN];
    strncpy(cmd_trimmed, cmd, MAX_CMD_LEN - 1);
    cmd_trimmed[MAX_CMD_LEN - 1] = '\0';

    char *start = cmd_trimmed;
    while (*start && isspace(*start)) start++;

    if (strlen(start) == 0) {
        return 0;
    }

    char *args[MAX_ARGS];
    int argc = 0;

    parse_arguments(start, args, &argc);

    if (argc == 0 || !args[0] || strlen(args[0]) == 0) {
        return 0;
    }

    char command_path[MAX_PATH];
    char *command_name = args[0];

    char *path_extensions[] = {"", ".exe", ".bat", ".cmd", ".com", ".ps1", ".vbs", ".wsf", ".sh", NULL};
    int found = 0;


    if (strchr(command_name, '\\') || strchr(command_name, '/') ||
            strncmp(command_name, "./", 2) == 0 || strncmp(command_name, ".\\", 2) == 0) {


        if (strncmp(command_name, "./", 2) == 0) {

            char relative_path[MAX_PATH];
            strcpy(relative_path, command_name + 2);


            for (int i = 0; path_extensions[i] != NULL && !found; i++) {
                snprintf(command_path, MAX_PATH, "%s%s", relative_path, path_extensions[i]);
                if (file_exists(command_path)) {
                    found = 1;
                    break;
                }
            }
        } else if (strncmp(command_name, ".\\", 2) == 0) {

            char relative_path[MAX_PATH];
            strcpy(relative_path, command_name + 2);


            for (int i = 0; path_extensions[i] != NULL && !found; i++) {
                snprintf(command_path, MAX_PATH, "%s%s", relative_path, path_extensions[i]);
                if (file_exists(command_path)) {
                    found = 1;
                    break;
                }
            }
        } else {

            strcpy(command_path, command_name);
            if (file_exists(command_path)) {
                found = 1;
            } else {

                for (int i = 0; path_extensions[i] != NULL && !found; i++) {
                    if (strlen(path_extensions[i]) > 0) {
                        snprintf(command_path, MAX_PATH, "%s%s", command_name, path_extensions[i]);
                        if (file_exists(command_path)) {
                            found = 1;
                            break;
                        }
                    }
                }
            }
        }
    } else {

        if (find_in_user_bin(command_name, command_path)) {
            found = 1;
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

                        if (file_exists(command_path)) {
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
                    if (file_exists(command_path)) {
                        found = 1;
                    }
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
    } else if (ext && strcmp(ext, ".sh") == 0) {


        char bash_test[MAX_PATH];
        snprintf(bash_test, MAX_PATH, "bash.exe");


        char *path_env = getenv("PATH");
        int bash_found = 0;
        if (path_env) {
            char path_copy[32768];
            strncpy(path_copy, path_env, sizeof(path_copy) - 1);
            path_copy[sizeof(path_copy) - 1] = '\0';

            char *path_token = strtok(path_copy, ";");
            while (path_token != NULL && !bash_found) {
                char bash_path[MAX_PATH];
                snprintf(bash_path, MAX_PATH, "%s\\bash.exe", path_token);
                if (file_exists(bash_path)) {
                    bash_found = 1;
                    break;
                }
                path_token = strtok(NULL, ";");
            }
        }

        if (bash_found) {
            snprintf(command_line, MAX_CMD_LEN, "bash.exe \"%s\"", command_path);
        } else {

            snprintf(command_line, MAX_CMD_LEN, "sh.exe \"%s\"", command_path);
        }
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

            if (ext && strcmp(ext, ".sh") == 0) {
                show_error_message("bash or sh interpreter not found. Install Git Bash, WSL, or MSYS2 to run shell scripts.");
            } else {
                show_error(ERROR_COMMAND_NOT_FOUND_VAL, command_name);
            }
            break;
        case ERROR_ACCESS_DENIED:
            show_error(ERROR_PERMISSION_DENIED_VAL, command_name);
            break;
        case ERROR_BAD_EXE_FORMAT: {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), "%s is not a valid executable", command_name);
            show_error(ERROR_INVALID_ARGUMENT_VAL, error_msg);
        }
        break;
        default: {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), "Failed to execute %s (Error: %lu)", command_name, error);
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
