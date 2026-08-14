/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "awk.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "regex.h"
#include "shell.h"
#include "util.h"

#define AWK_FIELDS 256

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
    E_GETLINE
} ExprKind;

typedef enum {
    S_PRINT,
    S_PRINTF,
    S_EXPR,
    S_IF,
    S_WHILE,
    S_FOR,
    S_BLOCK,
    S_NEXT,
    S_EXIT,
    S_FOR_IN,
    S_DELETE,
    S_RETURN
} StmtKind;

typedef struct Expr {
    ExprKind kind;
    double number;
    char *text;
    char op[3];
    int negate;
    struct Expr *left;
    struct Expr *right;
    struct Expr *third;
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
} Lexer;

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

    char *fields[AWK_FIELDS];
    int field_count;
    char *record;

    StrList arena;
    int exiting;
    int skipping;
    int status;
} Awk;

enum { T_END, T_NUMBER, T_STRING, T_REGEX, T_NAME, T_PUNCT };

static void *awk_alloc(size_t size) {
    void *p = xmalloc(size);
    memset(p, 0, size);
    return p;
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

static const char *number_text(Awk *awk, double value) {
    char buffer[64];
    if (value == (long long)value) snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
    else snprintf(buffer, sizeof(buffer), "%.6g", value);
    return arena_add(awk, xstrdup(buffer));
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

static void lex_next(Lexer *lx) {
    const char *p = lx->source;
    while (*p == ' ' || *p == '\t' || *p == '\\') {
        if (*p == '\\' && p[1] == '\n') p += 2;
        else if (*p == '\\') break;
        else p++;
    }

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

    if (*p == '/') {
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
                                "-=", "*=", "/=", NULL};
    for (int i = 0; TWO[i]; i++) {
        if (p[0] == TWO[i][0] && p[1] == TWO[i][1]) {
            lx->token[0] = p[0];
            lx->token[1] = p[1];
            lx->token[2] = '\0';
            lx->type = T_PUNCT;
            lx->source = p + 2;
            return;
        }
    }

    lx->token[0] = *p;
    lx->token[1] = '\0';
    lx->type = T_PUNCT;
    lx->source = p + 1;
}

static int is_token(Lexer *lx, const char *text) {
    return strcmp(lx->token, text) == 0;
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
        return e;
    }
    if (is_token(lx, "!")) {
        lex_next(lx);
        Expr *e = expr_new(E_UNARY);
        strcpy(e->op, "!");
        e->left = parse_primary(lx);
        return e;
    }
    if (is_token(lx, "-")) {
        lex_next(lx);
        Expr *e = expr_new(E_UNARY);
        strcpy(e->op, "-");
        e->left = parse_primary(lx);
        return e;
    }
    if (is_token(lx, "(")) {
        lex_next(lx);
        Expr *e = parse_expression(lx);
        accept(lx, ")");
        return e;
    }
    if (lx->type == T_NAME && strcmp(lx->token, "getline") == 0) {
        lex_next(lx);
        Expr *e = expr_new(E_GETLINE);
        if (lx->type == T_NAME && strcmp(lx->token, "in") != 0 && !is_token(lx, "<")) {
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
            e->left = parse_expression(lx);
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
            if (!is_token(lx, ")")) {
                e->left = parse_expression(lx);
                if (accept(lx, ",")) e->right = parse_expression(lx);
                if (accept(lx, ",")) e->third = parse_expression(lx);
            }
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

static int is_keyword(Lexer *lx) {
    static const char *WORDS[] = {"in", "else", "while", "do", "print", "printf", "delete",
                                  "return", "next", "exit", "function", NULL};
    if (lx->type != T_NAME) return 0;
    for (int i = 0; WORDS[i]; i++) {
        if (strcmp(lx->token, WORDS[i]) == 0) return 1;
    }
    return 0;
}

static Expr *parse_concat(Lexer *lx) {
    Expr *left = parse_primary(lx);

    while ((lx->type == T_STRING || lx->type == T_NUMBER ||
            (lx->type == T_NAME && !is_keyword(lx)) || is_token(lx, "$") || is_token(lx, "(")) &&
           !is_token(lx, ")")) {
        Expr *right = parse_primary(lx);
        Expr *concat = expr_new(E_CONCAT);
        concat->left = left;
        concat->right = right;
        left = concat;
    }
    return left;
}

static Expr *parse_term(Lexer *lx) {
    Expr *left = parse_concat(lx);
    while (is_token(lx, "*") || is_token(lx, "/") || is_token(lx, "%")) {
        Expr *e = expr_new(E_BINARY);
        snprintf(e->op, sizeof(e->op), "%s", lx->token);
        lex_next(lx);
        e->left = left;
        e->right = parse_concat(lx);
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

static Expr *parse_compare(Lexer *lx) {
    Expr *left = parse_sum(lx);

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
        e->right = parse_sum(lx);
        return e;
    }

    while (is_token(lx, "<") || is_token(lx, ">") || is_token(lx, "<=") || is_token(lx, ">=") ||
           is_token(lx, "==") || is_token(lx, "!=")) {
        Expr *e = expr_new(E_BINARY);
        snprintf(e->op, sizeof(e->op), "%s", lx->token);
        lex_next(lx);
        e->left = left;
        e->right = parse_sum(lx);
        left = e;
    }
    return left;
}

static Expr *parse_and(Lexer *lx) {
    Expr *left = parse_compare(lx);
    while (is_token(lx, "&&")) {
        lex_next(lx);
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
        Expr *e = expr_new(E_BINARY);
        strcpy(e->op, "||");
        e->left = left;
        e->right = parse_and(lx);
        left = e;
    }
    return left;
}

static Expr *parse_expression(Lexer *lx) {
    Expr *left = parse_or(lx);

    if (is_token(lx, "=") || is_token(lx, "+=") || is_token(lx, "-=") || is_token(lx, "*=") ||
        is_token(lx, "/=")) {
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

static Stmt *parse_statement(Lexer *lx) {
    if (is_token(lx, "{")) {
        lex_next(lx);
        Stmt *block = parse_block(lx);
        accept(lx, "}");
        return block;
    }

    if (lx->type == T_NAME && strcmp(lx->token, "print") == 0) {
        lex_next(lx);
        Stmt *s = stmt_new(S_PRINT);
        while (lx->type != T_END && !is_token(lx, ";") && !is_token(lx, "}") &&
               !is_token(lx, "\n")) {
            s->list = xrealloc(s->list, (size_t)(s->count + 1) * sizeof(Expr *));
            s->list[s->count++] = parse_expression(lx);
            if (!accept(lx, ",")) break;
        }
        return s;
    }

    if (lx->type == T_NAME && strcmp(lx->token, "printf") == 0) {
        lex_next(lx);
        Stmt *s = stmt_new(S_PRINTF);
        while (lx->type != T_END && !is_token(lx, ";") && !is_token(lx, "}") &&
               !is_token(lx, "\n")) {
            s->list = xrealloc(s->list, (size_t)(s->count + 1) * sizeof(Expr *));
            s->list[s->count++] = parse_expression(lx);
            if (!accept(lx, ",")) break;
        }
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
        skip_separators(lx);
        if (lx->type == T_NAME && strcmp(lx->token, "else") == 0) {
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
        s->body = parse_statement(lx);
        return s;
    }

    if (lx->type == T_NAME && strcmp(lx->token, "delete") == 0) {
        lex_next(lx);
        Stmt *s = stmt_new(S_DELETE);
        s->name = xstrdup(lx->token);
        lex_next(lx);
        if (is_token(lx, "[")) {
            lex_next(lx);
            s->expr = parse_expression(lx);
            accept(lx, "]");
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
        s->body = parse_statement(lx);
        return s;
    }

    if (lx->type == T_NAME && strcmp(lx->token, "next") == 0) {
        lex_next(lx);
        return stmt_new(S_NEXT);
    }

    if (lx->type == T_NAME && strcmp(lx->token, "exit") == 0) {
        lex_next(lx);
        Stmt *s = stmt_new(S_EXIT);
        if (lx->type == T_NUMBER) s->expr = parse_expression(lx);
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
    return awk->fields[index - 1];
}

static void assign_to(Awk *awk, Expr *target, const char *value) {
    if (target->kind == E_VAR) {
        variable_set(awk, target->text, value);
        return;
    }
    if (target->kind == E_SUBSCRIPT) {
        const char *key = evaluate(awk, target->left);
        char *copy = xstrdup(key);
        element_set(awk, target->text, copy, value);
        free(copy);
        return;
    }
    if (target->kind == E_FIELD) {
        int index = (int)evaluate_number(awk, target->left);
        if (index >= 1 && index <= AWK_FIELDS) {
            while (awk->field_count < index) {
                awk->fields[awk->field_count] = xstrdup("");
                awk->field_count++;
            }
            free(awk->fields[index - 1]);
            awk->fields[index - 1] = xstrdup(value);

            char count[32];
            snprintf(count, sizeof(count), "%d", awk->field_count);
            variable_set(awk, "NF", count);
        }
    }
}

static void execute(Awk *awk, Stmt *s);
static void split_record(Awk *awk, const char *line);

static Function *function_find(Awk *awk, const char *name) {
    for (size_t i = 0; i < awk->function_count; i++) {
        if (strcmp(awk->functions[i].name, name) == 0) return &awk->functions[i];
    }
    return NULL;
}

static const char *call_user(Awk *awk, Function *f, Expr *e) {
    Expr *arguments[3] = {e->left, e->right, e->third};

    StrList saved_names;
    StrList saved_values;
    sl_init(&saved_names);
    sl_init(&saved_values);

    for (size_t i = 0; i < f->params.len; i++) {
        const char *name = f->params.items[i];
        Variable *existing = variable_find(awk, name);
        sl_push_copy(&saved_names, name);
        sl_push_copy(&saved_values, existing && existing->value ? existing->value : "");

        const char *value = i < 3 && arguments[i] ? evaluate(awk, arguments[i]) : "";
        variable_set(awk, name, value);
    }

    awk->returning = 0;
    awk->return_value = "";
    execute(awk, f->body);
    const char *result = awk->return_value ? awk->return_value : "";
    awk->returning = 0;

    for (size_t i = 0; i < saved_names.len; i++)
        variable_set(awk, saved_names.items[i], saved_values.items[i]);

    sl_free(&saved_names);
    sl_free(&saved_values);
    return result;
}

static int split_into(Awk *awk, const char *text, const char *array, const char *separator) {
    array_clear(awk, array);

    int index = 0;
    const char *p = text;

    if (!separator || !*separator || strcmp(separator, " ") == 0) {
        while (*p) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            const char *start = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            char key[32];
            snprintf(key, sizeof(key), "%d", ++index);
            char *piece = xstrndup(start, (size_t)(p - start));
            element_set(awk, array, key, piece);
            free(piece);
        }
        return index;
    }

    char sep = separator[0];
    while (1) {
        const char *hit = strchr(p, sep);
        char key[32];
        snprintf(key, sizeof(key), "%d", ++index);
        if (!hit) {
            element_set(awk, array, key, p);
            break;
        }
        char *piece = xstrndup(p, (size_t)(hit - p));
        element_set(awk, array, key, piece);
        free(piece);
        p = hit + 1;
    }
    return index;
}

static FILE *readers[8];
static char *reader_names[8];

static FILE *reader_for(const char *path) {
    for (int i = 0; i < 8; i++) {
        if (reader_names[i] && strcmp(reader_names[i], path) == 0) return readers[i];
    }
    for (int i = 0; i < 8; i++) {
        if (reader_names[i]) continue;
        FILE *f = fopen(path, "rb");
        if (!f) return NULL;
        reader_names[i] = xstrdup(path);
        readers[i] = f;
        return f;
    }
    return NULL;
}

static void readers_close(void) {
    for (int i = 0; i < 8; i++) {
        if (readers[i]) fclose(readers[i]);
        free(reader_names[i]);
        readers[i] = NULL;
        reader_names[i] = NULL;
    }
}

static const char *run_getline(Awk *awk, Expr *e) {
    FILE *source = stdin;
    if (e->left) {
        const char *path = evaluate(awk, e->left);
        source = reader_for(path);
        if (!source) return "-1";
    }

    char line[8192];
    if (!fgets(line, sizeof(line), source)) return "0";
    line[strcspn(line, "\r\n")] = '\0';

    if (e->text) {
        variable_set(awk, e->text, line);
    } else {
        split_record(awk, line);
        double number = to_number(variable_get(awk, "NR")) + 1;
        variable_set(awk, "NR", number_text(awk, number));
    }
    return "1";
}

static const char *call_builtin(Awk *awk, Expr *e) {
    const char *name = e->text;

    Function *user = function_find(awk, name);
    if (user) return call_user(awk, user, e);

    if (strcmp(name, "split") == 0) {
        const char *text = evaluate(awk, e->left);
        char *copy = xstrdup(text);
        const char *array = e->right && e->right->text ? e->right->text : "";
        const char *separator = e->third ? evaluate(awk, e->third) : variable_get(awk, "FS");
        int count = split_into(awk, copy, array, separator);
        free(copy);
        return number_text(awk, count);
    }

    if (strcmp(name, "length") == 0) {
        const char *text = e->left ? evaluate(awk, e->left) : field_value(awk, 0);
        return number_text(awk, (double)strlen(text));
    }
    if (strcmp(name, "toupper") == 0 || strcmp(name, "tolower") == 0) {
        char *text = xstrdup(e->left ? evaluate(awk, e->left) : "");
        for (char *p = text; *p; p++)
            *p = name[2] == 'u' ? (char)toupper((unsigned char)*p) : (char)tolower((unsigned char)*p);
        return arena_add(awk, text);
    }
    if (strcmp(name, "int") == 0) {
        return number_text(awk, (double)(long long)evaluate_number(awk, e->left));
    }
    if (strcmp(name, "substr") == 0) {
        const char *text = evaluate(awk, e->left);
        int start = e->right ? (int)evaluate_number(awk, e->right) : 1;
        int length = e->third ? (int)evaluate_number(awk, e->third) : (int)strlen(text);
        if (start < 1) start = 1;
        if (start > (int)strlen(text)) return "";
        if (length < 0) length = 0;
        if (start - 1 + length > (int)strlen(text)) length = (int)strlen(text) - start + 1;
        return arena_add(awk, xstrndup(text + start - 1, (size_t)length));
    }
    if (strcmp(name, "index") == 0) {
        const char *haystack = evaluate(awk, e->left);
        const char *needle = e->right ? evaluate(awk, e->right) : "";
        const char *hit = strstr(haystack, needle);
        return number_text(awk, hit ? (double)(hit - haystack + 1) : 0);
    }
    return "";
}

static const char *evaluate(Awk *awk, Expr *e) {
    if (!e) return "";

    switch (e->kind) {
    case E_NUMBER: return number_text(awk, e->number);
    case E_STRING: return e->text;
    case E_REGEX: return regex_search(e->text, field_value(awk, 0), NULL) ? "1" : "0";
    case E_FIELD: return field_value(awk, (int)evaluate_number(awk, e->left));
    case E_VAR: return variable_get(awk, e->text);

    case E_CONCAT: {
        const char *left = evaluate(awk, e->left);
        StrBuf sb;
        sb_init(&sb);
        sb_puts(&sb, left);
        sb_puts(&sb, evaluate(awk, e->right));
        return arena_add(awk, sb_take(&sb));
    }

    case E_MATCH: {
        const char *text = evaluate(awk, e->left);
        const char *pattern =
            e->right->kind == E_REGEX ? e->right->text : evaluate(awk, e->right);
        int matched = regex_search(pattern, text, NULL);
        return (e->negate ? !matched : matched) ? "1" : "0";
    }

    case E_UNARY:
        if (e->op[0] == '!') return evaluate_number(awk, e->left) == 0 ? "1" : "0";
        return number_text(awk, -evaluate_number(awk, e->left));

    case E_INCREMENT: {
        double value = to_number(e->left->kind == E_VAR ? variable_get(awk, e->left->text)
                                                        : evaluate(awk, e->left));
        double updated = e->op[0] == '+' ? value + 1 : value - 1;
        assign_to(awk, e->left, number_text(awk, updated));
        return number_text(awk, value);
    }

    case E_ASSIGN: {
        const char *value = evaluate(awk, e->right);
        if (e->op[0] != '=') {
            double current = to_number(e->left->kind == E_VAR ? variable_get(awk, e->left->text)
                                                              : evaluate(awk, e->left));
            double operand = to_number(value);
            double result = e->op[0] == '+'   ? current + operand
                            : e->op[0] == '-' ? current - operand
                            : e->op[0] == '*' ? current * operand
                                              : (operand != 0 ? current / operand : 0);
            value = number_text(awk, result);
        }
        assign_to(awk, e->left, value);
        return value;
    }

    case E_CALL: return call_builtin(awk, e);

    case E_SUBSCRIPT: {
        const char *key = evaluate(awk, e->left);
        char *copy = xstrdup(key);
        const char *value = element_get(awk, e->text, copy);
        free(copy);
        return value;
    }

    case E_IN: {
        const char *key = evaluate(awk, e->left);
        Variable *v = variable_find(awk, e->text);
        return v && element_find(v, key) ? "1" : "0";
    }

    case E_GETLINE: return run_getline(awk, e);

    case E_BINARY: {
        if (strcmp(e->op, "&&") == 0)
            return evaluate_number(awk, e->left) != 0 && evaluate_number(awk, e->right) != 0 ? "1"
                                                                                            : "0";
        if (strcmp(e->op, "||") == 0)
            return evaluate_number(awk, e->left) != 0 || evaluate_number(awk, e->right) != 0 ? "1"
                                                                                            : "0";

        const char *left = evaluate(awk, e->left);
        char *left_copy = xstrdup(left);
        const char *right = evaluate(awk, e->right);

        int compared;
        if (looks_numeric(left_copy) && looks_numeric(right)) {
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

        double value = e->op[0] == '+'   ? a + b
                       : e->op[0] == '-' ? a - b
                       : e->op[0] == '*' ? a * b
                       : e->op[0] == '/' ? (b != 0 ? a / b : 0)
                                         : (b != 0 ? fmod(a, b) : 0);
        return number_text(awk, value);
    }
    }
    return "";
}

static void run_printf(Awk *awk, Stmt *s) {
    if (s->count == 0) return;
    const char *format = evaluate(awk, s->list[0]);
    int next = 1;

    for (const char *p = format; *p; p++) {
        if (*p != '%') {
            putchar(*p);
            continue;
        }
        p++;
        if (*p == '%') {
            putchar('%');
            continue;
        }

        char spec[32];
        size_t length = 0;
        spec[length++] = '%';
        while (*p && !strchr("diouxXeEfgGcs", *p) && length + 2 < sizeof(spec))
            spec[length++] = *p++;
        if (!*p) break;
        spec[length++] = *p;
        spec[length] = '\0';

        const char *value = next < s->count ? evaluate(awk, s->list[next++]) : "";
        if (*p == 's') printf(spec, value);
        else if (*p == 'c') printf(spec, value[0]);
        else if (strchr("eEfgG", *p)) printf(spec, to_number(value));
        else {
            char adjusted[32];
            snprintf(adjusted, sizeof(adjusted), "%.*sll%c", (int)(length - 2), spec, *p);
            printf(adjusted, (long long)to_number(value));
        }
    }
}

static void execute(Awk *awk, Stmt *s) {
    for (; s && !awk->exiting && !awk->skipping && !awk->returning; s = s->next) {
        switch (s->kind) {
        case S_RETURN:
            awk->return_value = s->expr ? evaluate(awk, s->expr) : "";
            awk->returning = 1;
            break;

        case S_DELETE:
            if (s->expr) {
                const char *key = evaluate(awk, s->expr);
                char *copy = xstrdup(key);
                element_delete(awk, s->name, copy);
                free(copy);
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

            for (size_t i = 0; i < keys.len && !awk->exiting && !awk->returning; i++) {
                variable_set(awk, s->name, keys.items[i]);
                execute(awk, s->body);
            }
            sl_free(&keys);
            break;
        }

        case S_BLOCK:
            execute(awk, s->body);
            break;

        case S_PRINT: {
            const char *separator = variable_get(awk, "OFS");
            if (s->count == 0) {
                printf("%s\n", field_value(awk, 0));
                break;
            }
            for (int i = 0; i < s->count; i++) {
                if (i > 0) printf("%s", *separator ? separator : " ");
                printf("%s", evaluate(awk, s->list[i]));
            }
            printf("\n");
            break;
        }

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

        case S_WHILE: {
            int guard = 0;
            while (evaluate_number(awk, s->expr) != 0 && !awk->exiting && guard++ < 1000000)
                execute(awk, s->body);
            break;
        }

        case S_FOR: {
            if (s->expr) evaluate(awk, s->expr);
            int guard = 0;
            while ((!s->second || evaluate_number(awk, s->second) != 0) && !awk->exiting &&
                   guard++ < 1000000) {
                execute(awk, s->body);
                if (s->third) evaluate(awk, s->third);
            }
            break;
        }

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
    for (int i = 0; i < awk->field_count; i++) free(awk->fields[i]);
    awk->field_count = 0;

    free(awk->record);
    awk->record = xstrdup(line);

    const char *separator = variable_get(awk, "FS");
    const char *p = line;

    if (!separator || !*separator || strcmp(separator, " ") == 0) {
        while (*p && awk->field_count < AWK_FIELDS) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            const char *start = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            awk->fields[awk->field_count++] = xstrndup(start, (size_t)(p - start));
        }
    } else {
        char sep = separator[0];
        while (awk->field_count < AWK_FIELDS) {
            const char *hit = strchr(p, sep);
            if (!hit) {
                awk->fields[awk->field_count++] = xstrdup(p);
                break;
            }
            awk->fields[awk->field_count++] = xstrndup(p, (size_t)(hit - p));
            p = hit + 1;
        }
    }

    char count[32];
    snprintf(count, sizeof(count), "%d", awk->field_count);
    variable_set(awk, "NF", count);
}

static void run_rules(Awk *awk, int begin, int end) {
    for (int i = 0; i < awk->rule_count && !awk->exiting; i++) {
        Rule *rule = &awk->rules[i];
        if (rule->begin != begin || rule->end != end) continue;

        if (!begin && !end && rule->pattern && evaluate_number(awk, rule->pattern) == 0) continue;

        if (!rule->action) {
            printf("%s\n", field_value(awk, 0));
            continue;
        }
        execute(awk, rule->action);
    }
}

static void parse_program(Awk *awk, const char *program) {
    Lexer lx;
    lx.source = program;
    lx.failed = 0;
    lex_next(&lx);

    while (lx.type != T_END) {
        while (is_token(&lx, ";") || is_token(&lx, "\n")) lex_next(&lx);
        if (lx.type == T_END) break;

        if (lx.type == T_NAME && strcmp(lx.token, "function") == 0) {
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
        }

        if (is_token(&lx, "{")) {
            lex_next(&lx);
            rule->action = parse_block(&lx);
            accept(&lx, "}");
        }
    }
}

int awk_main(int argc, char **argv) {
    Awk awk;
    memset(&awk, 0, sizeof(awk));
    sl_init(&awk.arena);

    variable_set(&awk, "FS", " ");
    variable_set(&awk, "OFS", " ");
    variable_set(&awk, "NR", "0");
    variable_set(&awk, "NF", "0");

    int index = 1;
    for (; index < argc; index++) {
        if (strcmp(argv[index], "-F") == 0 && index + 1 < argc) {
            variable_set(&awk, "FS", argv[++index]);
            continue;
        }
        if (strncmp(argv[index], "-F", 2) == 0 && argv[index][2]) {
            variable_set(&awk, "FS", argv[index] + 2);
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
        break;
    }

    if (index >= argc) {
        shell_error("awk: usage: awk [-F sep] [-v name=value] 'program' [file...]");
        return 2;
    }

    parse_program(&awk, argv[index++]);
    run_rules(&awk, 1, 0);

    long record_number = 0;
    int file_index = index;

    do {
        FILE *input = stdin;
        if (file_index < argc) {
            input = fopen(argv[file_index], "rb");
            if (!input) {
                shell_error("awk: %s: no such file", argv[file_index]);
                file_index++;
                continue;
            }
            variable_set(&awk, "FILENAME", argv[file_index]);
        }

        char line[8192];
        while (!awk.exiting && fgets(line, sizeof(line), input)) {
            line[strcspn(line, "\r\n")] = '\0';
            record_number++;

            char number[32];
            snprintf(number, sizeof(number), "%ld", record_number);
            variable_set(&awk, "NR", number);

            split_record(&awk, line);
            awk.skipping = 0;
            run_rules(&awk, 0, 0);
            arena_reset(&awk);
        }

        if (input != stdin) fclose(input);
        file_index++;
    } while (file_index < argc && !awk.exiting);

    awk.exiting = 0;
    run_rules(&awk, 0, 1);
    arena_reset(&awk);

    readers_close();
    for (int i = 0; i < awk.field_count; i++) free(awk.fields[i]);
    free(awk.record);
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
    }
    free(awk.functions);
    free(awk.variables);
    free(awk.rules);
    sl_free(&awk.arena);
    return awk.status;
}
