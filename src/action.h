/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_ACTION_H
#define FRESH_ACTION_H

#include "parser.h"
#include "util.h"

int action_start(const char *name, Node *body, const StrList *args);
int action_adopt(Node *body);
void action_note_function(const char *name);

void actions_begin(void);
void actions_statement(const Node *node, int status);
void actions_after_line(const char *line, int status);
void actions_before_prompt(void);
int actions_idle_waiting(void);
int actions_idle_due(void);
void actions_idle_fire(void);
void actions_load_folder(void);

int builtin_yield(int argc, char **argv);
int builtin_action(int argc, char **argv);

#endif
