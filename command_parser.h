/*
 * Copyright (c) 2025 Musa/S42
  * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

void parser_init();
int execute_external_command(const char *cmd);
void parser_cleanup();

#endif // COMMAND_PARSER_H
