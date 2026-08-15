/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "line.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtins.h"
#include "complete.h"
#include "coreutils.h"
#include "exec.h"
#include "foreign.h"
#include "history.h"
#include "keys.h"
#include "parser.h"
#include "prompt.h"
#include "shell.h"
#include "style.h"
#include "term.h"
#include "util.h"
#include "vars.h"

#define HL_COMMAND "\x1b[1;32m"
#define HL_KEYWORD "\x1b[32m"
#define HL_UNKNOWN "\x1b[1;31m"
#define HL_STRING "\x1b[33m"
#define HL_ASSIGN "\x1b[33m"
#define HL_VARIABLE "\x1b[36m"
#define HL_OPERATOR "\x1b[35m"
#define HL_OPTION "\x1b[2;37m"
#define HL_SUGGEST "\x1b[90m"
#define HL_RESET "\x1b[0m"

typedef struct {
    StrBuf buffer;
    size_t cursor;
    StrBuf prompt;
    int prompt_width;
    int prompt_rows;
    int rows_up;
    char *suggestion;
    int history_index;
    char *stashed;
    StrList menu;
    size_t menu_index;
    size_t menu_start;
    int menu_active;
} Editor;

static size_t prev_char(const char *text, size_t index) {
    if (index == 0) return 0;
    index--;
    while (index > 0 && ((unsigned char)text[index] & 0xC0) == 0x80) index--;
    return index;
}

static size_t next_char(const char *text, size_t index) {
    size_t length = strlen(text);
    if (index >= length) return length;
    index++;
    while (index < length && ((unsigned char)text[index] & 0xC0) == 0x80) index++;
    return index;
}

static int word_break(char c) {
    return isspace((unsigned char)c) || c == '/' || c == '\\';
}

static size_t word_left(const char *text, size_t index) {
    while (index > 0 && word_break(text[index - 1])) index--;
    while (index > 0 && !word_break(text[index - 1])) index--;
    return index;
}

static size_t word_right(const char *text, size_t index) {
    size_t length = strlen(text);
    while (index < length && word_break(text[index])) index++;
    while (index < length && !word_break(text[index])) index++;
    return index;
}

static int command_known(const char *name) {
    if (!*name) return 0;
    if (builtin_lookup(name) || coreutil_lookup(name) || function_defined(name) ||
        alias_get(name) || foreign_known(name))
        return 1;
    if (strpbrk(name, "/\\")) {
        char resolved[PATH_BUF];
        return resolve_command(name, resolved, sizeof(resolved));
    }
    return path_command_exists(name);
}

static int command_pending(const char *word) {
    if (!*word) return 1;
    size_t length = strlen(word);

    return keyword_prefix(word, length) || builtin_name_prefix(word, length) ||
           coreutil_name_prefix(word, length) || function_name_prefix(word, length) ||
           alias_name_prefix(word, length) || foreign_name_prefix(word, length) ||
           path_command_prefix(word);
}

static int assignment_word(const char *word) {
    if (!isalpha((unsigned char)*word) && *word != '_') return 0;

    const char *p = word;
    while (isalnum((unsigned char)*p) || *p == '_') p++;

    if (*p == '[') {
        while (*p && *p != ']') p++;
        if (*p == ']') p++;
    }
    if (*p == '+') p++;
    return *p == '=';
}

