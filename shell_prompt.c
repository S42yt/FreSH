/*
 * Copyright (c) 2025 Musa
 * MIT License - See LICENSE file for details
 */

#include "shell_prompt.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <direct.h>
#include <time.h>
#include <io.h>

#define COLOR_PATH_CUSTOM 11
#define COLOR_USER_CUSTOM 10
#define COLOR_PROMPT_CUSTOM 15
#define COLOR_TIME_CUSTOM 13
#define COLOR_RESET_CUSTOM 7
#define COLOR_GIT_CUSTOM 14

static HANDLE hConsole = NULL;

char* get_git_branch() {
    static char branch[128] = "";
    FILE *fp = NULL;
    char path[512];
    char line[256];

    snprintf(path, sizeof(path), ".git");
    if (_access(path, 0) != 0) {
        return NULL;
    }

    fp = _popen("git rev-parse --abbrev-ref HEAD 2> nul", "r");
    if (fp == NULL) {
        return NULL;
    }

    if (fgets(line, sizeof(line), fp) != NULL) {
        line[strcspn(line, "\r\n")] = 0;
        snprintf(branch, sizeof(branch), "%s", line);
        _pclose(fp);
        return branch;
    }

    _pclose(fp);
    return NULL;
}

void prompt_init() {
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
}

void set_prompt_color(int color) {
    if (hConsole) {
        SetConsoleTextAttribute(hConsole, color);
    }
}

void print_shell_prompt() {
    char current_dir[MAX_PATH];
    char username[256];
    DWORD username_size = sizeof(username);
    time_t current_time;
    struct tm *time_info;
    char time_string[9];
    char *git_branch;

    GetUserNameA(username, &username_size);

    if (_getcwd(current_dir, MAX_PATH) == NULL) {
        strcpy(current_dir, "unknown");
    }

    time(&current_time);
    time_info = localtime(&current_time);
    strftime(time_string, sizeof(time_string), "%H:%M:%S", time_info);

    printf("\n");

    set_prompt_color(COLOR_TIME_CUSTOM);
    printf("[%s] ", time_string);

    set_prompt_color(COLOR_USER_CUSTOM);
    printf("%s", username);

    set_prompt_color(COLOR_PATH_CUSTOM);
    printf(" %s", current_dir);

    git_branch = get_git_branch();
    if (git_branch) {
        set_prompt_color(COLOR_GIT_CUSTOM);
        printf(" (%s)", git_branch);
    }

    set_prompt_color(COLOR_PROMPT_CUSTOM);
    printf("\n> ");

    set_prompt_color(COLOR_RESET_CUSTOM);
    fflush(stdout);
}

void prompt_cleanup() {
}
