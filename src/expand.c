/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "expand.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "exec.h"
#include "shell.h"
#include "vars.h"

typedef struct {
    StrBuf field;
    int has_content;
    int quoted;
    StrList *out;
    int split;
    int glob;
} Expander;

static const char *param_get(int index) {
    if (index < 0 || (size_t)index >= shell.params.len) return NULL;
    return shell.params.items[index];
}

static void field_flush(Expander *ex) {
    if (!ex->has_content) return;
    char *value = xstrdup(ex->field.data);
    if (ex->glob && !ex->quoted && (strchr(value, '*') || strchr(value, '?'))) {
        if (glob_expand(value, ex->out)) {
            free(value);
        } else {
            sl_push(ex->out, value);
        }
    } else {
        sl_push(ex->out, value);
    }
    sb_clear(&ex->field);
    ex->has_content = 0;
    ex->quoted = 0;
}

static void field_add(Expander *ex, const char *text, size_t len) {
    sb_putn(&ex->field, text, len);
    ex->has_content = 1;
}

static void field_add_split(Expander *ex, const char *text) {
    if (!text) return;
    if (!ex->split) {
        field_add(ex, text, strlen(text));
        return;
    }
    const char *p = text;
    while (*p) {
        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            if (ex->has_content) field_flush(ex);
            p++;
            continue;
        }
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        field_add(ex, start, (size_t)(p - start));
    }
}

static char *read_name(const char **p) {
    const char *start = *p;
    if (!isalpha((unsigned char)**p) && **p != '_') return NULL;
    while (isalnum((unsigned char)**p) || **p == '_') (*p)++;
    return xstrndup(start, (size_t)(*p - start));
}

static char *special_value(char c) {
    StrBuf sb;
    sb_init(&sb);
    switch (c) {
    case '?':
        sb_printf(&sb, "%d", shell.last_status);
        break;
    case '$':
        sb_printf(&sb, "%lu", (unsigned long)GetCurrentProcessId());
        break;
    case '#':
        sb_printf(&sb, "%d", shell.params.len > 0 ? (int)shell.params.len - 1 : 0);
        break;
    case '*':
    case '@':
        for (size_t i = 1; i < shell.params.len; i++) {
            if (i > 1) sb_putc(&sb, ' ');
            sb_puts(&sb, shell.params.items[i]);
        }
        break;
    default:
        if (isdigit((unsigned char)c)) {
            const char *value = param_get(c - '0');
            sb_puts(&sb, value ? value : "");
        }
        break;
    }
    return sb_take(&sb);
}

static int split_subscript(const char *body, char *name, size_t name_size, char *index,
                           size_t index_size) {
    const char *open = strchr(body, '[');
    size_t length = strlen(body);
    if (!open || length == 0 || body[length - 1] != ']') return 0;

    size_t name_length = (size_t)(open - body);
    if (name_length == 0 || name_length >= name_size) return 0;
    memcpy(name, body, name_length);
    name[name_length] = '\0';

    size_t index_length = length - name_length - 2;
    if (index_length >= index_size) return 0;
    memcpy(index, open + 1, index_length);
    index[index_length] = '\0';
    return 1;
}

static int expand_array_body(const char *body, StrList *out) {
    char name[128];
    char index[128];
    const char *source = body;
    int keys_wanted = 0;

    if (*source == '#') return 0;
    if (*source == '!') {
        keys_wanted = 1;
        source++;
    }
    if (!split_subscript(source, name, sizeof(name), index, sizeof(index))) return 0;
    if (strcmp(index, "@") != 0 && strcmp(index, "*") != 0) return 0;

    if (keys_wanted) var_keys(name, out);
    else var_values(name, out);
    return 1;
}

