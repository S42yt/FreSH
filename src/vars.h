/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_VARS_H
#define FRESH_VARS_H

#include "util.h"

void vars_init(void);
void vars_cleanup(void);

const char *var_get(const char *name);
void var_set(const char *name, const char *value);
void var_set_exported(const char *name, const char *value);
void var_export(const char *name);
void var_unset(const char *name);
void vars_list(StrList *out);
int var_is_exported(const char *name);

void alias_set(const char *name, const char *value);
const char *alias_get(const char *name);
int alias_unset(const char *name);
void alias_list(StrList *out);

#endif
