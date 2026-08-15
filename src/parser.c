/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    T_EOF,
    T_WORD,
    T_PIPE,
    T_AND_AND,
    T_OR_OR,
    T_SEMI,
    T_DSEMI,
    T_AMP,
    T_NEWLINE,
    T_REDIR,
    T_LPAREN,
    T_RPAREN,
    T_ARITH,
    T_SEMI_AMP
} TokenType;

typedef struct {
    TokenType type;
    char *text;
    int quoted;
    int fd;
    RedirType redir;
} Token;

typedef struct {
    Token *items;
    size_t len;
    size_t cap;
} TokenList;

typedef struct {
    TokenList tokens;
    size_t pos;
    int incomplete;
    char *error;
} Parser;

static const char *RESERVED[] = {"if",     "then",  "elif",     "else", "fi",   "for",
                                 "select", "in",    "while",    "until", "do",   "done",
                                 "case",   "esac",  "function", "{",     "}",    "[[",
                                 "]]",     NULL};

int keyword_known(const char *word) {
    for (int i = 0; RESERVED[i]; i++) {
        if (strcmp(RESERVED[i], word) == 0) return 1;
    }
    return 0;
}

void keyword_names(StrList *out) {
    for (int i = 0; RESERVED[i]; i++) {
        if (isalpha((unsigned char)RESERVED[i][0])) sl_push_copy(out, RESERVED[i]);
    }
}

void keyword_complete(const char *prefix, size_t length, StrList *out) {
    for (int i = 0; RESERVED[i]; i++) {
        if (isalpha((unsigned char)RESERVED[i][0]) && strncmp(RESERVED[i], prefix, length) == 0)
            sl_push_copy(out, RESERVED[i]);
    }
}

int keyword_prefix(const char *prefix, size_t length) {
    for (int i = 0; RESERVED[i]; i++) {
        if (strncmp(RESERVED[i], prefix, length) == 0) return 1;
    }
    return 0;
}

static void token_push(TokenList *list, Token token) {
    if (list->len + 1 >= list->cap) {
        list->cap = list->cap ? list->cap * 2 : 32;
        list->items = xrealloc(list->items, list->cap * sizeof(Token));
    }
    list->items[list->len++] = token;
}

static void token_list_free(TokenList *list) {
    for (size_t i = 0; i < list->len; i++) free(list->items[i].text);
    free(list->items);
    list->items = NULL;
    list->len = list->cap = 0;
}

static int is_delim(char c) {
    return c == '\0' || c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '|' || c == '&' ||
           c == ';' || c == '<' || c == '>' || c == '(' || c == ')';
}

static void copy_until(const char **p, StrBuf *sb, char open, char close, int *incomplete) {
    int depth = 0;
    sb_putc(sb, **p);
    if (**p == open) depth++;
    (*p)++;
    while (**p) {
        char c = **p;
        if (c == '\\' && (*p)[1]) {
            sb_putc(sb, c);
            sb_putc(sb, (*p)[1]);
            *p += 2;
            continue;
        }
        sb_putc(sb, c);
        (*p)++;
        if (c == open && open != close) depth++;
        else if (c == close) {
            if (--depth <= 0) return;
        }
    }
    *incomplete = 1;
}

static void scan_quoted(const char **p, StrBuf *sb, char quote, int *incomplete) {
    sb_putc(sb, **p);
    (*p)++;
    while (**p) {
        char c = **p;
        if (quote == '"' && c == '\\' && (*p)[1]) {
            sb_putc(sb, c);
            sb_putc(sb, (*p)[1]);
            *p += 2;
            continue;
        }
        sb_putc(sb, c);
        (*p)++;
        if (c == quote) return;
    }
    *incomplete = 1;
}

