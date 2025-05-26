/*
 * Copyright (c) 2025 Musa
 * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#include "builtins.h"
#include "bin_register.h"
#include "history.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <direct.h>
#include <windows.h>
#include <io.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>

void print_separator(int width) {
    for (int i = 0; i < width; i++) {
        printf("-");
    }
    printf("\n");
}

void print_file_size(long size) {
    if (size < 1024) {
        printf("%8ld B", size);
    } else if (size < 1024 * 1024) {
        printf("%7.1f KB", size / 1024.0);
    } else if (size < 1024 * 1024 * 1024) {
        printf("%7.1f MB", size / (1024.0 * 1024.0));
    } else {
        printf("%7.1f GB", size / (1024.0 * 1024.0 * 1024.0));
    }
}

void handle_ls() {
    WIN32_FIND_DATAA findFileData;
    HANDLE hFind;
    char searchPattern[] = "*";
    int file_count = 0;
    int dir_count = 0;

    printf("Directory listing:\n");
    print_separator(80);
    printf("%-8s %-25s %10s %20s\n", "Type", "Name", "Size", "Modified");
    print_separator(80);

    hFind = FindFirstFileA(searchPattern, &findFileData);
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("Error reading directory\n");
        return;
    }

    do {
        if (strcmp(findFileData.cFileName, ".") != 0 && strcmp(findFileData.cFileName, "..") != 0) {
            char type[10];
            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                strcpy(type, "<DIR>");
                dir_count++;
            } else {
                strcpy(type, "<FILE>");
                file_count++;
            }


            SYSTEMTIME st;
            FileTimeToSystemTime(&findFileData.ftLastWriteTime, &st);

            printf("%-8s %-25s", type, findFileData.cFileName);

            if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                LARGE_INTEGER fileSize;
                fileSize.LowPart = findFileData.nFileSizeLow;
                fileSize.HighPart = findFileData.nFileSizeHigh;
                print_file_size(fileSize.QuadPart);
            } else {
                printf("%10s", "");
            }

            printf(" %02d/%02d/%04d %02d:%02d\n",
                   st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
        }
    } while (FindNextFileA(hFind, &findFileData) != 0);

    FindClose(hFind);

    print_separator(80);
    printf("Total: %d file(s), %d directory(ies)\n", file_count, dir_count);
}

void handle_pwd() {
    char current_dir[MAX_PATH];
    if (_getcwd(current_dir, MAX_PATH) != NULL) {

        for (char *p = current_dir; *p; p++) {
            if (*p == '\\') *p = '/';
        }
        printf("%s\n", current_dir);
    } else {
        printf("Error getting current directory\n");
    }
}

void handle_clear() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    DWORD count;
    DWORD cellCount;
    COORD homeCoords = { 0, 0 };

    if (hConsole == INVALID_HANDLE_VALUE) {

        for (int i = 0; i < 50; i++) {
            printf("\n");
        }
        return;
    }


    if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {

        for (int i = 0; i < 50; i++) {
            printf("\n");
        }
        return;
    }

    cellCount = csbi.dwSize.X * csbi.dwSize.Y;


    if (!FillConsoleOutputCharacter(
                hConsole,
                (TCHAR)' ',
                cellCount,
                homeCoords,
                &count
            )) {

        for (int i = 0; i < 50; i++) {
            printf("\n");
        }
        return;
    }


    if (!FillConsoleOutputAttribute(
                hConsole,
                csbi.wAttributes,
                cellCount,
                homeCoords,
                &count
            )) {

        for (int i = 0; i < 50; i++) {
            printf("\n");
        }
        return;
    }


    SetConsoleCursorPosition(hConsole, homeCoords);


}

void handle_help() {
    printf("FreSH v1.0 - Built-in Commands:\n");
    print_separator(60);
    printf("%-15s %s\n", "Command", "Description");
    print_separator(60);
    printf("%-15s %s\n", "help", "Show this help message");
    printf("%-15s %s\n", "exit/quit", "Exit the shell");
    printf("%-15s %s\n", "cd [dir]", "Change directory");
    printf("%-15s %s\n", "pwd", "Print working directory");
    printf("%-15s %s\n", "ls", "List directory contents (table format)");
    printf("%-15s %s\n", "clear", "Clear the screen completely");
    printf("%-15s %s\n", "history", "Show command history");
    printf("%-15s %s\n", "binlist", "Show registered bin commands");
    printf("%-15s %s\n", "which <cmd>", "Show path to command");
    printf("%-15s %s\n", "echo <text>", "Print text to screen");
    printf("%-15s %s\n", "shinfo", "Show shell script support info");
    print_separator(60);
    printf("Note: FreSH supports .exe, .bat, .cmd, .ps1, .sh and other executable types\n");
    printf("For .sh scripts, install Git Bash, WSL, or MSYS2\n");
    printf("Use ./script.sh to run scripts in current directory\n");
}

void handle_history() {
    printf("Command History:\n");
    print_separator(50);

    int count = history_count();
    if (count == 0) {
        printf("No commands in history\n");
        return;
    }

    printf("%-4s %s\n", "No.", "Command");
    print_separator(50);

    for (int i = 0; i < count; i++) {
        const char *cmd = history_get(i);
        if (cmd) {
            printf("%-4d %s\n", i + 1, cmd);
        }
    }

    print_separator(50);
    printf("Total: %d command(s)\n", count);
}

void handle_which(const char *command) {
    if (!command || strlen(command) == 0) {
        printf("Usage: which <command>\n");
        return;
    }


    if (strcmp(command, "$SHELL") == 0 || strcmp(command, "shell") == 0) {
        char exe_path[MAX_PATH];
        if (GetModuleFileNameA(NULL, exe_path, MAX_PATH)) {

            for (char *p = exe_path; *p; p++) {
                if (*p == '\\') *p = '/';
            }
            printf("FreSH shell located at: %s\n", exe_path);
        } else {
            printf("FreSH shell (path unavailable)\n");
        }
        return;
    }


    if (is_bin_command(command)) {
        const char *path = get_bin_command_path(command);
        if (path) {
            char display_path[MAX_PATH];
            strcpy(display_path, path);

            for (char *p = display_path; *p; p++) {
                if (*p == '\\') *p = '/';
            }
            printf("%s is %s (from bin)\n", command, display_path);
            return;
        }
    }


    char *path_env = getenv("PATH");
    if (path_env) {
        char path_copy[32768];
        strncpy(path_copy, path_env, sizeof(path_copy) - 1);
        path_copy[sizeof(path_copy) - 1] = '\0';

        char *extensions[] = {"", ".exe", ".bat", ".cmd", ".com", ".ps1", ".sh"};
        char *path_token = strtok(path_copy, ";");

        while (path_token != NULL) {
            for (int i = 0; i < 7; i++) {
                char full_path[MAX_PATH];
                snprintf(full_path, MAX_PATH, "%s\\%s%s", path_token, command, extensions[i]);

                if (GetFileAttributesA(full_path) != INVALID_FILE_ATTRIBUTES) {

                    for (char *p = full_path; *p; p++) {
                        if (*p == '\\') *p = '/';
                    }
                    printf("%s is %s\n", command, full_path);
                    return;
                }
            }
            path_token = strtok(NULL, ";");
        }
    }

    printf("%s: command not found\n", command);
}

void handle_echo(const char *text) {
    if (text && strlen(text) > 0) {
        printf("%s\n", text);
    } else {
        printf("\n");
    }
}

void handle_shinfo() {
    printf("Shell Script Support Information:\n");
    printf("=====================================\n");
    printf("FreSH supports executing .sh scripts but requires a Unix shell interpreter.\n\n");

    printf("Recommended interpreters:\n");
    printf("  1. Git Bash (comes with Git for Windows)\n");
    printf("  2. WSL (Windows Subsystem for Linux)\n");
    printf("  3. MSYS2\n");
    printf("  4. Cygwin\n\n");

    printf("To install Git Bash:\n");
    printf("  Download from: https:\n");

    printf("To enable WSL:\n");
    printf("  Run: wsl --install\n\n");

    printf("Once installed, you can run shell scripts like:\n");
    printf("  ./build.sh\n");
    printf("  ./script.sh\n");
}

int handle_builtin(char *cmd) {
    if (!cmd || strlen(cmd) == 0) {
        return 0;
    }


    char *start = cmd;
    while (*start && isspace(*start)) start++;

    if (strlen(start) == 0) {
        return 0;
    }

    char *end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) end--;
    *(end + 1) = '\0';

    if (strcmp(start, "help") == 0) {
        handle_help();
        return 1;
    }

    if (strcmp(start, "ls") == 0) {
        handle_ls();
        return 1;
    }

    if (strcmp(start, "pwd") == 0) {
        handle_pwd();
        return 1;
    }

    if (strcmp(start, "clear") == 0) {
        handle_clear();
        return 1;
    }

    if (strcmp(start, "history") == 0) {
        handle_history();
        return 1;
    }

    if (strcmp(start, "binlist") == 0) {
        list_bin_commands();
        return 1;
    }

    if (strcmp(start, "shinfo") == 0) {
        handle_shinfo();
        return 1;
    }

    if (strncmp(start, "which ", 6) == 0) {
        handle_which(start + 6);
        return 1;
    }

    if (strncmp(start, "echo ", 5) == 0) {
        handle_echo(start + 5);
        return 1;
    }

    if (strcmp(start, "echo") == 0) {
        handle_echo("");
        return 1;
    }

    return 0;
}