static int match_at(const char *pattern, const char *text, size_t start, size_t limit,
                    int longest, size_t *length) {
    int found = 0;
    size_t best = 0;
    size_t available = strlen(text) - start;
    if (limit < available) available = limit;

    for (size_t take = 0; take <= available; take++) {
        char *piece = xstrndup(text + start, take);
        int matched = pattern_match(pattern, piece);
        free(piece);
        if (!matched) continue;
        found = 1;
        best = take;
        if (!longest) break;
    }
    if (found) *length = best;
    return found;
}

static char *strip_prefix(const char *text, const char *pattern, int longest) {
    size_t length = 0;
    if (match_at(pattern, text, 0, strlen(text), longest, &length)) return xstrdup(text + length);
    return xstrdup(text);
}

static char *strip_suffix(const char *text, const char *pattern, int longest) {
    size_t total = strlen(text);
    size_t best = total;
    int found = 0;

    for (size_t start = 0; start <= total; start++) {
        size_t length = 0;
        if (!match_at(pattern, text, start, total - start, 1, &length)) continue;
        if (start + length != total) continue;
        found = 1;
        best = start;
        if (longest) break;
    }
    if (!found) return xstrdup(text);
    return xstrndup(text, best);
}

static char *replace_pattern(const char *text, const char *pattern, const char *replacement,
                             int all, int anchor_start, int anchor_end) {
    StrBuf out;
    sb_init(&out);
    size_t total = strlen(text);
    size_t cursor = 0;
    int replaced = 0;

    while (cursor <= total) {
        size_t length = 0;
        int matched = 0;

        if (!(replaced && !all) && !(anchor_start && cursor > 0))
            matched = match_at(pattern, text, cursor, total - cursor, 1, &length);
        if (matched && anchor_end && cursor + length != total) matched = 0;

        if (matched && length > 0) {
            sb_puts(&out, replacement);
            cursor += length;
            replaced = 1;
            continue;
        }
        if (cursor == total) break;
        sb_putc(&out, text[cursor]);
        cursor++;
    }
    return sb_take(&out);
}

static char *change_case(const char *text, int upper, int all) {
    char *copy = xstrdup(text);
    for (char *p = copy; *p; p++) {
        *p = upper ? (char)toupper((unsigned char)*p) : (char)tolower((unsigned char)*p);
        if (!all) break;
    }
    return copy;
}

