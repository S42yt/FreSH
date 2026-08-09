/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_TUI_H
#define FRESH_TUI_H

void tui_init(void);
void tui_screen(const char *title);
void tui_text(const char *text);
void tui_blank(void);
void tui_step(int ok, const char *message);
void tui_progress(int done, int total);
int tui_menu(const char **options, int count);
void tui_wait_key(void);

#endif
