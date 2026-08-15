/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "vars.h"

#include "shell.h"
#include "table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

typedef struct {
    char *name;
    char *value;
    StrList keys;
    StrList values;
    VarKind kind;
    int exported;
    int integer;
    int readonly;
    int nameref;
} Entry;

typedef struct {
    char *name;
    Entry saved;
    int existed;
} Saved;

typedef struct {
    Saved *items;
    size_t len;
    size_t cap;
} Scope;

static Entry *vars = NULL;
static size_t var_count_total = 0;
static size_t var_cap = 0;

static Table alias_table;

static Scope *scopes = NULL;
static size_t scope_depth = 0;
static size_t scope_cap = 0;

static Entry *find(Entry *table, size_t count, const char *name) {
    char first = name[0];
    for (size_t i = 0; i < count; i++) {
        if (table[i].name[0] == first && strcmp(table[i].name, name) == 0) return &table[i];
    }
    return NULL;
}

static void entry_reset(Entry *e) {
    free(e->value);
    e->value = NULL;
    sl_clear(&e->keys);
    sl_clear(&e->values);
}

static void entry_free(Entry *e) {
    free(e->name);
    free(e->value);
    sl_free(&e->keys);
    sl_free(&e->values);
}

static Entry *add(Entry **table, size_t *count, size_t *cap, const char *name) {
    if (*count + 1 >= *cap) {
        *cap = *cap ? *cap * 2 : 16;
        *table = xrealloc(*table, *cap * sizeof(Entry));
    }
    Entry *e = &(*table)[(*count)++];
    memset(e, 0, sizeof(Entry));
    e->name = xstrdup(name);
    e->kind = VAR_SCALAR;
    sl_init(&e->keys);
    sl_init(&e->values);
    return e;
}

static void remove_entry(Entry *table, size_t *count, Entry *e) {
    entry_free(e);
    size_t index = (size_t)(e - table);
    memmove(table + index, table + index + 1, (*count - index - 1) * sizeof(Entry));
    (*count)--;
}

static void apply_export(const char *name, const char *value) {
    SetEnvironmentVariableA(name, value);
    _putenv_s(name, value ? value : "");
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
    for (size_t i = 0; i < var_count_total; i++) entry_free(&vars[i]);
    free(vars);
    vars = NULL;
    var_count_total = var_cap = 0;

    table_free(&alias_table);

    for (size_t i = 0; i < scope_depth; i++) free(scopes[i].items);
    free(scopes);
    scopes = NULL;
    scope_depth = scope_cap = 0;
}

static const char *dynamic_value(const char *name) {
    static char buffer[32];

    if (strcmp(name, "RANDOM") == 0) {
        static int seeded = 0;
        if (!seeded) {
            srand((unsigned int)GetTickCount() ^ (unsigned int)GetCurrentProcessId());
            seeded = 1;
        }
        snprintf(buffer, sizeof(buffer), "%d", rand() % 32768);
        return buffer;
    }
    if (strcmp(name, "SECONDS") == 0) {
        snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)((GetTickCount() - shell.started) / 1000));
        return buffer;
    }
    if (strcmp(name, "LINENO") == 0) {
        snprintf(buffer, sizeof(buffer), "%d", shell.line);
        return buffer;
    }
    if (strcmp(name, "EPOCHSECONDS") == 0) {
        snprintf(buffer, sizeof(buffer), "%lld", (long long)time(NULL));
        return buffer;
    }
    if (strcmp(name, "EPOCHREALTIME") == 0) {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        unsigned long long ticks =
            ((unsigned long long)now.dwHighDateTime << 32) | now.dwLowDateTime;
        unsigned long long micros = ticks / 10ULL - 11644473600000000ULL;
        snprintf(buffer, sizeof(buffer), "%llu.%06llu", micros / 1000000ULL, micros % 1000000ULL);
        return buffer;
    }
    return NULL;
}

void var_mark_nameref(const char *name) {
    Entry *e = find(vars, var_count_total, name);
    if (!e) e = add(&vars, &var_count_total, &var_cap, name);
    e->nameref = 1;
}

static const char *follow_nameref(const char *name, int depth) {
    if (depth > 8) return name;
    Entry *e = find(vars, var_count_total, name);
    if (!e || !e->nameref || !e->value || !*e->value) return name;
    return follow_nameref(e->value, depth + 1);
}

const char *var_get(const char *name) {
    name = follow_nameref(name, 0);

    Entry *e = find(vars, var_count_total, name);
    if (e) {
        if (e->kind != VAR_SCALAR) return e->values.len > 0 ? e->values.items[0] : "";
        return e->value ? e->value : "";
    }
    const char *generated = dynamic_value(name);
    if (generated) return generated;

    const char *env = getenv(name);
    return env ? env : NULL;
}

