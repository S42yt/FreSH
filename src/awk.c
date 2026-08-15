/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "awk.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "exec.h"
#include "regex.h"
#include "shell.h"
#include "util.h"

#define AWK_STREAMS 16

typedef enum {
    E_NUMBER,
    E_STRING,
    E_REGEX,
    E_FIELD,
    E_VAR,
    E_ASSIGN,
    E_BINARY,
    E_UNARY,
    E_CONCAT,
    E_MATCH,
    E_CALL,
    E_INCREMENT,
    E_SUBSCRIPT,
    E_IN,
    E_GETLINE,
    E_TERNARY
} ExprKind;

typedef enum {
    S_PRINT,
    S_PRINTF,
    S_EXPR,
    S_IF,
    S_WHILE,
    S_DO,
    S_FOR,
    S_BLOCK,
    S_NEXT,
    S_EXIT,
    S_FOR_IN,
    S_DELETE,
    S_RETURN,
    S_BREAK,
    S_CONTINUE
} StmtKind;

typedef struct Expr {
    ExprKind kind;
    double number;
    char *text;
    char op[4];
    int negate;
    struct Expr *left;
    struct Expr *right;
    struct Expr *third;
    struct Expr **args;
    int argc;
} Expr;

typedef struct Stmt {
    StmtKind kind;
    char *name;
    char *name2;
    Expr *expr;
    Expr *second;
    Expr *third;
    struct Stmt *body;
    struct Stmt *other;
    struct Stmt *next;
    Expr **list;
    int count;
} Stmt;

typedef struct {
    Expr *pattern;
    Expr *pattern_end;
    int between;
    Stmt *action;
    int begin;
    int end;
} Rule;

typedef struct {
    char *key;
    char *value;
} Element;

typedef struct {
    char *name;
    char *value;
    Element *elements;
    size_t count;
    size_t cap;
    int is_array;
} Variable;

typedef struct Function {
    char *name;
    StrList params;
    struct Stmt *body;
} Function;

typedef struct {
    const char *source;
    char token[256];
    int type;
    int failed;
    int after_value;
} Lexer;

enum { LOOP_NONE, LOOP_BREAK, LOOP_CONTINUE };

typedef struct {
    Rule *rules;
    int rule_count;

    Function *functions;
    size_t function_count;
    size_t function_cap;

    Variable *variables;
    size_t variable_count;
    size_t variable_cap;

    int returning;
    const char *return_value;

    char **fields;
    int field_count;
    int field_cap;
    char *record;

    FILE *input;
    StrBuf record_buffer;

    StrList arena;
    int exiting;
    int skipping;
    int loop_signal;
    int printing;
    int status;
    unsigned long seed;
    unsigned long previous_seed;
} Awk;

enum { T_END, T_NUMBER, T_STRING, T_REGEX, T_NAME, T_PUNCT };

static void *awk_alloc(size_t size) {
    void *p = xmalloc(size);
    memset(p, 0, size);
    return p;
}

static void awk_fail(Awk *awk, const char *fmt, ...) {
    char message[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    fflush(stdout);
    shell_error("%s", message);
    awk->exiting = 1;
    awk->status = 2;
}

static const char *arena_add(Awk *awk, char *text) {
    sl_push(&awk->arena, text);
    return text;
}

static void arena_reset(Awk *awk) {
    sl_clear(&awk->arena);
}

static double to_number(const char *text) {
    return text ? atof(text) : 0;
}

static int looks_numeric(const char *text) {
    if (!text || !*text) return 0;
    char *end = NULL;
    strtod(text, &end);
    while (end && isspace((unsigned char)*end)) end++;
    return end && *end == '\0';
}

static Variable *variable_find(Awk *awk, const char *name) {
    for (size_t i = 0; i < awk->variable_count; i++) {
        if (strcmp(awk->variables[i].name, name) == 0) return &awk->variables[i];
    }
    return NULL;
}

static const char *variable_get(Awk *awk, const char *name) {
    Variable *v = variable_find(awk, name);
    return v && v->value ? v->value : "";
}

static const char *number_text(Awk *awk, double value) {
    char buffer[64];
    if (value == (long long)value && fabs(value) < 1e18) {
        snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
    } else {
        const char *format = variable_get(awk, awk->printing ? "OFMT" : "CONVFMT");
        snprintf(buffer, sizeof(buffer), *format ? format : "%.6g", value);
    }
    return arena_add(awk, xstrdup(buffer));
}

static Variable *variable_create(Awk *awk, const char *name) {
    Variable *v = variable_find(awk, name);
    if (v) return v;

    if (awk->variable_count + 1 >= awk->variable_cap) {
        awk->variable_cap = awk->variable_cap ? awk->variable_cap * 2 : 16;
        awk->variables = xrealloc(awk->variables, awk->variable_cap * sizeof(Variable));
    }
    v = &awk->variables[awk->variable_count++];
    memset(v, 0, sizeof(Variable));
    v->name = xstrdup(name);
    return v;
}

static void variable_set(Awk *awk, const char *name, const char *value) {
    Variable *v = variable_create(awk, name);
    free(v->value);
    v->value = xstrdup(value ? value : "");
}

static void variable_set_number(Awk *awk, const char *name, double value) {
    char buffer[64];
    if (value == (long long)value) snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
    else snprintf(buffer, sizeof(buffer), "%.6g", value);
    variable_set(awk, name, buffer);
}

static Element *element_find(Variable *v, const char *key) {
    for (size_t i = 0; i < v->count; i++) {
        if (strcmp(v->elements[i].key, key) == 0) return &v->elements[i];
    }
    return NULL;
}

static void element_set(Awk *awk, const char *name, const char *key, const char *value) {
    Variable *v = variable_create(awk, name);
    v->is_array = 1;

    Element *e = element_find(v, key);
    if (!e) {
        if (v->count + 1 >= v->cap) {
            v->cap = v->cap ? v->cap * 2 : 8;
            v->elements = xrealloc(v->elements, v->cap * sizeof(Element));
        }
        e = &v->elements[v->count++];
        e->key = xstrdup(key);
        e->value = NULL;
    }
    free(e->value);
    e->value = xstrdup(value ? value : "");
}

static const char *element_get(Awk *awk, const char *name, const char *key) {
    Variable *v = variable_find(awk, name);
    if (!v) return "";
    Element *e = element_find(v, key);
    return e ? e->value : "";
}

static void element_delete(Awk *awk, const char *name, const char *key) {
    Variable *v = variable_find(awk, name);
    if (!v) return;
    Element *e = element_find(v, key);
    if (!e) return;

    size_t index = (size_t)(e - v->elements);
    free(e->key);
    free(e->value);
    memmove(v->elements + index, v->elements + index + 1,
            (v->count - index - 1) * sizeof(Element));
    v->count--;
}

static void array_clear(Awk *awk, const char *name) {
    Variable *v = variable_find(awk, name);
    if (!v) return;
    for (size_t i = 0; i < v->count; i++) {
        free(v->elements[i].key);
        free(v->elements[i].value);
    }
    v->count = 0;
    v->is_array = 1;
}

static int is_keyword_name(const char *word) {
    static const char *WORDS[] = {"in",       "else", "while", "do",      "print",    "printf",
                                  "delete",   "return", "next", "exit",   "function", "break",
                                  "continue", "if",   "for",   "getline", NULL};
    for (int i = 0; WORDS[i]; i++) {
        if (strcmp(word, WORDS[i]) == 0) return 1;
    }
    return 0;
}

static void lex_finish(Lexer *lx) {
    if (lx->type == T_NUMBER || lx->type == T_STRING || lx->type == T_REGEX)
        lx->after_value = 1;
    else if (lx->type == T_NAME)
        lx->after_value = !is_keyword_name(lx->token);
    else if (lx->type == T_PUNCT)
        lx->after_value = strcmp(lx->token, ")") == 0 || strcmp(lx->token, "]") == 0 ||
                          strcmp(lx->token, "++") == 0 || strcmp(lx->token, "--") == 0;
    else
        lx->after_value = 0;
}

static void lex_scan(Lexer *lx) {
    const char *p = lx->source;
    while (*p == ' ' || *p == '\t' || *p == '\\' || *p == '#') {
        if (*p == '#') {
            while (*p && *p != '\n') p++;
        } else if (*p == '\\' && p[1] == '\n') {
            p += 2;
        } else if (*p == '\\' && p[1] == '\r' && p[2] == '\n') {
            p += 3;
        } else if (*p == '\\') {
            break;
        } else {
            p++;
        }
    }

    if (*p == '\r') p++;

    if (!*p) {
        lx->type = T_END;
        lx->token[0] = '\0';
        lx->source = p;
        return;
    }

    if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1]))) {
        char *end = NULL;
        strtod(p, &end);
        size_t length = (size_t)(end - p);
        if (length >= sizeof(lx->token)) length = sizeof(lx->token) - 1;
        memcpy(lx->token, p, length);
        lx->token[length] = '\0';
        lx->type = T_NUMBER;
        lx->source = end;
        return;
    }

    if (isalpha((unsigned char)*p) || *p == '_') {
        size_t length = 0;
        while ((isalnum((unsigned char)*p) || *p == '_') && length + 1 < sizeof(lx->token))
            lx->token[length++] = *p++;
        lx->token[length] = '\0';
        lx->type = T_NAME;
        lx->source = p;
        return;
    }

    if (*p == '"') {
        p++;
        size_t length = 0;
        while (*p && *p != '"' && length + 1 < sizeof(lx->token)) {
            if (*p == '\\' && p[1]) {
                p++;
                char escape = *p++;
                lx->token[length++] = escape == 'n'   ? '\n'
                                      : escape == 't' ? '\t'
                                      : escape == 'r' ? '\r'
                                      : escape == 'a' ? '\a'
                                      : escape == 'b' ? '\b'
                                      : escape == 'f' ? '\f'
                                      : escape == 'v' ? '\v'
                                      : escape == '0' ? '\0'
                                                      : escape;
                continue;
            }
            lx->token[length++] = *p++;
        }
        lx->token[length] = '\0';
        if (*p == '"') p++;
        lx->type = T_STRING;
        lx->source = p;
        return;
    }

    if (*p == '/' && !lx->after_value) {
        p++;
        size_t length = 0;
        while (*p && *p != '/' && length + 1 < sizeof(lx->token)) {
            if (*p == '\\' && p[1]) lx->token[length++] = *p++;
            lx->token[length++] = *p++;
        }
        lx->token[length] = '\0';
        if (*p == '/') p++;
        lx->type = T_REGEX;
        lx->source = p;
        return;
    }

    static const char *TWO[] = {"==", "!=", "<=", ">=", "&&", "||", "++", "--", "!~", "+=",
                                "-=", "*=", "/=", "%=", "^=", ">>", "**", NULL};
    for (int i = 0; TWO[i]; i++) {
        if (p[0] == TWO[i][0] && p[1] == TWO[i][1]) {
            lx->token[0] = p[0];
            lx->token[1] = p[1];
            lx->token[2] = '\0';
            lx->type = T_PUNCT;
            lx->source = p + 2;
            if (strcmp(lx->token, "**") == 0) {
                lx->token[0] = '^';
                lx->token[1] = '\0';
                if (*lx->source == '=') {
                    lx->token[1] = '=';
                    lx->token[2] = '\0';
                    lx->source++;
                }
            }
            return;
        }
    }

    lx->token[0] = *p;
    lx->token[1] = '\0';
    lx->type = T_PUNCT;
    lx->source = p + 1;
}

