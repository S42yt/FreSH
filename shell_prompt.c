/*
 * Copyright (c) 2025 Musa
 * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#include "shell_prompt.h"
#include "git_branch.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <direct.h>
#include <time.h>
#include <io.h>
#include <string.h>

#define COLOR_PATH_CUSTOM 11
#define COLOR_USER_CUSTOM 10
#define COLOR_PROMPT_CUSTOM 15
#define COLOR_TIME_CUSTOM 13
#define COLOR_RESET_CUSTOM 7
#define COLOR_DATE_CUSTOM 9
#define COLOR_GIT_BRANCH_CUSTOM 14
#define COLOR_GIT_USER_CUSTOM 12
#define COLOR_GIT_REPO_CUSTOM 6

static HANDLE hConsole = NULL;

void prompt_init() {
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) {
        hConsole = NULL;
    }
    git_config_init();
}

void set_prompt_color(int color) {
    if (hConsole && hConsole != INVALID_HANDLE_VALUE) {
        SetConsoleTextAttribute(hConsole, color);
    }
}

void print_shell_prompt() {
    char current_dir[MAX_PATH];
    char username[256];
    DWORD username_size = sizeof(username);
    char *home_dir = getenv("USERPROFILE");
    char display_dir[MAX_PATH];


    time_t now;
    struct tm *local_time;
    time(&now);
    local_time = localtime(&now);

    char time_str[64];
    char date_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", local_time);
    strftime(date_str, sizeof(date_str), "%a %d/%m", local_time);

    if (!GetUserNameA(username, &username_size)) {
        strcpy(username, "user");
    }

    if (_getcwd(current_dir, MAX_PATH) == NULL) {
        strcpy(current_dir, "unknown");
    }

    strcpy(display_dir, current_dir);
    if (home_dir && strstr(current_dir, home_dir) == current_dir) {
        snprintf(display_dir, MAX_PATH, "~%s", current_dir + strlen(home_dir));
    }

    for (char *p = display_dir; *p; p++) {
        if (*p == '\\') *p = '/';
    }


    char *git_branch = NULL;
    char *git_user = NULL;
    char *git_repo = NULL;

    if (is_git_repository()) {
        if (git_config_get_branch()) git_branch = get_git_branch();
        if (git_config_get_user()) git_user = get_git_user();
        if (git_config_get_repo()) git_repo = get_git_repo_name();
    }


    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int console_width = 80;
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        console_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }


    if (git_user && strlen(git_user) > 0) {

        char right_content[128];
        snprintf(right_content, sizeof(right_content), "%s - %s", time_str, date_str);


        char left_content[128];
        snprintf(left_content, sizeof(left_content), "git: %s", git_user);

        int left_len = strlen(left_content);
        int right_len = strlen(right_content);
        int spaces_needed = console_width - left_len - right_len - 1;


        set_prompt_color(COLOR_GIT_USER_CUSTOM);
        printf("git:");
        set_prompt_color(COLOR_RESET_CUSTOM);
        printf(" ");
        set_prompt_color(COLOR_GIT_USER_CUSTOM);
        printf("%s", git_user);
        set_prompt_color(COLOR_RESET_CUSTOM);


        for (int i = 0; i < spaces_needed && i >= 0; i++) {
            printf(" ");
        }


        set_prompt_color(COLOR_TIME_CUSTOM);
        printf("%s", time_str);
        set_prompt_color(COLOR_RESET_CUSTOM);
        printf(" - ");
        set_prompt_color(COLOR_DATE_CUSTOM);
        printf("%s", date_str);
        set_prompt_color(COLOR_RESET_CUSTOM);
        printf("\n");
    } else {

        int time_date_len = strlen(time_str) + strlen(date_str) + 3;
        int spaces_needed = console_width - time_date_len - 1;

        for (int i = 0; i < spaces_needed && i < console_width; i++) {
            printf(" ");
        }

        set_prompt_color(COLOR_TIME_CUSTOM);
        printf("%s", time_str);
        set_prompt_color(COLOR_RESET_CUSTOM);
        printf(" - ");
        set_prompt_color(COLOR_DATE_CUSTOM);
        printf("%s", date_str);
        set_prompt_color(COLOR_RESET_CUSTOM);
        printf("\n");
    }



    char git_info[256] = {0};
    if (git_repo && git_branch) {
        snprintf(git_info, sizeof(git_info), " FreSH::%s", git_branch);
    } else if (git_branch) {
        snprintf(git_info, sizeof(git_info), " (%s)", git_branch);
    }


    set_prompt_color(COLOR_USER_CUSTOM);
    printf("%s", username);
    set_prompt_color(COLOR_RESET_CUSTOM);
    printf(":");
    set_prompt_color(COLOR_PATH_CUSTOM);
    printf("%s", display_dir);


    if (strlen(git_info) > 0) {
        set_prompt_color(COLOR_GIT_REPO_CUSTOM);
        printf("%s", git_info);
    }

    set_prompt_color(COLOR_RESET_CUSTOM);
    printf("\n");


    set_prompt_color(COLOR_PROMPT_CUSTOM);
    printf("$ ");

    set_prompt_color(COLOR_RESET_CUSTOM);
    fflush(stdout);
}

void prompt_cleanup() {
    if (hConsole && hConsole != INVALID_HANDLE_VALUE) {
        SetConsoleTextAttribute(hConsole, COLOR_RESET_CUSTOM);
    }
    git_cleanup();
}
