/*
 * Copyright (c) 2025 Musa
 * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#ifndef BUILTINS_H
#define BUILTINS_H

#include <windows.h>

int handle_builtin(char *cmd);
void handle_ls();
void handle_pwd();
void handle_clear();
void handle_help();
void handle_history();
void handle_which(const char *command);
void handle_echo(const char *text);
void handle_shinfo();
void handle_gitinfo();
void handle_gitconfig(const char *args);

#endif //BUILTINS_H
