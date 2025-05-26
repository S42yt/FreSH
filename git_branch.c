/*
 * Copyright (c) 2025 Musa
 * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#include "git_branch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <direct.h>

#define MAX_PATH_LEN 512
#define MAX_BRANCH_LEN 256
#define MAX_USER_LEN 256
#define MAX_REPO_LEN 256

static char current_branch[MAX_BRANCH_LEN] = {0};
static char git_username[MAX_USER_LEN] = {0};
static char repo_name[MAX_REPO_LEN] = {0};
static int git_info_cached = 0;


static GitConfig git_config = {1, 1, 1};


static int execute_git_command(const char* cmd, char* output, size_t output_size) {
    if (!cmd || !output || output_size == 0) return 0;

    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa;

    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return 0;
    }

    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};

    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.wShowWindow = SW_HIDE;

    char command_line[512];
    snprintf(command_line, sizeof(command_line), "git %s", cmd);

    BOOL result = CreateProcessA(
                      NULL,
                      command_line,
                      NULL,
                      NULL,
                      TRUE,
                      CREATE_NO_WINDOW,
                      NULL,
                      NULL,
                      &si,
                      &pi
                  );

    CloseHandle(hWrite);

    if (!result) {
        CloseHandle(hRead);
        return 0;
    }

    DWORD bytes_read;
    DWORD total_read = 0;
    char buffer[4096];

    output[0] = '\0';

    while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytes_read, NULL) && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        if (total_read + bytes_read < output_size - 1) {
            strcat(output, buffer);
            total_read += bytes_read;
        } else {
            break;
        }
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);


    char *end = output + strlen(output) - 1;
    while (end > output && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')) {
        *end = '\0';
        end--;
    }

    return (exit_code == 0 && strlen(output) > 0);
}


int is_git_repository(void) {
    char output[256];
    return execute_git_command("rev-parse --is-inside-work-tree", output, sizeof(output));
}


char* get_git_repo_name(void) {
    if (!is_git_repository()) {
        repo_name[0] = '\0';
        return NULL;
    }

    char output[MAX_REPO_LEN];


    if (execute_git_command("config --get remote.origin.url", output, sizeof(output))) {

        char *last_slash = strrchr(output, '/');
        if (last_slash) {
            char *name_start = last_slash + 1;

            char *git_ext = strstr(name_start, ".git");
            if (git_ext) {
                *git_ext = '\0';
            }
            strncpy(repo_name, name_start, MAX_REPO_LEN - 1);
            repo_name[MAX_REPO_LEN - 1] = '\0';
            return repo_name;
        }
    }


    char current_dir[MAX_PATH];
    if (_getcwd(current_dir, MAX_PATH)) {
        char *last_slash = strrchr(current_dir, '\\');
        if (!last_slash) last_slash = strrchr(current_dir, '/');
        if (last_slash) {
            strncpy(repo_name, last_slash + 1, MAX_REPO_LEN - 1);
            repo_name[MAX_REPO_LEN - 1] = '\0';
            return repo_name;
        }
    }

    strcpy(repo_name, "unknown");
    return repo_name;
}


char* get_git_branch(void) {
    if (!is_git_repository()) {
        current_branch[0] = '\0';
        return NULL;
    }

    char output[MAX_BRANCH_LEN];


    if (execute_git_command("branch --show-current", output, sizeof(output))) {
        strncpy(current_branch, output, MAX_BRANCH_LEN - 1);
        current_branch[MAX_BRANCH_LEN - 1] = '\0';
        return current_branch;
    }


    if (execute_git_command("symbolic-ref --short HEAD", output, sizeof(output))) {
        strncpy(current_branch, output, MAX_BRANCH_LEN - 1);
        current_branch[MAX_BRANCH_LEN - 1] = '\0';
        return current_branch;
    }


    if (execute_git_command("rev-parse --short HEAD", output, sizeof(output))) {
        snprintf(current_branch, MAX_BRANCH_LEN, "HEAD@%s", output);
        return current_branch;
    }

    current_branch[0] = '\0';
    return NULL;
}


char* get_git_user(void) {
    if (!is_git_repository()) {
        git_username[0] = '\0';
        return NULL;
    }

    char output[MAX_USER_LEN];


    if (execute_git_command("config --local user.name", output, sizeof(output))) {
        strncpy(git_username, output, MAX_USER_LEN - 1);
        git_username[MAX_USER_LEN - 1] = '\0';
        return git_username;
    }


    if (execute_git_command("config --global user.name", output, sizeof(output))) {
        strncpy(git_username, output, MAX_USER_LEN - 1);
        git_username[MAX_USER_LEN - 1] = '\0';
        return git_username;
    }

    git_username[0] = '\0';
    return NULL;
}


void git_config_init(void) {
    git_config.show_user = 1;
    git_config.show_branch = 1;
    git_config.show_repo = 1;
    git_config_load();
}

void git_config_set_user(int show) {
    git_config.show_user = show;
    git_config_save();
}

void git_config_set_branch(int show) {
    git_config.show_branch = show;
    git_config_save();
}

void git_config_set_repo(int show) {
    git_config.show_repo = show;
    git_config_save();
}

int git_config_get_user(void) {
    return git_config.show_user;
}

int git_config_get_branch(void) {
    return git_config.show_branch;
}

int git_config_get_repo(void) {
    return git_config.show_repo;
}

void git_config_save(void) {
    char config_path[MAX_PATH];
    char *home = getenv("USERPROFILE");

    if (!home) return;

    snprintf(config_path, MAX_PATH, "%s\\.FreSH_git_config", home);

    FILE *file = fopen(config_path, "w");
    if (!file) return;

    fprintf(file, "show_user=%d\n", git_config.show_user);
    fprintf(file, "show_branch=%d\n", git_config.show_branch);
    fprintf(file, "show_repo=%d\n", git_config.show_repo);

    fclose(file);
}

void git_config_load(void) {
    char config_path[MAX_PATH];
    char *home = getenv("USERPROFILE");

    if (!home) return;

    snprintf(config_path, MAX_PATH, "%s\\.FreSH_git_config", home);

    FILE *file = fopen(config_path, "r");
    if (!file) return;

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = 0;

        if (strncmp(line, "show_user=", 10) == 0) {
            git_config.show_user = atoi(line + 10);
        } else if (strncmp(line, "show_branch=", 12) == 0) {
            git_config.show_branch = atoi(line + 12);
        } else if (strncmp(line, "show_repo=", 10) == 0) {
            git_config.show_repo = atoi(line + 10);
        }
    }

    fclose(file);
}

void git_cleanup(void) {
    current_branch[0] = '\0';
    git_username[0] = '\0';
    repo_name[0] = '\0';
    git_info_cached = 0;
}