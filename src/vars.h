/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_VARS_H
#define FRESH_VARS_H

#include "util.h"

typedef enum {
    VAR_SCALAR,
    VAR_INDEXED,
    VAR_ASSOC
} VarKind;

void vars_init(void);
void vars_cleanup(void);

const char *var_get(const char *name);
void var_set(const char *name, const char *value);
void var_set_exported(const char *name, const char *value);
void var_export(const char *name);
void var_unset(const char *name);
void vars_list(StrList *out);
int var_is_exported(const char *name);
int var_exists(const char *name);

void var_declare(const char *name, VarKind kind);
VarKind var_kind(const char *name);
void var_set_array(const char *name, const StrList *values, VarKind kind);
void var_mark_integer(const char *name);
int var_is_integer(const char *name);
void var_set_element(const char *name, const char *index, const char *value);
const char *var_get_element(const char *name, const char *index);
void var_values(const char *name, StrList *out);
void var_keys(const char *name, StrList *out);
int var_count(const char *name);
void var_append(const char *name, const char *value);

void scope_push(void);
void scope_pop(void);

void *vars_snapshot(void);
void vars_restore(void *handle);
void var_make_local(const char *name);

void alias_set(const char *name, const char *value);
const char *alias_get(const char *name);
int alias_unset(const char *name);
void alias_list(StrList *out);
int alias_name_prefix(const char *prefix, size_t length);

#endif
