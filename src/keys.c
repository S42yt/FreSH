/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "keys.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "table.h"
#include "term.h"

typedef struct {
    const char *name;
    int code;
} KeyName;

static const KeyName KEYS[] = {
    {"tab", KEY_TAB},
    {"enter", KEY_ENTER},
    {"escape", KEY_ESC},
    {"backspace", KEY_BACKSPACE},
    {"delete", KEY_DELETE},
    {"up", KEY_UP},
    {"down", KEY_DOWN},
    {"left", KEY_LEFT},
    {"right", KEY_RIGHT},
    {"home", KEY_HOME},
    {"end", KEY_END},
    {"pageup", KEY_PAGE_UP},
    {"pagedown", KEY_PAGE_DOWN},
    {"shift+tab", KEY_SHIFT_TAB},
    {"ctrl+left", KEY_WORD_LEFT},
    {"ctrl+right", KEY_WORD_RIGHT},
    {"ctrl+delete", KEY_WORD_DELETE},
    {"f1", KEY_F1},   {"f2", KEY_F2},   {"f3", KEY_F3},   {"f4", KEY_F4},
    {"f5", KEY_F5},   {"f6", KEY_F6},   {"f7", KEY_F7},   {"f8", KEY_F8},
    {"f9", KEY_F9},   {"f10", KEY_F10}, {"f11", KEY_F11}, {"f12", KEY_F12},
};

static const char *WIDGETS[] = {
    "accept-line",      "beginning-of-line", "end-of-line",      "backward-char",
    "forward-char",     "backward-word",     "forward-word",     "delete-char",
    "backward-delete",  "delete-word",       "backward-kill-word", "kill-line",
    "kill-to-start",    "clear-screen",      "history-previous",  "history-next",
    "history-search",   "complete",          "accept-suggestion", "cancel-line",
    NULL,
};

static Table bindings;

int key_code_from_name(const char *name) {
    for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        if (_stricmp(KEYS[i].name, name) == 0) return KEYS[i].code;
    }

    if (_strnicmp(name, "ctrl+", 5) == 0 && name[5] && !name[6]) {
        char letter = (char)toupper((unsigned char)name[5]);
        if (letter >= 'A' && letter <= 'Z') return letter - 'A' + 1;
    }
    return 0;
}

const char *key_name_from_code(int code) {
    static char buffer[16];

    for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        if (KEYS[i].code == code) return KEYS[i].name;
    }
    if (code >= 1 && code <= 26) {
        snprintf(buffer, sizeof(buffer), "ctrl+%c", 'a' + code - 1);
        return buffer;
    }
    return NULL;
}

int widget_known(const char *name) {
    for (int i = 0; WIDGETS[i]; i++) {
        if (strcmp(WIDGETS[i], name) == 0) return 1;
    }
    return 0;
}

void widget_names(StrList *out) {
    for (int i = 0; WIDGETS[i]; i++) sl_push_copy(out, WIDGETS[i]);
}

static void binding_ready(void) {
    if (!bindings.buckets) table_init(&bindings, 32, 0, free);
}

void bind_set(const char *key, const char *action) {
    binding_ready();
    table_put(&bindings, key, xstrdup(action));
}

int bind_remove(const char *key) {
    return table_remove(&bindings, key);
}

const char *bind_lookup(int code, BindKind *kind) {
    const char *name = key_name_from_code(code);
    if (!name) return NULL;

    const char *action = table_get(&bindings, name);
    if (!action) return NULL;

    if (widget_known(action)) *kind = BIND_WIDGET;
    else if (strncmp(action, "insert:", 7) == 0) *kind = BIND_INSERT;
    else *kind = BIND_RUN;
    return action;
}

void bind_list(StrList *out) {
    StrList names;
    sl_init(&names);
    table_names(&bindings, &names);
    sl_sort(&names);

    for (size_t i = 0; i < names.len; i++) {
        StrBuf row;
        sb_init(&row);
        sb_printf(&row, "%-14s %s", names.items[i], (const char *)table_get(&bindings, names.items[i]));
        sl_push(out, sb_take(&row));
    }
    sl_free(&names);
}

void bind_cleanup(void) {
    table_free(&bindings);
}
