/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "table.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define TABLE_MIN_SIZE 16

static size_t hash_name(const Table *table, const char *name) {
    size_t value = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        unsigned char c = table->fold_case ? (unsigned char)tolower(*p) : *p;
        value += (value << 5) + c;
    }
    return value & (table->size - 1);
}

static int same_name(const Table *table, const char *a, const char *b) {
    return table->fold_case ? _stricmp(a, b) == 0 : strcmp(a, b) == 0;
}

void table_init(Table *table, size_t size, int fold_case, void (*release)(void *value)) {
    size_t capacity = TABLE_MIN_SIZE;
    while (capacity < size) capacity *= 2;

    table->buckets = xmalloc(capacity * sizeof(TableNode *));
    memset(table->buckets, 0, capacity * sizeof(TableNode *));
    table->size = capacity;
    table->count = 0;
    table->fold_case = fold_case;
    table->release = release;
}

void table_free(Table *table) {
    if (!table->buckets) return;

    for (size_t i = 0; i < table->size; i++) {
        TableNode *node = table->buckets[i];
        while (node) {
            TableNode *next = node->next;
            if (table->release) table->release(node->value);
            free(node->name);
            free(node);
            node = next;
        }
    }
    free(table->buckets);
    table->buckets = NULL;
    table->size = table->count = 0;
}

static void table_grow(Table *table) {
    size_t old_size = table->size;
    TableNode **old_buckets = table->buckets;

    table->size = old_size * 4;
    table->buckets = xmalloc(table->size * sizeof(TableNode *));
    memset(table->buckets, 0, table->size * sizeof(TableNode *));

    for (size_t i = 0; i < old_size; i++) {
        TableNode *node = old_buckets[i];
        while (node) {
            TableNode *next = node->next;
            size_t bucket = hash_name(table, node->name);
            node->next = table->buckets[bucket];
            table->buckets[bucket] = node;
            node = next;
        }
    }
    free(old_buckets);
}

void *table_get(const Table *table, const char *name) {
    if (!table->buckets) return NULL;

    for (TableNode *node = table->buckets[hash_name(table, name)]; node; node = node->next) {
        if (same_name(table, node->name, name)) return node->value;
    }
    return NULL;
}

void table_put(Table *table, const char *name, void *value) {
    size_t bucket = hash_name(table, name);

    for (TableNode *node = table->buckets[bucket]; node; node = node->next) {
        if (same_name(table, node->name, name)) {
            if (table->release) table->release(node->value);
            node->value = value;
            return;
        }
    }

    TableNode *node = xmalloc(sizeof(TableNode));
    node->name = xstrdup(name);
    node->value = value;
    node->next = table->buckets[bucket];
    table->buckets[bucket] = node;

    if (++table->count >= table->size * 2) table_grow(table);
}

int table_remove(Table *table, const char *name) {
    if (!table->buckets) return 0;

    size_t bucket = hash_name(table, name);
    TableNode **slot = &table->buckets[bucket];

    while (*slot) {
        TableNode *node = *slot;
        if (same_name(table, node->name, name)) {
            *slot = node->next;
            if (table->release) table->release(node->value);
            free(node->name);
            free(node);
            table->count--;
            return 1;
        }
        slot = &node->next;
    }
    return 0;
}

int table_has_prefix(const Table *table, const char *prefix, size_t length) {
    if (!table->buckets) return 0;

    for (size_t i = 0; i < table->size; i++) {
        for (TableNode *node = table->buckets[i]; node; node = node->next) {
            if (_strnicmp(node->name, prefix, length) == 0) return 1;
        }
    }
    return 0;
}

void table_names(const Table *table, StrList *out) {
    if (!table->buckets) return;

    for (size_t i = 0; i < table->size; i++) {
        for (TableNode *node = table->buckets[i]; node; node = node->next)
            sl_push_copy(out, node->name);
    }
}

size_t table_count(const Table *table) {
    return table->count;
}
