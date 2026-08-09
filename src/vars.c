/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "vars.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

typedef struct {
    char *name;
    char *value;
    int exported;
} Entry;

static Entry *vars = NULL;
static size_t var_count = 0;
static size_t var_cap = 0;

static Entry *aliases = NULL;
static size_t alias_count = 0;
static size_t alias_cap = 0;

static Entry *table_find(Entry *table, size_t count, const char *name) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(table[i].name, name) == 0) return &table[i];
    }
    return NULL;
}

static Entry *table_add(Entry **table, size_t *count, size_t *cap, const char *name) {
    if (*count + 1 >= *cap) {
        *cap = *cap ? *cap * 2 : 16;
        *table = xrealloc(*table, *cap * sizeof(Entry));
    }
    Entry *e = &(*table)[(*count)++];
    e->name = xstrdup(name);
    e->value = NULL;
    e->exported = 0;
    return e;
}

static void table_remove(Entry *table, size_t *count, Entry *e) {
    free(e->name);
    free(e->value);
    size_t index = (size_t)(e - table);
    memmove(table + index, table + index + 1, (*count - index - 1) * sizeof(Entry));
    (*count)--;
}

void vars_init(void) {
    char cwd[PATH_BUF];
    if (GetCurrentDirectoryA(sizeof(cwd), cwd)) var_set_exported("PWD", cwd);
    if (!getenv("HOME")) var_set_exported("HOME", home_dir());
    if (!getenv("SHELL")) {
        char exe[PATH_BUF];
        if (GetModuleFileNameA(NULL, exe, sizeof(exe))) var_set_exported("SHELL", exe);
    }
}

void vars_cleanup(void) {
    for (size_t i = 0; i < var_count; i++) {
        free(vars[i].name);
        free(vars[i].value);
    }
    free(vars);
    vars = NULL;
    var_count = var_cap = 0;

    for (size_t i = 0; i < alias_count; i++) {
        free(aliases[i].name);
        free(aliases[i].value);
    }
    free(aliases);
    aliases = NULL;
    alias_count = alias_cap = 0;
}

const char *var_get(const char *name) {
    Entry *e = table_find(vars, var_count, name);
    if (e) return e->value ? e->value : "";
    const char *env = getenv(name);
    return env ? env : NULL;
}

static void apply_export(const char *name, const char *value) {
    SetEnvironmentVariableA(name, value);
    _putenv_s(name, value ? value : "");
}

void var_set(const char *name, const char *value) {
    Entry *e = table_find(vars, var_count, name);
    if (!e) {
        e = table_add(&vars, &var_count, &var_cap, name);
        e->exported = getenv(name) != NULL;
    }
    free(e->value);
    e->value = xstrdup(value ? value : "");
    if (e->exported) apply_export(name, e->value);
}

void var_set_exported(const char *name, const char *value) {
    Entry *e = table_find(vars, var_count, name);
    if (!e) e = table_add(&vars, &var_count, &var_cap, name);
    free(e->value);
    e->value = xstrdup(value ? value : "");
    e->exported = 1;
    apply_export(name, e->value);
}

void var_export(const char *name) {
    Entry *e = table_find(vars, var_count, name);
    if (!e) {
        const char *env = getenv(name);
        e = table_add(&vars, &var_count, &var_cap, name);
        e->value = xstrdup(env ? env : "");
    }
    e->exported = 1;
    apply_export(name, e->value ? e->value : "");
}

int var_is_exported(const char *name) {
    Entry *e = table_find(vars, var_count, name);
    if (e) return e->exported;
    return getenv(name) != NULL;
}

void var_unset(const char *name) {
    Entry *e = table_find(vars, var_count, name);
    if (e) {
        int was_exported = e->exported;
        table_remove(vars, &var_count, e);
        if (was_exported) apply_export(name, NULL);
    } else if (getenv(name)) {
        apply_export(name, NULL);
    }
}

void vars_list(StrList *out) {
    for (size_t i = 0; i < var_count; i++) {
        StrBuf sb;
        sb_init(&sb);
        sb_printf(&sb, "%s=%s", vars[i].name, vars[i].value ? vars[i].value : "");
        sl_push(out, sb_take(&sb));
    }
    char *block = GetEnvironmentStringsA();
    for (char *entry = block; entry && *entry; entry += strlen(entry) + 1) {
        char *eq = strchr(entry, '=');
        if (!eq || eq == entry) continue;
        char *name = xstrndup(entry, (size_t)(eq - entry));
        if (!table_find(vars, var_count, name)) sl_push_copy(out, entry);
        free(name);
    }
    if (block) FreeEnvironmentStringsA(block);
    sl_sort(out);
}

void alias_set(const char *name, const char *value) {
    Entry *e = table_find(aliases, alias_count, name);
    if (!e) e = table_add(&aliases, &alias_count, &alias_cap, name);
    free(e->value);
    e->value = xstrdup(value);
}

const char *alias_get(const char *name) {
    Entry *e = table_find(aliases, alias_count, name);
    return e ? e->value : NULL;
}

int alias_unset(const char *name) {
    Entry *e = table_find(aliases, alias_count, name);
    if (!e) return 0;
    table_remove(aliases, &alias_count, e);
    return 1;
}

void alias_list(StrList *out) {
    for (size_t i = 0; i < alias_count; i++) {
        StrBuf sb;
        sb_init(&sb);
        sb_printf(&sb, "%s='%s'", aliases[i].name, aliases[i].value);
        sl_push(out, sb_take(&sb));
    }
    sl_sort(out);
}
