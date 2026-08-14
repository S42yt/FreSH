/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_TERM_H
#define FRESH_TERM_H

enum {
    KEY_CTRL_A = 1,
    KEY_CTRL_B = 2,
    KEY_CTRL_C = 3,
    KEY_CTRL_D = 4,
    KEY_CTRL_E = 5,
    KEY_CTRL_F = 6,
    KEY_CTRL_G = 7,
    KEY_BACKSPACE = 8,
    KEY_TAB = 9,
    KEY_LINE_FEED = 10,
    KEY_CTRL_K = 11,
    KEY_CTRL_L = 12,
    KEY_ENTER = 13,
    KEY_CTRL_N = 14,
    KEY_CTRL_P = 16,
    KEY_CTRL_R = 18,
    KEY_CTRL_T = 20,
    KEY_CTRL_U = 21,
    KEY_CTRL_W = 23,
    KEY_ESC = 27,
    KEY_SPECIAL = 0x1000,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,
    KEY_WORD_LEFT,
    KEY_WORD_RIGHT,
    KEY_WORD_DELETE,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_SHIFT_TAB
};

void term_init(void);
void term_cleanup(void);
int term_width(void);
int term_height(void);
int term_read_key(void);
int term_input_pending(void);
void term_write(const char *s);
void term_clear_screen(void);
void term_set_title(const char *title);
int term_cursor_row(void);

#endif