static char *brace_expand(const char *body) {
    char name[128];
    char index[128];

    if (*body == '#' && split_subscript(body + 1, name, sizeof(name), index, sizeof(index)) &&
        (strcmp(index, "@") == 0 || strcmp(index, "*") == 0)) {
        StrBuf sb;
        sb_init(&sb);
        sb_printf(&sb, "%d", var_count(name));
        return sb_take(&sb);
    }

    if (split_subscript(body, name, sizeof(name), index, sizeof(index))) {
        if (strcmp(index, "@") == 0 || strcmp(index, "*") == 0) {
            StrList values;
            sl_init(&values);
            var_values(name, &values);

            StrBuf sb;
            sb_init(&sb);
            for (size_t i = 0; i < values.len; i++) {
                if (i > 0) sb_putc(&sb, ' ');
                sb_puts(&sb, values.items[i]);
            }
            sl_free(&values);
            return sb_take(&sb);
        }
        char *resolved = expand_single(index);
        const char *value = var_get_element(name, resolved);
        free(resolved);
        return xstrdup(value ? value : "");
    }

    if (*body == '#') {
        const char *name = body + 1;
        const char *value = var_get(name);
        StrBuf sb;
        sb_init(&sb);
        sb_printf(&sb, "%d", value ? (int)strlen(value) : 0);
        return sb_take(&sb);
    }

    size_t name_length = 0;
    while (body[name_length] && (isalnum((unsigned char)body[name_length]) ||
                                 body[name_length] == '_'))
        name_length++;

    if (name_length > 0 && name_length < sizeof(name) && body[name_length]) {
        char operator = body[name_length];
        const char *rest = body + name_length + 1;

        if (strchr("#%/^,:", operator)) {
            memcpy(name, body, name_length);
            name[name_length] = '\0';
            const char *value = var_get(name);
            char *text = xstrdup(value ? value : "");

            if (operator == '#' || operator == '%') {
                int longest = *rest == operator;
                if (longest) rest++;
                char *pattern = expand_single(rest);
                char *result = operator == '#' ? strip_prefix(text, pattern, longest)
                                               : strip_suffix(text, pattern, longest);
                free(pattern);
                free(text);
                return result;
            }

            if (operator == '/') {
                int all = *rest == '/';
                int anchor_start = 0;
                int anchor_end = 0;
                if (all) rest++;
                else if (*rest == '#') {
                    anchor_start = 1;
                    rest++;
                } else if (*rest == '%') {
                    anchor_end = 1;
                    rest++;
                }

                const char *slash = rest;
                int depth = 0;
                while (*slash && (*slash != '/' || depth)) {
                    if (*slash == '\\' && slash[1]) slash++;
                    slash++;
                }
                char *pattern_source = xstrndup(rest, (size_t)(slash - rest));
                char *pattern = expand_single(pattern_source);
                char *replacement = *slash ? expand_single(slash + 1) : xstrdup("");

                char *result = replace_pattern(text, pattern, replacement, all, anchor_start,
                                               anchor_end);
                free(pattern_source);
                free(pattern);
                free(replacement);
                free(text);
                return result;
            }

            if (operator == '^' || operator == ',') {
                int all = *rest == operator;
                char *result = change_case(text, operator == '^', all);
                free(text);
                return result;
            }

            if (operator == ':' && (isdigit((unsigned char)*rest) || *rest == ' ')) {
                char *spec = expand_single(rest);
                long offset = atol(spec);
                const char *comma = strchr(spec, ':');
                long total = (long)strlen(text);
                if (offset < 0) offset += total;
                if (offset < 0) offset = 0;
                if (offset > total) offset = total;

                long length = comma ? atol(comma + 1) : total - offset;
                if (length < 0) length = total - offset + length;
                if (length < 0) length = 0;
                if (offset + length > total) length = total - offset;

                char *result = xstrndup(text + offset, (size_t)length);
                free(spec);
                free(text);
                return result;
            }
            free(text);
        }
    }

    const char *colon = strpbrk(body, ":-+=?");
    if (!colon) {
        if (isdigit((unsigned char)body[0])) return special_value(body[0]);
        if (!body[1] && !isalpha((unsigned char)body[0]) && body[0] != '_')
            return special_value(body[0]);
        const char *value = var_get(body);
        return xstrdup(value ? value : "");
    }

    char *var_name = xstrndup(body, (size_t)(colon - body));
    const char *rest = colon;
    int has_colon = 0;
    if (*rest == ':') {
        has_colon = 1;
        rest++;
    }
    char op = *rest ? *rest++ : '\0';
    const char *value = var_get(var_name);
    int empty = !value || (has_colon && !*value);

    char *result = NULL;
    if (op == '-') {
        result = empty ? expand_single(rest) : xstrdup(value);
    } else if (op == '+') {
        result = empty ? xstrdup("") : expand_single(rest);
    } else if (op == '=') {
        if (empty) {
            char *fallback = expand_single(rest);
            var_set(var_name, fallback);
            result = fallback;
        } else {
            result = xstrdup(value);
        }
    } else {
        result = xstrdup(value ? value : "");
    }
    free(var_name);
    return result;
}

static char *capture_trimmed(const char *command) {
    StrBuf out;
    sb_init(&out);
    capture_command(command, &out);
    while (out.len > 0 && (out.data[out.len - 1] == '\n' || out.data[out.len - 1] == '\r'))
        out.data[--out.len] = '\0';
    return sb_take(&out);
}

static const char *scan_balanced(const char *p, char open, char close, StrBuf *inner) {
    int depth = 0;
    for (; *p; p++) {
        if (*p == open) {
            depth++;
            if (depth == 1) continue;
        } else if (*p == close) {
            depth--;
            if (depth == 0) return p + 1;
        }
        if (depth >= 1) sb_putc(inner, *p);
    }
    return p;
}

