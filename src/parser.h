/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_PARSER_H
#define FRESH_PARSER_H

#include "util.h"

typedef enum {
    R_IN,
    R_OUT,
    R_APPEND,
    R_DUP,
    R_HEREDOC,
    R_HEREDOC_RAW,
    R_HERESTRING
} RedirType;

typedef struct Redir {
    int fd;
    RedirType type;
    char *target;
    struct Redir *next;
} Redir;

typedef enum {
    N_SIMPLE,
    N_PIPE,
    N_AND,
    N_OR,
    N_SEQ,
    N_NOT,
    N_IF,
    N_WHILE,
    N_UNTIL,
    N_FOR,
    N_CASE,
    N_CASE_ITEM,
    N_GROUP,
    N_SUBSHELL,
    N_FUNC,
    N_ASSIGN_ARRAY,
    N_SELECT,
    N_TEST,
    N_ARITH,
    N_FOR_C,
    N_TIME
} NodeKind;

typedef struct Node {
    NodeKind kind;
    StrList words;
    Redir *redirs;
    struct Node *left;
    struct Node *right;
    struct Node *extra;
    char *name;
    int background;
    int line;
} Node;

Node *parse_string(const char *src, int *incomplete, char **error);
void node_free(Node *node);
int node_source(const Node *node, StrBuf *out);

int keyword_known(const char *word);
void keyword_names(StrList *out);
int keyword_prefix(const char *prefix, size_t length);
void keyword_complete(const char *prefix, size_t length, StrList *out);

#endif