static void lex_next(Lexer *lx) {
    lex_scan(lx);
    lex_finish(lx);
}

static int is_token(Lexer *lx, const char *text) {
    return strcmp(lx->token, text) == 0;
}

static void skip_newlines(Lexer *lx) {
    while (strcmp(lx->token, "\n") == 0) lex_next(lx);
}

static int accept(Lexer *lx, const char *text) {
    if (!is_token(lx, text)) return 0;
    lex_next(lx);
    return 1;
}

static Expr *parse_expression(Lexer *lx);

static Expr *expr_new(ExprKind kind) {
    Expr *e = awk_alloc(sizeof(Expr));
    e->kind = kind;
    return e;
}

static void expr_free(Expr *e) {
    if (!e) return;
    expr_free(e->left);
    expr_free(e->right);
    expr_free(e->third);
    for (int i = 0; i < e->argc; i++) expr_free(e->args[i]);
    free(e->args);
    free(e->text);
    free(e);
}

static void expr_add_arg(Expr *e, Expr *argument) {
    e->args = xrealloc(e->args, (size_t)(e->argc + 1) * sizeof(Expr *));
    e->args[e->argc++] = argument;
}

static void parse_arguments(Lexer *lx, Expr *e, const char *closer) {
    if (is_token(lx, closer)) return;
    do {
        expr_add_arg(e, parse_expression(lx));
    } while (accept(lx, ","));
}

static Expr *parse_unary(Lexer *lx);

static Expr *parse_primary(Lexer *lx) {
    if (lx->type == T_NUMBER) {
        Expr *e = expr_new(E_NUMBER);
        e->number = atof(lx->token);
        lex_next(lx);
        return e;
    }
    if (lx->type == T_STRING) {
        Expr *e = expr_new(E_STRING);
        e->text = xstrdup(lx->token);
        lex_next(lx);
        return e;
    }
    if (lx->type == T_REGEX) {
        Expr *e = expr_new(E_REGEX);
        e->text = xstrdup(lx->token);
        lex_next(lx);
        return e;
    }
    if (is_token(lx, "$")) {
        lex_next(lx);
        Expr *e = expr_new(E_FIELD);
        e->left = parse_primary(lx);
        if (is_token(lx, "++") || is_token(lx, "--")) {
            Expr *inc = expr_new(E_INCREMENT);
            snprintf(inc->op, sizeof(inc->op), "%s", lx->token);
            inc->left = e;
            lex_next(lx);
            return inc;
        }
        return e;
    }
    if (is_token(lx, "(")) {
        lex_next(lx);
        Expr *e = parse_expression(lx);
        if (is_token(lx, ",")) {
            Expr *group = expr_new(E_SUBSCRIPT);
            group->text = NULL;
            expr_add_arg(group, e);
            while (accept(lx, ",")) expr_add_arg(group, parse_expression(lx));
            accept(lx, ")");
            if (lx->type == T_NAME && strcmp(lx->token, "in") == 0) {
                lex_next(lx);
                Expr *in = expr_new(E_IN);
                in->left = group;
                in->text = xstrdup(lx->token);
                lex_next(lx);
                return in;
            }
            return group;
        }
        accept(lx, ")");
        return e;
    }
    if (lx->type == T_NAME && strcmp(lx->token, "getline") == 0) {
        lex_next(lx);
        Expr *e = expr_new(E_GETLINE);
        if (lx->type == T_NAME && !is_keyword_name(lx->token)) {
            e->text = xstrdup(lx->token);
            lex_next(lx);
        }
        if (is_token(lx, "<")) {
            lex_next(lx);
            e->left = parse_primary(lx);
        }
        return e;
    }

    if (lx->type == T_NAME) {
        char name[256];
        snprintf(name, sizeof(name), "%s", lx->token);
        lex_next(lx);

        if (is_token(lx, "[")) {
            lex_next(lx);
            Expr *e = expr_new(E_SUBSCRIPT);
            e->text = xstrdup(name);
            parse_arguments(lx, e, "]");
            accept(lx, "]");

            if (is_token(lx, "++") || is_token(lx, "--")) {
                Expr *inc = expr_new(E_INCREMENT);
                snprintf(inc->op, sizeof(inc->op), "%s", lx->token);
                inc->left = e;
                lex_next(lx);
                return inc;
            }
            return e;
        }

        if (is_token(lx, "(")) {
            lex_next(lx);
            Expr *e = expr_new(E_CALL);
            e->text = xstrdup(name);
            parse_arguments(lx, e, ")");
            accept(lx, ")");
            return e;
        }

        Expr *e = expr_new(E_VAR);
        e->text = xstrdup(name);

        if (is_token(lx, "++") || is_token(lx, "--")) {
            Expr *inc = expr_new(E_INCREMENT);
            snprintf(inc->op, sizeof(inc->op), "%s", lx->token);
            inc->left = e;
            lex_next(lx);
            return inc;
        }
        return e;
    }

    lx->failed = 1;
    Expr *e = expr_new(E_STRING);
    e->text = xstrdup("");
    lex_next(lx);
    return e;
}

static Expr *parse_power(Lexer *lx) {
    Expr *left = parse_primary(lx);
    if (is_token(lx, "^")) {
        lex_next(lx);
        Expr *e = expr_new(E_BINARY);
        strcpy(e->op, "^");
        e->left = left;
        e->right = parse_unary(lx);
        return e;
    }
    return left;
}

static Expr *parse_unary(Lexer *lx) {
    if (is_token(lx, "!") || is_token(lx, "-") || is_token(lx, "+")) {
        char op = lx->token[0];
        lex_next(lx);
        if (op == '+') return parse_unary(lx);
        Expr *e = expr_new(E_UNARY);
        e->op[0] = op;
        e->op[1] = '\0';
        e->left = parse_unary(lx);
        return e;
    }
    if (is_token(lx, "++") || is_token(lx, "--")) {
        char op[4];
        snprintf(op, sizeof(op), "%s", lx->token);
        lex_next(lx);
        Expr *e = expr_new(E_INCREMENT);
        snprintf(e->op, sizeof(e->op), "%s", op);
        e->negate = 1;
        e->left = parse_unary(lx);
        return e;
    }
    return parse_power(lx);
}

static Expr *parse_term(Lexer *lx) {
    Expr *left = parse_unary(lx);
    while (is_token(lx, "*") || is_token(lx, "/") || is_token(lx, "%")) {
        Expr *e = expr_new(E_BINARY);
        snprintf(e->op, sizeof(e->op), "%s", lx->token);
        lex_next(lx);
        e->left = left;
        e->right = parse_unary(lx);
        left = e;
    }
    return left;
}