static void expand_dollar(Expander *ex, const char **p, int in_quotes) {
    (*p)++;
    char c = **p;

    if (c == '(' && (*p)[1] == '(') {
        StrBuf inner;
        sb_init(&inner);
        const char *after = scan_balanced(*p + 1, '(', ')', &inner);
        if (*after == ')') after++;
        *p = after;
        int ok = 1;
        long value = eval_arith(inner.data, &ok);
        sb_free(&inner);
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%ld", value);
        field_add(ex, buffer, strlen(buffer));
        return;
    }
    if (c == '(') {
        StrBuf inner;
        sb_init(&inner);
        *p = scan_balanced(*p, '(', ')', &inner);
        char *result = capture_trimmed(inner.data);
        sb_free(&inner);
        if (in_quotes) field_add(ex, result, strlen(result));
        else field_add_split(ex, result);
        free(result);
        return;
    }
    if (c == '{') {
        StrBuf inner;
        sb_init(&inner);
        *p = scan_balanced(*p, '{', '}', &inner);

        StrList elements;
        sl_init(&elements);
        if (expand_array_body(inner.data, &elements)) {
            for (size_t i = 0; i < elements.len; i++) {
                if (i > 0) field_flush(ex);
                field_add(ex, elements.items[i], strlen(elements.items[i]));
                if (in_quotes) ex->quoted = 1;
            }
            if (elements.len == 0 && in_quotes) ex->has_content = 0;
            sl_free(&elements);
            sb_free(&inner);
            return;
        }
        sl_free(&elements);

        char *result = brace_expand(inner.data);
        sb_free(&inner);
        if (in_quotes) field_add(ex, result, strlen(result));
        else field_add_split(ex, result);
        free(result);
        return;
    }
    if (c == '@' && in_quotes) {
        (*p)++;
        for (size_t i = 1; i < shell.params.len; i++) {
            if (i > 1) field_flush(ex);
            field_add(ex, shell.params.items[i], strlen(shell.params.items[i]));
            ex->quoted = 1;
        }
        if (shell.params.len <= 1) ex->has_content = 1;
        return;
    }
    if (isalpha((unsigned char)c) || c == '_') {
        char *name = read_name(p);
        const char *value = var_get(name);
        if (shell.nounset && !var_exists(name)) {
            shell_error("%s: unbound variable", name);
            shell.running = 0;
        }
        free(name);
        if (in_quotes) field_add(ex, value ? value : "", value ? strlen(value) : 0);
        else field_add_split(ex, value);
        return;
    }
    if (c && strchr("?$#*@0123456789", c)) {
        (*p)++;
        char *value = special_value(c);
        if (in_quotes) field_add(ex, value, strlen(value));
        else field_add_split(ex, value);
        free(value);
        return;
    }
    field_add(ex, "$", 1);
}

static void expand_tilde(Expander *ex, const char **p) {
    const char *next = *p + 1;

    if (isalnum((unsigned char)*next)) {
        const char *end = next;
        while (*end && *end != '/' && *end != '\\') end++;

        char *user = xstrndup(next, (size_t)(end - next));
        char home[PATH_BUF];
        const char *base = getenv("SystemDrive");
        snprintf(home, sizeof(home), "%s\\Users\\%s", base ? base : "C:", user);

        if (path_is_dir(home)) {
            path_to_slashes(home);
            field_add(ex, home, strlen(home));
            free(user);
            *p = end;
            return;
        }
        free(user);
    }

    if (*next && *next != '/' && *next != '\\') {
        field_add(ex, "~", 1);
        (*p)++;
        return;
    }
    const char *home = var_get("HOME");
    if (!home) home = home_dir();
    field_add(ex, home, strlen(home));
    (*p)++;
}