int var_exists(const char *name) {
    return find(vars, var_count_total, name) != NULL || dynamic_value(name) != NULL ||
           getenv(name) != NULL;
}

void var_set(const char *name, const char *value) {
    name = follow_nameref(name, 0);

    Entry *existing = find(vars, var_count_total, name);
    if (existing && existing->readonly) {
        shell_error("%s: is read only", name);
        return;
    }

    Entry *e = existing;
    if (!e) {
        e = add(&vars, &var_count_total, &var_cap, name);
        e->exported = getenv(name) != NULL;
    }
    entry_reset(e);
    e->kind = VAR_SCALAR;
    e->value = xstrdup(value ? value : "");
    if (e->exported) apply_export(name, e->value);
}

void var_set_exported(const char *name, const char *value) {
    Entry *e = find(vars, var_count_total, name);
    if (!e) e = add(&vars, &var_count_total, &var_cap, name);
    entry_reset(e);
    e->kind = VAR_SCALAR;
    e->value = xstrdup(value ? value : "");
    e->exported = 1;
    apply_export(name, e->value);
}

void var_export(const char *name) {
    Entry *e = find(vars, var_count_total, name);
    if (!e) {
        const char *env = getenv(name);
        e = add(&vars, &var_count_total, &var_cap, name);
        e->value = xstrdup(env ? env : "");
    }
    e->exported = 1;
    apply_export(name, e->value ? e->value : "");
}

int var_is_exported(const char *name) {
    Entry *e = find(vars, var_count_total, name);
    if (e) return e->exported;
    return getenv(name) != NULL;
}

void var_unset(const char *name) {
    Entry *e = find(vars, var_count_total, name);
    if (e) {
        int was_exported = e->exported;
        remove_entry(vars, &var_count_total, e);
        if (was_exported) apply_export(name, NULL);
    } else if (getenv(name)) {
        apply_export(name, NULL);
    }
}

void var_declare(const char *name, VarKind kind) {
    Entry *e = find(vars, var_count_total, name);
    if (!e) e = add(&vars, &var_count_total, &var_cap, name);
    if (e->kind != kind) {
        entry_reset(e);
        e->kind = kind;
    }
}

VarKind var_kind(const char *name) {
    Entry *e = find(vars, var_count_total, name);
    return e ? e->kind : VAR_SCALAR;
}

void var_set_array(const char *name, const StrList *values, VarKind kind) {
    Entry *e = find(vars, var_count_total, name);
    if (!e) e = add(&vars, &var_count_total, &var_cap, name);
    entry_reset(e);
    e->kind = kind;

    for (size_t i = 0; i < values->len; i++) {
        if (kind == VAR_ASSOC && i + 1 < values->len) {
            sl_push_copy(&e->keys, values->items[i]);
            sl_push_copy(&e->values, values->items[i + 1]);
            i++;
            continue;
        }
        char index[32];
        snprintf(index, sizeof(index), "%zu", e->values.len);
        sl_push_copy(&e->keys, index);
        sl_push_copy(&e->values, values->items[i]);
    }
}

static int index_position(Entry *e, const char *index) {
    char resolved[24];
    if (index[0] == '-' && e->kind == VAR_INDEXED) {
        int n = (int)e->values.len + atoi(index);
        if (n < 0) return -1;
        snprintf(resolved, sizeof(resolved), "%d", n);
        index = resolved;
    }
    for (size_t i = 0; i < e->keys.len; i++) {
        if (strcmp(e->keys.items[i], index) == 0) return (int)i;
    }
    return -1;
}

void var_set_element(const char *name, const char *index, const char *value) {
    Entry *e = find(vars, var_count_total, name);
    if (!e) {
        e = add(&vars, &var_count_total, &var_cap, name);
        e->kind = VAR_INDEXED;
    }
    if (e->kind == VAR_SCALAR) {
        char *previous = e->value;
        e->value = NULL;
        e->kind = VAR_INDEXED;
        if (previous && *previous) {
            sl_push_copy(&e->keys, "0");
            sl_push(&e->values, previous);
        } else {
            free(previous);
        }
    }

    int position = index_position(e, index);
    if (position >= 0) {
        free(e->values.items[position]);
        e->values.items[position] = xstrdup(value);
        return;
    }
    sl_push_copy(&e->keys, index);
    sl_push_copy(&e->values, value);
}

