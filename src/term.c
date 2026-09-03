/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "term.h"

#include <stdio.h>

#include "platform.h"
#include "shell.h"

#ifdef _WIN32

#include <conio.h>

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

    if (GetConsoleMode(out, &saved_out_mode)) {
        saved_output_cp = GetConsoleOutputCP();
        saved_input_cp = GetConsoleCP();
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
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

void term_cooked(void) {
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
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    COORD origin = {0, 0};

    if (GetConsoleScreenBufferInfo(out, &info)) {
        DWORD cells = (DWORD)info.dwSize.X * (DWORD)info.dwSize.Y;
        DWORD written;
        FillConsoleOutputCharacterA(out, ' ', cells, origin, &written);
        FillConsoleOutputAttribute(out, info.wAttributes, cells, origin, &written);
        SetConsoleCursorPosition(out, origin);
    }
    term_write("\x1b[H\x1b[2J\x1b[3J");
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
    case 59: return KEY_F1;
    case 60: return KEY_F2;
    case 61: return KEY_F3;
    case 62: return KEY_F4;
    case 63: return KEY_F5;
    case 64: return KEY_F6;
    case 65: return KEY_F7;
    case 66: return KEY_F8;
    case 67: return KEY_F9;
    case 68: return KEY_F10;
    case 133: return KEY_F11;
    case 134: return KEY_F12;
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

#else

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>

static struct termios saved_termios;
static int termios_saved = 0;
static int raw_mode = 0;

static void on_hangup(int signal_number) {
    shell_handle_signal(SIGNAL_CLOSE);
    _exit(128 + signal_number);
}

static void on_quit(int signal_number) {
    (void)signal_number;
    shell_handle_signal(SIGNAL_QUIT);
}

void term_init(void) {
    if (isatty(0) && tcgetattr(0, &saved_termios) == 0) termios_saved = 1;

    signal(SIGPIPE, SIG_IGN);
    if (shell.interactive || isatty(0)) signal(SIGINT, SIG_IGN);
    signal(SIGHUP, on_hangup);
    signal(SIGTERM, on_hangup);
    signal(SIGQUIT, on_quit);
    setvbuf(stdout, NULL, _IOFBF, 8192);
}

void term_cooked(void) {
    if (!raw_mode) return;
    raw_mode = 0;
    if (termios_saved) tcsetattr(0, TCSAFLUSH, &saved_termios);
}

static void term_raw(void) {
    if (raw_mode || !termios_saved) return;
    struct termios raw = saved_termios;
    raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_iflag &= (tcflag_t) ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSAFLUSH, &raw) == 0) raw_mode = 1;
}

void term_cleanup(void) {
    term_cooked();
}

static int window_size(struct winsize *size) {
    return ioctl(1, TIOCGWINSZ, size) == 0 || ioctl(0, TIOCGWINSZ, size) == 0;
}

int term_width(void) {
    struct winsize size;
    if (!window_size(&size)) return 80;
    return size.ws_col > 10 ? size.ws_col : 80;
}

int term_height(void) {
    struct winsize size;
    if (!window_size(&size)) return 25;
    return size.ws_row > 4 ? size.ws_row : 25;
}

int term_cursor_row(void) {
    return 0;
}

void term_write(const char *s) {
    fputs(s, stdout);
    fflush(stdout);
}

void term_clear_screen(void) {
    term_write("\x1b[H\x1b[2J\x1b[3J");
}

void term_set_title(const char *title) {
    printf("\x1b]0;%s\x07", title);
    fflush(stdout);
}

static int byte_ready(int milliseconds) {
    struct pollfd probe = {0, POLLIN, 0};
    return poll(&probe, 1, milliseconds) > 0;
}

static int read_byte(void) {
    unsigned char c;
    ssize_t n;
    do {
        n = read(0, &c, 1);
    } while (n < 0 && errno == EINTR);
    return n == 1 ? c : -1;
}

int term_input_pending(void) {
    return byte_ready(0);
}

static int map_tilde(int number, int modifier) {
    int control = modifier == 5;
    switch (number) {
    case 1:
    case 7: return KEY_HOME;
    case 4:
    case 8: return KEY_END;
    case 3: return control ? KEY_WORD_DELETE : KEY_DELETE;
    case 5: return KEY_PAGE_UP;
    case 6: return KEY_PAGE_DOWN;
    case 11: return KEY_F1;
    case 12: return KEY_F2;
    case 13: return KEY_F3;
    case 14: return KEY_F4;
    case 15: return KEY_F5;
    case 17: return KEY_F6;
    case 18: return KEY_F7;
    case 19: return KEY_F8;
    case 20: return KEY_F9;
    case 21: return KEY_F10;
    case 23: return KEY_F11;
    case 24: return KEY_F12;
    default: return -1;
    }
}

static int map_letter(int letter, int modifier) {
    int control = modifier == 5 || modifier == 3;
    switch (letter) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return control ? KEY_WORD_RIGHT : KEY_RIGHT;
    case 'D': return control ? KEY_WORD_LEFT : KEY_LEFT;
    case 'H': return KEY_HOME;
    case 'F': return KEY_END;
    case 'Z': return KEY_SHIFT_TAB;
    case 'P': return KEY_F1;
    case 'Q': return KEY_F2;
    case 'R': return KEY_F3;
    case 'S': return KEY_F4;
    default: return -1;
    }
}

static int read_escape(void) {
    if (!byte_ready(40)) return KEY_ESC;
    int first = read_byte();
    if (first == 'b') return KEY_WORD_LEFT;
    if (first == 'f') return KEY_WORD_RIGHT;
    if (first == 'd') return KEY_WORD_DELETE;
    if (first == 127 || first == 8) return KEY_WORD_DELETE;
    if (first != '[' && first != 'O') return KEY_ESC;

    int number = 0;
    int modifier = 0;
    int *target = &number;
    for (;;) {
        if (!byte_ready(40)) return -1;
        int c = read_byte();
        if (c < 0) return -1;
        if (c >= '0' && c <= '9') {
            *target = *target * 10 + (c - '0');
            continue;
        }
        if (c == ';') {
            target = &modifier;
            continue;
        }
        if (c == '~') return map_tilde(number, modifier);
        return map_letter(c, modifier);
    }
}

int term_read_key(void) {
    term_raw();
    int c = read_byte();
    if (c < 0) return KEY_CTRL_D;
    if (c == 127) return KEY_BACKSPACE;
    if (c == '\n') return KEY_ENTER;
    if (c == 27) return read_escape();
    return c;
}

#endif