static char *scan_word(const char **p, int *quoted, int *incomplete) {
    StrBuf sb;
    sb_init(&sb);
    while (**p && !is_delim(**p)) {
        char c = **p;
        if (c == '\\') {
            *quoted = 1;
            sb_putc(&sb, c);
            (*p)++;
            if (**p) {
                sb_putc(&sb, **p);
                (*p)++;
            } else {
                *incomplete = 1;
            }
            continue;
        }
        if (c == '\'' || c == '"') {
            *quoted = 1;
            scan_quoted(p, &sb, c, incomplete);
            continue;
        }
        if (c == '`') {
            scan_quoted(p, &sb, '`', incomplete);
            continue;
        }
        if (c == '$' && (*p)[1] == '\'') {
            *quoted = 1;
            *p += 2;
            while (**p && **p != '\'') {
                if (**p == '\\' && (*p)[1]) {
                    char escaped = (*p)[1];
                    char out = 0;
                    int handled = 1;
                    switch (escaped) {
                    case 'n': out = '\n'; break;
                    case 't': out = '\t'; break;
                    case 'r': out = '\r'; break;
                    case 'e':
                    case 'E': out = 27; break;
                    case 'a': out = 7; break;
                    case 'b': out = 8; break;
                    case 'f': out = 12; break;
                    case 'v': out = 11; break;
                    case '\\': out = '\\'; break;
                    case '\'': out = '\''; break;
                    case '"': out = '"'; break;
                    default: handled = 0; break;
                    }
                    if (handled) {
                        sb_putc(&sb, out);
                        *p += 2;
                    } else {
                        sb_putc(&sb, **p);
                        (*p)++;
                    }
                    continue;
                }
                sb_putc(&sb, **p);
                (*p)++;
            }
            if (**p == '\'') (*p)++;
            else *incomplete = 1;
            continue;
        }
        if (strchr("?*+@!", c) && (*p)[1] == '(') {
            sb_putc(&sb, c);
            (*p)++;
            copy_until(p, &sb, '(', ')', incomplete);
            continue;
        }
        if (c == '$' && (*p)[1] == '(') {
            sb_putc(&sb, '$');
            (*p)++;
            copy_until(p, &sb, '(', ')', incomplete);
            continue;
        }
        if (c == '$' && (*p)[1] == '{') {
            sb_putc(&sb, '$');
            (*p)++;
            copy_until(p, &sb, '{', '}', incomplete);
            continue;
        }
        sb_putc(&sb, c);
        (*p)++;
    }
    return sb_take(&sb);
}

static char *scan_process_substitution(const char **p, int *incomplete) {
    StrBuf sb;
    sb_init(&sb);
    sb_putc(&sb, **p);
    (*p)++;

    int depth = 0;
    while (**p) {
        if (**p == '(') depth++;
        if (**p == ')') depth--;
        sb_putc(&sb, **p);
        (*p)++;
        if (depth == 0) break;
    }
    if (depth != 0) *incomplete = 1;
    return sb_take(&sb);
}