static Expr *parse_sum(Lexer *lx) {
    Expr *left = parse_term(lx);
    while (is_token(lx, "+") || is_token(lx, "-")) {
        Expr *e = expr_new(E_BINARY);
        snprintf(e->op, sizeof(e->op), "%s", lx->token);
        lex_next(lx);
        e->left = left;
        e->right = parse_term(lx);
        left = e;
    }
    return left;
}

static int is_keyword(Lexer *lx) {
    if (lx->type != T_NAME) return 0;
    return is_keyword_name(lx->token);
}

static Expr *parse_concat(Lexer *lx) {
    Expr *left = parse_sum(lx);

    while ((lx->type == T_STRING || lx->type == T_NUMBER ||
            (lx->type == T_NAME && !is_keyword(lx)) || is_token(lx, "$") || is_token(lx, "(")) &&
           !is_token(lx, ")")) {
        Expr *right = parse_sum(lx);
        Expr *concat = expr_new(E_CONCAT);
        concat->left = left;
        concat->right = right;
        left = concat;
    }
    return left;
}

static Expr *parse_compare(Lexer *lx) {
    Expr *left = parse_concat(lx);

    if (lx->type == T_NAME && strcmp(lx->token, "in") == 0) {
        lex_next(lx);
        Expr *e = expr_new(E_IN);
        e->left = left;
        e->text = xstrdup(lx->token);
        lex_next(lx);
        return e;
    }

    if (is_token(lx, "~") || is_token(lx, "!~")) {
        Expr *e = expr_new(E_MATCH);
        e->negate = is_token(lx, "!~");
        lex_next(lx);
        e->left = left;
        e->right = parse_concat(lx);
        return e;
    }

    while (is_token(lx, "<") || is_token(lx, ">") || is_token(lx, "<=") || is_token(lx, ">=") ||
           is_token(lx, "==") || is_token(lx, "!=")) {
        Expr *e = expr_new(E_BINARY);
        snprintf(e->op, sizeof(e->op), "%s", lx->token);
        lex_next(lx);
        e->left = left;
        e->right = parse_concat(lx);
        left = e;
    }
    return left;
}

static Expr *parse_and(Lexer *lx) {
    Expr *left = parse_compare(lx);
    while (is_token(lx, "&&")) {
        lex_next(lx);
        skip_newlines(lx);
        Expr *e = expr_new(E_BINARY);
        strcpy(e->op, "&&");
        e->left = left;
        e->right = parse_compare(lx);
        left = e;
    }
    return left;
}

static Expr *parse_or(Lexer *lx) {
    Expr *left = parse_and(lx);
    while (is_token(lx, "||")) {
        lex_next(lx);
        skip_newlines(lx);
        Expr *e = expr_new(E_BINARY);
        strcpy(e->op, "||");
        e->left = left;
        e->right = parse_and(lx);
        left = e;
    }
    return left;
}

static Expr *parse_ternary(Lexer *lx) {
    Expr *condition = parse_or(lx);
    if (!is_token(lx, "?")) return condition;

    lex_next(lx);
    Expr *e = expr_new(E_TERNARY);
    e->left = condition;
    e->right = parse_ternary(lx);
    accept(lx, ":");
    e->third = parse_ternary(lx);
    return e;
}

static Expr *parse_expression(Lexer *lx) {
    Expr *left = parse_ternary(lx);

    if (is_token(lx, "=") || is_token(lx, "+=") || is_token(lx, "-=") || is_token(lx, "*=") ||
        is_token(lx, "/=") || is_token(lx, "%=") || is_token(lx, "^=")) {
        Expr *e = expr_new(E_ASSIGN);
        snprintf(e->op, sizeof(e->op), "%s", lx->token);
        lex_next(lx);
        e->left = left;
        e->right = parse_expression(lx);
        return e;
    }
    return left;
}

static Stmt *parse_statement(Lexer *lx);

static Stmt *stmt_new(StmtKind kind) {
    Stmt *s = awk_alloc(sizeof(Stmt));
    s->kind = kind;
    return s;
}

static void stmt_free(Stmt *s) {
    while (s) {
        Stmt *next = s->next;
        expr_free(s->expr);
        expr_free(s->second);
        expr_free(s->third);
        stmt_free(s->body);
        stmt_free(s->other);
        for (int i = 0; i < s->count; i++) expr_free(s->list[i]);
        free(s->list);
        free(s->name);
        free(s->name2);
        free(s);
        s = next;
    }
}

static void skip_separators(Lexer *lx) {
    while (is_token(lx, ";") || is_token(lx, "\n")) lex_next(lx);
}

static Stmt *parse_block(Lexer *lx) {
    Stmt *block = stmt_new(S_BLOCK);
    Stmt **tail = &block->body;

    skip_separators(lx);
    while (lx->type != T_END && !is_token(lx, "}")) {
        Stmt *statement = parse_statement(lx);
        if (!statement) break;
        *tail = statement;
        tail = &statement->next;
        skip_separators(lx);
    }
    return block;
}

static void parse_output_list(Lexer *lx, Stmt *s) {
    while (lx->type != T_END && !is_token(lx, ";") && !is_token(lx, "}") && !is_token(lx, "\n") &&
           !is_token(lx, ">") && !is_token(lx, ">>") && !is_token(lx, "|")) {
        s->list = xrealloc(s->list, (size_t)(s->count + 1) * sizeof(Expr *));
        s->list[s->count++] = parse_expression(lx);
        if (!accept(lx, ",")) break;
    }

    if (is_token(lx, ">") || is_token(lx, ">>") || is_token(lx, "|")) {
        s->name = xstrdup(lx->token);
        lex_next(lx);
        s->second = parse_concat(lx);
    }
}

static Stmt *parse_statement(Lexer *lx) {
    if (is_token(lx, "{")) {
        lex_next(lx);
        Stmt *block = parse_block(lx);
        accept(lx, "}");
        return block;
    }

    if (is_token(lx, ";")) {
        lex_next(lx);
        return stmt_new(S_BLOCK);
    }

    if (lx->type == T_NAME && (strcmp(lx->token, "print") == 0 || strcmp(lx->token, "printf") == 0)) {
        Stmt *s = stmt_new(strcmp(lx->token, "print") == 0 ? S_PRINT : S_PRINTF);
        lex_next(lx);
        parse_output_list(lx, s);
        return s;
    }

    if (lx->type == T_NAME && strcmp(lx->token, "if") == 0) {
        lex_next(lx);
        accept(lx, "(");
        Stmt *s = stmt_new(S_IF);
        s->expr = parse_expression(lx);
        accept(lx, ")");
        skip_separators(lx);
        s->body = parse_statement(lx);

        Lexer probe = *lx;
        skip_separators(&probe);
        if (probe.type == T_NAME && strcmp(probe.token, "else") == 0) {
            *lx = probe;
            lex_next(lx);
            skip_separators(lx);
            s->other = parse_statement(lx);
        }
        return s;
    }

    if (lx->type == T_NAME && strcmp(lx->token, "while") == 0) {
        lex_next(lx);
        accept(lx, "(");
        Stmt *s = stmt_new(S_WHILE);
        s->expr = parse_expression(lx);
        accept(lx, ")");
        skip_separators(lx);
        if (is_token(lx, ";")) lex_next(lx);
        else s->body = parse_statement(lx);
        return s;
    }

    if (lx->type == T_NAME && strcmp(lx->token, "do") == 0) {
        lex_next(lx);
        skip_separators(lx);
        Stmt *s = stmt_new(S_DO);
        s->body = parse_statement(lx);
        skip_separators(lx);
        if (lx->type == T_NAME && strcmp(lx->token, "while") == 0) lex_next(lx);
        accept(lx, "(");
        s->expr = parse_expression(lx);
        accept(lx, ")");
        return s;
    }

    if (lx->type == T_NAME && strcmp(lx->token, "break") == 0) {
        lex_next(lx);
        return stmt_new(S_BREAK);
    }

    if (lx->type == T_NAME && strcmp(lx->token, "continue") == 0) {
        lex_next(lx);
        return stmt_new(S_CONTINUE);
    }

    if (lx->type == T_NAME && strcmp(lx->token, "delete") == 0) {
        lex_next(lx);
        Stmt *s = stmt_new(S_DELETE);
        s->name = xstrdup(lx->token);
        lex_next(lx);
        if (is_token(lx, "[")) {
            lex_next(lx);
            Expr *key = expr_new(E_SUBSCRIPT);
            parse_arguments(lx, key, "]");
            accept(lx, "]");
            s->expr = key;
        }
        return s;
    }

    if (lx->type == T_NAME && strcmp(lx->token, "return") == 0) {
        lex_next(lx);
        Stmt *s = stmt_new(S_RETURN);
        if (lx->type != T_END && !is_token(lx, ";") && !is_token(lx, "}") && !is_token(lx, "\n"))
            s->expr = parse_expression(lx);
        return s;
    }

    if (lx->type == T_NAME && strcmp(lx->token, "for") == 0) {
        Lexer probe = *lx;
        lex_next(&probe);
        if (is_token(&probe, "(")) {
            lex_next(&probe);
            if (probe.type == T_NAME) {
                char candidate[256];
                snprintf(candidate, sizeof(candidate), "%s", probe.token);
                lex_next(&probe);
                if (probe.type == T_NAME && strcmp(probe.token, "in") == 0) {
                    lex_next(&probe);
                    Stmt *s = stmt_new(S_FOR_IN);
                    s->name = xstrdup(candidate);
                    s->name2 = xstrdup(probe.token);
                    lex_next(&probe);
                    accept(&probe, ")");
                    *lx = probe;
                    skip_separators(lx);
                    s->body = parse_statement(lx);
                    return s;
                }
            }
        }

        lex_next(lx);
        accept(lx, "(");
        Stmt *s = stmt_new(S_FOR);
        if (!is_token(lx, ";")) s->expr = parse_expression(lx);
        accept(lx, ";");
        if (!is_token(lx, ";")) s->second = parse_expression(lx);
        accept(lx, ";");
        if (!is_token(lx, ")")) s->third = parse_expression(lx);
        accept(lx, ")");
        skip_separators(lx);
        if (is_token(lx, ";")) lex_next(lx);
        else s->body = parse_statement(lx);
        return s;
    }

    if (lx->type == T_NAME && strcmp(lx->token, "next") == 0) {
        lex_next(lx);
        return stmt_new(S_NEXT);
    }

    if (lx->type == T_NAME && strcmp(lx->token, "exit") == 0) {
        lex_next(lx);
        Stmt *s = stmt_new(S_EXIT);
        if (lx->type != T_END && !is_token(lx, ";") && !is_token(lx, "}") && !is_token(lx, "\n"))
            s->expr = parse_expression(lx);
        return s;
    }

    Stmt *s = stmt_new(S_EXPR);
    s->expr = parse_expression(lx);
    return s;
}

