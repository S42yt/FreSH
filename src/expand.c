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
    int meta;
    int bracket;
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
    if (ex->glob && ex->meta) {
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
    ex->meta = 0;
    ex->bracket = 0;
}

static void field_add(Expander *ex, const char *text, size_t len) {
    sb_putn(&ex->field, text, len);
    ex->has_content = 1;
}

static void note_meta(Expander *ex, const char *text, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '*' || text[i] == '?') ex->meta = 1;
        else if (text[i] == '[') ex->bracket = 1;
        else if (text[i] == ']' && ex->bracket) ex->meta = 1;
    }
}

static int is_separator(char c) {
    const char *ifs = var_get("IFS");
    if (!ifs) return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    return *ifs && strchr(ifs, c) != NULL;
}

static int is_blank_separator(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r') && is_separator(c);
}

static char separator_join(void) {
    const char *ifs = var_get("IFS");
    if (!ifs) return ' ';
    return *ifs ? *ifs : '\0';
}

static void field_add_split(Expander *ex, const char *text) {
    if (!text) return;
    note_meta(ex, text, strlen(text));
    if (!ex->split) {
        field_add(ex, text, strlen(text));
        return;
    }

    const char *p = text;
    while (*p && is_blank_separator(*p)) p++;
    if (!*p) return;

    for (;;) {
        const char *start = p;
        while (*p && !is_separator(*p)) p++;
        field_add(ex, start, (size_t)(p - start));
        if (!*p) return;

        while (*p && is_blank_separator(*p)) p++;
        if (*p && is_separator(*p)) p++;
        while (*p && is_blank_separator(*p)) p++;
        if (!*p) return;
        field_flush(ex);
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
    case '!':
        if (shell.last_background_pid) sb_printf(&sb, "%lu", shell.last_background_pid);
        break;
    case '#':
        sb_printf(&sb, "%d", shell.params.len > 0 ? (int)shell.params.len - 1 : 0);
        break;
    case '*': {
        char separator = separator_join();
        for (size_t i = 1; i < shell.params.len; i++) {
            if (i > 1 && separator) sb_putc(&sb, separator);
            sb_puts(&sb, shell.params.items[i]);
        }
        break;
    }
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

static char *brace_expand(const char *body);

static char *apply_to_element(const char *value, const char *operator_text) {
    var_set("FRESH_ELEMENT", value);

    StrBuf body;
    sb_init(&body);
    sb_puts(&body, "FRESH_ELEMENT");
    sb_puts(&body, operator_text);

    char *result = brace_expand(body.data);
    sb_free(&body);
    return result;
}

static void slice_elements(StrList *all, const char *spec_text, StrList *out) {
    char spec[128];
    snprintf(spec, sizeof(spec), "%s", spec_text);
    char *cursor = spec;
    char *offset_text = str_next_field(&cursor, ':');
    char *length_text = str_next_field(&cursor, ':');

    int ok = 1;
    long offset = offset_text ? eval_arith(offset_text, &ok) : 0;
    if (offset < 0) offset += (long)all->len;
    if (offset < 0) offset = 0;
    long count = length_text ? eval_arith(length_text, &ok) : (long)all->len - offset;
    if (count < 0) count = 0;

    for (long i = offset; i < offset + count && i < (long)all->len; i++)
        if (i >= 0) sl_push_copy(out, all->items[i]);
}

static int expand_array_body(const char *body, StrList *out) {
    const char *source = body;
    int keys_wanted = 0;

    if (*source == '#') return 0;

    if ((*source == '@' || *source == '*') && (source[1] == ':' || source[1] == '\0')) {
        StrList all;
        sl_init(&all);
        for (size_t i = 0; i < shell.params.len; i++) sl_push_copy(&all, shell.params.items[i]);

        if (source[1] == ':') slice_elements(&all, source + 2, out);
        else for (size_t i = 1; i < all.len; i++) sl_push_copy(out, all.items[i]);

        sl_free(&all);
        return 1;
    }

    if (*source == '!') {
        keys_wanted = 1;
        source++;
    }
    const char *open = strchr(source, '[');
    if (!open) return 0;
    const char *close = strchr(open, ']');
    if (!close || close != open + 2 || (open[1] != '@' && open[1] != '*')) return 0;

    size_t name_length = (size_t)(open - source);
    char name[128];
    if (name_length == 0 || name_length >= sizeof(name)) return 0;
    memcpy(name, source, name_length);
    name[name_length] = '\0';

    StrList all;
    sl_init(&all);
    if (keys_wanted) var_keys(name, &all);
    else var_values(name, &all);

    const char *after = close + 1;
    if (!*after) {
        for (size_t i = 0; i < all.len; i++) sl_push_copy(out, all.items[i]);
    } else if (*after == ':') {
        slice_elements(&all, after + 1, out);
    } else {
        for (size_t i = 0; i < all.len; i++) {
            char *changed = apply_to_element(all.items[i], after);
            sl_push(out, changed);
        }
    }

    sl_free(&all);
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

    if (body[0] == '#' && (body[1] == '@' || body[1] == '*') && !body[2]) {
        StrBuf sb;
        sb_init(&sb);
        sb_printf(&sb, "%d", shell.params.len > 0 ? (int)shell.params.len - 1 : 0);
        return sb_take(&sb);
    }

    if (body[0] == '!' && body[strlen(body) - 1] == '*' && strlen(body) > 2) {
        char prefix[128];
        size_t length = strlen(body) - 2;
        if (length < sizeof(prefix)) {
            memcpy(prefix, body + 1, length);
            prefix[length] = '\0';

            StrList names;
            sl_init(&names);
            vars_list(&names);

            StrBuf sb;
            sb_init(&sb);
            for (size_t i = 0; i < names.len; i++) {
                char *eq = strchr(names.items[i], '=');
                if (eq) *eq = '\0';
                if (strncmp(names.items[i], prefix, length) != 0) continue;
                if (sb.len > 0) sb_putc(&sb, ' ');
                sb_puts(&sb, names.items[i]);
            }
            sl_free(&names);
            return sb_take(&sb);
        }
    }

    if (*body == '!' && (isalpha((unsigned char)body[1]) || body[1] == '_')) {
        const char *target = var_get(body + 1);
        if (!target || !*target) return xstrdup("");
        const char *value = var_get(target);
        return xstrdup(value ? value : "");
    }

    if (*body == '#' && split_subscript(body + 1, name, sizeof(name), index, sizeof(index))) {
        if (strcmp(index, "@") == 0 || strcmp(index, "*") == 0) {
            StrBuf sb;
            sb_init(&sb);
            sb_printf(&sb, "%d", var_count(name));
            return sb_take(&sb);
        }
        char *resolved = expand_single(index);
        const char *element = var_get_element(name, resolved);
        free(resolved);
        StrBuf sb;
        sb_init(&sb);
        sb_printf(&sb, "%d", element ? (int)strlen(element) : 0);
        return sb_take(&sb);
    }

    if (body[0] != '#' && split_subscript(body, name, sizeof(name), index, sizeof(index))) {
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
        char ename[128];
        char eindex[128];
        const char *value;
        if (split_subscript(body + 1, ename, sizeof(ename), eindex, sizeof(eindex))) {
            char *resolved = expand_single(eindex);
            value = var_get_element(ename, resolved);
            free(resolved);
        } else {
            value = var_get(body + 1);
        }
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

        if (operator == '@' && rest[0] && !rest[1]) {
            memcpy(name, body, name_length);
            name[name_length] = '\0';
            const char *value = var_get(name);
            const char *text = value ? value : "";

            if (rest[0] == 'U') return change_case(text, 1, 1);
            if (rest[0] == 'L') return change_case(text, 0, 1);
            if (rest[0] == 'Q') {
                StrBuf sb;
                sb_init(&sb);
                sb_putc(&sb, '\'');
                sb_puts(&sb, text);
                sb_putc(&sb, '\'');
                return sb_take(&sb);
            }
            return xstrdup(text);
        }

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

            if (operator == ':' && *rest && !strchr("-+=?", *rest)) {
                char *spec = expand_single(rest);
                char *split = strchr(spec, ':');
                if (split) *split = '\0';

                int ok = 1;
                long total = (long)strlen(text);
                long offset = eval_arith(spec, &ok);
                long length = split ? eval_arith(split + 1, &ok) : total;

                if (!ok) {
                    shell_error("${%s}: bad substring expression", body);
                    shell.last_status = 1;
                    free(spec);
                    free(text);
                    return xstrdup("");
                }

                if (offset < 0) offset += total;
                if (offset < 0) offset = 0;
                if (offset > total) offset = total;
                if (!split) length = total - offset;
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
    char *special = NULL;
    const char *rest = colon;
    int has_colon = 0;
    if (*rest == ':') {
        has_colon = 1;
        rest++;
    }
    char op = *rest ? *rest++ : '\0';

    const char *value;
    if (isdigit((unsigned char)var_name[0])) {
        value = param_get(atoi(var_name));
    } else if (var_name[0] && !var_name[1] && !isalpha((unsigned char)var_name[0]) &&
               var_name[0] != '_') {
        special = special_value(var_name[0]);
        value = special;
    } else {
        value = var_get(var_name);
    }
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
    } else if (op == '?') {
        if (empty) {
            char *message = expand_single(rest);
            shell_error("%s: %s", var_name,
                        *message ? message : "parameter null or not set");
            free(message);
            shell.last_status = 1;
            if (!shell.interactive) shell.running = 0;
            result = xstrdup("");
        } else {
            result = xstrdup(value);
        }
    } else {
        result = xstrdup(value ? value : "");
    }
    free(special);
    free(var_name);
    return result;
}

static char *read_file_text(const char *path) {
    char native[PATH_BUF];
    snprintf(native, sizeof(native), "%s", path);
    path_to_backslashes(native);

    FILE *f = fopen(native, "rb");
    if (!f) return xstrdup("");

    StrBuf out;
    sb_init(&out);
    char buffer[4096];
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), f)) > 0) sb_putn(&out, buffer, read);
    fclose(f);

    while (out.len > 0 && (out.data[out.len - 1] == '\n' || out.data[out.len - 1] == '\r'))
        out.data[--out.len] = '\0';
    return sb_take(&out);
}

