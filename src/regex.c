/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "regex.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    R_CHAR,
    R_ANY,
    R_CLASS,
    R_SEQ,
    R_ALT,
    R_REPEAT,
    R_GROUP,
    R_GROUP_END,
    R_BOL,
    R_EOL,
    R_BACKREF
} RegexKind;

typedef struct RegexNode {
    RegexKind kind;
    char ch;
    unsigned char set[32];
    int negate;
    int min;
    int max;
    int group;
    struct RegexNode *left;
    struct RegexNode *right;
    struct RegexNode *child;
} RegexNode;

typedef struct {
    RegexNode **items;
    size_t len;
    size_t cap;
} NodePool;

typedef struct {
    const char *pattern;
    NodePool *pool;
    int groups;
    int failed;
} Parser;

typedef struct Cont {
    RegexNode *node;
    RegexNode *repeat;
    int done;
    const char *repeat_start;
    struct Cont *next;
} Cont;

typedef struct {
    const char *text;
    int start[REGEX_GROUPS];
    int end[REGEX_GROUPS];
    int steps;
} State;

static RegexNode *node_new(NodePool *pool, RegexKind kind) {
    if (pool->len + 1 >= pool->cap) {
        pool->cap = pool->cap ? pool->cap * 2 : 32;
        pool->items = xrealloc(pool->items, pool->cap * sizeof(RegexNode *));
    }
    RegexNode *node = xmalloc(sizeof(RegexNode));
    memset(node, 0, sizeof(RegexNode));
    node->kind = kind;
    pool->items[pool->len++] = node;
    return node;
}

static void pool_free(NodePool *pool) {
    for (size_t i = 0; i < pool->len; i++) free(pool->items[i]);
    free(pool->items);
}

static void set_add(RegexNode *node, unsigned char c) {
    node->set[c >> 3] |= (unsigned char)(1 << (c & 7));
}

static int set_has(RegexNode *node, unsigned char c) {
    return (node->set[c >> 3] >> (c & 7)) & 1;
}

static void add_escape_class(RegexNode *node, char escape) {
    for (int c = 0; c < 256; c++) {
        int matched = 0;
        switch (escape) {
        case 'd': matched = isdigit(c); break;
        case 'D': matched = !isdigit(c); break;
        case 'w': matched = isalnum(c) || c == '_'; break;
        case 'W': matched = !(isalnum(c) || c == '_'); break;
        case 's': matched = isspace(c); break;
        case 'S': matched = !isspace(c); break;
        default: break;
        }
        if (matched) set_add(node, (unsigned char)c);
    }
}

static RegexNode *parse_alt(Parser *ps);

static RegexNode *parse_class(Parser *ps) {
    RegexNode *node = node_new(ps->pool, R_CLASS);
    ps->pattern++;

    if (*ps->pattern == '^') {
        node->negate = 1;
        ps->pattern++;
    }
    int first = 1;
    while (*ps->pattern && (*ps->pattern != ']' || first)) {
        first = 0;
        if (*ps->pattern == '\\' && ps->pattern[1]) {
            ps->pattern++;
            char escape = *ps->pattern++;
            if (strchr("dDwWsS", escape)) {
                add_escape_class(node, escape);
                continue;
            }
            if (escape == 'n') escape = '\n';
            else if (escape == 't') escape = '\t';
            set_add(node, (unsigned char)escape);
            continue;
        }
        unsigned char low = (unsigned char)*ps->pattern++;
        if (*ps->pattern == '-' && ps->pattern[1] && ps->pattern[1] != ']') {
            ps->pattern++;
            unsigned char high = (unsigned char)*ps->pattern++;
            for (int c = low; c <= high; c++) set_add(node, (unsigned char)c);
            continue;
        }
        set_add(node, low);
    }
    if (*ps->pattern == ']') ps->pattern++;
    else ps->failed = 1;
    return node;
}