static const char *evaluate(Awk *awk, Expr *e);

static double evaluate_number(Awk *awk, Expr *e) {
    return to_number(evaluate(awk, e));
}

static const char *field_value(Awk *awk, int index) {
    if (index == 0) return awk->record ? awk->record : "";
    if (index < 1 || index > awk->field_count) return "";
    return awk->fields[index - 1] ? awk->fields[index - 1] : "";
}

static void field_reserve(Awk *awk, int wanted) {
    if (wanted <= awk->field_cap) return;
    int cap = awk->field_cap ? awk->field_cap : 32;
    while (cap < wanted) cap *= 2;
    awk->fields = xrealloc(awk->fields, (size_t)cap * sizeof(char *));
    for (int i = awk->field_cap; i < cap; i++) awk->fields[i] = NULL;
    awk->field_cap = cap;
}

static void rebuild_record(Awk *awk) {
    const char *separator = variable_get(awk, "OFS");
    if (!*separator) separator = " ";

    StrBuf sb;
    sb_init(&sb);
    for (int i = 0; i < awk->field_count; i++) {
        if (i) sb_puts(&sb, separator);
        sb_puts(&sb, awk->fields[i] ? awk->fields[i] : "");
    }
    free(awk->record);
    awk->record = sb_take(&sb);
}

static void split_record(Awk *awk, const char *line);

static void set_field_count(Awk *awk, int wanted) {
    if (wanted < 0) wanted = 0;
    field_reserve(awk, wanted + 1);
    for (int i = wanted; i < awk->field_count; i++) {
        free(awk->fields[i]);
        awk->fields[i] = NULL;
    }
    while (awk->field_count < wanted) {
        awk->fields[awk->field_count] = xstrdup("");
        awk->field_count++;
    }
    awk->field_count = wanted;
    variable_set_number(awk, "NF", awk->field_count);
    rebuild_record(awk);
}

static const char *subscript_key(Awk *awk, Expr *e) {
    if (e->argc <= 1) return e->argc == 1 ? evaluate(awk, e->args[0]) : "";

    const char *separator = variable_get(awk, "SUBSEP");
    StrBuf sb;
    sb_init(&sb);
    for (int i = 0; i < e->argc; i++) {
        if (i) sb_puts(&sb, separator);
        sb_puts(&sb, evaluate(awk, e->args[i]));
    }
    return arena_add(awk, sb_take(&sb));
}

static void assign_to(Awk *awk, Expr *target, const char *value) {
    if (!target) return;

    if (target->kind == E_VAR) {
        if (strcmp(target->text, "NF") == 0) {
            set_field_count(awk, (int)to_number(value));
            return;
        }
        variable_set(awk, target->text, value);
        return;
    }
    if (target->kind == E_SUBSCRIPT) {
        char *key = xstrdup(subscript_key(awk, target));
        element_set(awk, target->text, key, value);
        free(key);
        return;
    }
    if (target->kind == E_FIELD) {
        int index = (int)evaluate_number(awk, target->left);
        if (index == 0) {
            split_record(awk, value);
            return;
        }
        if (index < 1) {
            awk_fail(awk, "awk: attempt to assign to field %d", index);
            return;
        }
        field_reserve(awk, index);
        while (awk->field_count < index) {
            awk->fields[awk->field_count] = xstrdup("");
            awk->field_count++;
        }
        free(awk->fields[index - 1]);
        awk->fields[index - 1] = xstrdup(value);
        variable_set_number(awk, "NF", awk->field_count);
        rebuild_record(awk);
    }
}

static void execute(Awk *awk, Stmt *s);

static Function *function_find(Awk *awk, const char *name) {
    for (size_t i = 0; i < awk->function_count; i++) {
        if (strcmp(awk->functions[i].name, name) == 0) return &awk->functions[i];
    }
    return NULL;
}

static const char *call_user(Awk *awk, Function *f, Expr *e) {
    StrList saved_names;
    StrList saved_values;
    sl_init(&saved_names);
    sl_init(&saved_values);

    StrList incoming;
    sl_init(&incoming);
    for (size_t i = 0; i < f->params.len; i++)
        sl_push_copy(&incoming, (int)i < e->argc ? evaluate(awk, e->args[i]) : "");

    for (size_t i = 0; i < f->params.len; i++) {
        const char *name = f->params.items[i];
        Variable *existing = variable_find(awk, name);
        sl_push_copy(&saved_names, name);
        sl_push_copy(&saved_values, existing && existing->value ? existing->value : "");
        variable_set(awk, name, incoming.items[i]);
    }
    sl_free(&incoming);

    int saved_returning = awk->returning;
    awk->returning = 0;
    awk->return_value = "";
    execute(awk, f->body);
    const char *result = awk->return_value ? awk->return_value : "";
    awk->returning = saved_returning;

    for (size_t i = 0; i < saved_names.len; i++)
        variable_set(awk, saved_names.items[i], saved_values.items[i]);

    sl_free(&saved_names);
    sl_free(&saved_values);
    return result;
}

static int separator_is_regex(const char *separator) {
    if (!separator || !separator[0] || !separator[1]) return 0;
    return 1;
}

static int next_separator(const char *separator, const char *text, int *start, int *width) {
    if (separator_is_regex(separator)) {
        RegexMatch match;
        for (const char *p = text; ; p++) {
            if (regex_search(separator, p, &match) && match.end[0] > match.start[0]) {
                *start = (int)(p - text) + match.start[0];
                *width = match.end[0] - match.start[0];
                return 1;
            }
            if (!*p) break;
            if (match.start[0] < 0) break;
        }
        return 0;
    }

    const char *hit = strchr(text, separator[0]);
    if (!hit) return 0;
    *start = (int)(hit - text);
    *width = 1;
    return 1;
}

static int split_into(Awk *awk, const char *text, const char *array, const char *separator) {
    array_clear(awk, array);

    int index = 0;
    const char *p = text;

    if (!separator || !*separator || strcmp(separator, " ") == 0) {
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\n') p++;
            if (!*p) break;
            const char *start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
            char key[32];
            snprintf(key, sizeof(key), "%d", ++index);
            char *piece = xstrndup(start, (size_t)(p - start));
            element_set(awk, array, key, piece);
            free(piece);
        }
        return index;
    }

    while (1) {
        int start = 0;
        int width = 0;
        char key[32];
        snprintf(key, sizeof(key), "%d", ++index);

        if (!next_separator(separator, p, &start, &width)) {
            element_set(awk, array, key, p);
            break;
        }
        char *piece = xstrndup(p, (size_t)start);
        element_set(awk, array, key, piece);
        free(piece);
        p += start + width;
    }
    return index;
}

static FILE *readers[AWK_STREAMS];
static char *reader_names[AWK_STREAMS];
static FILE *writers[AWK_STREAMS];
static char *writer_names[AWK_STREAMS];
static int writer_is_pipe[AWK_STREAMS];