static void tokenize(const char *src, TokenList *out, int *incomplete) {
    const char *p = src;
    const char *heredoc_resume = NULL;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (*p == '\\' && p[1] == '\r' && p[2] == '\n') {
            p += 3;
            continue;
        }
        if (*p == '\\' && p[1] == '\n') {
            p += 2;
            continue;
        }
        if (*p == '\\' && p[1] == '\0') {
            *incomplete = 1;
            break;
        }
        if (!*p) break;

        if (*p == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (*p == '\n') {
            Token t = {T_NEWLINE, NULL, 0, 0, R_IN};
            token_push(out, t);
            if (heredoc_resume) {
                p = heredoc_resume;
                heredoc_resume = NULL;
            } else {
                p++;
            }
            continue;
        }
        if (*p == '|' && p[1] == '|') {
            Token t = {T_OR_OR, NULL, 0, 0, R_IN};
            token_push(out, t);
            p += 2;
            continue;
        }
        if (*p == '&' && p[1] == '&') {
            Token t = {T_AND_AND, NULL, 0, 0, R_IN};
            token_push(out, t);
            p += 2;
            continue;
        }
        if (*p == '|') {
            Token t = {T_PIPE, NULL, 0, 0, R_IN};
            token_push(out, t);
            p++;
            continue;
        }
        if (*p == '&') {
            Token t = {T_AMP, NULL, 0, 0, R_IN};
            token_push(out, t);
            p++;
            continue;
        }
        if (*p == ';' && p[1] == '&') {
            Token t = {T_SEMI_AMP, NULL, 0, 0, R_IN};
            token_push(out, t);
            p += 2;
            continue;
        }
        if (*p == ';' && p[1] == ';') {
            Token t = {T_DSEMI, NULL, 0, 0, R_IN};
            token_push(out, t);
            p += 2;
            continue;
        }
        if (*p == ';') {
            Token t = {T_SEMI, NULL, 0, 0, R_IN};
            token_push(out, t);
            p++;
            continue;
        }
        if (*p == '(' && p[1] == '(') {
            int depth = 0;
            const char *q = p;
            while (*q) {
                if (*q == '(') depth++;
                else if (*q == ')') {
                    depth--;
                    if (depth == 0) { q++; break; }
                }
                q++;
            }
            if (depth == 0) {
                Token t = {T_ARITH, NULL, 0, 0, R_IN};
                t.text = xstrndup(p + 2, (size_t)(q - p) - 4);
                token_push(out, t);
                p = q;
                continue;
            }
        }
        if (*p == '(') {
            Token t = {T_LPAREN, NULL, 0, 0, R_IN};
            token_push(out, t);
            p++;
            continue;
        }
        if (*p == ')') {
            Token t = {T_RPAREN, NULL, 0, 0, R_IN};
            token_push(out, t);
            p++;
            continue;
        }

        int fd = -1;
        const char *digits = p;
        while (isdigit((unsigned char)*digits)) digits++;
        if (digits > p && (*digits == '>' || *digits == '<')) {
            fd = atoi(p);
            p = digits;
        }

        if ((*p == '<' || *p == '>') && p[1] == '(') {
            Token t = {T_WORD, NULL, 0, 0, R_IN};
            t.text = scan_process_substitution(&p, incomplete);
            token_push(out, t);
            continue;
        }

        if (*p == '<' && p[1] == '<' && p[2] == '<') {
            Token t = {T_REDIR, NULL, 0, 0, R_HERESTRING};
            t.fd = 0;
            p += 3;
            while (*p == ' ' || *p == '\t') p++;
            int quoted = 0;
            t.text = scan_word(&p, &quoted, incomplete);
            token_push(out, t);
            continue;
        }
        if (*p == '<' && p[1] == '<') {
            Token t = {T_REDIR, NULL, 0, 0, R_HEREDOC};
            t.fd = 0;
            p += 2;
            if (*p == '-') p++;
            while (*p == ' ' || *p == '\t') p++;

            int quoted = 0;
            char *delimiter = scan_word(&p, &quoted, incomplete);
            if (quoted) t.redir = R_HEREDOC_RAW;

            char *plain = delimiter;
            StrBuf clean;
            sb_init(&clean);
            for (char *c = plain; *c; c++) {
                if (*c != '"' && *c != '\'' && *c != '\\') sb_putc(&clean, *c);
            }
            free(delimiter);

            const char *rest_of_line = p;
            const char *scan = p;
            while (*scan && *scan != '\n') scan++;
            if (*scan == '\n') scan++;

            StrBuf body;
            sb_init(&body);
            while (*scan) {
                const char *line_start = scan;
                while (*scan && *scan != '\n') scan++;
                size_t line_length = (size_t)(scan - line_start);
                char *line = xstrndup(line_start, line_length);
                char *trimmed = str_trim(line);
                int is_delimiter = strcmp(trimmed, clean.data) == 0;
                free(line);

                if (*scan == '\n') scan++;
                else if (!is_delimiter) *incomplete = 1;

                if (is_delimiter) break;
                sb_putn(&body, line_start, line_length);
                sb_putc(&body, '\n');
            }

            sb_free(&clean);
            t.text = sb_take(&body);
            token_push(out, t);

            heredoc_resume = scan;
            p = rest_of_line;
            continue;
        }

        if (*p == '>' || *p == '<') {
            Token t = {T_REDIR, NULL, 0, 0, R_IN};
            if (*p == '<') {
                t.redir = R_IN;
                t.fd = fd < 0 ? 0 : fd;
                p++;
            } else {
                t.fd = fd < 0 ? 1 : fd;
                p++;
                if (*p == '>') {
                    t.redir = R_APPEND;
                    p++;
                } else if (*p == '&') {
                    t.redir = R_DUP;
                    p++;
                } else {
                    t.redir = R_OUT;
                }
            }
            while (*p == ' ' || *p == '\t') p++;
            if ((*p == '<' || *p == '>') && p[1] == '(') {
                t.text = scan_process_substitution(&p, incomplete);
            } else {
                int quoted = 0;
                t.text = scan_word(&p, &quoted, incomplete);
            }
            token_push(out, t);
            continue;
        }

        Token t = {T_WORD, NULL, 0, 0, R_IN};
        t.text = scan_word(&p, &t.quoted, incomplete);
        if (!t.text || !*t.text) {
            free(t.text);
            p++;
            continue;
        }
        token_push(out, t);
    }
    Token end = {T_EOF, NULL, 0, 0, R_IN};
    token_push(out, end);
}