static RegexNode *parse_atom(Parser *ps) {
    char c = *ps->pattern;

    if (c == '(') {
        ps->pattern++;
        int capture = 1;
        if (ps->pattern[0] == '?' && ps->pattern[1] == ':') {
            capture = 0;
            ps->pattern += 2;
        }
        int index = capture && ps->groups < REGEX_GROUPS - 1 ? ++ps->groups : 0;

        RegexNode *inner = parse_alt(ps);
        if (*ps->pattern == ')') ps->pattern++;
        else ps->failed = 1;

        if (!index) return inner;

        RegexNode *group = node_new(ps->pool, R_GROUP);
        group->group = index;
        RegexNode *end = node_new(ps->pool, R_GROUP_END);
        end->group = index;

        RegexNode *seq = node_new(ps->pool, R_SEQ);
        seq->left = inner;
        seq->right = end;
        group->child = seq;
        return group;
    }
    if (c == '[') return parse_class(ps);
    if (c == '.') {
        ps->pattern++;
        return node_new(ps->pool, R_ANY);
    }
    if (c == '^') {
        ps->pattern++;
        return node_new(ps->pool, R_BOL);
    }
    if (c == '$') {
        ps->pattern++;
        return node_new(ps->pool, R_EOL);
    }
    if (c == '\\' && ps->pattern[1]) {
        ps->pattern++;
        char escape = *ps->pattern++;
        if (strchr("dDwWsS", escape)) {
            RegexNode *node = node_new(ps->pool, R_CLASS);
            add_escape_class(node, escape);
            return node;
        }
        if (isdigit((unsigned char)escape)) {
            RegexNode *node = node_new(ps->pool, R_BACKREF);
            node->group = escape - '0';
            return node;
        }
        RegexNode *node = node_new(ps->pool, R_CHAR);
        node->ch = escape == 'n' ? '\n' : escape == 't' ? '\t' : escape;
        return node;
    }

    ps->pattern++;
    RegexNode *node = node_new(ps->pool, R_CHAR);
    node->ch = c;
    return node;
}

static RegexNode *parse_repeat(Parser *ps) {
    RegexNode *atom = parse_atom(ps);

    while (1) {
        char c = *ps->pattern;
        int min = 0;
        int max = 0;

        if (c == '*') {
            min = 0;
            max = -1;
        } else if (c == '+') {
            min = 1;
            max = -1;
        } else if (c == '?') {
            min = 0;
            max = 1;
        } else if (c == '{' && isdigit((unsigned char)ps->pattern[1])) {
            const char *scan = ps->pattern + 1;
            min = atoi(scan);
            while (isdigit((unsigned char)*scan)) scan++;
            if (*scan == ',') {
                scan++;
                max = isdigit((unsigned char)*scan) ? atoi(scan) : -1;
                while (isdigit((unsigned char)*scan)) scan++;
            } else {
                max = min;
            }
            if (*scan != '}') return atom;
            ps->pattern = scan;
        } else {
            return atom;
        }
        ps->pattern++;

        RegexNode *repeat = node_new(ps->pool, R_REPEAT);
        repeat->child = atom;
        repeat->min = min;
        repeat->max = max;
        atom = repeat;
    }
}

static RegexNode *parse_seq(Parser *ps) {
    RegexNode *first = NULL;
    RegexNode *last = NULL;

    while (*ps->pattern && *ps->pattern != '|' && *ps->pattern != ')') {
        RegexNode *piece = parse_repeat(ps);
        if (!first) {
            first = piece;
            last = piece;
            continue;
        }
        RegexNode *seq = node_new(ps->pool, R_SEQ);
        seq->left = last == first ? first : last;
        seq->right = piece;
        if (last == first) {
            first = seq;
        } else {
            RegexNode *walk = first;
            while (walk->kind == R_SEQ && walk->right != last) walk = walk->right;
            if (walk->kind == R_SEQ) walk->right = seq;
            else first = seq;
        }
        last = piece;
    }
    return first;
}

static RegexNode *parse_alt(Parser *ps) {
    RegexNode *left = parse_seq(ps);
    while (*ps->pattern == '|') {
        ps->pattern++;
        RegexNode *right = parse_seq(ps);
        RegexNode *alt = node_new(ps->pool, R_ALT);
        alt->left = left;
        alt->right = right;
        left = alt;
    }
    return left;
}

static int match_node(RegexNode *node, const char *pos, Cont *k, State *st);

static int match_cont(Cont *k, const char *pos, State *st) {
    if (!k) {
        st->end[0] = (int)(pos - st->text);
        return 1;
    }
    if (k->repeat) {
        RegexNode *repeat = k->repeat;
        int done = k->done;
        Cont *next = k->next;

        int can_more = repeat->max < 0 || done < repeat->max;
        int advanced = pos != k->repeat_start;

        if (can_more && (advanced || done < repeat->min)) {
            Cont frame = {NULL, repeat, done + 1, pos, next};
            if (match_node(repeat->child, pos, &frame, st)) return 1;
        }
        if (done >= repeat->min) return match_cont(next, pos, st);
        return 0;
    }
    return match_node(k->node, pos, k->next, st);
}

