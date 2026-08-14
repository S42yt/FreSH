/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_TABLE_H
#define FRESH_TABLE_H

#include "util.h"

typedef struct TableNode {
    struct TableNode *next;
    char *name;
    void *value;
} TableNode;

typedef struct Table {
    TableNode **buckets;
    size_t size;
    size_t count;
    int fold_case;
    void (*release)(void *value);
} Table;

void table_init(Table *table, size_t size, int fold_case, void (*release)(void *value));
void table_free(Table *table);

void *table_get(const Table *table, const char *name);
void table_put(Table *table, const char *name, void *value);
int table_remove(Table *table, const char *name);

int table_has_prefix(const Table *table, const char *prefix, size_t length);
void table_collect_prefix(const Table *table, const char *prefix, size_t length, StrList *out);
void table_names(const Table *table, StrList *out);
size_t table_count(const Table *table);

#endif