static Node *node_new(NodeKind kind) {
    Node *node = xmalloc(sizeof(Node));
    memset(node, 0, sizeof(Node));
    node->kind = kind;
    sl_init(&node->words);
    return node;
}

void node_free(Node *node) {
    if (!node) return;
    sl_free(&node->words);
    Redir *r = node->redirs;
    while (r) {
        Redir *next = r->next;
        free(r->target);
        free(r);
        r = next;
    }
    node_free(node->left);
    node_free(node->right);
    node_free(node->extra);
    free(node->name);
    free(node);
}

static Token *peek(Parser *ps) {
    return &ps->tokens.items[ps->pos];
}

static Token *advance(Parser *ps) {
    Token *t = &ps->tokens.items[ps->pos];
    if (t->type != T_EOF) ps->pos++;
    return t;
}

static const char *token_label(Token *t) {
    switch (t->type) {
    case T_EOF: return "the end of the input";
    case T_NEWLINE: return "a new line";
    case T_SEMI: return ";";
    case T_DSEMI: return ";;";
    case T_PIPE: return "|";
    case T_AND_AND: return "&&";
    case T_OR_OR: return "||";
    case T_AMP: return "&";
    case T_LPAREN: return "(";
    case T_RPAREN: return ")";
    case T_REDIR: return "a redirection";
    case T_ARITH: return "an arithmetic expression";
    default: return t->text && *t->text ? t->text : "a word";
    }
}

static int line_at(Parser *ps, size_t index) {
    int line = 1;
    for (size_t i = 0; i < index && i < ps->tokens.len; i++) {
        if (ps->tokens.items[i].type == T_NEWLINE) line++;
    }
    return line;
}

static void fail_hint(Parser *ps, size_t index, const char *problem, const char *hint) {
    if (ps->error) return;
    StrBuf sb;
    sb_init(&sb);
    sb_printf(&sb, "line %d: %s", line_at(ps, index), problem);
    if (hint) sb_printf(&sb, "\n  %s", hint);
    ps->error = sb_take(&sb);
}

static void unfinished(Parser *ps, size_t opener, const char *problem, const char *hint) {
    ps->incomplete = 1;
    fail_hint(ps, opener, problem, hint);
}

static int is_reserved(Token *t, const char *word) {
    return t->type == T_WORD && !t->quoted && t->text && strcmp(t->text, word) == 0;
}

static int is_any_reserved(Token *t) {
    static const char *TERMINATORS[] = {"then", "else", "elif", "fi",  "do", "done",
                                        "esac", "in",   "}",    "]]", NULL};
    if (t->type != T_WORD || t->quoted || !t->text) return 0;
    for (int i = 0; TERMINATORS[i]; i++) {
        if (strcmp(t->text, TERMINATORS[i]) == 0) return 1;
    }
    return 0;
}

static void skip_newlines(Parser *ps) {
    while (peek(ps)->type == T_NEWLINE || peek(ps)->type == T_SEMI) advance(ps);
}

static void skip_line_breaks(Parser *ps) {
    while (peek(ps)->type == T_NEWLINE) advance(ps);
}

static Node *parse_list(Parser *ps, const char **stop);
static Node *parse_and_or(Parser *ps);

static void add_redir(Node *node, Token *t) {
    Redir *r = xmalloc(sizeof(Redir));
    r->fd = t->fd;
    r->type = t->redir;
    r->target = xstrdup(t->text ? t->text : "");
    r->next = NULL;
    Redir **slot = &node->redirs;
    while (*slot) slot = &(*slot)->next;
    *slot = r;
}

static int at_stop(Parser *ps, const char **stop) {
    if (!stop) return 0;
    Token *t = peek(ps);
    for (int i = 0; stop[i]; i++) {
        if (is_reserved(t, stop[i])) return 1;
    }
    return 0;
}

static Node *parse_body(Parser *ps, const char *opener, const char *closer) {
    size_t start = ps->pos;
    if (!is_reserved(peek(ps), opener)) {
        char problem[128];
        char hint[128];
        snprintf(problem, sizeof(problem), "expected %s here and found %s", opener,
                 token_label(peek(ps)));
        snprintf(hint, sizeof(hint), "the loop reads: <loop> <condition>; %s <commands>; %s", opener,
                 closer);
        fail_hint(ps, ps->pos, problem, hint);
        return NULL;
    }
    advance(ps);
    const char *stop[] = {closer, NULL};
    Node *body = parse_list(ps, stop);
    if (!is_reserved(peek(ps), closer)) {
        char problem[128];
        char hint[128];
        snprintf(problem, sizeof(problem), "this %s has no %s", opener, closer);
        snprintf(hint, sizeof(hint), "close the loop with %s, on its own line or after a ;", closer);
        unfinished(ps, start, problem, hint);
        node_free(body);
        return NULL;
    }
    advance(ps);
    return body;
}