static void highlight(const char *text, StrBuf *out) {
    const char *p = text;
    int expect_command = 1;

    while (*p) {
        if (isspace((unsigned char)*p)) {
            sb_putc(out, *p++);
            continue;
        }
        if (*p == '|' || *p == '&' || *p == ';' || *p == '<' || *p == '>') {
            int redirect = *p == '<' || *p == '>';
            sb_puts(out, HL_OPERATOR);
            while (*p == '|' || *p == '&' || *p == ';' || *p == '<' || *p == '>') sb_putc(out, *p++);
            sb_puts(out, HL_RESET);
            expect_command = !redirect;
            continue;
        }
        if (*p == '\'' || *p == '"') {
            char quote = *p;
            sb_puts(out, HL_STRING);
            sb_putc(out, *p++);
            while (*p && *p != quote) sb_putc(out, *p++);
            if (*p) sb_putc(out, *p++);
            sb_puts(out, HL_RESET);
            continue;
        }
        if (*p == '$') {
            sb_puts(out, HL_VARIABLE);
            sb_putc(out, *p++);
            while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '{' || *p == '}'))
                sb_putc(out, *p++);
            sb_puts(out, HL_RESET);
            continue;
        }

        const char *start = p;
        while (*p && !isspace((unsigned char)*p) && !strchr("|&;<>'\"$", *p)) p++;
        char *word = xstrndup(start, (size_t)(p - start));

        if (expect_command && assignment_word(word)) {
            sb_puts(out, HL_ASSIGN);
            sb_puts(out, word);
            sb_puts(out, HL_RESET);
        } else if (keyword_known(word)) {
            sb_puts(out, HL_KEYWORD);
            sb_puts(out, word);
            sb_puts(out, HL_RESET);
            expect_command = 1;
        } else if (expect_command) {
            int func_def = strchr(word, '(') != NULL;
            const char *color;
            if (func_def || command_known(word)) color = HL_COMMAND;
            else if (command_pending(word)) color = NULL;
            else color = HL_UNKNOWN;
            if (color) sb_puts(out, color);
            sb_puts(out, word);
            if (color) sb_puts(out, HL_RESET);
            expect_command = 0;
        } else if (word[0] == '-') {
            sb_puts(out, HL_OPTION);
            sb_puts(out, word);
            sb_puts(out, HL_RESET);
        } else {
            sb_puts(out, word);
        }
        free(word);
    }
}

static void update_suggestion(Editor *editor) {
    free(editor->suggestion);
    editor->suggestion = NULL;
    if (!option_enabled("FRESH_SUGGEST", 1)) return;
    if (editor->buffer.len == 0 || editor->cursor != editor->buffer.len) return;

    int index = history_search_prefix(editor->buffer.data, history_count() - 1, -1);
    if (index < 0) return;
    const char *entry = history_get(index);
    if (!entry || strlen(entry) <= editor->buffer.len) return;
    editor->suggestion = xstrdup(entry + editor->buffer.len);
}

static void prompt_metrics(Editor *editor) {
    const char *last = editor->prompt.data;
    int rows = 0;
    for (const char *p = editor->prompt.data; *p; p++) {
        if (*p == '\n') rows++;
        if (*p == '\n' || *p == '\r') last = p + 1;
    }
    editor->prompt_rows = rows;
    editor->prompt_width = display_width(last);
}

static void render(Editor *editor) {
    StrBuf frame;
    sb_init(&frame);

    if (editor->rows_up > 0) sb_printf(&frame, "\x1b[%dA", editor->rows_up);
    sb_puts(&frame, "\r\x1b[J");
    sb_puts(&frame, editor->prompt.data);
    if (option_enabled("FRESH_HIGHLIGHT", 1)) highlight(editor->buffer.data, &frame);
    else sb_puts(&frame, editor->buffer.data);

    int width = term_width();
    int total = editor->prompt_width + display_width(editor->buffer.data);

    if (editor->suggestion) {
        sb_puts(&frame, HL_SUGGEST);
        sb_puts(&frame, editor->suggestion);
        sb_puts(&frame, HL_RESET);
        total += display_width(editor->suggestion);
    }

    char *prefix = xstrndup(editor->buffer.data, editor->cursor);
    int cursor_cell = editor->prompt_width + display_width(prefix);
    free(prefix);

    int end_row = editor->prompt_rows + total / width;
    int cursor_row = editor->prompt_rows + cursor_cell / width;
    int cursor_col = cursor_cell % width;

    if (end_row > cursor_row) sb_printf(&frame, "\x1b[%dA", end_row - cursor_row);
    sb_puts(&frame, "\r");
    if (cursor_col > 0) sb_printf(&frame, "\x1b[%dC", cursor_col);

    editor->rows_up = cursor_row;
    term_write(frame.data);
    sb_free(&frame);
}

static void finish_line(Editor *editor) {
    editor->cursor = editor->buffer.len;
    free(editor->suggestion);
    editor->suggestion = NULL;
    render(editor);
    term_write("\r\n");
    editor->rows_up = 0;
}

static void editor_set_text(Editor *editor, const char *text) {
    sb_clear(&editor->buffer);
    sb_puts(&editor->buffer, text);
    editor->cursor = editor->buffer.len;
}

