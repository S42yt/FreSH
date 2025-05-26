/*
 * Copyright (c) 2025 Musa
 * MIT License - See LICENSE file for details
 */

#include "error_handler.h"
#include "shell_core.h"
#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <windows.h>
#include <ctype.h>

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
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    SetConsoleTextAttribute(hConsole, HELP_TITLE_COLOR);
    printf("\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║                 FreeShell 2.0 Help                       ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n\n");

    SetConsoleTextAttribute(hConsole, HELP_CATEGORY_COLOR);
    printf("【 Basic Commands 】\n");

    SetConsoleTextAttribute(hConsole, HELP_TEXT_COLOR);
    printf("  help            - Show this help message\n");
    printf("  exit, quit      - Exit the shell\n");
    printf("  cd [directory]  - Change working directory\n");
    printf("  clear, cls      - Clear the screen\n\n");

    SetConsoleTextAttribute(hConsole, HELP_CATEGORY_COLOR);
    printf("【 File Operations 】\n");

    SetConsoleTextAttribute(hConsole, HELP_TEXT_COLOR);
    printf("  dir, ls         - List directory contents\n");
    printf("  mkdir [dir]     - Create a new directory\n");
    printf("  rmdir [dir]     - Remove a directory\n");
    printf("  del, rm [file]  - Delete a file\n");
    printf("  type, cat [file] - Display file contents\n\n");

    SetConsoleTextAttribute(hConsole, HELP_CATEGORY_COLOR);
    printf("【 Shortcuts 】\n");

    SetConsoleTextAttribute(hConsole, HELP_TEXT_COLOR);
    printf("  Up/Down arrows  - Navigate command history\n");
    printf("  Tab             - File/directory name completion\n");
    printf("  Ctrl+C          - Cancel current command\n\n");

    SetConsoleTextAttribute(hConsole, HELP_RESET_COLOR);
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

    if (strcmp(cmd, "help") == 0) {
        show_help();
        return 1;
    }

    if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
        clear_screen();
        return 1;
    }

    return 0;
}
