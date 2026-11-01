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
int exec_subshell(const char *text);
void exec_keep_redirections(void);
int exec_node(Node *node);
int exec_script_file(const char *path, const StrList *args);
int exec_import(const char *path, const StrList *names);
int capture_command(const char *command, StrBuf *out);

int resolve_command(const char *name, char *out, size_t out_size);
void path_commands(StrList *out);
int path_command_exists(const char *name);
int path_command_prefix(const char *prefix);
void path_command_complete(const char *prefix, StrList *out);
void function_complete(const char *prefix, size_t length, StrList *out);
void path_rehash(void);
void path_reload_environment(void);
void path_ensure_environment(void);

void function_define(const char *name, Node *body);
int function_defined(const char *name);
int function_undefine(const char *name);
void function_names(StrList *out);
int function_name_prefix(const char *prefix, size_t length);

char *apply_aliases(const char *line);
int pattern_match(const char *pattern, const char *text);

int jobs_lookup(const char *who, const char *spec);
int jobs_find_pid(unsigned long pid);
int jobs_next(int slot, int state);
long jobs_pid(int slot);
void jobs_report(int slot, int format);
void jobs_sweep(void);
void jobs_notify(void);
void jobs_refresh(void);
int jobs_wait(int slot);
int jobs_wait_pid(unsigned long pid);
int jobs_wait_any(const int *slots, int count, unsigned long *pid);
void jobs_wait_all(void);
void jobs_disown(int slot);
int jobs_foreground(int slot);
int jobs_resume(int slot);
int jobs_suspend(int slot);
void jobs_set_stopped(unsigned long pid, int stopped);
int jobs_suspend_pid(unsigned long pid);
int jobs_resume_pid(unsigned long pid);
void jobs_cleanup(void);
void run_trap(const char *command);

#endif
