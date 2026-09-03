/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "util.h"

#include "rustcore.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

#ifndef _WIN32
#include <pwd.h>
#endif

void *xmalloc(size_t size) {
    void *p = malloc(size ? size : 1);
    if (!p) {
        fputs("FreSH: out of memory\n", stderr);
        exit(1);
    }
    return p;
}

void *xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size ? size : 1);
    if (!p) {
        fputs("FreSH: out of memory\n", stderr);
        exit(1);
    }
    return p;
}

char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

char *xstrndup(const char *s, size_t n) {
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

#ifdef FRESH_BORROWS

#define STATE_LIVE 0xa11fe5u
#define STATE_DEAD 0xdeadd1eu

static void borrow_fail(const char *what) {
    fprintf(stderr, "FreSH: borrow violation: %s\n", what);
    fflush(stderr);
    abort();
}

#define ASSERT_LIVE(p, kind) \
    do { \
        if ((p)->state == STATE_DEAD) borrow_fail(kind " used after free"); \
        if ((p)->state != STATE_LIVE) MARK_LIVE(p); \
    } while (0)

#define ASSERT_UNBORROWED(p, kind) \
    do { \
        ASSERT_LIVE(p, kind); \
        if ((p)->borrows > 0) borrow_fail(kind " mutated while borrowed"); \
    } while (0)

#define MARK_LIVE(p) (*(unsigned *)&(p)->state = STATE_LIVE, *(int *)&(p)->borrows = 0)
#define MARK_DEAD(p) (*(unsigned *)&(p)->state = STATE_DEAD)

const StrList *sl_borrow(const StrList *l) {
    ASSERT_LIVE(l, "a list");
    ((StrList *)l)->borrows++;
    return l;
}

void sl_release(const StrList *l) {
    ASSERT_LIVE(l, "a list");
    if (l->borrows <= 0) borrow_fail("a list released more often than borrowed");
    ((StrList *)l)->borrows--;
}

const StrBuf *sb_borrow(const StrBuf *sb) {
    ASSERT_LIVE(sb, "a buffer");
    ((StrBuf *)sb)->borrows++;
    return sb;
}

void sb_release(const StrBuf *sb) {
    ASSERT_LIVE(sb, "a buffer");
    if (sb->borrows <= 0) borrow_fail("a buffer released more often than borrowed");
    ((StrBuf *)sb)->borrows--;
}

#else

#define ASSERT_LIVE(p, kind) ((void)0)
#define ASSERT_UNBORROWED(p, kind) ((void)0)
#define MARK_LIVE(p) ((void)0)
#define MARK_DEAD(p) ((void)0)

#endif

void sb_init(StrBuf *sb) {
    sb->cap = 64;
    sb->len = 0;
    sb->data = xmalloc(sb->cap);
    sb->data[0] = '\0';
    MARK_LIVE(sb);
}

void sb_free(StrBuf *sb) {
    ASSERT_UNBORROWED(sb, "a buffer");
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
    MARK_DEAD(sb);
}

void sb_clear(StrBuf *sb) {
    ASSERT_UNBORROWED(sb, "a buffer");
    sb->len = 0;
    if (sb->data) sb->data[0] = '\0';
}

void sb_reserve(StrBuf *sb, size_t extra) {
    ASSERT_UNBORROWED(sb, "a buffer");
    if (sb->len + extra + 1 <= sb->cap) return;
    while (sb->cap < sb->len + extra + 1) sb->cap *= 2;
    sb->data = xrealloc(sb->data, sb->cap);
}

void sb_putc(StrBuf *sb, char c) {
    sb_reserve(sb, 1);
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
}

void sb_putn(StrBuf *sb, const char *s, size_t n) {
    sb_reserve(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_puts(StrBuf *sb, const char *s) {
    if (s) sb_putn(sb, s, strlen(s));
}

void sb_printf(StrBuf *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    sb_reserve(sb, (size_t)n);
    va_start(ap, fmt);
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    sb->len += (size_t)n;
}

void sb_put_quoted(StrBuf *sb, const char *text) {
    sb_putc(sb, '\'');
    for (const char *p = text; *p; p++) {
        if (*p == '\'') sb_puts(sb, "'\\''");
        else sb_putc(sb, *p);
    }
    sb_putc(sb, '\'');
}

char *sb_take(StrBuf *sb) {
    ASSERT_UNBORROWED(sb, "a buffer");
    char *p = sb->data;
    sb->data = NULL;
    sb->len = sb->cap = 0;
    MARK_DEAD(sb);
    return p;
}

int read_line(FILE *f, StrBuf *out) {
    sb_clear(out);
    if (!f) return 0;

    int c;
    int any = 0;
    int ended = 0;

    _lock_file(f);
    while ((c = _fgetc_nolock(f)) != EOF) {
        any = 1;
        if (c == '\n') {
            ended = 1;
            break;
        }
        sb_putc(out, (char)c);
    }
    _unlock_file(f);

    if (ended) {
        if (out->len && out->data[out->len - 1] == '\r') {
            out->len--;
            out->data[out->len] = '\0';
        }
        return 1;
    }
    return any ? 2 : 0;
}

int read_line_fd(int fd, StrBuf *out) {
    sb_clear(out);

    char c;
    int any = 0;

    while (_read(fd, &c, 1) == 1) {
        any = 1;
        if (c == '\n') {
            if (out->len && out->data[out->len - 1] == '\r') {
                out->len--;
                out->data[out->len] = '\0';
            }
            return 1;
        }
        sb_putc(out, c);
    }
    return any ? 2 : 0;
}

void sl_init(StrList *l) {
    l->cap = 8;
    l->len = 0;
    l->items = xmalloc(l->cap * sizeof(char *));
    MARK_LIVE(l);
}

void sl_free(StrList *l) {
    ASSERT_UNBORROWED(l, "a list");
    for (size_t i = 0; i < l->len; i++) free(l->items[i]);
    free(l->items);
    l->items = NULL;
    l->len = 0;
    l->cap = 0;
    MARK_DEAD(l);
}

void sl_clear(StrList *l) {
    ASSERT_UNBORROWED(l, "a list");
    for (size_t i = 0; i < l->len; i++) free(l->items[i]);
    l->len = 0;
}

void sl_push(StrList *l, char *s) {
    ASSERT_UNBORROWED(l, "a list");
    if (l->len + 1 >= l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->items = xrealloc(l->items, l->cap * sizeof(char *));
    }
    l->items[l->len++] = s;
}

void sl_push_copy(StrList *l, const char *s) {
    sl_push(l, xstrdup(s));
}

void sl_sort(StrList *l) {
    ASSERT_UNBORROWED(l, "a list");
    core_sort_pointers(l->items, l->len, FRESH_SORT_FOLD);
}

void sl_dedup_adjacent_fold(StrList *l) {
    ASSERT_UNBORROWED(l, "a list");

    size_t kept = 0;
    for (size_t i = 0; i < l->len; i++) {
        if (kept > 0 && _stricmp(l->items[kept - 1], l->items[i]) == 0) {
            free(l->items[i]);
            continue;
        }
        l->items[kept++] = l->items[i];
    }
    l->len = kept;
}

int sl_contains(const StrList *l, const char *s) {
    ASSERT_LIVE(l, "a list");
    for (size_t i = 0; i < l->len; i++) {
        if (strcmp(l->items[i], s) == 0) return 1;
    }
    return 0;
}

#ifdef _WIN32

typedef BOOL(WINAPI *UserNameFn)(LPSTR, LPDWORD);

int win_user_name(char *out, unsigned long *size) {
    static UserNameFn get_user_name;
    static HMODULE library = NULL;

    if (!library) {
        library = LoadLibraryA("advapi32.dll");
        if (library)
            get_user_name = (UserNameFn)(void *)GetProcAddress(library, "GetUserNameA");
    }
    return get_user_name ? get_user_name(out, size) != 0 : 0;
}

#else

int win_user_name(char *out, unsigned long *size) {
    struct passwd *entry = getpwuid(getuid());
    const char *name = entry && entry->pw_name ? entry->pw_name : getenv("USER");
    if (!name || !*name) return 0;
    int written = snprintf(out, *size, "%s", name);
    if (written < 0 || (unsigned long)written >= *size) return 0;
    *size = (unsigned long)written;
    return 1;
}

#endif

#ifdef _WIN32

typedef BOOL(WINAPI *ElevatedFn)(void);

int running_elevated(void) {
    static ElevatedFn is_admin;
    static HMODULE library = NULL;

    if (!library) {
        library = LoadLibraryA("shell32.dll");
        if (library) is_admin = (ElevatedFn)(void *)GetProcAddress(library, "IsUserAnAdmin");
    }
    return is_admin ? is_admin() != 0 : 0;
}

#else

int running_elevated(void) {
    return 0;
}

#endif

int str_ieq(const char *a, const char *b) {
    return _stricmp(a, b) == 0;
}

int str_has_prefix(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

int str_has_suffix_i(const char *s, const char *suffix) {
    size_t sl = strlen(s), xl = strlen(suffix);
    return sl >= xl && _stricmp(s + sl - xl, suffix) == 0;
}

char *str_trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

char *str_next_field(char **cursor, char separator) {
    if (!*cursor || !**cursor) return NULL;
    char *start = *cursor;
    char *end = strchr(start, separator);
    if (end) {
        *end = '\0';
        *cursor = end + 1;
    } else {
        *cursor = start + strlen(start);
    }
    return start;
}

int str_word_at(const char *p, const char *start, const char *word) {
    size_t length = strlen(word);
    if (strncmp(p, word, length) != 0) return 0;
    if (p > start && (isalnum((unsigned char)p[-1]) || p[-1] == '_')) return 0;
    return !isalnum((unsigned char)p[length]) && p[length] != '_';
}

void path_to_slashes(char *p) {
    for (; *p; p++)
        if (*p == '\\') *p = '/';
}

void path_to_backslashes(char *p) {
#ifdef _WIN32
    for (; *p; p++)
        if (*p == '/') *p = '\\';
#else
    path_to_slashes(p);
#endif
}

char *path_last_sep(const char *p) {
    char *back = strrchr(p, '\\');
    char *fwd = strrchr(p, '/');
    return fwd > back ? fwd : back;
}

int path_is_dir(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int path_is_file(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

int path_is_absolute(const char *p) {
    if (!p || !*p) return 0;
    if (p[0] == '/' || p[0] == '\\') return 1;
    return isalpha((unsigned char)p[0]) && p[1] == ':';
}

const char *path_ext(const char *p) {
    const char *slash = strrchr(p, '\\');
    const char *fwd = strrchr(p, '/');
    if (fwd > slash) slash = fwd;
    const char *dot = strrchr(slash ? slash : p, '.');
    return dot ? dot : "";
}

char *path_join(const char *dir, const char *name) {
    size_t dn = strlen(dir);
    int sep = dn > 0 && dir[dn - 1] != '\\' && dir[dn - 1] != '/';
    size_t total = dn + (sep ? 1 : 0) + strlen(name) + 1;
    char *out = xmalloc(total);
    snprintf(out, total, "%s%s%s", dir, sep ? PATH_SEP_STR : "", name);
    return out;
}

int path_mkdirs(const char *dir) {
    char tmp[PATH_BUF];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    path_to_backslashes(tmp);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == PATH_SEP) {
            *p = '\0';
            if (!path_is_dir(tmp)) _mkdir(tmp);
            *p = PATH_SEP;
        }
    }
    if (!path_is_dir(tmp) && _mkdir(tmp) != 0) return path_is_dir(tmp);
    return 1;
}

char *home_dir(void) {
    char *h;
#ifdef _WIN32
    h = getenv("USERPROFILE");
    if (h && *h) return h;
#endif
    h = getenv("HOME");
    if (h && *h) return h;
#ifdef _WIN32
    return "C:\\";
#else
    return "/";
#endif
}

char *config_path(const char *name) {
    return path_join(home_dir(), name);
}
