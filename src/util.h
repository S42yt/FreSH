/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_UTIL_H
#define FRESH_UTIL_H

#include <stddef.h>

#define PATH_BUF 1024

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} StrList;

void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

void sb_init(StrBuf *sb);
void sb_free(StrBuf *sb);
void sb_clear(StrBuf *sb);
void sb_reserve(StrBuf *sb, size_t extra);
void sb_putc(StrBuf *sb, char c);
void sb_putn(StrBuf *sb, const char *s, size_t n);
void sb_puts(StrBuf *sb, const char *s);
void sb_printf(StrBuf *sb, const char *fmt, ...);
char *sb_take(StrBuf *sb);

void sl_init(StrList *l);
void sl_free(StrList *l);
void sl_clear(StrList *l);
void sl_push(StrList *l, char *s);
void sl_push_copy(StrList *l, const char *s);
void sl_sort(StrList *l);
int sl_contains(const StrList *l, const char *s);

int str_ieq(const char *a, const char *b);
int str_has_prefix(const char *s, const char *prefix);
int str_has_suffix_i(const char *s, const char *suffix);
char *str_trim(char *s);

void path_to_slashes(char *p);
void path_to_backslashes(char *p);
int path_is_dir(const char *p);
int path_is_file(const char *p);
int path_is_absolute(const char *p);
const char *path_ext(const char *p);
char *path_join(const char *dir, const char *name);
int path_mkdirs(const char *dir);
char *home_dir(void);
char *config_path(const char *name);

#endif