static void expand_into(const char *word, Expander *ex) {
    const char *p = word;
    int at_start = 1;

    while (*p) {
        char c = *p;
        if (c == '~' && at_start) {
            expand_tilde(ex, &p);
            at_start = 0;
            continue;
        }
        at_start = 0;

        if (c == '\\') {
            p++;
            if (*p) {
                field_add(ex, p, 1);
                ex->quoted = 1;
                p++;
            }
            continue;
        }
        if (c == '\'') {
            p++;
            ex->quoted = 1;
            ex->has_content = 1;
            while (*p && *p != '\'') {
                field_add(ex, p, 1);
                p++;
            }
            if (*p) p++;
            continue;
        }
        if (c == '"') {
            p++;
            ex->quoted = 1;
            ex->has_content = 1;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1] && strchr("\"\\$`", p[1])) {
                    field_add(ex, p + 1, 1);
                    p += 2;
                    continue;
                }
                if (*p == '$') {
                    expand_dollar(ex, &p, 1);
                    continue;
                }
                if (*p == '`') {
                    const char *end = strchr(p + 1, '`');
                    if (!end) end = p + strlen(p);
                    char *command = xstrndup(p + 1, (size_t)(end - p - 1));
                    char *result = capture_trimmed(command);
                    free(command);
                    field_add(ex, result, strlen(result));
                    free(result);
                    p = *end ? end + 1 : end;
                    continue;
                }
                field_add(ex, p, 1);
                p++;
            }
            if (*p) p++;
            continue;
        }
        if (c == '`') {
            const char *end = strchr(p + 1, '`');
            if (!end) end = p + strlen(p);
            char *command = xstrndup(p + 1, (size_t)(end - p - 1));
            char *result = capture_trimmed(command);
            free(command);
            field_add_split(ex, result);
            free(result);
            p = *end ? end + 1 : end;
            continue;
        }
        if (c == '$') {
            expand_dollar(ex, &p, 0);
            continue;
        }
        field_add(ex, p, 1);
        p++;
    }
}

static const char *find_brace_close(const char *start) {
    int depth = 0;
    for (const char *p = start; *p; p++) {
        if (*p == '{') depth++;
        else if (*p == '}' && --depth == 0) return p;
    }
    return NULL;
}

void brace_expand_word(const char *word, StrList *out) {
    const char *open = NULL;
    for (const char *p = word; *p; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            continue;
        }
        if (*p == '\'' || *p == '"') {
            char quote = *p++;
            while (*p && *p != quote) {
                if (quote == '"' && *p == '\\' && p[1]) p++;
                p++;
            }
            if (!*p) break;
            continue;
        }
        if (*p == '$' && p[1] == '{') {
            p++;
            const char *close = find_brace_close(p);
            if (!close) break;
            p = close;
            continue;
        }
        if (*p == '{') {
            open = p;
            break;
        }
    }

    const char *close = open ? find_brace_close(open) : NULL;
    if (!close) {
        sl_push_copy(out, word);
        return;
    }

    char *prefix = xstrndup(word, (size_t)(open - word));
    char *body = xstrndup(open + 1, (size_t)(close - open - 1));
    const char *suffix = close + 1;

    StrList pieces;
    sl_init(&pieces);

    int low = 0;
    int high = 0;
    char extra = 0;
    if (sscanf(body, "%d..%d%c", &low, &high, &extra) == 2) {
        int step = low <= high ? 1 : -1;
        for (int value = low;; value += step) {
            char number[32];
            snprintf(number, sizeof(number), "%d", value);
            sl_push_copy(&pieces, number);
            if (value == high) break;
        }
    } else {
        int depth = 0;
        StrBuf current;
        sb_init(&current);
        for (const char *p = body; *p; p++) {
            if (*p == '{') depth++;
            if (*p == '}') depth--;
            if (*p == ',' && depth == 0) {
                sl_push(&pieces, sb_take(&current));
                sb_init(&current);
                continue;
            }
            sb_putc(&current, *p);
        }
        sl_push(&pieces, sb_take(&current));
        if (pieces.len < 2) {
            sl_free(&pieces);
            free(prefix);
            free(body);
            sl_push_copy(out, word);
            return;
        }
    }

    for (size_t i = 0; i < pieces.len; i++) {
        StrBuf combined;
        sb_init(&combined);
        sb_puts(&combined, prefix);
        sb_puts(&combined, pieces.items[i]);
        sb_puts(&combined, suffix);
        brace_expand_word(combined.data, out);
        sb_free(&combined);
    }

    sl_free(&pieces);
    free(prefix);
    free(body);
}

