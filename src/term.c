/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "term.h"

#include <conio.h>
#include <stdio.h>
#include <windows.h>

#include "shell.h"

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

static DWORD saved_out_mode = 0;
static UINT saved_output_cp = 0;
static UINT saved_input_cp = 0;

static BOOL WINAPI ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT) {
        shell_handle_signal(SIGNAL_INT);
        return TRUE;
    }
    if (type == CTRL_BREAK_EVENT) {
        shell_handle_signal(SIGNAL_QUIT);
        return TRUE;
    }
    if (type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        shell_handle_signal(SIGNAL_CLOSE);
        return FALSE;
    }
    return FALSE;
}

void term_init(void) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

    saved_output_cp = GetConsoleOutputCP();
    saved_input_cp = GetConsoleCP();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (GetConsoleMode(out, &saved_out_mode)) {
        SetConsoleMode(out, saved_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    SetConsoleCtrlHandler(ctrl_handler, TRUE);
    setvbuf(stdout, NULL, _IOFBF, 8192);
}

void term_cleanup(void) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (saved_out_mode) SetConsoleMode(out, saved_out_mode);
    if (saved_output_cp) SetConsoleOutputCP(saved_output_cp);
    if (saved_input_cp) SetConsoleCP(saved_input_cp);
    SetConsoleCtrlHandler(ctrl_handler, FALSE);
}

static int screen_info(CONSOLE_SCREEN_BUFFER_INFO *csbi) {
    return GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), csbi) != 0;
}

int term_width(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!screen_info(&csbi)) return 80;
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    return width > 10 ? width : 80;
}

int term_height(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!screen_info(&csbi)) return 25;
    int height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return height > 4 ? height : 25;
}

int term_cursor_row(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!screen_info(&csbi)) return 0;
    return csbi.dwCursorPosition.Y;
}

void term_write(const char *s) {
    fputs(s, stdout);
    fflush(stdout);
}

void term_clear_screen(void) {
    term_write("\x1b[2J\x1b[H");
}

void term_set_title(const char *title) {
    SetConsoleTitleA(title);
    printf("\x1b]0;%s\x07", title);
    fflush(stdout);
}

static int map_extended(int code) {
    switch (code) {
    case 72: return KEY_UP;
    case 80: return KEY_DOWN;
    case 75: return KEY_LEFT;
    case 77: return KEY_RIGHT;
    case 71: return KEY_HOME;
    case 79: return KEY_END;
    case 83: return KEY_DELETE;
    case 73: return KEY_PAGE_UP;
    case 81: return KEY_PAGE_DOWN;
    case 115: return KEY_WORD_LEFT;
    case 116: return KEY_WORD_RIGHT;
    case 147: return KEY_WORD_DELETE;
    case 48: return KEY_WORD_LEFT;
    case 33: return KEY_WORD_RIGHT;
    case 32: return KEY_WORD_DELETE;
    case 15: return KEY_SHIFT_TAB;
    default: return -1;
    }
}

static int pushback = -1;

int term_input_pending(void) {
    return pushback >= 0 || _kbhit() != 0;
}

int term_read_key(void) {
    int ch;
    if (pushback >= 0) {
        ch = pushback;
        pushback = -1;
    } else {
        ch = _getch();
    }
    if (ch == '\r' && _kbhit()) {
        int next = _getch();
        if (next != '\n') pushback = next;
    }
    if (ch == 0 || ch == 224) {
        int mapped = map_extended(_getch());
        return mapped > 0 ? mapped : -1;
    }
    return ch;
}