static void insert_text(Editor *editor, const char *text, size_t length) {
    editor->history_index = -1;
    sb_reserve(&editor->buffer, length);
    memmove(editor->buffer.data + editor->cursor + length, editor->buffer.data + editor->cursor,
            editor->buffer.len - editor->cursor + 1);
    memcpy(editor->buffer.data + editor->cursor, text, length);
    editor->buffer.len += length;
    editor->cursor += length;
}

static void delete_range(Editor *editor, size_t from, size_t to) {
    if (to <= from) return;
    editor->history_index = -1;
    memmove(editor->buffer.data + from, editor->buffer.data + to, editor->buffer.len - to + 1);
    editor->buffer.len -= to - from;
    if (editor->cursor > editor->buffer.len) editor->cursor = editor->buffer.len;
}

static void print_matches(Editor *editor, const StrList *matches) {
    term_write("\r\n");
    size_t longest = 0;
    for (size_t i = 0; i < matches->len; i++) {
        size_t length = strlen(matches->items[i]);
        if (length > longest) longest = length;
    }
    int column_width = (int)longest + 2;
    int columns = term_width() / (column_width > 0 ? column_width : 1);
    if (columns < 1) columns = 1;

    StrBuf out;
    sb_init(&out);
    for (size_t i = 0; i < matches->len; i++) {
        sb_printf(&out, "%-*s", column_width, matches->items[i]);
        if ((i + 1) % (size_t)columns == 0) sb_puts(&out, "\r\n");
    }
    if (matches->len % (size_t)columns) sb_puts(&out, "\r\n");
    term_write(out.data);
    sb_free(&out);
    editor->rows_up = 0;
}

static void apply_match(Editor *editor, size_t start, const char *match, int single) {
    delete_range(editor, start, editor->cursor);
    editor->cursor = start;

    int needs_quotes = strchr(match, ' ') != NULL;
    if (needs_quotes) insert_text(editor, "\"", 1);
    insert_text(editor, match, strlen(match));
    if (needs_quotes) insert_text(editor, "\"", 1);

    size_t length = strlen(match);
    if (single && length > 0 && match[length - 1] != '/' && match[length - 1] != '\\')
        insert_text(editor, " ", 1);
}

static void complete(Editor *editor) {
    if (editor->menu_active && editor->menu.len > 1) {
        editor->menu_index = (editor->menu_index + 1) % editor->menu.len;
        apply_match(editor, editor->menu_start, editor->menu.items[editor->menu_index], 0);
        return;
    }

    sl_clear(&editor->menu);
    size_t start = 0;
    complete_at(editor->buffer.data, editor->cursor, &editor->menu, &start);
    editor->menu_start = start;

    if (editor->menu.len == 0) return;
    if (editor->menu.len == 1) {
        apply_match(editor, start, editor->menu.items[0], 1);
        sl_clear(&editor->menu);
        return;
    }

    size_t common = strlen(editor->menu.items[0]);
    for (size_t i = 1; i < editor->menu.len; i++) {
        size_t j = 0;
        while (j < common && editor->menu.items[i][j] &&
               tolower((unsigned char)editor->menu.items[i][j]) ==
                   tolower((unsigned char)editor->menu.items[0][j]))
            j++;
        common = j;
    }

    size_t token_length = editor->cursor - start;
    if (common > token_length) {
        char *prefix = xstrndup(editor->menu.items[0], common);
        apply_match(editor, start, prefix, 0);
        free(prefix);
    }

    print_matches(editor, &editor->menu);
    editor->menu_active = 1;
    editor->menu_index = editor->menu.len - 1;
}

static void search_history(Editor *editor) {
    StrBuf query;
    sb_init(&query);
    int index = history_count() - 1;
    int found = -1;

    while (1) {
        StrBuf frame;
        sb_init(&frame);
        if (editor->rows_up > 0) sb_printf(&frame, "\x1b[%dA", editor->rows_up);
        sb_puts(&frame, "\r\x1b[J");
        sb_printf(&frame, HL_OPERATOR "search:" HL_RESET " %s", query.data);
        if (found >= 0) sb_printf(&frame, HL_SUGGEST "  %s" HL_RESET, history_get(found));
        editor->rows_up = 0;
        term_write(frame.data);
        sb_free(&frame);

        int key = term_read_key();
        if (key == KEY_ENTER || key == KEY_ESC || key == KEY_CTRL_C) {
            if (key == KEY_ENTER && found >= 0) editor_set_text(editor, history_get(found));
            break;
        }
        if (key == KEY_BACKSPACE) {
            if (query.len > 0) query.data[--query.len] = '\0';
        } else if (key == KEY_CTRL_R) {
            if (found > 0) index = found - 1;
        } else if (key >= 32 && key < 256) {
            sb_putc(&query, (char)key);
            index = history_count() - 1;
        } else {
            continue;
        }
        found = history_search_substring(query.data, index);
    }

    sb_free(&query);
    editor->rows_up = 0;
    term_write("\r\x1b[J");
}