void expand_words(const StrList *in, StrList *out) {
    for (size_t i = 0; i < in->len; i++) {
        StrList braced;
        sl_init(&braced);
        brace_expand_word(in->items[i], &braced);

        for (size_t b = 0; b < braced.len; b++) {
            Expander ex;
            sb_init(&ex.field);
            ex.has_content = 0;
            ex.quoted = 0;
            ex.out = out;
            ex.split = 1;
            ex.glob = 1;
            expand_into(braced.items[b], &ex);
            field_flush(&ex);
            sb_free(&ex.field);
        }
        sl_free(&braced);
    }
}

char *expand_heredoc(const char *body) {
    StrList fields;
    sl_init(&fields);

    Expander ex;
    sb_init(&ex.field);
    ex.has_content = 0;
    ex.quoted = 0;
    ex.out = &fields;
    ex.split = 0;
    ex.glob = 0;

    StrBuf quoted;
    sb_init(&quoted);
    sb_putc(&quoted, '"');
    for (const char *p = body; *p; p++) {
        if (*p == '"' || *p == '\\') sb_putc(&quoted, '\\');
        sb_putc(&quoted, *p);
    }
    sb_putc(&quoted, '"');

    expand_into(quoted.data, &ex);
    field_flush(&ex);
    sb_free(&ex.field);
    sb_free(&quoted);

    StrBuf joined;
    sb_init(&joined);
    for (size_t i = 0; i < fields.len; i++) sb_puts(&joined, fields.items[i]);
    sl_free(&fields);
    return sb_take(&joined);
}

char *expand_single(const char *word) {
    StrList fields;
    sl_init(&fields);
    Expander ex;
    sb_init(&ex.field);
    ex.has_content = 0;
    ex.quoted = 0;
    ex.out = &fields;
    ex.split = 0;
    ex.glob = 0;
    expand_into(word, &ex);
    field_flush(&ex);
    sb_free(&ex.field);

    StrBuf joined;
    sb_init(&joined);
    for (size_t i = 0; i < fields.len; i++) {
        if (i > 0) sb_putc(&joined, ' ');
        sb_puts(&joined, fields.items[i]);
    }
    sl_free(&fields);
    return sb_take(&joined);
}

int glob_expand(const char *pattern, StrList *out) {
    char normalized[PATH_BUF];
    snprintf(normalized, sizeof(normalized), "%s", pattern);
    path_to_backslashes(normalized);

    char *slash = strrchr(normalized, '\\');
    char dir[PATH_BUF] = "";
    if (slash) {
        size_t dir_len = (size_t)(slash - normalized) + 1;
        memcpy(dir, normalized, dir_len);
        dir[dir_len] = '\0';
    }

    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(normalized, &data);
    if (find == INVALID_HANDLE_VALUE) return 0;

    StrList matches;
    sl_init(&matches);
    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        if (data.cFileName[0] == '.' && !strchr(pattern, '.')) continue;
        StrBuf sb;
        sb_init(&sb);
        sb_puts(&sb, dir);
        sb_puts(&sb, data.cFileName);
        if (strchr(pattern, '/')) path_to_slashes(sb.data);
        sl_push(&matches, sb_take(&sb));
    } while (FindNextFileA(find, &data));
    FindClose(find);

    if (matches.len == 0) {
        sl_free(&matches);
        return 0;
    }
    sl_sort(&matches);
    for (size_t i = 0; i < matches.len; i++) sl_push(out, matches.items[i]);
    free(matches.items);
    return 1;
}

