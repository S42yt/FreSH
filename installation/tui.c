/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "tui.h"

#include <conio.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

#include "installer.h"

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

#define WIDTH 72
#define TUI_ACCENT "\x1b[1;32m"
#define TUI_DIM "\x1b[90m"
#define TUI_ERROR "\x1b[31m"
#define TUI_RESET "\x1b[0m"

void tui_init(void) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode))
        SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleA("FreSH Setup");
}

static void rule(void) {
    fputs(TUI_DIM, stdout);
    for (int i = 0; i < WIDTH; i++) fputs("\xe2\x94\x80", stdout);
    fputs(TUI_RESET "\n", stdout);
}

void tui_screen(const char *title) {
    fputs("\x1b[2J\x1b[H\n", stdout);
    printf("  " TUI_ACCENT "FreSH" TUI_RESET " " TUI_DIM "%s" TUI_RESET "\n", FRESH_VERSION);
    printf("  %s\n", title);
    fputs("  ", stdout);
    rule();
    fputs("\n", stdout);
}

void tui_text(const char *text) {
    printf("  %s\n", text);
}

void tui_blank(void) {
    fputs("\n", stdout);
}

void tui_step(int ok, const char *message) {
    if (ok) printf("  " TUI_ACCENT "\xe2\x9c\x93" TUI_RESET " %s\n", message);
    else printf("  " TUI_ERROR "\xe2\x9c\x97" TUI_RESET " %s\n", message);
    fflush(stdout);
}

void tui_progress(int done, int total) {
    int filled = total > 0 ? done * 40 / total : 0;
    printf("\r  " TUI_ACCENT);
    for (int i = 0; i < filled; i++) fputs("\xe2\x96\x88", stdout);
    fputs(TUI_DIM, stdout);
    for (int i = filled; i < 40; i++) fputs("\xe2\x96\x91", stdout);
    printf(TUI_RESET " %d%%", total > 0 ? done * 100 / total : 100);
    fflush(stdout);
}

int tui_menu(const char **options, int count) {
    int selected = 0;
    int drawn = 0;

    while (1) {
        if (drawn) printf("\x1b[%dA", count);
        for (int i = 0; i < count; i++) {
            printf("\r\x1b[K");
            if (i == selected) printf("  " TUI_ACCENT "\xe2\x9d\xaf %s" TUI_RESET "\n", options[i]);
            else printf("    " TUI_DIM "%s" TUI_RESET "\n", options[i]);
        }
        drawn = 1;
        fflush(stdout);

        int key = _getch();
        if (key == 0 || key == 224) {
            key = _getch();
            if (key == 72) selected = (selected - 1 + count) % count;
            else if (key == 80) selected = (selected + 1) % count;
        } else if (key == 13) {
            return selected;
        } else if (key == 27) {
            return -1;
        } else if (key >= '1' && key < '1' + count) {
            return key - '1';
        }
    }
}

void tui_wait_key(void) {
    printf("\n  " TUI_DIM "press any key to continue" TUI_RESET);
    fflush(stdout);
    _getch();
    printf("\n");
}