static int match_node(RegexNode *node, const char *pos, Cont *k, State *st) {
    if (++st->steps > 400000) return 0;
    if (!node) return match_cont(k, pos, st);

    switch (node->kind) {
    case R_CHAR:
        if (*pos != node->ch) return 0;
        return match_cont(k, pos + 1, st);

    case R_ANY:
        if (!*pos || *pos == '\n') return 0;
        return match_cont(k, pos + 1, st);

    case R_CLASS: {
        if (!*pos) return 0;
        int has = set_has(node, (unsigned char)*pos);
        if (node->negate ? has : !has) return 0;
        return match_cont(k, pos + 1, st);
    }

    case R_BOL:
        if (pos != st->text && pos[-1] != '\n') return 0;
        return match_cont(k, pos, st);

    case R_EOL:
        if (*pos && *pos != '\n') return 0;
        return match_cont(k, pos, st);

    case R_SEQ: {
        Cont frame = {node->right, NULL, 0, NULL, k};
        return match_node(node->left, pos, &frame, st);
    }

    case R_ALT:
        if (match_node(node->left, pos, k, st)) return 1;
        return match_node(node->right, pos, k, st);

    case R_GROUP: {
        int saved = st->start[node->group];
        st->start[node->group] = (int)(pos - st->text);
        if (match_node(node->child, pos, k, st)) return 1;
        st->start[node->group] = saved;
        return 0;
    }

    case R_GROUP_END: {
        int saved = st->end[node->group];
        st->end[node->group] = (int)(pos - st->text);
        if (match_cont(k, pos, st)) return 1;
        st->end[node->group] = saved;
        return 0;
    }

    case R_BACKREF: {
        int group = node->group;
        if (group >= REGEX_GROUPS || st->start[group] < 0 || st->end[group] < 0) return 0;
        size_t length = (size_t)(st->end[group] - st->start[group]);
        if (strncmp(pos, st->text + st->start[group], length) != 0) return 0;
        return match_cont(k, pos + length, st);
    }

    case R_REPEAT: {
        Cont frame = {NULL, node, 0, NULL, k};
        return match_cont(&frame, pos, st);
    }
    }
    return 0;
}

static int run(const char *pattern, const char *text, RegexMatch *match) {
    NodePool pool;
    memset(&pool, 0, sizeof(pool));

    Parser ps;
    ps.pattern = pattern;
    ps.pool = &pool;
    ps.groups = 0;
    ps.failed = 0;

    RegexNode *root = parse_alt(&ps);
    if (ps.failed) {
        pool_free(&pool);
        return 0;
    }

    for (const char *start = text;; start++) {
        State st;
        st.text = text;
        st.steps = 0;
        for (int i = 0; i < REGEX_GROUPS; i++) {
            st.start[i] = -1;
            st.end[i] = -1;
        }
        st.start[0] = (int)(start - text);

        if (match_node(root, start, NULL, &st)) {
            if (match) {
                memcpy(match->start, st.start, sizeof(st.start));
                memcpy(match->end, st.end, sizeof(st.end));
                match->count = ps.groups + 1;
            }
            pool_free(&pool);
            return 1;
        }
        if (!*start) break;
    }

    pool_free(&pool);
    return 0;
}

void regex_bre_to_ere(const char *bre, StrBuf *out) {
    for (const char *p = bre; *p; p++) {
        if (*p == '\\' && p[1]) {
            char next = p[1];
            if (strchr("(){}|+?", next)) {
                sb_putc(out, next);
                p++;
                continue;
            }
            sb_putc(out, *p);
            sb_putc(out, next);
            p++;
            continue;
        }
        if (strchr("(){}|+?", *p)) {
            sb_putc(out, '\\');
            sb_putc(out, *p);
            continue;
        }
        sb_putc(out, *p);
    }
}

int regex_search(const char *pattern, const char *text, RegexMatch *match) {
    if (!pattern || !text) return 0;
    return run(pattern, text, match);
}

int regex_replace(const char *pattern, const char *replacement, const char *text, int global,
                  StrBuf *out) {
    const char *cursor = text;
    int replaced = 0;

    while (*cursor) {
        RegexMatch match;
        if (!regex_search(pattern, cursor, &match)) break;

        sb_putn(out, cursor, (size_t)match.start[0]);
        for (const char *r = replacement; *r; r++) {
            if (*r == '\\' && isdigit((unsigned char)r[1])) {
                int group = *++r - '0';
                if (group < REGEX_GROUPS && match.start[group] >= 0 && match.end[group] >= 0)
                    sb_putn(out, cursor + match.start[group],
                            (size_t)(match.end[group] - match.start[group]));
                continue;
            }
            if (*r == '\\' && r[1]) {
                r++;
                sb_putc(out, *r == 'n' ? '\n' : *r == 't' ? '\t' : *r);
                continue;
            }
            if (*r == '&') {
                sb_putn(out, cursor + match.start[0], (size_t)(match.end[0] - match.start[0]));
                continue;
            }
            sb_putc(out, *r);
        }

        replaced = 1;
        int width = match.end[0] - match.start[0];
        cursor += match.end[0];
        if (width == 0) {
            if (!*cursor) break;
            sb_putc(out, *cursor);
            cursor++;
        }
        if (!global) break;
    }

    sb_puts(out, cursor);
    return replaced;
}