static int substitution_status = 0;

void expand_forget_substitution_status(void) {
    substitution_status = 0;
}

int expand_substitution_status(void) {
    return substitution_status;
}

static char *capture_trimmed(const char *command) {
    StrBuf out;
    sb_init(&out);
    substitution_status = capture_command(command, &out);
    while (out.len > 0 && (out.data[out.len - 1] == '\n' || out.data[out.len - 1] == '\r'))
        out.data[--out.len] = '\0';
    return sb_take(&out);
}

static const char *scan_balanced(const char *p, char open, char close, StrBuf *inner) {
    const char *start = p;
    int depth = 0;
    int branching = 0;

    for (; *p; p++) {
        if (open == '(' && depth >= 1) {
            if (str_word_at(p, start, "case")) branching++;
            else if (branching > 0 && str_word_at(p, start, "esac")) branching--;

            if (*p == '\'' || *p == '"') {
                char quote = *p;
                sb_putc(inner, *p);
                p++;
                while (*p && *p != quote) {
                    if (quote == '"' && *p == '\\' && p[1]) {
                        sb_putc(inner, *p);
                        p++;
                    }
                    sb_putc(inner, *p);
                    p++;
                }
                if (!*p) return p;
                sb_putc(inner, *p);
                continue;
            }
        }

        if (*p == open) {
            depth++;
            if (depth == 1) continue;
        } else if (*p == close) {
            if (branching > 0 && depth == 1) {
                sb_putc(inner, *p);
                continue;
            }
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
        if (!ok) {
            shell_error("$((%s)): bad arithmetic expression", inner.data);
            shell.last_status = 1;
            if (!shell.interactive) shell.running = 0;
            sb_free(&inner);
            return;
        }
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

        char *result;
        const char *body = inner.data;
        while (*body == ' ' || *body == '\t') body++;
        if (*body == '<') {
            body++;
            while (*body == ' ' || *body == '\t') body++;
            char *path = expand_single(body);
            result = read_file_text(path);
            free(path);
        } else {
            result = capture_trimmed(inner.data);
        }
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
            int joined = in_quotes && strstr(inner.data, "[*]") != NULL;
            char separator = separator_join();
            for (size_t i = 0; i < elements.len; i++) {
                if (i > 0) {
                    if (joined) {
                        if (separator) field_add(ex, &separator, 1);
                    } else {
                        field_flush(ex);
                    }
                }
                if (in_quotes) {
                    field_add(ex, elements.items[i], strlen(elements.items[i]));
                    ex->quoted = 1;
                } else {
                    field_add_split(ex, elements.items[i]);
                }
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
    if (c == '@') {
        (*p)++;
        for (size_t i = 1; i < shell.params.len; i++) {
            if (i > 1) field_flush(ex);
            if (in_quotes) {
                field_add(ex, shell.params.items[i], strlen(shell.params.items[i]));
                ex->quoted = 1;
            } else {
                field_add_split(ex, shell.params.items[i]);
            }
        }
        if (shell.params.len <= 1 && in_quotes) ex->has_content = 1;
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
    if (c && strchr("?$#*@!0123456789", c)) {
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
        note_meta(ex, p, 1);
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
    int given_step = 0;
    char extra = 0;
    char from = 0;
    char to = 0;
    char letter_extra = 0;
    int letters = sscanf(body, "%c..%c%c", &from, &to, &letter_extra) == 2 &&
                  isalpha((unsigned char)from) && isalpha((unsigned char)to);

    int fields = letters ? 0 : sscanf(body, "%d..%d..%d%c", &low, &high, &given_step, &extra);
    int simple = letters || fields == 3 ? 0 : sscanf(body, "%d..%d%c", &low, &high, &extra) == 2;

    if (letters) {
        int direction = from <= to ? 1 : -1;
        for (char value = from;; value = (char)(value + direction)) {
            char single[2] = {value, '\0'};
            sl_push_copy(&pieces, single);
            if (value == to) break;
        }
    } else if (fields == 3 || simple) {
        int width = 0;
        const char *dots = strstr(body, "..");
        if (dots) {
            const char *l = body;
            const char *r = dots + 2;
            if (*l == '-' || *l == '+') l++;
            if (*r == '-' || *r == '+') r++;
            int lw = (int)strspn(l, "0123456789");
            int rw = (int)strspn(r, "0123456789");
            if ((l[0] == '0' && lw > 1) || (r[0] == '0' && rw > 1))
                width = lw > rw ? lw : rw;
        }
        int step = given_step ? (given_step < 0 ? -given_step : given_step) : 1;
        int direction = low <= high ? 1 : -1;
        for (int value = low;; value += step * direction) {
            char number[32];
            if (width > 0) snprintf(number, sizeof(number), "%0*d", width, value);
            else snprintf(number, sizeof(number), "%d", value);
            sl_push_copy(&pieces, number);
            if (direction > 0 ? value + step * direction > high : value + step * direction < high)
                break;
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

static size_t assignment_prefix(const char *word) {
    if (!isalpha((unsigned char)word[0]) && word[0] != '_') return 0;

    size_t i = 1;
    while (isalnum((unsigned char)word[i]) || word[i] == '_') i++;
    if (word[i] == '[') {
        const char *close = strchr(word + i, ']');
        if (!close) return 0;
        i = (size_t)(close - word) + 1;
    }
    if (word[i] == '+' && word[i + 1] == '=') return i + 2;
    return word[i] == '=' ? i + 1 : 0;
}

static void expand_assignment(const char *word, size_t prefix, StrList *out) {
    Expander ex;
    sb_init(&ex.field);
    ex.has_content = 1;
    ex.quoted = 0;
    ex.meta = 0;
    ex.out = out;
    ex.split = 0;
    ex.glob = 0;

    sb_putn(&ex.field, word, prefix);
    expand_into(word + prefix, &ex);
    ex.has_content = 1;
    field_flush(&ex);
    sb_free(&ex.field);
}

static int declaration_command(const char *word) {
    return strcmp(word, "local") == 0 || strcmp(word, "export") == 0 ||
           strcmp(word, "declare") == 0 || strcmp(word, "typeset") == 0 ||
           strcmp(word, "readonly") == 0;
}

void expand_words(const StrList *in, StrList *out) {
    int leading = 1;

    for (size_t i = 0; i < in->len; i++) {
        size_t prefix = leading ? assignment_prefix(in->items[i]) : 0;
        if (!prefix && !(i == 0 && declaration_command(in->items[i]))) leading = 0;

        if (prefix) {
            expand_assignment(in->items[i], prefix, out);
            continue;
        }

        StrList braced;
        sl_init(&braced);
        brace_expand_word(in->items[i], &braced);

        for (size_t b = 0; b < braced.len; b++) {
            Expander ex;
            sb_init(&ex.field);
            ex.has_content = 0;
            ex.quoted = 0;
            ex.meta = 0;
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
    ex.meta = 0;
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
    ex.meta = 0;
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

static void glob_one_level(const char *pattern, StrList *matches) {
    char normalized[PATH_BUF];
    if ((size_t)snprintf(normalized, sizeof(normalized), "%s", pattern) >= sizeof(normalized)) {
        shell_error("%s: pattern is too long to expand", pattern);
        return;
    }
    path_to_backslashes(normalized);

    size_t length = strlen(normalized);
    int directories_only = length > 1 && normalized[length - 1] == '\\';
    if (directories_only) normalized[length - 1] = '\0';

    char *slash = strrchr(normalized, '\\');
    char dir[PATH_BUF] = "";
    const char *leaf = normalized;
    if (slash) {
        size_t dir_len = (size_t)(slash - normalized) + 1;
        memcpy(dir, normalized, dir_len);
        dir[dir_len] = '\0';
        leaf = slash + 1;
    }

    char search[PATH_BUF];
    snprintf(search, sizeof(search), "%s*", dir);

    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(search, &data);
    if (find == INVALID_HANDLE_VALUE) return;

    int folding = shell.nocasematch;
    shell.nocasematch = 1;

    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        if (data.cFileName[0] == '.' && leaf[0] != '.' && !shell.dotglob) continue;
        if (directories_only && !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!pattern_match(leaf, data.cFileName)) continue;

        StrBuf sb;
        sb_init(&sb);
        sb_puts(&sb, dir);
        sb_puts(&sb, data.cFileName);
        if (strchr(pattern, '/')) path_to_slashes(sb.data);
        if (directories_only) sb_putc(&sb, '/');
        sl_push(matches, sb_take(&sb));
    } while (FindNextFileA(find, &data));

    shell.nocasematch = folding;
    FindClose(find);
}

static void glob_walk(const char *base, const char *rest, StrList *matches, int depth) {
    if (depth > 24) return;

    char pattern[PATH_BUF];
    snprintf(pattern, sizeof(pattern), "%s%s", base, rest);
    glob_one_level(pattern, matches);

    char search[PATH_BUF];
    snprintf(search, sizeof(search), "%s*", base);
    path_to_backslashes(search);

    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(search, &data);
    if (find == INVALID_HANDLE_VALUE) return;

    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        if (data.cFileName[0] == '.' && !shell.dotglob) continue;

        char nested[PATH_BUF];
        snprintf(nested, sizeof(nested), "%s%s/", base, data.cFileName);
        glob_walk(nested, rest, matches, depth + 1);
    } while (FindNextFileA(find, &data));
    FindClose(find);
}

int glob_expand(const char *pattern, StrList *out) {
    StrList matches;
    sl_init(&matches);

    const char *recursive = shell.globstar ? strstr(pattern, "**") : NULL;
    if (recursive) {
        char base[PATH_BUF] = "";
        size_t prefix = (size_t)(recursive - pattern);
        if (prefix >= sizeof(base)) prefix = sizeof(base) - 1;
        memcpy(base, pattern, prefix);
        base[prefix] = '\0';

        const char *rest = recursive + 2;
        if (*rest == '/' || *rest == '\\') rest++;
        glob_walk(base, *rest ? rest : "*", &matches, 0);
    } else {
        glob_one_level(pattern, &matches);
    }

    if (matches.len == 0) {
        sl_free(&matches);
        return shell.nullglob;
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

static long arith_comma(Arith *a);
static long arith_assign(Arith *a);

static void arith_space(Arith *a) {
    while (*a->p == ' ' || *a->p == '\t') a->p++;
}

static long arith_getvar(const char *name) {
    const char *value = var_get(name);
    if (!value) return 0;
    char *end;
    long n = strtol(value, &end, 0);
    return n;
}

static void arith_setvar(const char *name, long value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%ld", value);
    var_set(name, buffer);
}

static int arith_peek_name(Arith *a, char *buffer) {
    arith_space(a);
    const char *q = a->p;
    if (!(isalpha((unsigned char)*q) || *q == '_')) return 0;
    size_t i = 0;
    while (isalnum((unsigned char)*q) || *q == '_') {
        if (i < 63) buffer[i++] = *q;
        q++;
    }
    buffer[i] = '\0';
    return 1;
}

static long arith_primary(Arith *a) {
    arith_space(a);
    if (a->p[0] == '+' && a->p[1] == '+') {
        a->p += 2;
        char name[64];
        if (!arith_peek_name(a, name)) { a->ok = 0; return 0; }
        while (isalnum((unsigned char)*a->p) || *a->p == '_') a->p++;
        long value = arith_getvar(name) + 1;
        arith_setvar(name, value);
        return value;
    }
    if (a->p[0] == '-' && a->p[1] == '-') {
        a->p += 2;
        char name[64];
        if (!arith_peek_name(a, name)) { a->ok = 0; return 0; }
        while (isalnum((unsigned char)*a->p) || *a->p == '_') a->p++;
        long value = arith_getvar(name) - 1;
        arith_setvar(name, value);
        return value;
    }
    if (*a->p == '(') {
        a->p++;
        long value = arith_comma(a);
        arith_space(a);
        if (*a->p == ')') a->p++;
        return value;
    }
    if (*a->p == '-') { a->p++; return -arith_primary(a); }
    if (*a->p == '+') { a->p++; return arith_primary(a); }
    if (*a->p == '!') { a->p++; return !arith_primary(a); }
    if (*a->p == '~') { a->p++; return ~arith_primary(a); }
    if (*a->p == '$') { a->p++; return arith_primary(a); }
    if (isdigit((unsigned char)*a->p)) {
        char *end;
        long value = strtol(a->p, &end, 0);
        if (*end == '#' && value >= 2 && value <= 36) {
            long digits = strtol(end + 1, &end, (int)value);
            a->p = end;
            return digits;
        }
        a->p = end;
        return value;
    }
    char name[64];
    if (arith_peek_name(a, name)) {
        while (isalnum((unsigned char)*a->p) || *a->p == '_') a->p++;
        if (a->p[0] == '+' && a->p[1] == '+') {
            a->p += 2;
            long value = arith_getvar(name);
            arith_setvar(name, value + 1);
            return value;
        }
        if (a->p[0] == '-' && a->p[1] == '-') {
            a->p += 2;
            long value = arith_getvar(name);
            arith_setvar(name, value - 1);
            return value;
        }
        return arith_getvar(name);
    }
    a->ok = 0;
    return 0;
}

static long arith_power(Arith *a) {
    long left = arith_primary(a);
    arith_space(a);
    if (a->p[0] == '*' && a->p[1] == '*') {
        a->p += 2;
        long right = arith_power(a);
        long result = 1;
        for (long i = 0; i < right; i++) result *= left;
        return result;
    }
    return left;
}

static long arith_mul(Arith *a) {
    long left = arith_power(a);
    while (1) {
        arith_space(a);
        char op = *a->p;
        if (op == '*' && a->p[1] == '*') return left;
        if (op != '*' && op != '/' && op != '%') return left;
        a->p++;
        long right = arith_power(a);
        if ((op == '/' || op == '%') && right == 0) { a->ok = 0; return 0; }
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
        if (op == '+' && a->p[1] == '+') return left;
        if (op == '-' && a->p[1] == '-') return left;
        if (op != '+' && op != '-') return left;
        a->p++;
        long right = arith_mul(a);
        left = op == '+' ? left + right : left - right;
    }
}

static long arith_shift(Arith *a) {
    long left = arith_add(a);
    while (1) {
        arith_space(a);
        if (a->p[0] == '<' && a->p[1] == '<') { a->p += 2; left <<= arith_add(a); }
        else if (a->p[0] == '>' && a->p[1] == '>') { a->p += 2; left >>= arith_add(a); }
        else return left;
    }
}

static long arith_compare(Arith *a) {
    long left = arith_shift(a);
    while (1) {
        arith_space(a);
        if (a->p[0] == '<' && a->p[1] == '=') { a->p += 2; left = left <= arith_shift(a); }
        else if (a->p[0] == '>' && a->p[1] == '=') { a->p += 2; left = left >= arith_shift(a); }
        else if (a->p[0] == '<' && a->p[1] != '<') { a->p++; left = left < arith_shift(a); }
        else if (a->p[0] == '>' && a->p[1] != '>') { a->p++; left = left > arith_shift(a); }
        else return left;
    }
}

static long arith_equality(Arith *a) {
    long left = arith_compare(a);
    while (1) {
        arith_space(a);
        if (a->p[0] == '=' && a->p[1] == '=') { a->p += 2; left = left == arith_compare(a); }
        else if (a->p[0] == '!' && a->p[1] == '=') { a->p += 2; left = left != arith_compare(a); }
        else return left;
    }
}

static long arith_bit_and(Arith *a) {
    long left = arith_equality(a);
    while (1) {
        arith_space(a);
        if (a->p[0] == '&' && a->p[1] != '&') { a->p++; left &= arith_equality(a); }
        else return left;
    }
}

static long arith_bit_xor(Arith *a) {
    long left = arith_bit_and(a);
    while (1) {
        arith_space(a);
        if (a->p[0] == '^') { a->p++; left ^= arith_bit_and(a); }
        else return left;
    }
}

static long arith_bit_or(Arith *a) {
    long left = arith_bit_xor(a);
    while (1) {
        arith_space(a);
        if (a->p[0] == '|' && a->p[1] != '|') { a->p++; left |= arith_bit_xor(a); }
        else return left;
    }
}

static long arith_logic_and(Arith *a) {
    long left = arith_bit_or(a);
    while (1) {
        arith_space(a);
        if (a->p[0] == '&' && a->p[1] == '&') { a->p += 2; long r = arith_bit_or(a); left = left && r; }
        else return left;
    }
}

static long arith_logic_or(Arith *a) {
    long left = arith_logic_and(a);
    while (1) {
        arith_space(a);
        if (a->p[0] == '|' && a->p[1] == '|') { a->p += 2; long r = arith_logic_and(a); left = left || r; }
        else return left;
    }
}

static long arith_ternary(Arith *a) {
    long condition = arith_logic_or(a);
    arith_space(a);
    if (*a->p == '?') {
        a->p++;
        long yes = arith_assign(a);
        arith_space(a);
        if (*a->p == ':') a->p++;
        long no = arith_assign(a);
        return condition ? yes : no;
    }
    return condition;
}

static long arith_assign(Arith *a) {
    char name[64];
    const char *save = a->p;
    if (arith_peek_name(a, name)) {
        const char *after = a->p;
        while (isalnum((unsigned char)*after) || *after == '_') after++;
        const char *q = after;
        while (*q == ' ' || *q == '\t') q++;
        char op = *q;
        int compound = (op == '+' || op == '-' || op == '*' || op == '/' || op == '%' ||
                        op == '&' || op == '|' || op == '^') &&
                       q[1] == '=';
        int plain = op == '=' && q[1] != '=';
        if (plain || compound) {
            a->p = compound ? q + 2 : q + 1;
            long right = arith_assign(a);
            long current = compound ? arith_getvar(name) : 0;
            long value = right;
            if (compound) {
                switch (op) {
                case '+': value = current + right; break;
                case '-': value = current - right; break;
                case '*': value = current * right; break;
                case '/': value = right ? current / right : (a->ok = 0, 0); break;
                case '%': value = right ? current % right : (a->ok = 0, 0); break;
                case '&': value = current & right; break;
                case '|': value = current | right; break;
                case '^': value = current ^ right; break;
                }
            }
            arith_setvar(name, value);
            return value;
        }
    }
    a->p = save;
    return arith_ternary(a);
}

static long arith_comma(Arith *a) {
    long value = arith_assign(a);
    while (1) {
        arith_space(a);
        if (*a->p == ',') { a->p++; value = arith_assign(a); }
        else return value;
    }
}

long eval_arith(const char *expr, int *ok) {
    char *expanded = strpbrk(expr, "$`'\"") ? expand_single(expr) : NULL;
    Arith a = {expanded ? expanded : expr, 1};
    long value = arith_comma(&a);
    arith_space(&a);
    if (*a.p) a.ok = 0;
    if (ok) *ok = a.ok;
    free(expanded);
    return value;
}