int var_unset_element(const char *name, const char *index) {
    Entry *e = find(vars, var_count_total, name);
    if (!e || e->kind == VAR_SCALAR) return 0;

    int position = index_position(e, index);
    if (position < 0) return 0;

    free(e->keys.items[position]);
    free(e->values.items[position]);
    memmove(e->keys.items + position, e->keys.items + position + 1,
            (e->keys.len - (size_t)position - 1) * sizeof(char *));
    memmove(e->values.items + position, e->values.items + position + 1,
            (e->values.len - (size_t)position - 1) * sizeof(char *));
    e->keys.len--;
    e->values.len--;
    return 1;
}

void var_mark_readonly(const char *name) {
    Entry *e = find(vars, var_count_total, name);
    if (!e) e = add(&vars, &var_count_total, &var_cap, name);
    e->readonly = 1;
}

int var_is_readonly(const char *name) {
    Entry *e = find(vars, var_count_total, name);
    return e ? e->readonly : 0;
}

void vars_readonly_names(StrList *out) {
    for (size_t i = 0; i < var_count_total; i++)
        if (vars[i].readonly) sl_push_copy(out, vars[i].name);
}

void vars_exported_names(StrList *out) {
    for (size_t i = 0; i < var_count_total; i++)
        if (vars[i].exported) sl_push_copy(out, vars[i].name);
}

void var_mark_integer(const char *name) {
    Entry *e = find(vars, var_count_total, name);
    if (!e) e = add(&vars, &var_count_total, &var_cap, name);
    e->integer = 1;
}

int var_is_integer(const char *name) {
    Entry *e = find(vars, var_count_total, name);
    return e ? e->integer : 0;
}

const char *var_get_element(const char *name, const char *index) {
    Entry *e = find(vars, var_count_total, name);
    if (!e) return NULL;
    if (e->kind == VAR_SCALAR) return strcmp(index, "0") == 0 ? e->value : NULL;

    int position = index_position(e, index);
    return position >= 0 ? e->values.items[position] : NULL;
}

void var_values(const char *name, StrList *out) {
    Entry *e = find(vars, var_count_total, name);
    if (!e) {
        const char *env = getenv(name);
        if (env) sl_push_copy(out, env);
        return;
    }
    if (e->kind == VAR_SCALAR) {
        if (e->value) sl_push_copy(out, e->value);
        return;
    }
    for (size_t i = 0; i < e->values.len; i++) sl_push_copy(out, e->values.items[i]);
}

void var_keys(const char *name, StrList *out) {
    Entry *e = find(vars, var_count_total, name);
    if (!e) return;
    if (e->kind == VAR_SCALAR) {
        if (e->value) sl_push_copy(out, "0");
        return;
    }
    for (size_t i = 0; i < e->keys.len; i++) sl_push_copy(out, e->keys.items[i]);
}

int var_count(const char *name) {
    Entry *e = find(vars, var_count_total, name);
    if (!e) return getenv(name) ? 1 : 0;
    if (e->kind == VAR_SCALAR) return e->value && *e->value ? 1 : 0;
    return (int)e->values.len;
}

void var_append(const char *name, const char *value) {
    Entry *e = find(vars, var_count_total, name);
    if (!e || e->kind == VAR_SCALAR) {
        const char *current = var_get(name);
        StrBuf sb;
        sb_init(&sb);
        sb_puts(&sb, current ? current : "");
        sb_puts(&sb, value);
        var_set(name, sb.data);
        sb_free(&sb);
        return;
    }
    char index[32];
    snprintf(index, sizeof(index), "%zu", e->values.len);
    sl_push_copy(&e->keys, index);
    sl_push_copy(&e->values, value);
}

void vars_list(StrList *out) {
    for (size_t i = 0; i < var_count_total; i++) {
        StrBuf sb;
        sb_init(&sb);
        if (vars[i].kind == VAR_SCALAR) {
            sb_printf(&sb, "%s=%s", vars[i].name, vars[i].value ? vars[i].value : "");
        } else {
            sb_printf(&sb, "%s=(", vars[i].name);
            for (size_t v = 0; v < vars[i].values.len; v++) {
                if (v > 0) sb_putc(&sb, ' ');
                if (vars[i].kind == VAR_ASSOC) sb_printf(&sb, "[%s]=", vars[i].keys.items[v]);
                sb_puts(&sb, vars[i].values.items[v]);
            }
            sb_putc(&sb, ')');
        }
        sl_push(out, sb_take(&sb));
    }

    char *block = GetEnvironmentStringsA();
    for (char *entry = block; entry && *entry; entry += strlen(entry) + 1) {
        char *eq = strchr(entry, '=');
        if (!eq || eq == entry) continue;
        char *name = xstrndup(entry, (size_t)(eq - entry));
        if (!find(vars, var_count_total, name)) sl_push_copy(out, entry);
        free(name);
    }
    if (block) FreeEnvironmentStringsA(block);
    sl_sort(out);
}