static void history_step(Editor *editor, int direction) {
    int count = history_count();
    if (count == 0) return;
    if (editor->history_index < 0 && direction > 0) return;

    if (editor->history_index < 0) {
        free(editor->stashed);
        editor->stashed = xstrdup(editor->buffer.data);
    }

    const char *prefix = editor->stashed ? editor->stashed : "";
    int start = editor->history_index < 0 ? count - 1 : editor->history_index + direction;
    int index = *prefix ? history_search_prefix(prefix, start, direction) : start;

    if (index < 0 || index >= count) {
        if (direction > 0) {
            editor->history_index = -1;
            editor_set_text(editor, editor->stashed ? editor->stashed : "");
        }
        return;
    }
    editor->history_index = index;
    editor_set_text(editor, history_get(index));
}

static int widget_key(const char *widget, int fallback) {
    if (strcmp(widget, "accept-line") == 0) return KEY_ENTER;
    if (strcmp(widget, "beginning-of-line") == 0) return KEY_HOME;
    if (strcmp(widget, "end-of-line") == 0) return KEY_END;
    if (strcmp(widget, "backward-char") == 0) return KEY_LEFT;
    if (strcmp(widget, "forward-char") == 0) return KEY_RIGHT;
    if (strcmp(widget, "backward-word") == 0) return KEY_WORD_LEFT;
    if (strcmp(widget, "forward-word") == 0) return KEY_WORD_RIGHT;
    if (strcmp(widget, "delete-char") == 0) return KEY_DELETE;
    if (strcmp(widget, "backward-delete") == 0) return KEY_BACKSPACE;
    if (strcmp(widget, "delete-word") == 0) return KEY_WORD_DELETE;
    if (strcmp(widget, "backward-kill-word") == 0) return KEY_CTRL_W;
    if (strcmp(widget, "kill-line") == 0) return KEY_CTRL_K;
    if (strcmp(widget, "kill-to-start") == 0) return KEY_CTRL_U;
    if (strcmp(widget, "clear-screen") == 0) return KEY_CTRL_L;
    if (strcmp(widget, "history-previous") == 0) return KEY_UP;
    if (strcmp(widget, "history-next") == 0) return KEY_DOWN;
    if (strcmp(widget, "history-search") == 0) return KEY_CTRL_R;
    if (strcmp(widget, "complete") == 0) return KEY_TAB;
    if (strcmp(widget, "accept-suggestion") == 0) return KEY_CTRL_F;
    if (strcmp(widget, "cancel-line") == 0) return KEY_ESC;
    return fallback;
}

