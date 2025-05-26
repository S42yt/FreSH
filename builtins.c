/*
 * Copyright (c) 2025 Musa
 * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#include "error_handler.h"
#include "shell_core.h"
#include "builtins.h"
#include "history.h"
#include "bin_register.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <windows.h>
#include <ctype.h>
#include <errno.h>

#define MAX_ALIASES 50
#define MAX_ALIAS_LEN 128

#define HELP_TITLE_COLOR 11
#define HELP_CATEGORY_COLOR 10
#define HELP_TEXT_COLOR 15
#define HELP_RESET_COLOR 7

typedef struct {
    char name[MAX_ALIAS_LEN];
    char value[MAX_ALIAS_LEN];
} Alias;

static Alias aliases[MAX_ALIASES];
static int alias_count = 0;

void add_alias(const char *alias_def) {
    if (alias_count >= MAX_ALIASES) return;

    char temp[MAX_ALIAS_LEN * 2];
    strncpy(temp, alias_def, MAX_ALIAS_LEN * 2);

    char *equals = strchr(temp, '=');
    if (!equals) return;

    *equals = '\0';

    strncpy(aliases[alias_count].name, temp, MAX_ALIAS_LEN);
    strncpy(aliases[alias_count].value, equals + 1, MAX_ALIAS_LEN);

    aliases[alias_count].name[MAX_ALIAS_LEN-1] = '\0';
    aliases[alias_count].value[MAX_ALIAS_LEN-1] = '\0';

    alias_count++;
}

void expand_aliases(char *cmd) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(cmd, aliases[i].name) == 0) {
            strcpy(cmd, aliases[i].value);
            return;
        }
    }
}

void show_help() {
    printf("FreSH - First-Run Experience Shell\n\n");

    printf("BUILT-IN COMMANDS:\n");
    printf("  help                    display this help message\n");
    printf("  exit, quit              exit the shell\n");
    printf("  cd [dir]                change working directory\n");
    printf("  clear, cls              clear the terminal screen\n");
    printf("  pwd                     print working directory\n");
    printf("  echo [text]             display text\n");
    printf("  which [command]         locate a command\n");
    printf("  alias [name=value]      create command alias\n");
    printf("  history                 show command history\n");
    printf("  export [var=value]      set environment variable\n");
    printf("  env                     show environment variables\n");
    printf("  type [command]          show command type\n\n");

    printf("FILE OPERATIONS:\n");
    printf("  ls, dir                 list directory contents\n");
    printf("  cat, type [file]        display file contents\n");
    printf("  cp [src] [dest]         copy files\n");
    printf("  mv [src] [dest]         move/rename files\n");
    printf("  rm, del [file]          remove files\n");
    printf("  mkdir [dir]             create directory\n");
    printf("  rmdir [dir]             remove directory\n");
    printf("  find [path] [pattern]   search for files\n");
    printf("  grep [pattern] [file]   search text in files\n\n");

    printf("TEXT PROCESSING:\n");
    printf("  head [file]             show first lines of file\n");
    printf("  tail [file]             show last lines of file\n");
    printf("  wc [file]               count lines, words, characters\n");
    printf("  sort [file]             sort lines in file\n");
    printf("  uniq [file]             remove duplicate lines\n\n");

    printf("GIT INTEGRATION:\n");
    printf("  Git commands are passed through to git.exe\n");
    printf("  Current branch shown in prompt when in git repository\n\n");

    printf("KEYBOARD SHORTCUTS:\n");
    printf("  Up/Down arrows          navigate command history\n");
    printf("  Tab                     file/directory completion\n");
    printf("  Ctrl+C                  interrupt current command\n");
}

void clear_screen() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD count;
    DWORD cellCount;
    COORD homeCoords = { 0, 0 };

    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        cellCount = csbi.dwSize.X * csbi.dwSize.Y;
        FillConsoleOutputCharacterA(hConsole, ' ', cellCount, homeCoords, &count);
        FillConsoleOutputAttribute(hConsole, csbi.wAttributes, cellCount, homeCoords, &count);
        SetConsoleCursorPosition(hConsole, homeCoords);
    }
}

int handle_builtin(char *cmd) {
    if (!cmd || strlen(cmd) == 0) {
        return 0;
    }

    while (*cmd && isspace(*cmd)) cmd++;

    char *args = strchr(cmd, ' ');
    if (args) {
        *args = '\0';
        args++;
        while (*args && isspace(*args)) args++;
        if (*args == '\0') args = NULL;
    }

    if (strcmp(cmd, "help") == 0) {
        show_help();
        return 1;
    }

    if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
        clear_screen();
        return 1;
    }

    if (strcmp(cmd, "pwd") == 0) {
        builtin_pwd();
        return 1;
    }

    if (strcmp(cmd, "echo") == 0) {
        builtin_echo(args);
        return 1;
    }

    if (strcmp(cmd, "which") == 0) {
        if (args) {
            char *first_arg = strtok(args, " ");
            builtin_which(first_arg);
        } else {
            printf("FreSH: which: missing argument\n");
        }
        return 1;
    }

    if (strcmp(cmd, "env") == 0) {
        builtin_env();
        return 1;
    }

    if (strcmp(cmd, "export") == 0) {
        builtin_export(args);
        return 1;
    }

    if (strcmp(cmd, "history") == 0) {
        builtin_history();
        return 1;
    }

    if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0) {
        char *first_arg = NULL;
        if (args) first_arg = strtok(args, " ");
        builtin_ls(first_arg);
        return 1;
    }

    if (strcmp(cmd, "cat") == 0 || strcmp(cmd, "type") == 0) {
        if (args) {
            char *first_arg = strtok(args, " ");
            builtin_cat(first_arg);
        } else {
            printf("FreSH: %s: missing file argument\n", cmd);
        }
        return 1;
    }

    if (strcmp(cmd, "mkdir") == 0) {
        if (args) {
            char *first_arg = strtok(args, " ");
            builtin_mkdir(first_arg);
        } else {
            printf("FreSH: mkdir: missing directory argument\n");
        }
        return 1;
    }

    if (strcmp(cmd, "rm") == 0 || strcmp(cmd, "del") == 0) {
        if (args) {
            char *first_arg = strtok(args, " ");
            builtin_rm(first_arg);
        } else {
            printf("FreSH: %s: missing file argument\n", cmd);
        }
        return 1;
    }

    if (strcmp(cmd, "cp") == 0 || strcmp(cmd, "copy") == 0) {
        if (args) {
            char *first_arg = strtok(args, " ");
            char *second_arg = strtok(NULL, " ");
            if (first_arg && second_arg) {
                builtin_cp(first_arg, second_arg);
            } else {
                printf("FreSH: %s: missing arguments (source destination)\n", cmd);
            }
        } else {
            printf("FreSH: %s: missing arguments\n", cmd);
        }
        return 1;
    }

    if (strcmp(cmd, "mv") == 0 || strcmp(cmd, "move") == 0) {
        if (args) {
            char *first_arg = strtok(args, " ");
            char *second_arg = strtok(NULL, " ");
            if (first_arg && second_arg) {
                builtin_mv(first_arg, second_arg);
            } else {
                printf("FreSH: %s: missing arguments (source destination)\n", cmd);
            }
        } else {
            printf("FreSH: %s: missing arguments\n", cmd);
        }
        return 1;
    }

    if (strcmp(cmd, "grep") == 0) {
        if (args) {
            char *pattern = strtok(args, " ");
            char *filename = strtok(NULL, " ");
            builtin_grep(pattern, filename);
        } else {
            printf("FreSH: grep: missing pattern\n");
        }
        return 1;
    }

    if (strcmp(cmd, "head") == 0) {
        if (args) {
            char *first_arg = strtok(args, " ");
            builtin_head(first_arg);
        } else {
            printf("FreSH: head: missing file argument\n");
        }
        return 1;
    }

    if (strcmp(cmd, "tail") == 0) {
        if (args) {
            char *first_arg = strtok(args, " ");
            builtin_tail(first_arg);
        } else {
            printf("FreSH: tail: missing file argument\n");
        }
        return 1;
    }

    if (strcmp(cmd, "wc") == 0) {
        if (args) {
            char *first_arg = strtok(args, " ");
            builtin_wc(first_arg);
        } else {
            printf("FreSH: wc: missing file argument\n");
        }
        return 1;
    }

    if (strncmp(cmd, "alias", 5) == 0) {
        if (args) {
            add_alias(args);
        } else {
            for (int i = 0; i < alias_count; i++) {
                printf("%s=%s\n", aliases[i].name, aliases[i].value);
            }
        }
        return 1;
    }

    if (strcmp(cmd, "binlist") == 0) {
        list_bin_commands();
        return 1;
    }

    return 0;
}

int builtin_pwd() {
    char current_dir[MAX_PATH];
    if (_getcwd(current_dir, MAX_PATH) != NULL) {
        printf("%s\n", current_dir);
        return 0;
    }
    printf("FreSH: pwd: error getting current directory\n");
    return 1;
}

int builtin_echo(char *args) {
    if (args) {
        printf("%s\n", args);
    } else {
        printf("\n");
    }
    return 0;
}

int builtin_which(char *cmd) {
    if (!cmd) {
        printf("FreSH: which: missing argument\n");
        return 1;
    }

    char command_path[MAX_PATH];
    char *path_extensions[] = {"", ".exe", ".bat", ".cmd", ".com", NULL};
    int found = 0;

    if (strchr(cmd, '\\') || strchr(cmd, '/')) {
        if (GetFileAttributesA(cmd) != INVALID_FILE_ATTRIBUTES) {
            printf("%s\n", cmd);
            return 0;
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
                             path_token, cmd, path_extensions[i]);

                    if (GetFileAttributesA(command_path) != INVALID_FILE_ATTRIBUTES) {
                        printf("%s\n", command_path);
                        found = 1;
                        break;
                    }
                }
                path_token = strtok(NULL, ";");
            }
        }
    }

    if (!found) {
        printf("FreSH: which: %s: not found\n", cmd);
        return 1;
    }
    return 0;
}

int builtin_env() {
    char *env_block = GetEnvironmentStringsA();
    if (!env_block) {
        printf("FreSH: env: failed to get environment variables\n");
        return 1;
    }

    char *env_ptr = env_block;
    while (*env_ptr) {
        printf("%s\n", env_ptr);
        env_ptr += strlen(env_ptr) + 1;
    }

    FreeEnvironmentStringsA(env_block);
    return 0;
}

int builtin_export(char *args) {
    if (!args) {
        return builtin_env();
    }

    char *equals = strchr(args, '=');
    if (!equals) {
        printf("FreSH: export: %s: invalid format (use var=value)\n", args);
        return 1;
    }

    *equals = '\0';
    char *var = args;
    char *value = equals + 1;

    if (SetEnvironmentVariableA(var, value)) {
        return 0;
    } else {
        printf("FreSH: export: failed to set %s\n", var);
        return 1;
    }
}

int builtin_history() {
    int count = history_count();
    for (int i = 0; i < count; i++) {
        const char *cmd = history_get(i);
        if (cmd) {
            printf("%4d  %s\n", i + 1, cmd);
        }
    }
    return 0;
}

int builtin_ls(char *path) {
    WIN32_FIND_DATAA findFileData;
    HANDLE hFind;
    char searchPath[MAX_PATH];

    if (!path || strlen(path) == 0) {
        strcpy(searchPath, "*");
    } else {
        snprintf(searchPath, MAX_PATH, "%s\\*", path);
    }

    hFind = FindFirstFileA(searchPath, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("FreSH: ls: cannot access '%s': No such file or directory\n", path ? path : ".");
        return 1;
    }

    do {
        if (strcmp(findFileData.cFileName, ".") != 0 && strcmp(findFileData.cFileName, "..") != 0) {
            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                printf("%s/\n", findFileData.cFileName);
            } else {
                printf("%s\n", findFileData.cFileName);
            }
        }
    } while (FindNextFileA(hFind, &findFileData) != 0);

    FindClose(hFind);
    return 0;
}

int builtin_cat(char *filename) {
    if (!filename) {
        printf("FreSH: cat: missing file argument\n");
        return 1;
    }

    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("FreSH: cat: %s: No such file or directory\n", filename);
        return 1;
    }

    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        printf("%s", line);
    }

    fclose(file);
    return 0;
}

int builtin_mkdir(char *dirname) {
    if (!dirname) {
        printf("FreSH: mkdir: missing directory argument\n");
        return 1;
    }

    if (_mkdir(dirname) == 0) {
        return 0;
    } else {
        printf("FreSH: mkdir: cannot create directory '%s': %s\n", dirname, strerror(errno));
        return 1;
    }
}

int builtin_rm(char *filename) {
    if (!filename) {
        printf("FreSH: rm: missing file argument\n");
        return 1;
    }

    if (remove(filename) == 0) {
        return 0;
    } else {
        printf("FreSH: rm: cannot remove '%s': %s\n", filename, strerror(errno));
        return 1;
    }
}

int builtin_cp(char *src, char *dest) {
    if (!src || !dest) {
        printf("FreSH: cp: missing file arguments\n");
        return 1;
    }

    if (CopyFileA(src, dest, FALSE)) {
        return 0;
    } else {
        printf("FreSH: cp: cannot copy '%s' to '%s'\n", src, dest);
        return 1;
    }
}

int builtin_mv(char *src, char *dest) {
    if (!src || !dest) {
        printf("FreSH: mv: missing file arguments\n");
        return 1;
    }

    if (MoveFileA(src, dest)) {
        return 0;
    } else {
        printf("FreSH: mv: cannot move '%s' to '%s'\n", src, dest);
        return 1;
    }
}

int builtin_grep(char *pattern, char *filename) {
    if (!pattern) {
        printf("FreSH: grep: missing pattern\n");
        return 1;
    }

    FILE *file = stdin;
    if (filename) {
        file = fopen(filename, "r");
        if (!file) {
            printf("FreSH: grep: %s: No such file or directory\n", filename);
            return 1;
        }
    }

    char line[1024];
    int line_num = 1;
    int found = 0;

    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, pattern)) {
            printf("%s", line);
            found = 1;
        }
        line_num++;
    }

    if (filename) {
        fclose(file);
    }

    return found ? 0 : 1;
}

int builtin_head(char *filename) {
    if (!filename) {
        printf("FreSH: head: missing file argument\n");
        return 1;
    }

    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("FreSH: head: %s: No such file or directory\n", filename);
        return 1;
    }

    char line[1024];
    int count = 0;

    while (fgets(line, sizeof(line), file) && count < 10) {
        printf("%s", line);
        count++;
    }

    fclose(file);
    return 0;
}

int builtin_tail(char *filename) {
    if (!filename) {
        printf("FreSH: tail: missing file argument\n");
        return 1;
    }

    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("FreSH: tail: %s: No such file or directory\n", filename);
        return 1;
    }

    char lines[10][1024];
    int line_count = 0;
    int current_line = 0;

    while (fgets(lines[current_line], sizeof(lines[current_line]), file)) {
        current_line = (current_line + 1) % 10;
        if (line_count < 10) line_count++;
    }

    int start = (line_count == 10) ? current_line : 0;
    for (int i = 0; i < line_count; i++) {
        printf("%s", lines[(start + i) % 10]);
    }

    fclose(file);
    return 0;
}

int builtin_wc(char *filename) {
    if (!filename) {
        printf("FreSH: wc: missing file argument\n");
        return 1;
    }

    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("FreSH: wc: %s: No such file or directory\n", filename);
        return 1;
    }

    int lines = 0, words = 0, chars = 0;
    char line[1024];

    while (fgets(line, sizeof(line), file)) {
        lines++;
        chars += strlen(line);

        char *token = strtok(line, " \t\n");
        while (token) {
            words++;
            token = strtok(NULL, " \t\n");
        }
    }

    printf("%d %d %d %s\n", lines, words, chars, filename);
    fclose(file);
    return 0;
}
