/*
 * Copyright (c) 2025 Musa
 * FRESH - Feature-Rich Enhanced Shell
 * MIT License - See LICENSE file for details
 */

#ifndef BUILTINS_H
#define BUILTINS_H

int handle_builtin(char *cmd);
void add_alias(const char *alias_def);
void expand_aliases(char *cmd);
void show_help();
void clear_screen();

int builtin_pwd();
int builtin_echo(char *args);
int builtin_which(char *cmd);
int builtin_env();
int builtin_export(char *args);
int builtin_history();
int builtin_ls(char *path);
int builtin_cat(char *filename);
int builtin_mkdir(char *dirname);
int builtin_rm(char *filename);
int builtin_cp(char *src, char *dest);
int builtin_mv(char *src, char *dest);
int builtin_grep(char *pattern, char *filename);
int builtin_find(char *path, char *pattern);
int builtin_head(char *filename);
int builtin_tail(char *filename);
int builtin_wc(char *filename);

#endif // BUILTINS_H