char *line_read(int continuation) {
    Editor editor;
    memset(&editor, 0, sizeof(editor));
    sb_init(&editor.buffer);
    sb_init(&editor.prompt);
    sl_init(&editor.menu);
    editor.history_index = -1;
    shell.interrupted = 0;

    if (continuation) {
        prompt_build_continuation(&editor.prompt);
    } else {
        prompt_set_title();
        prompt_build(&editor.prompt);
    }
    prompt_metrics(&editor);

    render(&editor);

    char *result = NULL;
    while (1) {
        int key = term_read_key();
        int pasting = term_input_pending();
        int was_tab = key == KEY_TAB;

        BindKind kind = BIND_WIDGET;
        const char *action = pasting ? NULL : bind_lookup(key, &kind);
        if (action) {
            if (kind == BIND_INSERT) {
                insert_text(&editor, action + 7, strlen(action + 7));
                update_suggestion(&editor);
                render(&editor);
                continue;
            }
            if (kind == BIND_RUN) {
                editor_set_text(&editor, action);
                finish_line(&editor);
                result = xstrdup(editor.buffer.data);
                break;
            }
            key = widget_key(action, key);
            if (key == 0) {
                render(&editor);
                continue;
            }
            was_tab = key == KEY_TAB;
        }

        if (key == KEY_ENTER || key == KEY_LINE_FEED) {
            finish_line(&editor);
            result = xstrdup(editor.buffer.data);
            break;
        }
        if (key == KEY_CTRL_C) {
            finish_line(&editor);
            term_write("^C\r\n");
            result = xstrdup(shell.trap_int ? shell.trap_int : "");
            shell.last_status = 130;
            shell.interrupted = 1;
            break;
        }
        if (key == KEY_CTRL_D) {
            if (editor.buffer.len == 0) {
                finish_line(&editor);
                result = NULL;
                break;
            }
            delete_range(&editor, editor.cursor, next_char(editor.buffer.data, editor.cursor));
        } else if (key == KEY_TAB) {
            if (pasting) insert_text(&editor, " ", 1);
            else complete(&editor);
        } else if (key == KEY_BACKSPACE) {
            size_t from = prev_char(editor.buffer.data, editor.cursor);
            delete_range(&editor, from, editor.cursor);
            editor.cursor = from;
        } else if (key == KEY_DELETE) {
            delete_range(&editor, editor.cursor, next_char(editor.buffer.data, editor.cursor));
        } else if (key == KEY_LEFT || key == KEY_CTRL_B) {
            editor.cursor = prev_char(editor.buffer.data, editor.cursor);
        } else if (key == KEY_RIGHT) {
            if (editor.cursor == editor.buffer.len && editor.suggestion)
                insert_text(&editor, editor.suggestion, strlen(editor.suggestion));
            else editor.cursor = next_char(editor.buffer.data, editor.cursor);
        } else if (key == KEY_CTRL_F) {
            if (editor.suggestion) insert_text(&editor, editor.suggestion, strlen(editor.suggestion));
        } else if (key == KEY_WORD_LEFT) {
            editor.cursor = word_left(editor.buffer.data, editor.cursor);
        } else if (key == KEY_WORD_RIGHT) {
            editor.cursor = word_right(editor.buffer.data, editor.cursor);
        } else if (key == KEY_WORD_DELETE) {
            delete_range(&editor, editor.cursor, word_right(editor.buffer.data, editor.cursor));
        } else if (key == KEY_HOME || key == KEY_CTRL_A) {
            editor.cursor = 0;
        } else if (key == KEY_END || key == KEY_CTRL_E) {
            if (editor.cursor == editor.buffer.len && editor.suggestion)
                insert_text(&editor, editor.suggestion, strlen(editor.suggestion));
            else editor.cursor = editor.buffer.len;
        } else if (key == KEY_UP || key == KEY_CTRL_P) {
            history_step(&editor, -1);
        } else if (key == KEY_DOWN || key == KEY_CTRL_N) {
            history_step(&editor, 1);
        } else if (key == KEY_CTRL_U) {
            delete_range(&editor, 0, editor.cursor);
            editor.cursor = 0;
        } else if (key == KEY_CTRL_K) {
            delete_range(&editor, editor.cursor, editor.buffer.len);
        } else if (key == KEY_CTRL_W || key == 127) {
            size_t from = word_left(editor.buffer.data, editor.cursor);
            delete_range(&editor, from, editor.cursor);
            editor.cursor = from;
        } else if (key == KEY_CTRL_L) {
            term_clear_screen();
            editor.rows_up = 0;
        } else if (key == KEY_CTRL_R) {
            search_history(&editor);
        } else if (key == KEY_ESC) {
            sb_clear(&editor.buffer);
            editor.cursor = 0;
            editor.history_index = -1;
        } else if (key >= 32 && key < 256 && key != 127) {
            char c = (char)key;
            insert_text(&editor, &c, 1);
            editor.history_index = -1;
        }

        if (!was_tab) {
            editor.menu_active = 0;
            sl_clear(&editor.menu);
        }
        if (pasting) continue;
        update_suggestion(&editor);
        render(&editor);
    }

    sb_free(&editor.buffer);
    sb_free(&editor.prompt);
    sl_free(&editor.menu);
    free(editor.suggestion);
    free(editor.stashed);
    return result;
}
