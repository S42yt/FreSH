/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_EXEC_H
#define FRESH_EXEC_H

#include "parser.h"
#include "util.h"

void exec_init(void);
void exec_cleanup(void);

int exec_line(const char *line);
int exec_text(const char *text);
int exec_bypassing_functions(const char *text);
int exec_node(Node *node);
int exec_script_file(const char *path, const StrList *args);
int capture_command(const char *command, StrBuf *out);

int resolve_command(const char *name, char *out, size_t out_size);
void path_commands(StrList *out);
int path_command_exists(const char *name);
int path_command_prefix(const char *prefix);
void path_command_complete(const char *prefix, StrList *out);
void function_complete(const char *prefix, size_t length, StrList *out);
void path_rehash(void);
void path_reload_environment(void);

void function_define(const char *name, Node *body);
int function_defined(const char *name);
int function_undefine(const char *name);
void function_names(StrList *out);
int function_name_prefix(const char *prefix, size_t length);

char *apply_aliases(const char *line);
int pattern_match(const char *pattern, const char *text);

void jobs_list(StrList *out);
int jobs_wait(int id);
int jobs_wait_all(void);
int jobs_foreground(int id);
int jobs_resume(int id);
int jobs_suspend(int id);
void jobs_cleanup(void);
void run_trap(const char *command);

#endif