static FILE *reader_for(const char *path) {
    for (int i = 0; i < AWK_STREAMS; i++) {
        if (reader_names[i] && strcmp(reader_names[i], path) == 0) return readers[i];
    }
    for (int i = 0; i < AWK_STREAMS; i++) {
        if (reader_names[i]) continue;
        FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
        if (!f) return NULL;
        reader_names[i] = xstrdup(path);
        readers[i] = f;
        return f;
    }
    return NULL;
}

static FILE *writer_for(Awk *awk, const char *target, const char *mode) {
    for (int i = 0; i < AWK_STREAMS; i++) {
        if (writer_names[i] && strcmp(writer_names[i], target) == 0) return writers[i];
    }

    int pipe = strcmp(mode, "|") == 0;
    if (!pipe && (strcmp(target, "/dev/stdout") == 0 || strcmp(target, "-") == 0)) return stdout;
    if (!pipe && strcmp(target, "/dev/stderr") == 0) return stderr;

    for (int i = 0; i < AWK_STREAMS; i++) {
        if (writer_names[i]) continue;
        fflush(stdout);
        FILE *f = pipe ? _popen(target, "w") : fopen(target, strcmp(mode, ">>") == 0 ? "a" : "w");
        if (!f) {
            awk_fail(awk, "awk: cannot write to %s", target);
            return NULL;
        }
        writer_names[i] = xstrdup(target);
        writers[i] = f;
        writer_is_pipe[i] = pipe;
        return f;
    }

    awk_fail(awk, "awk: too many open output streams, %s did not open", target);
    return NULL;
}

static int stream_close(const char *name) {
    int closed = -1;
    for (int i = 0; i < AWK_STREAMS; i++) {
        if (reader_names[i] && strcmp(reader_names[i], name) == 0) {
            if (readers[i] != stdin) fclose(readers[i]);
            free(reader_names[i]);
            reader_names[i] = NULL;
            readers[i] = NULL;
            closed = 0;
        }
        if (writer_names[i] && strcmp(writer_names[i], name) == 0) {
            if (writer_is_pipe[i]) _pclose(writers[i]);
            else fclose(writers[i]);
            free(writer_names[i]);
            writer_names[i] = NULL;
            writers[i] = NULL;
            writer_is_pipe[i] = 0;
            closed = 0;
        }
    }
    return closed;
}

static void streams_close(void) {
    for (int i = 0; i < AWK_STREAMS; i++) {
        if (readers[i] && readers[i] != stdin) fclose(readers[i]);
        free(reader_names[i]);
        readers[i] = NULL;
        reader_names[i] = NULL;

        if (writers[i]) {
            if (writer_is_pipe[i]) _pclose(writers[i]);
            else fclose(writers[i]);
        }
        free(writer_names[i]);
        writers[i] = NULL;
        writer_names[i] = NULL;
        writer_is_pipe[i] = 0;
    }
}

static int read_record(Awk *awk, FILE *input, StrBuf *out) {
    sb_clear(out);
    if (!input) return 0;

    const char *rs = variable_get(awk, "RS");
    int c;

    if (!rs || !*rs) {
        while ((c = fgetc(input)) != EOF && (c == '\n' || c == '\r')) {
        }
        if (c == EOF) return 0;

        int newlines = 0;
        do {
            if (c == '\r') continue;
            if (c == '\n') {
                if (++newlines >= 2) return 1;
                continue;
            }
            if (newlines) sb_putc(out, '\n');
            newlines = 0;
            sb_putc(out, (char)c);
        } while ((c = fgetc(input)) != EOF);
        return 1;
    }

    char stop = rs[0];
    int any = 0;
    while ((c = fgetc(input)) != EOF) {
        any = 1;
        if (c == stop) break;
        sb_putc(out, (char)c);
    }
    if (!any) return 0;
    if (stop == '\n' && out->len && out->data[out->len - 1] == '\r') {
        out->len--;
        out->data[out->len] = '\0';
    }
    return 1;
}

static const char *run_getline(Awk *awk, Expr *e) {
    FILE *source = awk->input ? awk->input : stdin;
    int counts_records = 1;

    if (e->left) {
        const char *path = evaluate(awk, e->left);
        source = reader_for(path);
        counts_records = 0;
        if (!source) return "-1";
    }

    StrBuf line;
    sb_init(&line);
    if (!read_record(awk, source, &line)) {
        sb_free(&line);
        return "0";
    }

    if (e->text) {
        variable_set(awk, e->text, line.data ? line.data : "");
    } else {
        split_record(awk, line.data ? line.data : "");
    }
    if (counts_records) {
        variable_set_number(awk, "NR", to_number(variable_get(awk, "NR")) + 1);
        variable_set_number(awk, "FNR", to_number(variable_get(awk, "FNR")) + 1);
    }
    sb_free(&line);
    return "1";
}

static const char *regex_operand(Awk *awk, Expr *e) {
    if (!e) return "";
    if (e->kind == E_REGEX) return e->text;
    return evaluate(awk, e);
}

static int expr_is_numeric(Expr *e) {
    if (!e) return 0;
    switch (e->kind) {
    case E_NUMBER:
    case E_UNARY:
    case E_INCREMENT:
    case E_BINARY:
    case E_MATCH:
    case E_IN:
        return 1;
    case E_CALL:
        return e->text && strcmp(e->text, "sprintf") != 0 && strcmp(e->text, "substr") != 0 &&
               strcmp(e->text, "toupper") != 0 && strcmp(e->text, "tolower") != 0;
    default:
        return 0;
    }
}

static void format_into(Awk *awk, StrBuf *out, Expr **list, int count) {
    if (count == 0) return;
    const char *format = evaluate(awk, list[0]);
    int next = 1;

    for (const char *p = format; *p; p++) {
        if (*p != '%') {
            sb_putc(out, *p);
            continue;
        }
        p++;
        if (*p == '%') {
            sb_putc(out, '%');
            continue;
        }

        char spec[40];
        size_t length = 0;
        spec[length++] = '%';

        int stars = 0;
        while (*p && !strchr("diouxXeEfFgGcs", *p) && length + 3 < sizeof(spec)) {
            if (*p == '*') stars++;
            spec[length++] = *p++;
        }
        if (!*p) break;
        char conversion = *p;
        spec[length++] = conversion;
        spec[length] = '\0';

        while (stars-- > 0) {
            int width = next < count ? (int)to_number(evaluate(awk, list[next++])) : 0;
            char widened[40];
            char *star = strchr(spec, '*');
            if (!star) break;
            snprintf(widened, sizeof(widened), "%.*s%d%s", (int)(star - spec), spec, width,
                     star + 1);
            snprintf(spec, sizeof(spec), "%s", widened);
            length = strlen(spec);
        }

        Expr *argument = next < count ? list[next] : NULL;
        const char *value = next < count ? evaluate(awk, list[next++]) : "";

        if (conversion == 's') {
            sb_printf(out, spec, value);
        } else if (conversion == 'c') {
            if (expr_is_numeric(argument)) sb_printf(out, spec, (int)to_number(value));
            else sb_printf(out, spec, value[0] ? value[0] : ' ');
        } else if (strchr("eEfFgG", conversion)) {
            sb_printf(out, spec, to_number(value));
        } else {
            char adjusted[44];
            snprintf(adjusted, sizeof(adjusted), "%.*sll%c", (int)(length - 2), spec, conversion);
            sb_printf(out, adjusted, (long long)to_number(value));
        }
    }
}

static int do_sub(Awk *awk, Expr *e, int global) {
    if (e->argc < 2) {
        awk_fail(awk, "awk: %s needs a pattern and a replacement", e->text);
        return 0;
    }

    const char *pattern = regex_operand(awk, e->args[0]);
    char *replacement = xstrdup(evaluate(awk, e->args[1]));
    Expr *target = e->argc > 2 ? e->args[2] : NULL;
    char *source = xstrdup(target ? evaluate(awk, target) : field_value(awk, 0));

    StrBuf out;
    sb_init(&out);
    const char *cursor = source;
    int count = 0;

    for (;;) {
        RegexMatch match;
        if (!regex_search(pattern, cursor, &match)) break;

        const char *matched = cursor + match.start[0];
        size_t width = (size_t)(match.end[0] - match.start[0]);
        sb_putn(&out, cursor, (size_t)match.start[0]);

        for (const char *r = replacement; *r; r++) {
            if (*r == '\\' && (r[1] == '&' || r[1] == '\\')) {
                sb_putc(&out, r[1]);
                r++;
                continue;
            }
            if (*r == '&') {
                sb_putn(&out, matched, width);
                continue;
            }
            sb_putc(&out, *r);
        }

        count++;
        cursor = matched + width;
        if (width == 0) {
            if (!*cursor) break;
            sb_putc(&out, *cursor);
            cursor++;
        }
        if (!global) break;
    }
    sb_puts(&out, cursor);

    if (count) {
        char *result = sb_take(&out);
        if (target) assign_to(awk, target, result);
        else split_record(awk, result);
        free(result);
    } else {
        sb_free(&out);
    }

    free(replacement);
    free(source);
    return count;
}

