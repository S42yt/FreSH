/*
 * Copyright (c) 2025 Musa
 * MIT License - See LICENSE file for details
 */

#ifndef BUILTINS_H
#define BUILTINS_H

int handle_builtin(char *cmd);
void add_alias(const char *alias_def);
void expand_aliases(char *cmd);
void show_help();
void clear_screen();

#endif // BUILTINS_H
