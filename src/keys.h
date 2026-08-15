/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_KEYS_H
#define FRESH_KEYS_H

#include "util.h"

typedef enum {
    BIND_WIDGET,
    BIND_RUN,
    BIND_INSERT
} BindKind;

int key_code_from_name(const char *name);
const char *key_name_from_code(int code);

void bind_set(const char *key, const char *action);
int bind_remove(const char *key);
const char *bind_lookup(int code, BindKind *kind);
void bind_list(StrList *out);
void bind_cleanup(void);

void widget_names(StrList *out);
int widget_known(const char *name);

#endif
