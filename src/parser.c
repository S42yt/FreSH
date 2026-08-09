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
    T_AMP,
    T_NEWLINE,
    T_REDIR,
    T_LPAREN,
    T_RPAREN
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

static const char *RESERVED[] = {"if",    "then", "elif", "else", "fi",   "for",  "in",
                                 "while", "until", "do",  "done", "function", "{", "}", NULL};

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

static void tokenize(const char *src, TokenList *out, int *incomplete) {
    const char *p = src;
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
            p++;
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
        if (*p == ';') {
            Token t = {T_SEMI, NULL, 0, 0, R_IN};
            token_push(out, t);
            p++;
            continue;
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
            int quoted = 0;
            t.text = scan_word(&p, &quoted, incomplete);
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

static void fail(Parser *ps, const char *message) {
    if (!ps->error) ps->error = xstrdup(message);
}

static int is_reserved(Token *t, const char *word) {
    return t->type == T_WORD && !t->quoted && t->text && strcmp(t->text, word) == 0;
}

static int is_any_reserved(Token *t) {
    if (t->type != T_WORD || t->quoted || !t->text) return 0;
    for (int i = 0; RESERVED[i]; i++) {
        if (strcmp(t->text, RESERVED[i]) == 0) return 1;
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
    if (!is_reserved(peek(ps), opener)) {
        fail(ps, "syntax error: expected keyword");
        return NULL;
    }
    advance(ps);
    const char *stop[] = {closer, NULL};
    Node *body = parse_list(ps, stop);
    if (!is_reserved(peek(ps), closer)) {
        ps->incomplete = 1;
        node_free(body);
        return NULL;
    }
    advance(ps);
    return body;
}

static Node *parse_if(Parser *ps) {
    advance(ps);
    const char *cond_stop[] = {"then", NULL};
    Node *node = node_new(N_IF);
    node->left = parse_list(ps, cond_stop);
    if (!is_reserved(peek(ps), "then")) {
        ps->incomplete = 1;
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
        ps->incomplete = 1;
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
    Token *name = peek(ps);
    if (name->type != T_WORD) {
        fail(ps, "syntax error: for requires a variable name");
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

static Node *parse_group(Parser *ps) {
    advance(ps);
    const char *stop[] = {"}", NULL};
    Node *node = node_new(N_GROUP);
    node->right = parse_list(ps, stop);
    if (!is_reserved(peek(ps), "}")) {
        ps->incomplete = 1;
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
    if (!is_reserved(peek(ps), "{")) {
        fail(ps, "syntax error: function body must be a { } block");
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
        if (node->words.len > 0 && is_any_reserved(t)) break;

        if (node->words.len == 0 && ps->tokens.items[ps->pos + 1].type == T_LPAREN) {
            char *name = xstrdup(t->text);
            advance(ps);
            advance(ps);
            if (peek(ps)->type != T_RPAREN) {
                fail(ps, "syntax error: expected ')'");
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

static Node *parse_command(Parser *ps) {
    Token *t = peek(ps);

    if (is_reserved(t, "if")) return parse_if(ps);
    if (is_reserved(t, "while")) return parse_loop(ps, N_WHILE);
    if (is_reserved(t, "until")) return parse_loop(ps, N_UNTIL);
    if (is_reserved(t, "for")) return parse_for(ps);
    if (is_reserved(t, "{")) return parse_group(ps);
    if (is_reserved(t, "function")) {
        advance(ps);
        Token *name = peek(ps);
        if (name->type != T_WORD) {
            fail(ps, "syntax error: function requires a name");
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
    if (t->type == T_WORD && !t->quoted && strcmp(t->text, "!") == 0) {
        advance(ps);
        Node *node = node_new(N_NOT);
        node->right = parse_command(ps);
        if (!node->right) {
            node_free(node);
            return NULL;
        }
        return node;
    }
    return parse_simple(ps);
}

static Node *parse_pipeline(Parser *ps) {
    Node *left = parse_command(ps);
    if (!left) return NULL;
    while (peek(ps)->type == T_PIPE) {
        advance(ps);
        skip_line_breaks(ps);
        Node *right = parse_command(ps);
        if (!right) {
            ps->incomplete = 1;
            node_free(left);
            return NULL;
        }
        Node *pipe_node = node_new(N_PIPE);
        pipe_node->left = left;
        pipe_node->right = right;
        left = pipe_node;
    }
    return left;
}

static Node *parse_and_or(Parser *ps) {
    Node *left = parse_pipeline(ps);
    if (!left) return NULL;
    while (peek(ps)->type == T_AND_AND || peek(ps)->type == T_OR_OR) {
        NodeKind kind = peek(ps)->type == T_AND_AND ? N_AND : N_OR;
        advance(ps);
        skip_line_breaks(ps);
        Node *right = parse_pipeline(ps);
        if (!right) {
            ps->incomplete = 1;
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
        if (peek(ps)->type == T_EOF || peek(ps)->type == T_RPAREN) break;
        if (at_stop(ps, stop)) break;

        Node *cmd = parse_and_or(ps);
        if (!cmd) {
            if (!ps->error && !ps->incomplete) fail(ps, "syntax error near unexpected token");
            node_free(list);
            return NULL;
        }
        if (peek(ps)->type == T_AMP) {
            cmd->background = 1;
            advance(ps);
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
        if (t->type != T_SEMI && t->type != T_NEWLINE && t->type != T_AMP) break;
    }
    return list;
}

Node *parse_string(const char *src, int *incomplete, char **error) {
    Parser ps;
    memset(&ps, 0, sizeof(ps));
    tokenize(src, &ps.tokens, &ps.incomplete);

    Node *node = parse_list(&ps, NULL);
    if (!ps.error && !ps.incomplete && peek(&ps)->type != T_EOF) {
        fail(&ps, "syntax error near unexpected token");
        node_free(node);
        node = NULL;
    }
    if (incomplete) *incomplete = ps.incomplete;
    if (error) {
        *error = ps.error;
    } else {
        free(ps.error);
    }
    token_list_free(&ps.tokens);
    return node;
}
