/*
 * Copyright (c) 2025 Musa
 * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#ifndef BIN_REGISTER_H
#define BIN_REGISTER_H

void register_bin_commands();
int is_bin_command(const char *command);
const char* get_bin_command_path(const char *command);
void list_bin_commands();

#endif // BIN_REGISTER_H