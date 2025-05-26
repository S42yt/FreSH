/*
 * Copyright (c) 2025 Musa
 * MIT License - See LICENSE file for details
 */

#include "error_handler.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define COLOR_RESET 7
#define COLOR_ERROR 12
#define COLOR_WARNING 14
#define COLOR_INFO 11
#define COLOR_HIGHLIGHT_CUSTOM 15
#define COLOR_SUGGESTION 10

static HANDLE hConsole = NULL;

void error_handler_init() {
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
}

void set_console_color(int color) {
    if (hConsole) {
        SetConsoleTextAttribute(hConsole, color);
    }
}

void reset_console_color() {
    if (hConsole) {
        SetConsoleTextAttribute(hConsole, COLOR_RESET);
    }
}

void show_suggestion(ErrorCode code, const char *details) {
    set_console_color(COLOR_SUGGESTION);
    printf("│ ");

    switch (code) {
        case ERROR_COMMAND_NOT_FOUND_VAL:
            if (details && strlen(details) > 0) {
                // Check for common typos
                if (strcmp(details, "gt") == 0 || strcmp(details, "gi") == 0) {
                    printf("Did you mean 'git'?");
                } else if (strcmp(details, "cd..") == 0) {
                    printf("Did you mean 'cd ..'?");
                } else if (strcmp(details, "makedir") == 0) {
                    printf("Did you mean 'mkdir'?");
                } else if (strcmp(details, "cls") == 0) {
                    printf("Did you mean 'clear'?");
                } else {
                    printf("Check if the command is installed or in your PATH");
                }
            }
            break;

        case ERROR_PERMISSION_DENIED_VAL:
            printf("Try running with administrator privileges");
            break;

        case ERROR_DIRECTORY_NOT_FOUND_VAL:
            printf("Check if the directory name is correct");
            break;

        case ERROR_FILE_NOT_FOUND_VAL:
            printf("Check if the file name is correct");
            break;

        case ERROR_INVALID_ARGUMENT_VAL:
            printf("Check command syntax: missing or incorrect argument");
            break;

        default:
            break;
    }

    set_console_color(COLOR_ERROR);
    printf(" │\n");
}

void show_error(ErrorCode code, const char *details) {
    set_console_color(COLOR_ERROR);
    printf("\n╭───────────────────────────────────────────────────────╮\n");
    printf("│ ");
    set_console_color(COLOR_HIGHLIGHT_CUSTOM);
    printf("ERROR");
    set_console_color(COLOR_ERROR);
    printf(": ");

    switch (code) {
        case ERROR_COMMAND_NOT_FOUND_VAL:
            printf("Command not found");
            break;
        case ERROR_PERMISSION_DENIED_VAL:
            printf("Permission denied");
            break;
        case ERROR_DIRECTORY_NOT_FOUND_VAL:
            printf("Directory not found");
            break;
        case ERROR_FILE_NOT_FOUND_VAL:
            printf("File not found");
            break;
        case ERROR_PIPE_CREATION_FAILED_VAL:
            printf("Pipe creation failed");
            break;
        case ERROR_PROCESS_CREATION_FAILED_VAL:
            if (strstr(details, "2") != NULL) {
                printf("Command execution failed - not found");
            } else if (strstr(details, "3") != NULL) {
                printf("Command execution failed - path not found");
            } else if (strstr(details, "5") != NULL) {
                printf("Command execution failed - access denied");
            } else {
                printf("Process creation failed");
            }
            break;
        case ERROR_INVALID_ARGUMENT_VAL:
            printf("Invalid argument");
            break;
        case ERROR_GENERAL_VAL:
            printf("General error");
            break;
        default:
            printf("Unknown error");
            break;
    }

    printf(" │\n");

    if (details && *details) {
        printf("│ ");
        set_console_color(COLOR_INFO);
        printf("%s", details);
        set_console_color(COLOR_ERROR);
        printf(" │\n");
    }

    show_suggestion(code, details);

    printf("╰───────────────────────────────────────────────────────╯\n");
    reset_console_color();
}

void show_error_message(const char *message) {
    if (!message) return;

    set_console_color(COLOR_ERROR);
    printf("\n╭───────────────────────────────────────────────────────╮\n");
    printf("│ ");
    set_console_color(COLOR_HIGHLIGHT_CUSTOM);
    printf("ERROR");
    set_console_color(COLOR_ERROR);
    printf(": %s │\n", message);
    printf("╰───────────────────────────────────────────────────────╯\n");
    reset_console_color();
}

void error_handler_cleanup() {
}