static Node *parse_if(Parser *ps) {
    size_t start = ps->pos;
    advance(ps);
    const char *cond_stop[] = {"then", NULL};
    Node *node = node_new(N_IF);
    node->left = parse_list(ps, cond_stop);
    if (!is_reserved(peek(ps), "then")) {
        unfinished(ps, start, "this if has no then",
                   "it reads: if <command>; then <commands>; fi");
        node_free(node);
        return NULL;
    }
    advance(ps);
    const char *body_stop[] = {"elif", "else", "fi", NULL};
    node->right = parse_list(ps, body_stop);

    if (is_reserved(peek(ps), "elif")) {
        node->extra = parse_if(ps);
        return node;
    }
    if (is_reserved(peek(ps), "else")) {
        advance(ps);
        const char *else_stop[] = {"fi", NULL};
        node->extra = parse_list(ps, else_stop);
    }
    if (!is_reserved(peek(ps), "fi")) {
        unfinished(ps, start, "this if has no fi",
                   "close it with fi, and note that it is fi rather than end or endif");
        node_free(node);
        return NULL;
    }
    advance(ps);
    return node;
}

static Node *parse_loop(Parser *ps, NodeKind kind) {
    advance(ps);
    const char *cond_stop[] = {"do", NULL};
    Node *node = node_new(kind);
    node->left = parse_list(ps, cond_stop);
    node->right = parse_body(ps, "do", "done");
    if (!node->right) {
        node_free(node);
        return NULL;
    }
    return node;
}

static Node *parse_for(Parser *ps) {
    advance(ps);
    if (peek(ps)->type == T_ARITH) {
        Node *node = node_new(N_FOR_C);
        char *spec = xstrdup(peek(ps)->text);
        advance(ps);
        char *cursor = spec;
        for (int i = 0; i < 3; i++) {
            char *part = str_next_field(&cursor, ';');
            sl_push_copy(&node->words, part ? str_trim(part) : "");
        }
        free(spec);
        skip_newlines(ps);
        node->right = parse_body(ps, "do", "done");
        if (!node->right) {
            node_free(node);
            return NULL;
        }
        return node;
    }
    Token *name = peek(ps);
    if (name->type != T_WORD) {
        fail_hint(ps, ps->pos, "for needs a variable name",
                  "it reads: for <name> in <words>; do <commands>; done");
        return NULL;
    }
    Node *node = node_new(N_FOR);
    node->name = xstrdup(name->text);
    advance(ps);

    if (is_reserved(peek(ps), "in")) {
        advance(ps);
        while (peek(ps)->type == T_WORD && !is_reserved(peek(ps), "do")) {
            sl_push_copy(&node->words, peek(ps)->text);
            advance(ps);
        }
    } else {
        sl_push_copy(&node->words, "\"$@\"");
    }
    skip_newlines(ps);
    node->right = parse_body(ps, "do", "done");
    if (!node->right) {
        node_free(node);
        return NULL;
    }
    return node;
}

static Node *parse_case(Parser *ps) {
    size_t start = ps->pos;
    advance(ps);
    Token *subject = peek(ps);
    if (subject->type != T_WORD) {
        fail_hint(ps, ps->pos, "case needs a word to match on",
                  "it reads: case $1 in <pattern>) <commands> ;; esac");
        return NULL;
    }
    Node *node = node_new(N_CASE);
    node->name = xstrdup(subject->text);
    advance(ps);
    skip_line_breaks(ps);

    if (!is_reserved(peek(ps), "in")) {
        unfinished(ps, start, "this case has no in",
                   "the word to match is followed by in: case $1 in");
        node_free(node);
        return NULL;
    }
    advance(ps);

    Node **slot = &node->right;
    while (1) {
        skip_line_breaks(ps);
        if (is_reserved(peek(ps), "esac")) break;
        if (peek(ps)->type == T_EOF) {
            unfinished(ps, start, "this case has no esac",
                       "each branch ends with ;; and the whole thing with esac");
            node_free(node);
            return NULL;
        }

        Node *item = node_new(N_CASE_ITEM);
        if (peek(ps)->type == T_LPAREN) advance(ps);
        while (peek(ps)->type == T_WORD) {
            sl_push_copy(&item->words, peek(ps)->text);
            advance(ps);
            if (peek(ps)->type == T_PIPE) advance(ps);
            else break;
        }
        if (peek(ps)->type != T_RPAREN) {
            fail_hint(ps, ps->pos, "a case pattern needs a ) after it",
                      "write: start|up) <commands> ;;");
            node_free(item);
            node_free(node);
            return NULL;
        }
        advance(ps);

        const char *stop[] = {"esac", NULL};
        item->right = parse_list(ps, stop);
        if (peek(ps)->type == T_SEMI_AMP) {
            item->background = 1;
            advance(ps);
        } else if (peek(ps)->type == T_DSEMI) {
            advance(ps);
        }

        *slot = item;
        slot = &item->extra;
    }

    if (!is_reserved(peek(ps), "esac")) {
        unfinished(ps, start, "this case has no esac",
                   "each branch ends with ;; and the whole thing with esac");
        node_free(node);
        return NULL;
    }
    advance(ps);
    return node;
}

