/*
 * Copyright (c) 2025 Musa/S42
  * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#ifndef HISTORY_H
#define HISTORY_H

#define HISTORY_MAX 100
#define MAX_CMD 1024

void history_add(const char *cmd);
const char *history_get(int index);
int history_count();
void history_load();
void history_save();

#endif // HISTORY_H