typedef struct {
    Entry *entries;
    size_t count;
} Snapshot;

static Entry entry_copy(const Entry *src) {
    Entry e;
    memset(&e, 0, sizeof(e));
    e.name = xstrdup(src->name);
    e.value = src->value ? xstrdup(src->value) : NULL;
    e.kind = src->kind;
    e.exported = src->exported;
    e.integer = src->integer;
    sl_init(&e.keys);
    sl_init(&e.values);
    for (size_t i = 0; i < src->keys.len; i++) sl_push_copy(&e.keys, src->keys.items[i]);
    for (size_t i = 0; i < src->values.len; i++) sl_push_copy(&e.values, src->values.items[i]);
    return e;
}

void *vars_snapshot(void) {
    Snapshot *snap = xmalloc(sizeof(Snapshot));
    snap->count = var_count_total;
    snap->entries = var_count_total ? xmalloc(var_count_total * sizeof(Entry)) : NULL;
    for (size_t i = 0; i < var_count_total; i++) snap->entries[i] = entry_copy(&vars[i]);
    return snap;
}

void vars_restore(void *handle) {
    Snapshot *snap = handle;
    for (size_t i = 0; i < var_count_total; i++) entry_free(&vars[i]);
    free(vars);
    vars = snap->entries;
    var_count_total = snap->count;
    var_cap = snap->count;
    free(snap);
}

void scope_push(void) {
    if (scope_depth + 1 >= scope_cap) {
        scope_cap = scope_cap ? scope_cap * 2 : 8;
        scopes = xrealloc(scopes, scope_cap * sizeof(Scope));
    }
    Scope *scope = &scopes[scope_depth++];
    scope->items = NULL;
    scope->len = 0;
    scope->cap = 0;
}

void scope_pop(void) {
    if (scope_depth == 0) return;
    Scope *scope = &scopes[--scope_depth];

    for (size_t i = scope->len; i > 0; i--) {
        Saved *saved = &scope->items[i - 1];
        Entry *current = find(vars, var_count_total, saved->name);
        if (current) remove_entry(vars, &var_count_total, current);

        if (saved->existed) {
            Entry *restored = add(&vars, &var_count_total, &var_cap, saved->name);
            free(restored->name);
            sl_free(&restored->keys);
            sl_free(&restored->values);
            *restored = saved->saved;
        } else {
            entry_free(&saved->saved);
        }
        free(saved->name);
    }
    free(scope->items);
}

void var_make_local(const char *name) {
    if (scope_depth == 0) return;
    Scope *scope = &scopes[scope_depth - 1];

    for (size_t i = 0; i < scope->len; i++) {
        if (strcmp(scope->items[i].name, name) == 0) return;
    }
    if (scope->len + 1 >= scope->cap) {
        scope->cap = scope->cap ? scope->cap * 2 : 8;
        scope->items = xrealloc(scope->items, scope->cap * sizeof(Saved));
    }

    Saved *saved = &scope->items[scope->len++];
    saved->name = xstrdup(name);
    Entry *current = find(vars, var_count_total, name);

    if (current) {
        saved->existed = 1;
        saved->saved = *current;
        size_t index = (size_t)(current - vars);
        memmove(vars + index, vars + index + 1, (var_count_total - index - 1) * sizeof(Entry));
        var_count_total--;
    } else {
        saved->existed = 0;
        memset(&saved->saved, 0, sizeof(Entry));
        sl_init(&saved->saved.keys);
        sl_init(&saved->saved.values);
    }

    Entry *fresh = add(&vars, &var_count_total, &var_cap, name);
    fresh->value = xstrdup("");
}

static void alias_table_ready(void) {
    if (!alias_table.buckets) table_init(&alias_table, 32, 0, free);
}

void alias_set(const char *name, const char *value) {
    alias_table_ready();
    table_put(&alias_table, name, xstrdup(value));
}

const char *alias_get(const char *name) {
    return table_get(&alias_table, name);
}

int alias_unset(const char *name) {
    return table_remove(&alias_table, name);
}

void alias_list(StrList *out) {
    StrList names;
    sl_init(&names);
    table_names(&alias_table, &names);

    for (size_t i = 0; i < names.len; i++) {
        StrBuf sb;
        sb_init(&sb);
        sb_printf(&sb, "%s='%s'", names.items[i], (const char *)table_get(&alias_table, names.items[i]));
        sl_push(out, sb_take(&sb));
    }
    sl_free(&names);
    sl_sort(out);
}

int alias_name_prefix(const char *prefix, size_t length) {
    return table_has_prefix(&alias_table, prefix, length);
}

void alias_complete(const char *prefix, size_t length, StrList *out) {
    table_collect_prefix(&alias_table, prefix, length, out);
}
