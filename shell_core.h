/*
* Copyright (c) 2025 Musa/S42
 * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */


#ifndef SHELL_CORE_H
#define SHELL_CORE_H

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_CMD 1024
#define MAX_PATH_LEN 512
#define MAX_ARGS 64

void shell_init();
int shell_execute(const char *cmd);
void shell_cleanup();
int is_shell_running();

#endif //SHELL_CORE_H