static Node *parse_test(Parser *ps) {
    size_t start = ps->pos;
    advance(ps);
    Node *node = node_new(N_TEST);

    while (1) {
        Token *t = peek(ps);
        if (t->type == T_EOF) {
            unfinished(ps, start, "this [[ has no ]]",
                       "close it with ]] , with a space before it");
            node_free(node);
            return NULL;
        }
        if (t->type == T_WORD && t->text && strcmp(t->text, "]]") == 0) {
            advance(ps);
            break;
        }

        switch (t->type) {
        case T_WORD: sl_push_copy(&node->words, t->text); break;
        case T_AND_AND: sl_push_copy(&node->words, "&&"); break;
        case T_OR_OR: sl_push_copy(&node->words, "||"); break;
        case T_PIPE: sl_push_copy(&node->words, "|"); break;
        case T_LPAREN: sl_push_copy(&node->words, "("); break;
        case T_RPAREN: sl_push_copy(&node->words, ")"); break;
        case T_REDIR:
            sl_push_copy(&node->words, t->redir == R_IN ? "<" : ">");
            if (t->text && *t->text) sl_push_copy(&node->words, t->text);
            break;
        default: break;
        }
        advance(ps);
    }
    return node;
}

static Node *parse_select(Parser *ps) {
    advance(ps);
    Token *name = peek(ps);
    if (name->type != T_WORD) {
        fail_hint(ps, ps->pos, "select needs a variable name",
                  "it reads: select <name> in <words>; do <commands>; done");
        return NULL;
    }
    Node *node = node_new(N_SELECT);
    node->name = xstrdup(name->text);
    advance(ps);

    if (is_reserved(peek(ps), "in")) {
        advance(ps);
        while (peek(ps)->type == T_WORD && !is_reserved(peek(ps), "do")) {
            sl_push_copy(&node->words, peek(ps)->text);
            advance(ps);
        }
    } else {
        sl_push_copy(&node->words, "\"$@\"");
    }
    skip_newlines(ps);

    node->right = parse_body(ps, "do", "done");
    if (!node->right) {
        node_free(node);
        return NULL;
    }
    return node;
}

static int is_brace_open(Token *t) {
    return t->type == T_WORD && !t->quoted && t->text && t->text[0] == '{';
}

static void open_brace(Parser *ps) {
    Token *t = peek(ps);
    if (t->text[1] == '\0') {
        advance(ps);
        return;
    }
    memmove(t->text, t->text + 1, strlen(t->text));
}

static Node *parse_group(Parser *ps) {
    size_t start = ps->pos;
    open_brace(ps);
    const char *stop[] = {"}", NULL};
    Node *node = node_new(N_GROUP);
    node->right = parse_list(ps, stop);
    if (!is_reserved(peek(ps), "}")) {
        unfinished(ps, start, "this { has no }",
                   "the last command inside needs a ; or a new line before the }");
        node_free(node);
        return NULL;
    }
    advance(ps);
    return node;
}

static Node *parse_function(Parser *ps, const char *name) {
    Node *node = node_new(N_FUNC);
    node->name = xstrdup(name);
    skip_line_breaks(ps);
    if (!is_brace_open(peek(ps))) {
        fail_hint(ps, ps->pos, "a function body has to be a { } block",
                  "write: name() { <commands>; }");
        node_free(node);
        return NULL;
    }
    node->right = parse_group(ps);
    if (!node->right) {
        node_free(node);
        return NULL;
    }
    return node;
}

