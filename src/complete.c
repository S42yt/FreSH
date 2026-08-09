/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "complete.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "builtins.h"
#include "coreutils.h"
#include "exec.h"
#include "vars.h"

static size_t token_start(const char *buffer, size_t cursor) {
    size_t start = 0;
    char quote = 0;

    for (size_t i = 0; i < cursor; i++) {
        char c = buffer[i];
        if (quote) {
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            continue;
        }
        if (strchr(" \t|&;<>(", c)) start = i + 1;
    }
    return start;
}

static int at_command_position(const char *buffer, size_t start) {
    for (size_t i = start; i > 0; i--) {
        char c = buffer[i - 1];
        if (c == ' ' || c == '\t') continue;
        return c == '|' || c == '&' || c == ';' || c == '(';
    }
    return 1;
}

static void push_unique(StrList *list, const char *value) {
    if (!sl_contains(list, value)) sl_push_copy(list, value);
}

static void complete_variables(const char *token, StrList *out) {
    const char *prefix = token + 1;
    int braced = *prefix == '{';
    if (braced) prefix++;
    size_t prefix_len = strlen(prefix);

    StrList names;
    sl_init(&names);
    vars_list(&names);
    for (size_t i = 0; i < names.len; i++) {
        char *eq = strchr(names.items[i], '=');
        if (eq) *eq = '\0';
        if (strncmp(names.items[i], prefix, prefix_len) == 0) {
            StrBuf sb;
            sb_init(&sb);
            sb_printf(&sb, braced ? "${%s}" : "$%s", names.items[i]);
            push_unique(out, sb.data);
            sb_free(&sb);
        }
    }
    sl_free(&names);
}

static void complete_commands(const char *token, StrList *out) {
    size_t token_len = strlen(token);

    StrList names;
    sl_init(&names);
    builtin_names(&names);
    coreutil_names(&names);
    function_names(&names);
    alias_list(&names);
    path_commands(&names);

    for (size_t i = 0; i < names.len; i++) {
        char *quote = strchr(names.items[i], '=');
        if (quote) *quote = '\0';
        if (strncmp(names.items[i], token, token_len) == 0) push_unique(out, names.items[i]);
    }
    sl_free(&names);
    sl_sort(out);
}

static char *command_word(const char *buffer, size_t start) {
    size_t begin = start;
    while (begin > 0) {
        char c = buffer[begin - 1];
        if (c == '|' || c == ';' || c == '&' || c == '(') break;
        begin--;
    }
    while (buffer[begin] == ' ' || buffer[begin] == '\t') begin++;
    size_t end = begin;
    while (buffer[end] && !isspace((unsigned char)buffer[end])) end++;
    return xstrndup(buffer + begin, end - begin);
}

static int wants_directories(const char *command) {
    return strcmp(command, "cd") == 0 || strcmp(command, "rmdir") == 0 ||
           strcmp(command, "pushd") == 0 || strcmp(command, "mkdir") == 0;
}

static void complete_paths(const char *token, StrList *out, int directories_only) {
    char expanded[PATH_BUF];
    const char *home = var_get("HOME");
    if (token[0] == '~' && (token[1] == '/' || token[1] == '\\' || token[1] == '\0'))
        snprintf(expanded, sizeof(expanded), "%s%s", home ? home : home_dir(), token + 1);
    else snprintf(expanded, sizeof(expanded), "%s", token);

    char native[PATH_BUF];
    snprintf(native, sizeof(native), "%s", expanded);
    path_to_backslashes(native);

    char directory[PATH_BUF] = "";
    const char *leaf = native;
    char *slash = strrchr(native, '\\');
    if (slash) {
        size_t length = (size_t)(slash - native) + 1;
        memcpy(directory, native, length);
        directory[length] = '\0';
        leaf = slash + 1;
    }

    char pattern[PATH_BUF];
    snprintf(pattern, sizeof(pattern), "%s%s*", directory, leaf);

    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return;

    const char *token_slash = strpbrk(token, "/\\");
    const char *token_last = NULL;
    while (token_slash) {
        token_last = token_slash;
        token_slash = strpbrk(token_slash + 1, "/\\");
    }
    size_t prefix_length = token_last ? (size_t)(token_last - token) + 1 : 0;
    int use_slashes = strchr(token, '/') != NULL || token[0] == '~';

    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        if (data.cFileName[0] == '.' && leaf[0] != '.') continue;
        if (directories_only && !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;

        StrBuf sb;
        sb_init(&sb);
        sb_putn(&sb, token, prefix_length);
        sb_puts(&sb, data.cFileName);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) sb_putc(&sb, use_slashes ? '/' : '\\');
        if (use_slashes) path_to_slashes(sb.data);
        push_unique(out, sb.data);
        sb_free(&sb);
    } while (FindNextFileA(find, &data));
    FindClose(find);
    sl_sort(out);
}

void complete_at(const char *buffer, size_t cursor, StrList *matches, size_t *replace_start) {
    size_t start = token_start(buffer, cursor);
    *replace_start = start;

    char *raw = xstrndup(buffer + start, cursor - start);
    char *token = raw;
    if (*token == '"' || *token == '\'') token++;

    if (token[0] == '$') {
        complete_variables(token, matches);
    } else if (at_command_position(buffer, start) && !strpbrk(token, "/\\.")) {
        complete_commands(token, matches);
        complete_paths(token, matches, 0);
    } else {
        char *command = command_word(buffer, start);
        complete_paths(token, matches, wants_directories(command));
        free(command);
    }
    free(raw);
}
