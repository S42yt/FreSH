/*
 * Copyright (c) 2025 Musa/S42
 * FreSH Installation Wizard - TUI Interface Header
 * MIT License - See LICENSE file for details
 */

#ifndef TUI_H
#define TUI_H

#include "installer.h"


void show_welcome_screen();
int show_license_agreement();
install_type_t choose_installation_type();
void show_installation_progress();
void show_completion_screen(int success);


void clear_screen();
void set_console_color(int color);
void gotoxy(int x, int y);
void draw_box(int x, int y, int width, int height);
void print_centered(const char *text, int width);
void show_progress_bar(int progress, int total);
int get_user_choice(const char *options[], int count);
void wait_for_keypress();
void show_ascii_logo();

#endif
