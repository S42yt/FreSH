/*
* Copyright (c) 2025 Musa/S42
 * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#ifndef SHELL_PROMPT_H
#define SHELL_PROMPT_H

#include <windows.h>

#define MAX_PATH 260

void prompt_init();
void print_shell_prompt();
void prompt_cleanup();
void set_prompt_color(int color);

#endif //SHELL_PROMPT_H
