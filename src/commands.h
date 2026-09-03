/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_COMMANDS_H
#define FRESH_COMMANDS_H

#include <stdio.h>

#include "builtins.h"
#include "util.h"

typedef struct {
    const char *name;
    BuiltinFn fn;
    const char *usage;
} Command;

extern const Command TEXT_COMMANDS[];
extern const size_t TEXT_COMMAND_COUNT;
extern const Command FILE_COMMANDS[];
extern const size_t FILE_COMMAND_COUNT;
extern const Command SYSTEM_COMMANDS[];
extern const size_t SYSTEM_COMMAND_COUNT;

BuiltinFn coreutil_lookup(const char *name);
int coreutil_preferred(const char *name);
void coreutil_names(StrList *out);
int coreutil_name_prefix(const char *prefix, size_t length);
void coreutil_complete(const char *prefix, size_t length, StrList *out);
const char *coreutil_usage(const char *name);

typedef struct {
    char letter;
    const char *name;
    int takes_value;
} OptSpec;

#define ARGS_MAX 128

typedef struct {
    const char *tool;
    char letters[ARGS_MAX];
    const char *values[ARGS_MAX];
    int count;
    char **operands;
    int operand_count;
    int stop_at_operand;
    int number_shorthand;
    int negative_operands;
} Args;

enum { ARGS_OK, ARGS_ERROR, ARGS_DONE };

int args_parse(int argc, char **argv, const OptSpec *specs, const char *tool, Args *out);
void args_free(Args *args);
int args_has(const Args *args, char letter);
int args_count(const Args *args, char letter);
const char *args_value(const Args *args, char letter);
const char *args_value_at(const Args *args, char letter, int index);
char args_last_of(const Args *args, const char *letters);

void cmd_error(const char *tool, const char *fmt, ...);
void cmd_file_error(const char *tool, const char *path);
FILE *cmd_open(const char *tool, const char *path);
void cmd_close(FILE *f);
int cmd_read_line(FILE *f, StrBuf *out);
int cmd_read_lines(const char *tool, const Args *args, StrList *out);
int cmd_read_all(const char *tool, const Args *args, StrBuf *out);
long long cmd_parse_size(const char *text, int *ok);
long long cmd_parse_number(const char *tool, const char *what, const char *text, int *ok);
const char *cmd_name(const char *path);
int cmd_want_stdin(const Args *args);
int cmd_status_from_errno(void);

#endif
