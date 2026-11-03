/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "vars.h"

#include "exec.h"
#include "expand.h"
#include "shell.h"
#include "table.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "platform.h"

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
    char letter_case;
    int used;
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

static Table var_index;

static size_t *free_slots = NULL;
static size_t free_count = 0;
static size_t free_cap = 0;

static void index_ready(void) {
    if (!var_index.buckets) table_init(&var_index, 128, 0, NULL);
}

static void index_rebuild(void) {
    table_free(&var_index);
    table_init(&var_index, 128, 0, NULL);
    for (size_t i = 0; i < var_count_total; i++) {
        if (vars[i].used) table_put(&var_index, vars[i].name, (void *)(i + 1));
    }
    free_count = 0;
    for (size_t i = 0; i < var_count_total; i++) {
        if (vars[i].used) continue;
        if (free_count + 1 >= free_cap) {
            free_cap = free_cap ? free_cap * 2 : 16;
            free_slots = xrealloc(free_slots, free_cap * sizeof(size_t));
        }
        free_slots[free_count++] = i;
    }
}

static Entry *find(const char *name) {
    index_ready();
    size_t slot = (size_t)table_get(&var_index, name);
    return slot ? &vars[slot - 1] : NULL;
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

static Entry *add(const char *name) {
    index_ready();

    size_t slot;
    if (free_count) {
        slot = free_slots[--free_count];
    } else {
        if (var_count_total + 1 >= var_cap) {
            var_cap = var_cap ? var_cap * 2 : 16;
            vars = xrealloc(vars, var_cap * sizeof(Entry));
        }
        slot = var_count_total++;
    }

    Entry *e = &vars[slot];
    memset(e, 0, sizeof(Entry));
    e->name = xstrdup(name);
    e->kind = VAR_SCALAR;
    e->used = 1;
    sl_init(&e->keys);
    sl_init(&e->values);

    table_put(&var_index, name, (void *)(slot + 1));
    return e;
}

static void release_slot(Entry *e) {
    size_t slot = (size_t)(e - vars);
    table_remove(&var_index, e->name);
    e->used = 0;

    if (free_count + 1 >= free_cap) {
        free_cap = free_cap ? free_cap * 2 : 16;
        free_slots = xrealloc(free_slots, free_cap * sizeof(size_t));
    }
    free_slots[free_count++] = slot;
}

static void remove_entry(Entry *e) {
    release_slot(e);
    entry_free(e);
    memset(e, 0, sizeof(Entry));
}

static Table env_table;
static int env_loaded = 0;

static void env_ready(void) {
    if (env_loaded) return;
    env_loaded = 1;
    table_init(&env_table, 128, 1, free);

    char *block = GetEnvironmentStringsA();
    for (char *entry = block; entry && *entry; entry += strlen(entry) + 1) {
        const char *equals = strchr(entry, '=');
        if (!equals || equals == entry) continue;
        char *name = xstrndup(entry, (size_t)(equals - entry));
        table_put(&env_table, name, xstrdup(equals + 1));
        free(name);
    }
    if (block) FreeEnvironmentStringsA(block);
}

static const char *env_value(const char *name) {
    env_ready();
    return table_get(&env_table, name);
}

static void apply_export(const char *name, const char *value) {
    SetEnvironmentVariableA(name, value);
    _putenv_s(name, value ? value : "");

    if (!env_loaded) return;
    if (value) table_put(&env_table, name, xstrdup(value));
    else table_remove(&env_table, name);
}

void vars_init(void) {
    char cwd[PATH_BUF];
    if (GetCurrentDirectoryA(sizeof(cwd), cwd)) {
        path_to_slashes(cwd);
        var_set_exported("PWD", cwd);
    }
    if (!getenv("HOME")) var_set_exported("HOME", home_dir());
    var_set("COMP_WORDBREAKS", " \t\n\"'><=;|&(");
    var_unexport("_");
    if (!getenv("SHELL")) {
        char exe[PATH_BUF];
        if (GetModuleFileNameA(NULL, exe, sizeof(exe))) var_set_exported("SHELL", exe);
    }
}

void vars_cleanup(void) {
    for (size_t i = 0; i < var_count_total; i++) {
        if (vars[i].used) entry_free(&vars[i]);
    }
    free(vars);
    vars = NULL;
    var_count_total = var_cap = 0;

    table_free(&var_index);
    free(free_slots);
    free_slots = NULL;
    free_count = free_cap = 0;

    table_free(&env_table);
    env_loaded = 0;

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
    Entry *e = find(name);
    if (!e) e = add(name);
    e->nameref = 1;
}

static const char *follow_nameref(const char *name, int depth) {
    if (depth > 8) return name;
    Entry *e = find(name);
    if (!e || !e->nameref || !e->value || !*e->value) return name;
    return follow_nameref(e->value, depth + 1);
}

const char *var_get(const char *name) {
    if (name[0] == 'P' && strcmp(name, "PATH") == 0) path_ensure_environment();
    name = follow_nameref(name, 0);

    Entry *e = find(name);
    if (e) {
        if (e->kind != VAR_SCALAR) return e->values.len > 0 ? e->values.items[0] : "";
        return e->value;
    }
    const char *generated = dynamic_value(name);
    if (generated) return generated;

    const char *env = env_value(name);
    return env ? env : NULL;
}

int var_exists(const char *name) {
    Entry *e = find(name);
    if (e) return e->kind != VAR_SCALAR || e->value != NULL;
    return dynamic_value(name) != NULL || env_value(name) != NULL;
}

static char *cased_copy(const Entry *e, const char *value) {
    char *copy = xstrdup(value ? value : "");
    for (char *p = copy; e->letter_case && *p; p++)
        *p = e->letter_case == 'u' ? (char)toupper((unsigned char)*p) : (char)tolower((unsigned char)*p);
    return copy;
}

int var_set(const char *name, const char *value) {
    name = follow_nameref(name, 0);

    Entry *existing = find(name);
    if (existing && existing->readonly) {
        shell_error("%s: is read only", name);
        return 1;
    }

    Entry *e = existing;
    if (!e) {
        e = add(name);
        e->exported = env_value(name) != NULL;
    }
    entry_reset(e);
    e->kind = VAR_SCALAR;
    e->value = cased_copy(e, value);
    if (e->exported) apply_export(name, e->value);
    return 0;
}

void var_set_exported(const char *name, const char *value) {
    Entry *e = find(name);
    if (!e) e = add(name);
    entry_reset(e);
    e->kind = VAR_SCALAR;
    e->value = xstrdup(value ? value : "");
    e->exported = 1;
    apply_export(name, e->value);
}

void var_export(const char *name) {
    Entry *e = find(name);
    if (!e) {
        const char *env = env_value(name);
        e = add(name);
        e->value = env ? xstrdup(env) : NULL;
    }
    e->exported = 1;
    if (e->value) apply_export(name, e->value);
}

void var_unexport(const char *name) {
    Entry *e = find(name);
    if (!e && env_value(name)) {
        e = add(name);
        e->value = xstrdup(env_value(name));
    }
    if (!e) return;
    e->exported = 0;
    apply_export(name, NULL);
}

void var_attribute(const char *name, char letter, int on) {
    Entry *e = find(name);
    if (!e && !on) return;
    if (!e) e = add(name);
    if (letter == 'i') e->integer = on;
    else if (letter == 'n') e->nameref = on;
    else if (letter == 'r') e->readonly = 1;
    else if (letter == 'l' || letter == 'u') e->letter_case = on ? letter : 0;
}

int var_valid_name(const char *name, int subscript) {
    const char *p = name;
    if (!isalpha((unsigned char)*p) && *p != '_') return 0;
    while (isalnum((unsigned char)*p) || *p == '_') p++;
    if (subscript && *p == '[' && p[1] && name[strlen(name) - 1] == ']') return 1;
    return *p == '\0';
}

int var_assign(const char *target, const char *value) {
    const char *open = strchr(target, '[');
    if (!open) return var_set(target, value);
    char *base = xstrndup(target, (size_t)(open - target));
    char *index = xstrndup(open + 1, strlen(open + 1) - 1);
    char *resolved = expand_single(index);
    var_set_element(base, resolved, value);
    free(resolved);
    free(index);
    free(base);
    return 0;
}

int var_is_exported(const char *name) {
    Entry *e = find(name);
    if (e) return e->exported;
    return env_value(name) != NULL;
}

void var_unset(const char *name) {
    Entry *e = find(name);
    if (e) {
        int was_exported = e->exported;
        remove_entry(e);
        if (was_exported) apply_export(name, NULL);
    } else if (env_value(name)) {
        apply_export(name, NULL);
    }
}

void var_declare(const char *name, VarKind kind) {
    Entry *e = find(name);
    if (!e) e = add(name);
    if (e->kind != kind) {
        entry_reset(e);
        e->kind = kind;
    }
}

VarKind var_kind(const char *name) {
    Entry *e = find(name);
    return e ? e->kind : VAR_SCALAR;
}

void var_set_array(const char *name, const StrList *values, VarKind kind) {
    Entry *e = find(name);
    if (!e) e = add(name);
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

static const char *indexed_key(Entry *e, const char *index, char *buffer, size_t size) {
    if (e->kind == VAR_ASSOC) return index;
    const char *digits = index[0] == '-' ? index + 1 : index;
    int numeric = *digits != '\0';
    for (const char *p = digits; *p; p++) {
        if (!isdigit((unsigned char)*p)) numeric = 0;
    }
    int ok = 1;
    long n = numeric ? atol(index) : eval_arith(index, &ok);
    if (n < 0) n += (long)e->values.len;
    snprintf(buffer, size, "%ld", n);
    return buffer;
}

static int index_position(Entry *e, const char *index) {
    char resolved[24];
    index = indexed_key(e, index, resolved, sizeof(resolved));
    for (size_t i = 0; i < e->keys.len; i++) {
        if (strcmp(e->keys.items[i], index) == 0) return (int)i;
    }
    return -1;
}

void var_set_element(const char *name, const char *index, const char *value) {
    Entry *e = find(name);
    if (e && e->readonly) {
        shell_error("%s: is read only", name);
        return;
    }
    if (!e) {
        e = add(name);
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
        e->values.items[position] = cased_copy(e, value);
        return;
    }
    char resolved[24];
    sl_push_copy(&e->keys, indexed_key(e, index, resolved, sizeof(resolved)));
    sl_push(&e->values, cased_copy(e, value));
}

int var_unset_element(const char *name, const char *index) {
    Entry *e = find(name);
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
    Entry *e = find(name);
    if (!e) e = add(name);
    e->readonly = 1;
}

int var_is_nameref(const char *name) {
    Entry *e = find(name);
    return e ? e->nameref : 0;
}

int var_is_readonly(const char *name) {
    Entry *e = find(name);
    return e ? e->readonly : 0;
}

void vars_readonly_names(StrList *out) {
    for (size_t i = 0; i < var_count_total; i++)
        if (vars[i].used && vars[i].readonly) sl_push_copy(out, vars[i].name);
}

void vars_exported_names(StrList *out) {
    for (size_t i = 0; i < var_count_total; i++)
        if (vars[i].used && vars[i].exported) sl_push_copy(out, vars[i].name);

    env_ready();
    StrList names;
    sl_init(&names);
    table_names(&env_table, &names);
    for (size_t i = 0; i < names.len; i++)
        if (!find(names.items[i])) sl_push_copy(out, names.items[i]);
    sl_free(&names);
}

static void put_double_quoted(StrBuf *out, const char *text) {
    sb_putc(out, '"');
    for (const char *p = text; *p; p++) {
        if (strchr("\\\"$`", *p)) sb_putc(out, '\\');
        sb_putc(out, *p);
    }
    sb_putc(out, '"');
}

int var_describe(const char *name, StrBuf *out) {
    Entry *e = find(name);
    const char *env = e ? NULL : env_value(name);
    if (!e && !env) return 0;

    char flags[12];
    size_t count = 0;
    if (e) {
        if (e->kind == VAR_INDEXED) flags[count++] = 'a';
        if (e->kind == VAR_ASSOC) flags[count++] = 'A';
        if (e->integer) flags[count++] = 'i';
        if (e->nameref) flags[count++] = 'n';
        if (e->readonly) flags[count++] = 'r';
    }
    if (!e || e->exported) flags[count++] = 'x';
    if (e && e->letter_case) flags[count++] = e->letter_case;
    flags[count] = '\0';

    sb_printf(out, "declare -%s %s", count ? flags : "-", name);
    if (!e) {
        sb_putc(out, '=');
        put_double_quoted(out, env);
    } else if (e->kind == VAR_SCALAR) {
        if (e->value) {
            sb_putc(out, '=');
            put_double_quoted(out, e->value);
        }
    } else {
        sb_puts(out, "=(");
        for (size_t i = 0; i < e->values.len; i++) {
            if (i > 0) sb_putc(out, ' ');
            sb_printf(out, "[%s]=", e->keys.items[i]);
            put_double_quoted(out, e->values.items[i]);
        }
        if (e->kind == VAR_ASSOC && e->values.len > 0) sb_putc(out, ' ');
        sb_putc(out, ')');
    }
    return 1;
}

void var_mark_integer(const char *name) {
    Entry *e = find(name);
    if (!e) e = add(name);
    e->integer = 1;
}

int var_is_integer(const char *name) {
    Entry *e = find(name);
    return e ? e->integer : 0;
}

const char *var_get_element(const char *name, const char *index) {
    Entry *e = find(name);
    if (!e) return NULL;
    if (e->kind == VAR_SCALAR) {
        char resolved[24];
        return strcmp(indexed_key(e, index, resolved, sizeof(resolved)), "0") == 0 ? e->value : NULL;
    }

    int position = index_position(e, index);
    return position >= 0 ? e->values.items[position] : NULL;
}

void var_values(const char *name, StrList *out) {
    Entry *e = find(name);
    if (!e) {
        const char *env = env_value(name);
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
    Entry *e = find(name);
    if (!e) return;
    if (e->kind == VAR_SCALAR) {
        if (e->value) sl_push_copy(out, "0");
        return;
    }
    for (size_t i = 0; i < e->keys.len; i++) sl_push_copy(out, e->keys.items[i]);
}

int var_count(const char *name) {
    Entry *e = find(name);
    if (!e) return env_value(name) ? 1 : 0;
    if (e->kind == VAR_SCALAR) return e->value && *e->value ? 1 : 0;
    return (int)e->values.len;
}

void var_append(const char *name, const char *value) {
    Entry *e = find(name);
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
        if (!vars[i].used) continue;
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
        if (!find(name)) sl_push_copy(out, entry);
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
    e.readonly = src->readonly;
    e.nameref = src->nameref;
    e.letter_case = src->letter_case;
    e.used = 1;
    sl_init(&e.keys);
    sl_init(&e.values);
    for (size_t i = 0; i < src->keys.len; i++) sl_push_copy(&e.keys, src->keys.items[i]);
    for (size_t i = 0; i < src->values.len; i++) sl_push_copy(&e.values, src->values.items[i]);
    return e;
}

void *vars_snapshot(void) {
    Snapshot *snap = xmalloc(sizeof(Snapshot));
    snap->count = 0;
    snap->entries = var_count_total ? xmalloc(var_count_total * sizeof(Entry)) : NULL;
    for (size_t i = 0; i < var_count_total; i++) {
        if (vars[i].used) snap->entries[snap->count++] = entry_copy(&vars[i]);
    }
    return snap;
}

void vars_restore(void *handle) {
    Snapshot *snap = handle;
    for (size_t i = 0; i < var_count_total; i++) {
        if (vars[i].used) entry_free(&vars[i]);
    }
    free(vars);
    vars = snap->entries;
    var_count_total = snap->count;
    var_cap = snap->count;
    free(snap);
    index_rebuild();
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
        Entry *current = find(saved->name);
        if (current) remove_entry(current);

        if (saved->existed) {
            Entry *restored = add(saved->name);
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
    Entry *current = find(name);

    if (current) {
        saved->existed = 1;
        saved->saved = *current;
        release_slot(current);
        memset(current, 0, sizeof(Entry));
    } else {
        saved->existed = 0;
        memset(&saved->saved, 0, sizeof(Entry));
        sl_init(&saved->saved.keys);
        sl_init(&saved->saved.values);
    }

    add(name);
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
    return alias_table.buckets && table_remove(&alias_table, name);
}

void alias_clear(void) {
    if (alias_table.buckets) table_free(&alias_table);
    memset(&alias_table, 0, sizeof(alias_table));
}

void alias_list(StrList *out) {
    StrList names;
    sl_init(&names);
    table_names(&alias_table, &names);

    for (size_t i = 0; i < names.len; i++) {
        StrBuf sb;
        sb_init(&sb);
        sb_printf(&sb, "%s=", names.items[i]);
        sb_put_quoted(&sb, (const char *)table_get(&alias_table, names.items[i]));
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
