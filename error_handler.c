/*
 * Copyright (c) 2025 Musa
 * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#include "error_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

void error_handler_init() {

}

void show_error(int error_code, const char *additional_info) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);


    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);

    switch (error_code) {
    case ERROR_COMMAND_NOT_FOUND_VAL:
        if (additional_info && strlen(additional_info) > 0) {
            printf("FreSH: %s: command not found\n", additional_info);
        } else {
            printf("FreSH: command not found\n");
        }
        break;
    case ERROR_DIRECTORY_NOT_FOUND_VAL:
        printf("FreSH: cd: %s: No such file or directory\n", additional_info ? additional_info : "unknown");
        break;
    case ERROR_FILE_NOT_FOUND_VAL:
        printf("FreSH: %s: No such file or directory\n", additional_info ? additional_info : "unknown");
        break;
    case ERROR_PERMISSION_DENIED_VAL:
        printf("FreSH: %s: Permission denied\n", additional_info ? additional_info : "unknown");
        break;
    case ERROR_INVALID_ARGUMENT_VAL:
        printf("FreSH: %s\n", additional_info ? additional_info : "Invalid argument");
        break;
    case ERROR_PROCESS_CREATION_FAILED_VAL:
        printf("FreSH: %s\n", additional_info ? additional_info : "Failed to execute command");
        break;
    case ERROR_PIPE_CREATION_FAILED_VAL:
        printf("FreSH: Failed to create pipe for %s\n", additional_info ? additional_info : "command");
        break;
    case ERROR_GENERAL_VAL:
        printf("FreSH: %s\n", additional_info ? additional_info : "An error occurred");
        break;
    default:
        printf("FreSH: Unknown error occurred");
        if (additional_info && strlen(additional_info) > 0) {
            printf(": %s", additional_info);
        }
        printf("\n");
        break;
    }


    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    fflush(stdout);
}

void show_error_message(const char *message) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);


    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);

    printf("FreSH: %s\n", message ? message : "An error occurred");


    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    fflush(stdout);
}

void error_handler_cleanup() {

}