static Node *parse_simple(Parser *ps) {
    Node *node = node_new(N_SIMPLE);
    while (1) {
        Token *t = peek(ps);
        if (t->type == T_REDIR) {
            add_redir(node, t);
            advance(ps);
            continue;
        }
        if (t->type != T_WORD) break;
        if (node->words.len == 0 && is_any_reserved(t)) break;

        size_t text_length = t->text ? strlen(t->text) : 0;
        if (text_length > 1 && t->text[text_length - 1] == '=' &&
            ps->tokens.items[ps->pos + 1].type == T_LPAREN) {
            Node *assign = node_new(N_ASSIGN_ARRAY);
            assign->name = xstrndup(t->text, text_length - 1);
            advance(ps);
            advance(ps);

            while (peek(ps)->type == T_WORD || peek(ps)->type == T_NEWLINE) {
                if (peek(ps)->type == T_WORD) sl_push_copy(&assign->words, peek(ps)->text);
                advance(ps);
            }
            if (peek(ps)->type != T_RPAREN) {
                unfinished(ps, ps->pos, "this array assignment has no closing )",
                           "write: names=(one two three)");
                node_free(assign);
                node_free(node);
                return NULL;
            }
            advance(ps);
            node_free(node);
            return assign;
        }

        if (node->words.len == 0 && ps->tokens.items[ps->pos + 1].type == T_LPAREN) {
            char *name = xstrdup(t->text);
            advance(ps);
            advance(ps);
            if (peek(ps)->type != T_RPAREN) {
                fail_hint(ps, ps->pos, "a function name is followed by ()",
                          "write: name() { <commands>; }");
                free(name);
                node_free(node);
                return NULL;
            }
            advance(ps);
            node_free(node);
            Node *func = parse_function(ps, name);
            free(name);
            return func;
        }

        sl_push_copy(&node->words, t->text);
        advance(ps);
    }
    if (node->words.len == 0 && !node->redirs) {
        node_free(node);
        return NULL;
    }
    return node;
}

static Node *parse_compound(Parser *ps);

static Node *parse_command(Parser *ps) {
    size_t start = ps->pos;
    Node *node = parse_compound(ps);
    if (node) node->line = line_at(ps, start);
    if (node && node->kind != N_SIMPLE) {
        while (peek(ps)->type == T_REDIR) {
            add_redir(node, peek(ps));
            advance(ps);
        }
    }
    return node;
}

static Node *parse_compound(Parser *ps) {
    Token *t = peek(ps);

    if (t->type == T_ARITH) {
        Node *node = node_new(N_ARITH);
        node->name = xstrdup(t->text);
        advance(ps);
        return node;
    }
    if (t->type == T_LPAREN) {
        advance(ps);
        Node *node = node_new(N_SUBSHELL);
        node->right = parse_list(ps, NULL);
        if (peek(ps)->type != T_RPAREN) {
            unfinished(ps, ps->pos, "this ( has no )", "a subshell runs a list: ( cd x && make )");
            node_free(node);
            return NULL;
        }
        advance(ps);
        return node;
    }
    if (is_reserved(t, "if")) return parse_if(ps);
    if (is_reserved(t, "while")) return parse_loop(ps, N_WHILE);
    if (is_reserved(t, "until")) return parse_loop(ps, N_UNTIL);
    if (is_reserved(t, "for")) return parse_for(ps);
    if (is_reserved(t, "select")) return parse_select(ps);
    if (is_reserved(t, "case")) return parse_case(ps);
    if (is_reserved(t, "[[")) return parse_test(ps);
    if (is_reserved(t, "{")) return parse_group(ps);
    if (is_reserved(t, "function")) {
        advance(ps);
        Token *name = peek(ps);
        if (name->type != T_WORD) {
            fail_hint(ps, ps->pos, "function needs a name",
                      "write: function name { <commands>; }");
            return NULL;
        }
        char *fname = xstrdup(name->text);
        advance(ps);
        if (peek(ps)->type == T_LPAREN) {
            advance(ps);
            if (peek(ps)->type == T_RPAREN) advance(ps);
        }
        Node *func = parse_function(ps, fname);
        free(fname);
        return func;
    }
    return parse_simple(ps);
}