static double awk_random(Awk *awk) {
    awk->seed = awk->seed * 6364136223846793005UL + 1442695040888963407UL;
    return (double)((awk->seed >> 17) & 0x7fffffff) / 2147483648.0;
}

static const char *call_builtin(Awk *awk, Expr *e) {
    const char *name = e->text;
    Expr *first = e->argc > 0 ? e->args[0] : NULL;
    Expr *second = e->argc > 1 ? e->args[1] : NULL;
    Expr *third = e->argc > 2 ? e->args[2] : NULL;

    Function *user = function_find(awk, name);
    if (user) {
        if (e->argc > (int)user->params.len) {
            awk_fail(awk, "awk: %s takes %d arguments, %d given", name, (int)user->params.len,
                     e->argc);
            return "";
        }
        return call_user(awk, user, e);
    }

    if (strcmp(name, "split") == 0) {
        char *copy = xstrdup(first ? evaluate(awk, first) : "");
        const char *array = second && second->text ? second->text : "";
        char *separator = xstrdup(third ? regex_operand(awk, third) : variable_get(awk, "FS"));
        int count = split_into(awk, copy, array, separator);
        free(separator);
        free(copy);
        return number_text(awk, count);
    }

    if (strcmp(name, "length") == 0) {
        const char *text = first ? evaluate(awk, first) : field_value(awk, 0);
        if (first && first->kind == E_VAR) {
            Variable *v = variable_find(awk, first->text);
            if (v && v->is_array) return number_text(awk, (double)v->count);
        }
        return number_text(awk, (double)strlen(text));
    }
    if (strcmp(name, "toupper") == 0 || strcmp(name, "tolower") == 0) {
        char *text = xstrdup(first ? evaluate(awk, first) : "");
        for (char *p = text; *p; p++)
            *p = name[2] == 'u' ? (char)toupper((unsigned char)*p)
                                : (char)tolower((unsigned char)*p);
        return arena_add(awk, text);
    }
    if (strcmp(name, "int") == 0) return number_text(awk, (double)(long long)evaluate_number(awk, first));
    if (strcmp(name, "sqrt") == 0) return number_text(awk, sqrt(evaluate_number(awk, first)));
    if (strcmp(name, "sin") == 0) return number_text(awk, sin(evaluate_number(awk, first)));
    if (strcmp(name, "cos") == 0) return number_text(awk, cos(evaluate_number(awk, first)));
    if (strcmp(name, "exp") == 0) return number_text(awk, exp(evaluate_number(awk, first)));
    if (strcmp(name, "log") == 0) return number_text(awk, log(evaluate_number(awk, first)));
    if (strcmp(name, "atan2") == 0)
        return number_text(awk, atan2(evaluate_number(awk, first), evaluate_number(awk, second)));

    if (strcmp(name, "rand") == 0) return number_text(awk, awk_random(awk));
    if (strcmp(name, "srand") == 0) {
        unsigned long previous = awk->previous_seed;
        unsigned long seed = first ? (unsigned long)evaluate_number(awk, first)
                                   : (unsigned long)time(NULL);
        awk->previous_seed = seed;
        awk->seed = seed ? seed : 1;
        return number_text(awk, (double)previous);
    }

    if (strcmp(name, "substr") == 0) {
        const char *text = first ? evaluate(awk, first) : "";
        int size = (int)strlen(text);
        double start_value = second ? evaluate_number(awk, second) : 1;
        int start = (int)(start_value < 0 ? start_value - 0.5 : start_value + 0.5);
        int length = third ? (int)(evaluate_number(awk, third) + 0.5) : size - start + 1;

        if (start < 1) {
            length += start - 1;
            start = 1;
        }
        if (length < 0) length = 0;
        if (start > size) return "";
        if (start - 1 + length > size) length = size - start + 1;
        return arena_add(awk, xstrndup(text + start - 1, (size_t)length));
    }
    if (strcmp(name, "index") == 0) {
        const char *haystack = first ? evaluate(awk, first) : "";
        char *needle = xstrdup(second ? evaluate(awk, second) : "");
        const char *hit = strstr(haystack, needle);
        double position = hit ? (double)(hit - haystack + 1) : 0;
        free(needle);
        return number_text(awk, position);
    }

    if (strcmp(name, "sub") == 0) return number_text(awk, do_sub(awk, e, 0));
    if (strcmp(name, "gsub") == 0) return number_text(awk, do_sub(awk, e, 1));

    if (strcmp(name, "match") == 0) {
        const char *text = first ? evaluate(awk, first) : "";
        const char *pattern = regex_operand(awk, second);
        RegexMatch match;
        if (regex_search(pattern, text, &match)) {
            variable_set_number(awk, "RSTART", match.start[0] + 1);
            variable_set_number(awk, "RLENGTH", match.end[0] - match.start[0]);
            return number_text(awk, match.start[0] + 1);
        }
        variable_set_number(awk, "RSTART", 0);
        variable_set_number(awk, "RLENGTH", -1);
        return number_text(awk, 0);
    }

    if (strcmp(name, "sprintf") == 0) {
        StrBuf sb;
        sb_init(&sb);
        format_into(awk, &sb, e->args, e->argc);
        return arena_add(awk, sb_take(&sb));
    }

    if (strcmp(name, "system") == 0) {
        char *command = xstrdup(first ? evaluate(awk, first) : "");
        fflush(stdout);
        fflush(stderr);
        int status = exec_subshell(command);
        free(command);
        return number_text(awk, status);
    }

    if (strcmp(name, "close") == 0) {
        char *target = xstrdup(first ? evaluate(awk, first) : "");
        int result = stream_close(target);
        free(target);
        return number_text(awk, result);
    }

    if (strcmp(name, "fflush") == 0) {
        fflush(stdout);
        return "0";
    }

    awk_fail(awk, "awk: calling undefined function %s", name);
    return "";
}

static const char *evaluate(Awk *awk, Expr *e) {
    if (!e || awk->exiting) return "";

    switch (e->kind) {
    case E_NUMBER: return number_text(awk, e->number);
    case E_STRING: return e->text;
    case E_REGEX: return regex_search(e->text, field_value(awk, 0), NULL) ? "1" : "0";
    case E_FIELD: return field_value(awk, (int)evaluate_number(awk, e->left));

    case E_VAR: {
        Variable *v = variable_find(awk, e->text);
        if (!v && strcmp(e->text, "length") == 0)
            return number_text(awk, (double)strlen(field_value(awk, 0)));
        return v && v->value ? v->value : "";
    }

    case E_CONCAT: {
        int printing = awk->printing;
        awk->printing = 0;
        const char *left = evaluate(awk, e->left);
        StrBuf sb;
        sb_init(&sb);
        sb_puts(&sb, left);
        sb_puts(&sb, evaluate(awk, e->right));
        awk->printing = printing;
        return arena_add(awk, sb_take(&sb));
    }

    case E_MATCH: {
        char *text = xstrdup(evaluate(awk, e->left));
        const char *pattern = regex_operand(awk, e->right);
        int matched = regex_search(pattern, text, NULL);
        free(text);
        return (e->negate ? !matched : matched) ? "1" : "0";
    }

    case E_TERNARY:
        return evaluate_number(awk, e->left) != 0 ? evaluate(awk, e->right)
                                                  : evaluate(awk, e->third);

    case E_UNARY:
        if (e->op[0] == '!') return evaluate_number(awk, e->left) == 0 ? "1" : "0";
        return number_text(awk, -evaluate_number(awk, e->left));

    case E_INCREMENT: {
        double value = evaluate_number(awk, e->left);
        double updated = e->op[0] == '+' ? value + 1 : value - 1;
        const char *written = number_text(awk, updated);
        assign_to(awk, e->left, written);
        return e->negate ? written : number_text(awk, value);
    }

    case E_ASSIGN: {
        const char *value = evaluate(awk, e->right);
        if (e->op[0] != '=') {
            double current = evaluate_number(awk, e->left);
            double operand = to_number(value);
            double result = e->op[0] == '+'   ? current + operand
                            : e->op[0] == '-' ? current - operand
                            : e->op[0] == '*' ? current * operand
                            : e->op[0] == '^' ? pow(current, operand)
                            : e->op[0] == '%' ? (operand != 0 ? fmod(current, operand) : 0)
                                              : (operand != 0 ? current / operand : 0);
            if (operand == 0 && (e->op[0] == '/' || e->op[0] == '%')) {
                awk_fail(awk, "awk: division by zero");
                return "";
            }
            value = number_text(awk, result);
        }
        assign_to(awk, e->left, value);
        return value;
    }

    case E_CALL: return call_builtin(awk, e);

    case E_SUBSCRIPT: {
        char *key = xstrdup(subscript_key(awk, e));
        if (!e->text) return arena_add(awk, key);
        const char *value = element_get(awk, e->text, key);
        free(key);
        return value;
    }

    case E_IN: {
        const char *key = e->left->kind == E_SUBSCRIPT && !e->left->text
                              ? subscript_key(awk, e->left)
                              : evaluate(awk, e->left);
        char *copy = xstrdup(key);
        Variable *v = variable_find(awk, e->text);
        int found = v && element_find(v, copy) != NULL;
        free(copy);
        return found ? "1" : "0";
    }

    case E_GETLINE: return run_getline(awk, e);

    case E_BINARY: {
        if (strcmp(e->op, "&&") == 0)
            return evaluate_number(awk, e->left) != 0 && evaluate_number(awk, e->right) != 0 ? "1"
                                                                                            : "0";
        if (strcmp(e->op, "||") == 0)
            return evaluate_number(awk, e->left) != 0 || evaluate_number(awk, e->right) != 0 ? "1"
                                                                                            : "0";

        int printing = awk->printing;
        awk->printing = 0;
        char *left_copy = xstrdup(evaluate(awk, e->left));
        const char *right = evaluate(awk, e->right);
        awk->printing = printing;

        int compared;
        int numeric = looks_numeric(left_copy) && looks_numeric(right);
        if (numeric) {
            double a = to_number(left_copy);
            double b = to_number(right);
            compared = a < b ? -1 : a > b ? 1 : 0;
        } else {
            compared = strcmp(left_copy, right);
        }

        const char *result = NULL;
        if (strcmp(e->op, "==") == 0) result = compared == 0 ? "1" : "0";
        else if (strcmp(e->op, "!=") == 0) result = compared != 0 ? "1" : "0";
        else if (strcmp(e->op, "<") == 0) result = compared < 0 ? "1" : "0";
        else if (strcmp(e->op, ">") == 0) result = compared > 0 ? "1" : "0";
        else if (strcmp(e->op, "<=") == 0) result = compared <= 0 ? "1" : "0";
        else if (strcmp(e->op, ">=") == 0) result = compared >= 0 ? "1" : "0";

        if (result) {
            free(left_copy);
            return result;
        }

        double a = to_number(left_copy);
        double b = to_number(right);
        free(left_copy);

        if (b == 0 && (e->op[0] == '/' || e->op[0] == '%')) {
            awk_fail(awk, "awk: division by zero");
            return "";
        }

        double value = e->op[0] == '+'   ? a + b
                       : e->op[0] == '-' ? a - b
                       : e->op[0] == '*' ? a * b
                       : e->op[0] == '/' ? a / b
                       : e->op[0] == '^' ? pow(a, b)
                                         : fmod(a, b);
        return number_text(awk, value);
    }
    }
    return "";
}