typedef struct {
    const char *p;
    int ok;
} Arith;

static long arith_expr(Arith *a);

static void arith_space(Arith *a) {
    while (*a->p == ' ' || *a->p == '\t') a->p++;
}

static long arith_primary(Arith *a) {
    arith_space(a);
    if (*a->p == '(') {
        a->p++;
        long value = arith_expr(a);
        arith_space(a);
        if (*a->p == ')') a->p++;
        return value;
    }
    if (*a->p == '-') {
        a->p++;
        return -arith_primary(a);
    }
    if (*a->p == '+') {
        a->p++;
        return arith_primary(a);
    }
    if (*a->p == '!') {
        a->p++;
        return !arith_primary(a);
    }
    if (*a->p == '$') {
        a->p++;
        return arith_primary(a);
    }
    if (isdigit((unsigned char)*a->p)) {
        char *end;
        long value = strtol(a->p, &end, 0);
        a->p = end;
        return value;
    }
    if (isalpha((unsigned char)*a->p) || *a->p == '_') {
        char *name = read_name(&a->p);
        const char *value = var_get(name);
        free(name);
        return value ? atol(value) : 0;
    }
    a->ok = 0;
    return 0;
}

static long arith_mul(Arith *a) {
    long left = arith_primary(a);
    while (1) {
        arith_space(a);
        char op = *a->p;
        if (op != '*' && op != '/' && op != '%') return left;
        a->p++;
        long right = arith_primary(a);
        if ((op == '/' || op == '%') && right == 0) {
            a->ok = 0;
            return 0;
        }
        if (op == '*') left *= right;
        else if (op == '/') left /= right;
        else left %= right;
    }
}

static long arith_add(Arith *a) {
    long left = arith_mul(a);
    while (1) {
        arith_space(a);
        char op = *a->p;
        if (op != '+' && op != '-') return left;
        a->p++;
        long right = arith_mul(a);
        left = op == '+' ? left + right : left - right;
    }
}

static long arith_compare(Arith *a) {
    long left = arith_add(a);
    while (1) {
        arith_space(a);
        if (a->p[0] == '<' && a->p[1] == '=') {
            a->p += 2;
            left = left <= arith_add(a);
        } else if (a->p[0] == '>' && a->p[1] == '=') {
            a->p += 2;
            left = left >= arith_add(a);
        } else if (a->p[0] == '<') {
            a->p++;
            left = left < arith_add(a);
        } else if (a->p[0] == '>') {
            a->p++;
            left = left > arith_add(a);
        } else {
            return left;
        }
    }
}

static long arith_equality(Arith *a) {
    long left = arith_compare(a);
    while (1) {
        arith_space(a);
        if (a->p[0] == '=' && a->p[1] == '=') {
            a->p += 2;
            left = left == arith_compare(a);
        } else if (a->p[0] == '!' && a->p[1] == '=') {
            a->p += 2;
            left = left != arith_compare(a);
        } else {
            return left;
        }
    }
}

static long arith_expr(Arith *a) {
    long left = arith_equality(a);
    while (1) {
        arith_space(a);
        if (a->p[0] == '&' && a->p[1] == '&') {
            a->p += 2;
            long right = arith_equality(a);
            left = left && right;
        } else if (a->p[0] == '|' && a->p[1] == '|') {
            a->p += 2;
            long right = arith_equality(a);
            left = left || right;
        } else {
            return left;
        }
    }
}

long eval_arith(const char *expr, int *ok) {
    char *expanded = expand_single(expr);
    Arith a = {expanded, 1};
    long value = arith_expr(&a);
    arith_space(&a);
    if (*a.p) a.ok = 0;
    if (ok) *ok = a.ok;
    free(expanded);
    return value;
}