static Node *parse_pipeline(Parser *ps) {
    int negated = 0;
    Token *first = peek(ps);

    if (first->type == T_WORD && !first->quoted && strcmp(first->text, "time") == 0 &&
        ps->tokens.items[ps->pos + 1].type == T_WORD) {
        advance(ps);
        Node *timed = node_new(N_TIME);
        timed->right = parse_pipeline(ps);
        if (!timed->right) {
            node_free(timed);
            return NULL;
        }
        return timed;
    }
    if (first->type == T_WORD && !first->quoted && strcmp(first->text, "!") == 0) {
        negated = 1;
        advance(ps);
    }

    Node *left = parse_command(ps);
    if (!left) return NULL;
    while (peek(ps)->type == T_PIPE) {
        advance(ps);
        skip_line_breaks(ps);
        Node *right = parse_command(ps);
        if (!right) {
            unfinished(ps, ps->pos, "a | needs a command after it",
                       "the left side sends its output to the right, as in: ls | wc -l");
            node_free(left);
            return NULL;
        }
        Node *pipe_node = node_new(N_PIPE);
        pipe_node->left = left;
        pipe_node->right = right;
        left = pipe_node;
    }

    if (negated) {
        Node *node = node_new(N_NOT);
        node->right = left;
        left = node;
    }
    return left;
}

static Node *parse_and_or(Parser *ps) {
    Node *left = parse_pipeline(ps);
    if (!left) return NULL;
    while (peek(ps)->type == T_AND_AND || peek(ps)->type == T_OR_OR) {
        NodeKind kind = peek(ps)->type == T_AND_AND ? N_AND : N_OR;
        int is_and = kind == N_AND;
        advance(ps);
        skip_line_breaks(ps);
        Node *right = parse_pipeline(ps);
        if (!right) {
            unfinished(ps, ps->pos, is_and ? "a && needs a command after it"
                                           : "a || needs a command after it",
                       is_and ? "&& runs the next command only when this one succeeds"
                              : "|| runs the next command only when this one fails");
            node_free(left);
            return NULL;
        }
        Node *node = node_new(kind);
        node->left = left;
        node->right = right;
        left = node;
    }
    return left;
}

static Node *parse_list(Parser *ps, const char **stop) {
    Node *list = NULL;
    while (1) {
        skip_newlines(ps);
        if (peek(ps)->type == T_EOF || peek(ps)->type == T_RPAREN ||
            peek(ps)->type == T_DSEMI)
            break;
        if (at_stop(ps, stop)) break;

        Node *cmd = parse_and_or(ps);
        if (!cmd) {
            if (!ps->error && !ps->incomplete) {
                char problem[128];
                snprintf(problem, sizeof(problem), "%s cannot start a command",
                         token_label(peek(ps)));
                fail_hint(ps, ps->pos, problem, "a command starts with a name, a variable or a (");
            }
            node_free(list);
            return NULL;
        }
        int separated = 0;
        if (peek(ps)->type == T_AMP) {
            cmd->background = 1;
            advance(ps);
            separated = 1;
        }
        if (!list) {
            list = cmd;
        } else {
            Node *seq = node_new(N_SEQ);
            seq->left = list;
            seq->right = cmd;
            list = seq;
        }
        Token *t = peek(ps);
        if (!separated && t->type != T_SEMI && t->type != T_NEWLINE && t->type != T_AMP) break;
    }
    return list;
}

Node *parse_string(const char *src, int *incomplete, char **error) {
    Parser ps;
    memset(&ps, 0, sizeof(ps));
    tokenize(src, &ps.tokens, &ps.incomplete);

    Node *node = parse_list(&ps, NULL);
    if (!ps.error && !ps.incomplete && peek(&ps)->type != T_EOF) {
        Token *t = peek(&ps);
        char problem[128];
        const char *hint = "a stray keyword usually means the block above it was never opened";
        snprintf(problem, sizeof(problem), "%s was not expected here", token_label(t));
        if (is_any_reserved(t)) {
            snprintf(problem, sizeof(problem), "%s without the block it belongs to", t->text);
            hint = "if goes with then and fi, while and for go with do and done";
        }
        fail_hint(&ps, ps.pos, problem, hint);
        node_free(node);
        node = NULL;
    }
    if (ps.incomplete && !ps.error)
        fail_hint(&ps, ps.tokens.len, "the input ends inside something that is not finished",
                  "a quote, a $( ), or a block that was never closed");
    if (incomplete) *incomplete = ps.incomplete;
    if (error) {
        *error = ps.error;
    } else {
        free(ps.error);
    }
    token_list_free(&ps.tokens);
    return node;
}