static FILE *output_for(Awk *awk, Stmt *s) {
    if (!s->name || !s->second) return stdout;
    char *target = xstrdup(evaluate(awk, s->second));
    FILE *out = writer_for(awk, target, s->name);
    free(target);
    return out;
}

static void run_print(Awk *awk, Stmt *s) {
    FILE *out = output_for(awk, s);
    if (!out) return;

    const char *separator = variable_get(awk, "OFS");
    const char *terminator = variable_get(awk, "ORS");
    if (!*separator) separator = " ";

    awk->printing = 1;
    if (s->count == 0) {
        fputs(field_value(awk, 0), out);
    } else {
        for (int i = 0; i < s->count; i++) {
            if (i > 0) fputs(separator, out);
            fputs(evaluate(awk, s->list[i]), out);
        }
    }
    awk->printing = 0;
    fputs(*terminator ? terminator : "\n", out);
}

static void run_printf(Awk *awk, Stmt *s) {
    FILE *out = output_for(awk, s);
    if (!out) return;

    StrBuf sb;
    sb_init(&sb);
    format_into(awk, &sb, s->list, s->count);
    if (sb.len) fwrite(sb.data, 1, sb.len, out);
    sb_free(&sb);
}

static int loop_running(Awk *awk) {
    return !awk->exiting && !awk->returning && !awk->skipping && !shell.interrupted;
}

static int loop_step(Awk *awk) {
    if (awk->loop_signal == LOOP_BREAK) {
        awk->loop_signal = LOOP_NONE;
        return 0;
    }
    if (awk->loop_signal == LOOP_CONTINUE) awk->loop_signal = LOOP_NONE;
    return 1;
}

static void execute(Awk *awk, Stmt *s) {
    for (; s && !awk->exiting && !awk->skipping && !awk->returning && !awk->loop_signal;
         s = s->next) {
        switch (s->kind) {
        case S_RETURN:
            awk->return_value = s->expr ? evaluate(awk, s->expr) : "";
            awk->returning = 1;
            break;

        case S_BREAK:
            awk->loop_signal = LOOP_BREAK;
            break;

        case S_CONTINUE:
            awk->loop_signal = LOOP_CONTINUE;
            break;

        case S_DELETE:
            if (s->expr) {
                char *key = xstrdup(subscript_key(awk, s->expr));
                element_delete(awk, s->name, key);
                free(key);
            } else {
                array_clear(awk, s->name);
            }
            break;

        case S_FOR_IN: {
            Variable *v = variable_find(awk, s->name2);
            if (!v) break;

            StrList keys;
            sl_init(&keys);
            for (size_t i = 0; i < v->count; i++) sl_push_copy(&keys, v->elements[i].key);

            for (size_t i = 0; i < keys.len && loop_running(awk); i++) {
                variable_set(awk, s->name, keys.items[i]);
                execute(awk, s->body);
                if (!loop_step(awk)) break;
            }
            sl_free(&keys);
            break;
        }

        case S_BLOCK:
            execute(awk, s->body);
            break;

        case S_PRINT:
            run_print(awk, s);
            break;

        case S_PRINTF:
            run_printf(awk, s);
            break;

        case S_EXPR:
            evaluate(awk, s->expr);
            break;

        case S_IF:
            if (evaluate_number(awk, s->expr) != 0) execute(awk, s->body);
            else if (s->other) execute(awk, s->other);
            break;

        case S_WHILE:
            while (loop_running(awk) && evaluate_number(awk, s->expr) != 0) {
                execute(awk, s->body);
                if (!loop_step(awk)) break;
            }
            break;

        case S_DO:
            do {
                execute(awk, s->body);
                if (!loop_step(awk)) break;
            } while (loop_running(awk) && evaluate_number(awk, s->expr) != 0);
            break;

        case S_FOR:
            if (s->expr) evaluate(awk, s->expr);
            while (loop_running(awk) && (!s->second || evaluate_number(awk, s->second) != 0)) {
                execute(awk, s->body);
                if (!loop_step(awk)) break;
                if (s->third) evaluate(awk, s->third);
            }
            break;

        case S_NEXT:
            awk->skipping = 1;
            break;

        case S_EXIT:
            if (s->expr) awk->status = (int)evaluate_number(awk, s->expr);
            awk->exiting = 1;
            break;
        }
    }
}

static void split_record(Awk *awk, const char *line) {
    char *copy = xstrdup(line);
    for (int i = 0; i < awk->field_count; i++) {
        free(awk->fields[i]);
        awk->fields[i] = NULL;
    }
    awk->field_count = 0;
    free(awk->record);
    awk->record = copy;

    const char *separator = variable_get(awk, "FS");
    const char *p = copy;

    if (!separator || !*separator || strcmp(separator, " ") == 0) {
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\n') p++;
            if (!*p) break;
            const char *start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
            field_reserve(awk, awk->field_count + 1);
            awk->fields[awk->field_count++] = xstrndup(start, (size_t)(p - start));
        }
    } else if (strcmp(separator, "") == 0) {
        while (*p) {
            field_reserve(awk, awk->field_count + 1);
            awk->fields[awk->field_count++] = xstrndup(p, 1);
            p++;
        }
    } else {
        while (1) {
            int start = 0;
            int width = 0;
            field_reserve(awk, awk->field_count + 1);
            if (!next_separator(separator, p, &start, &width)) {
                awk->fields[awk->field_count++] = xstrdup(p);
                break;
            }
            awk->fields[awk->field_count++] = xstrndup(p, (size_t)start);
            p += start + width;
        }
    }

    variable_set_number(awk, "NF", awk->field_count);
}

static int rule_matches(Awk *awk, Rule *rule) {
    if (!rule->pattern) return 1;

    if (!rule->pattern_end) return evaluate_number(awk, rule->pattern) != 0;

    if (!rule->between) {
        if (evaluate_number(awk, rule->pattern) == 0) return 0;
        rule->between = evaluate_number(awk, rule->pattern_end) == 0;
        return 1;
    }
    if (evaluate_number(awk, rule->pattern_end) != 0) rule->between = 0;
    return 1;
}

static void run_rules(Awk *awk, int begin, int end) {
    for (int i = 0; i < awk->rule_count && !awk->exiting && !awk->skipping; i++) {
        Rule *rule = &awk->rules[i];
        if (rule->begin != begin || rule->end != end) continue;

        if (!begin && !end && !rule_matches(awk, rule)) continue;

        if (!rule->action) {
            const char *terminator = variable_get(awk, "ORS");
            fputs(field_value(awk, 0), stdout);
            fputs(*terminator ? terminator : "\n", stdout);
            continue;
        }
        execute(awk, rule->action);
        awk->loop_signal = LOOP_NONE;
    }
}

static void parse_program(Awk *awk, const char *program) {
    Lexer lx;
    memset(&lx, 0, sizeof(lx));
    lx.source = program;
    lex_next(&lx);

    while (lx.type != T_END) {
        while (is_token(&lx, ";") || is_token(&lx, "\n")) lex_next(&lx);
        if (lx.type == T_END) break;

        if (lx.type == T_NAME && (strcmp(lx.token, "function") == 0 || strcmp(lx.token, "func") == 0)) {
            lex_next(&lx);
            if (awk->function_count + 1 >= awk->function_cap) {
                awk->function_cap = awk->function_cap ? awk->function_cap * 2 : 8;
                awk->functions = xrealloc(awk->functions, awk->function_cap * sizeof(Function));
            }
            Function *f = &awk->functions[awk->function_count++];
            memset(f, 0, sizeof(Function));
            f->name = xstrdup(lx.token);
            sl_init(&f->params);
            lex_next(&lx);

            accept(&lx, "(");
            while (lx.type == T_NAME) {
                sl_push_copy(&f->params, lx.token);
                lex_next(&lx);
                if (!accept(&lx, ",")) break;
            }
            accept(&lx, ")");
            skip_separators(&lx);

            accept(&lx, "{");
            f->body = parse_block(&lx);
            accept(&lx, "}");
            continue;
        }

        awk->rules = xrealloc(awk->rules, (size_t)(awk->rule_count + 1) * sizeof(Rule));
        Rule *rule = &awk->rules[awk->rule_count++];
        memset(rule, 0, sizeof(Rule));

        if (lx.type == T_NAME && strcmp(lx.token, "BEGIN") == 0) {
            rule->begin = 1;
            lex_next(&lx);
        } else if (lx.type == T_NAME && strcmp(lx.token, "END") == 0) {
            rule->end = 1;
            lex_next(&lx);
        } else if (!is_token(&lx, "{")) {
            rule->pattern = parse_expression(&lx);
            if (accept(&lx, ",")) rule->pattern_end = parse_expression(&lx);
        }

        if (is_token(&lx, "{")) {
            lex_next(&lx);
            rule->action = parse_block(&lx);
            accept(&lx, "}");
        }
    }

    if (lx.failed) shell_error("awk: the program has a syntax error");
}

static char *read_program_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    StrBuf sb;
    sb_init(&sb);
    char chunk[4096];
    size_t read;
    while ((read = fread(chunk, 1, sizeof(chunk), f)) > 0) sb_putn(&sb, chunk, read);
    fclose(f);
    return sb_take(&sb);
}

static void run_input(Awk *awk, FILE *input) {
    awk->input = input;
    variable_set(awk, "FNR", "0");

    while (!awk->exiting && !shell.interrupted &&
           read_record(awk, input, &awk->record_buffer)) {
        variable_set_number(awk, "NR", to_number(variable_get(awk, "NR")) + 1);
        variable_set_number(awk, "FNR", to_number(variable_get(awk, "FNR")) + 1);

        split_record(awk, awk->record_buffer.data ? awk->record_buffer.data : "");
        awk->skipping = 0;
        run_rules(awk, 0, 0);
        awk->skipping = 0;
        arena_reset(awk);
    }
    awk->input = NULL;
}

int awk_main(int argc, char **argv) {
    Awk awk;
    memset(&awk, 0, sizeof(awk));
    sl_init(&awk.arena);
    sb_init(&awk.record_buffer);
    awk.seed = 1;

    variable_set(&awk, "FS", " ");
    variable_set(&awk, "OFS", " ");
    variable_set(&awk, "ORS", "\n");
    variable_set(&awk, "RS", "\n");
    variable_set(&awk, "NR", "0");
    variable_set(&awk, "NF", "0");
    variable_set(&awk, "FNR", "0");
    variable_set(&awk, "SUBSEP", "\034");
    variable_set(&awk, "CONVFMT", "%.6g");
    variable_set(&awk, "OFMT", "%.6g");
    variable_set(&awk, "RSTART", "0");
    variable_set(&awk, "RLENGTH", "-1");

    StrBuf program;
    sb_init(&program);
    int have_program = 0;

    int index = 1;
    for (; index < argc; index++) {
        if (strcmp(argv[index], "--") == 0) {
            index++;
            break;
        }
        if (strcmp(argv[index], "-F") == 0 && index + 1 < argc) {
            variable_set(&awk, "FS", argv[++index]);
            continue;
        }
        if (strncmp(argv[index], "-F", 2) == 0 && argv[index][2]) {
            const char *value = argv[index] + 2;
            variable_set(&awk, "FS", strcmp(value, "t") == 0 ? "\t" : value);
            continue;
        }
        if ((strcmp(argv[index], "-f") == 0 || strcmp(argv[index], "--file") == 0) &&
            index + 1 < argc) {
            char *text = read_program_file(argv[++index]);
            if (!text) {
                shell_error("awk: %s: no such file", argv[index]);
                sb_free(&program);
                return 2;
            }
            if (have_program) sb_putc(&program, '\n');
            sb_puts(&program, text);
            free(text);
            have_program = 1;
            continue;
        }
        if (strncmp(argv[index], "-f", 2) == 0 && argv[index][2]) {
            char *text = read_program_file(argv[index] + 2);
            if (!text) {
                shell_error("awk: %s: no such file", argv[index] + 2);
                sb_free(&program);
                return 2;
            }
            if (have_program) sb_putc(&program, '\n');
            sb_puts(&program, text);
            free(text);
            have_program = 1;
            continue;
        }
        if (strcmp(argv[index], "-v") == 0 && index + 1 < argc) {
            char *pair = argv[++index];
            char *eq = strchr(pair, '=');
            if (eq) {
                *eq = '\0';
                variable_set(&awk, pair, eq + 1);
                *eq = '=';
            }
            continue;
        }
        if (strncmp(argv[index], "-v", 2) == 0 && argv[index][2]) {
            char *pair = xstrdup(argv[index] + 2);
            char *eq = strchr(pair, '=');
            if (eq) {
                *eq = '\0';
                variable_set(&awk, pair, eq + 1);
            }
            free(pair);
            continue;
        }
        break;
    }

    if (!have_program) {
        if (index >= argc) {
            shell_error("awk: usage: awk [-F sep] [-v name=value] [-f file | 'program'] [file...]");
            sb_free(&program);
            return 2;
        }
        sb_puts(&program, argv[index++]);
    }

    parse_program(&awk, program.data ? program.data : "");
    sb_free(&program);

    run_rules(&awk, 1, 0);
    awk.skipping = 0;
    awk.loop_signal = LOOP_NONE;

    int file_index = index;
    int read_any = 0;

    for (; file_index < argc && !awk.exiting; file_index++) {
        char *pair = strchr(argv[file_index], '=');
        if (pair && pair != argv[file_index]) {
            *pair = '\0';
            variable_set(&awk, argv[file_index], pair + 1);
            *pair = '=';
            continue;
        }

        FILE *input = strcmp(argv[file_index], "-") == 0 ? stdin : fopen(argv[file_index], "rb");
        if (!input) {
            shell_error("awk: %s: no such file", argv[file_index]);
            awk.status = 2;
            continue;
        }
        variable_set(&awk, "FILENAME", argv[file_index]);
        read_any = 1;
        run_input(&awk, input);
        if (input != stdin) fclose(input);
    }

    if (!read_any && !awk.exiting) run_input(&awk, stdin);

    awk.exiting = 0;
    awk.skipping = 0;
    run_rules(&awk, 0, 1);
    arena_reset(&awk);

    fflush(stdout);
    streams_close();

    for (int i = 0; i < awk.field_count; i++) free(awk.fields[i]);
    free(awk.fields);
    free(awk.record);
    sb_free(&awk.record_buffer);

    for (size_t i = 0; i < awk.variable_count; i++) {
        free(awk.variables[i].name);
        free(awk.variables[i].value);
        for (size_t e = 0; e < awk.variables[i].count; e++) {
            free(awk.variables[i].elements[e].key);
            free(awk.variables[i].elements[e].value);
        }
        free(awk.variables[i].elements);
    }
    for (size_t i = 0; i < awk.function_count; i++) {
        free(awk.functions[i].name);
        sl_free(&awk.functions[i].params);
        stmt_free(awk.functions[i].body);
    }
    for (int i = 0; i < awk.rule_count; i++) {
        expr_free(awk.rules[i].pattern);
        expr_free(awk.rules[i].pattern_end);
        stmt_free(awk.rules[i].action);
    }
    free(awk.functions);
    free(awk.variables);
    free(awk.rules);
    sl_free(&awk.arena);
    return awk.status;
}
