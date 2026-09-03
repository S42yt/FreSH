/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "commands.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "platform.h"

#include "awk.h"
#include "exec.h"
#include "regex.h"
#include "rustcore.h"
#include "shell.h"
#include "vars.h"

#define END(list) (sizeof(list) / sizeof(list[0]))

static const OptSpec NO_OPTIONS[] = {{0, NULL, 0}};

static int parse_or_exit(int argc, char **argv, const OptSpec *specs, const char *tool, Args *args,
                         int failure) {
    memset(args, 0, sizeof(*args));
    int parsed = args_parse(argc, argv, specs, tool, args);
    if (parsed == ARGS_OK) return -1;
    return parsed == ARGS_DONE ? 0 : failure;
}

static void put_line(const char *text, int newline) {
    fputs(text, stdout);
    if (newline) fputc('\n', stdout);
}

static int each_operand_line(const char *tool, const Args *args, int *status,
                             int (*visit)(const char *line, int newline, void *ctx), void *ctx) {
    int count = args->operand_count ? args->operand_count : 1;
    for (int i = 0; i < count; i++) {
        const char *name = args->operand_count ? args->operands[i] : NULL;
        FILE *f = cmd_open(tool, name);
        if (!f) {
            *status = 1;
            continue;
        }
        StrBuf line;
        sb_init(&line);
        int kind;
        while ((kind = cmd_read_line(f, &line)) != 0) {
            if (!visit(line.data, kind == 1, ctx)) break;
        }
        sb_free(&line);
        cmd_close(f);
    }
    return *status;
}

static void show_nonprinting(const char *text, int show_tabs, StrBuf *out) {
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        unsigned char c = *p;
        if (c >= 128) {
            sb_puts(out, "M-");
            c -= 128;
        }
        if (c == '\t' && !show_tabs) sb_putc(out, '\t');
        else if (c < 32) {
            sb_putc(out, '^');
            sb_putc(out, (char)(c + 64));
        } else if (c == 127) sb_puts(out, "^?");
        else sb_putc(out, (char)c);
    }
}

typedef struct {
    int number;
    int number_nonblank;
    int squeeze;
    int show_ends;
    int show_tabs;
    int show_nonprinting;
    long line_number;
    int previous_blank;
} CatState;

static int cat_line(const char *line, int newline, void *ctx) {
    CatState *cat = ctx;
    int blank = line[0] == '\0';
    if (cat->squeeze && blank && cat->previous_blank) return 1;
    cat->previous_blank = blank;

    if (cat->number_nonblank) {
        if (!blank) printf("%6ld\t", ++cat->line_number);
    } else if (cat->number) {
        printf("%6ld\t", ++cat->line_number);
    }

    if (cat->show_nonprinting || cat->show_tabs) {
        StrBuf shown;
        sb_init(&shown);
        if (cat->show_nonprinting) show_nonprinting(line, cat->show_tabs, &shown);
        else {
            for (const char *p = line; *p; p++) {
                if (*p == '\t') sb_puts(&shown, "^I");
                else sb_putc(&shown, *p);
            }
        }
        fputs(shown.data, stdout);
        sb_free(&shown);
    } else {
        fputs(line, stdout);
    }
    if (newline) {
        if (cat->show_ends) fputc('$', stdout);
        fputc('\n', stdout);
    }
    return 1;
}

static int cmd_cat(int argc, char **argv) {
    static const OptSpec specs[] = {
        {'A', "show-all", 0},        {'b', "number-nonblank", 0}, {'e', NULL, 0},
        {'E', "show-ends", 0},       {'n', "number", 0},          {'s', "squeeze-blank", 0},
        {'t', NULL, 0},              {'T', "show-tabs", 0},       {'u', NULL, 0},
        {'v', "show-nonprinting", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "cat", &args, 1);
    if (early >= 0) return early;

    CatState cat;
    memset(&cat, 0, sizeof(cat));
    cat.number = args_has(&args, 'n') || args_has(&args, 'b');
    cat.number_nonblank = args_has(&args, 'b');
    cat.squeeze = args_has(&args, 's');
    cat.show_ends = args_has(&args, 'E') || args_has(&args, 'A') || args_has(&args, 'e');
    cat.show_tabs = args_has(&args, 'T') || args_has(&args, 'A') || args_has(&args, 't');
    cat.show_nonprinting = args_has(&args, 'v') || args_has(&args, 'A') || args_has(&args, 'e') ||
                           args_has(&args, 't');

    int status = 0;
    int plain = !cat.number && !cat.squeeze && !cat.show_ends && !cat.show_tabs &&
                !cat.show_nonprinting;
    if (plain) {
        int count = args.operand_count ? args.operand_count : 1;
        for (int i = 0; i < count; i++) {
            const char *name = args.operand_count ? args.operands[i] : NULL;
            FILE *f = cmd_open("cat", name);
            if (!f) {
                status = 1;
                continue;
            }
            char buffer[65536];
            size_t n;
            while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) fwrite(buffer, 1, n, stdout);
            cmd_close(f);
        }
    } else {
        each_operand_line("cat", &args, &status, cat_line, &cat);
    }
    args_free(&args);
    return status;
}

static int open_for_reading(const char *tool, const char *name, FILE **out) {
    if (!name || strcmp(name, "-") == 0) {
        *out = stdin;
        return 1;
    }
    if (path_is_dir(name)) {
        cmd_error(tool, "error reading '%s': Is a directory", name);
        return 0;
    }
    *out = fopen(name, "rb");
    if (!*out) {
        cmd_error(tool, "cannot open '%s' for reading: %s", name, strerror(errno));
        return 0;
    }
    return 1;
}

static void file_header(const char *name, int first) {
    printf("%s==> %s <==\n", first ? "" : "\n", name ? name : "standard input");
}

static int head_tail(int argc, char **argv, int is_head) {
    const char *tool = is_head ? "head" : "tail";
    static const OptSpec specs[] = {{'c', "bytes", 1}, {'n', "lines", 1}, {'q', "quiet", 0},
                                    {'q', "silent", 0}, {'v', "verbose", 0}, {'z', "zero-terminated", 0},
                                    {'f', "follow", 2}, {'F', NULL, 0}, {0, NULL, 0}};
    Args args;
    memset(&args, 0, sizeof(args));
    args.number_shorthand = 'n';
    int parsed = args_parse(argc, argv, specs, tool, &args);
    if (parsed != ARGS_OK) return parsed == ARGS_DONE ? 0 : 1;

    int bytes_mode = args_has(&args, 'c');
    const char *spec = bytes_mode ? args_value(&args, 'c') : args_value(&args, 'n');
    long long count = 10;
    int from_start = 0;
    int all_but = 0;
    if (spec) {
        const char *digits = spec;
        if (*digits == '+') {
            from_start = 1;
            digits++;
        } else if (*digits == '-') {
            all_but = 1;
            digits++;
        }
        int ok;
        count = cmd_parse_size(digits, &ok);
        if (!ok || count < 0) {
            cmd_error(tool, "invalid number of %s: '%s'", bytes_mode ? "bytes" : "lines", spec);
            args_free(&args);
            return 1;
        }
    }
    if (is_head && from_start) from_start = 0;
    if (!is_head && all_but) all_but = 0;
    if (args_has(&args, 'f') || args_has(&args, 'F')) {
        cmd_error(tool, "following files is not supported by the bundled tail");
        args_free(&args);
        return 1;
    }

    int files = args.operand_count ? args.operand_count : 1;
    int headers = args_has(&args, 'v') || (files > 1 && !args_has(&args, 'q'));
    int status = 0;

    for (int i = 0; i < files; i++) {
        const char *name = args.operand_count ? args.operands[i] : NULL;
        FILE *f;
        if (!open_for_reading(tool, name, &f)) {
            status = 1;
            continue;
        }
        if (headers) file_header(name, i == 0);

        StrBuf data;
        sb_init(&data);
        char block[65536];
        size_t n;
        while ((n = fread(block, 1, sizeof(block), f)) > 0) sb_putn(&data, block, n);
        cmd_close(f);

        if (bytes_mode) {
            size_t total = data.len;
            size_t start = 0, end = total;
            if (is_head) end = all_but ? (total > (size_t)count ? total - (size_t)count : 0)
                                       : ((size_t)count < total ? (size_t)count : total);
            else if (from_start) start = count > 0 ? (size_t)(count - 1) : 0;
            else start = total > (size_t)count ? total - (size_t)count : 0;
            if (start > total) start = total;
            fwrite(data.data + start, 1, end - start, stdout);
        } else {
            size_t total_lines = 0;
            for (size_t p = 0; p < data.len; p++)
                if (data.data[p] == '\n') total_lines++;
            int trailing = data.len > 0 && data.data[data.len - 1] != '\n';
            size_t records = total_lines + (size_t)trailing;

            size_t first_line, last_line;
            if (is_head) {
                first_line = 0;
                last_line = all_but ? (records > (size_t)count ? records - (size_t)count : 0)
                                    : ((size_t)count < records ? (size_t)count : records);
            } else if (from_start) {
                first_line = count > 0 ? (size_t)(count - 1) : 0;
                last_line = records;
            } else {
                first_line = records > (size_t)count ? records - (size_t)count : 0;
                last_line = records;
            }
            if (first_line > records) first_line = records;

            size_t line = 0;
            size_t start = 0;
            for (size_t p = 0; p <= data.len && line < last_line; p++) {
                if (p == data.len || data.data[p] == '\n') {
                    if (p == data.len && start == p) break;
                    size_t length = p - start + (p < data.len ? 1 : 0);
                    if (line >= first_line) fwrite(data.data + start, 1, length, stdout);
                    line++;
                    start = p + 1;
                }
            }
        }
        sb_free(&data);
    }
    args_free(&args);
    return status;
}

static int cmd_head(int argc, char **argv) {
    return head_tail(argc, argv, 1);
}

static int cmd_tail(int argc, char **argv) {
    return head_tail(argc, argv, 0);
}

typedef struct {
    unsigned long long lines, words, chars, bytes, longest;
} Counts;

static void count_data(const char *data, size_t length, Counts *counts) {
    FreshCounts fast;
    memset(&fast, 0, sizeof(fast));
    core_count_block(data, length, &fast);
    counts->lines = fast.lines;
    counts->words = fast.words;
    counts->bytes = fast.bytes;

    unsigned long long chars = 0;
    unsigned long long current = 0;
    unsigned long long longest = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)data[i];
        if ((c & 0xC0) != 0x80) chars++;
        if (c == '\n') {
            if (current > longest) longest = current;
            current = 0;
        } else if (c == '\t') {
            current = (current / 8 + 1) * 8;
        } else if ((c & 0xC0) != 0x80 && c >= 32 && c != 127) {
            current++;
        }
    }
    if (current > longest) longest = current;
    counts->chars = chars;
    counts->longest = longest;
}

static int digits_of(unsigned long long value) {
    int digits = 1;
    while (value >= 10) {
        value /= 10;
        digits++;
    }
    return digits;
}

static void print_counts(const Counts *c, const char *name, int width, const char *which) {
    int first = 1;
    for (const char *w = which; *w; w++) {
        unsigned long long value = *w == 'l' ? c->lines
                                   : *w == 'w' ? c->words
                                   : *w == 'm' ? c->chars
                                   : *w == 'c' ? c->bytes
                                               : c->longest;
        printf("%s%*llu", first ? "" : " ", width, value);
        first = 0;
    }
    if (name) printf(" %s", name);
    printf("\n");
}

static int cmd_wc(int argc, char **argv) {
    static const OptSpec specs[] = {{'c', "bytes", 0},          {'m', "chars", 0},
                                    {'l', "lines", 0},          {'L', "max-line-length", 0},
                                    {'w', "words", 0},          {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "wc", &args, 1);
    if (early >= 0) return early;

    char which[8] = "";
    if (args_has(&args, 'l')) strcat(which, "l");
    if (args_has(&args, 'w')) strcat(which, "w");
    if (args_has(&args, 'm')) strcat(which, "m");
    if (args_has(&args, 'c')) strcat(which, "c");
    if (args_has(&args, 'L')) strcat(which, "L");
    if (!*which) strcpy(which, "lwc");

    int files = args.operand_count ? args.operand_count : 1;
    unsigned long long regular_total = 0;
    int all_regular = 1;
    for (int i = 0; i < files; i++) {
        const char *name = args.operand_count ? args.operands[i] : NULL;
        struct stat info;
        if (!name || strcmp(name, "-") == 0 || stat(name, &info) != 0 || !S_ISREG(info.st_mode))
            all_regular = 0;
        else regular_total += (unsigned long long)info.st_size;
    }
    int width = all_regular ? digits_of(regular_total) : 7;
    if (files == 1 && strlen(which) == 1) width = 1;
    if (width < 1) width = 1;

    Counts total;
    memset(&total, 0, sizeof(total));
    int status = 0;
    for (int i = 0; i < files; i++) {
        const char *name = args.operand_count ? args.operands[i] : NULL;
        FILE *f = cmd_open("wc", name);
        if (!f) {
            status = 1;
            continue;
        }
        StrBuf data;
        sb_init(&data);
        char block[65536];
        size_t n;
        while ((n = fread(block, 1, sizeof(block), f)) > 0) sb_putn(&data, block, n);
        cmd_close(f);

        Counts counts;
        count_data(data.data, data.len, &counts);
        sb_free(&data);
        print_counts(&counts, name, width, which);
        total.lines += counts.lines;
        total.words += counts.words;
        total.chars += counts.chars;
        total.bytes += counts.bytes;
        if (counts.longest > total.longest) total.longest = counts.longest;
    }
    if (files > 1) print_counts(&total, "total", width, which);
    args_free(&args);
    return status;
}

typedef struct {
    int start_field, start_char, end_field, end_char;
    int numeric, general, human, version, reverse, fold, blanks, dictionary, nonprinting;
    int has_end;
} SortKey;

typedef struct {
    SortKey keys[16];
    int key_count;
    char separator;
    int has_separator;
    SortKey global;
    int stable;
    int unique;
} SortSpec;

static const char *skip_blanks(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static void field_bounds(const char *line, int field, const SortSpec *spec, const char **start,
                         const char **end) {
    const char *p = line;
    for (int f = 1; f < field; f++) {
        if (spec->has_separator) {
            const char *sep = strchr(p, spec->separator);
            if (!sep) {
                p = line + strlen(line);
                break;
            }
            p = sep + 1;
        } else {
            while (*p == ' ' || *p == '\t') p++;
            while (*p && *p != ' ' && *p != '\t') p++;
        }
    }
    *start = p;
    if (spec->has_separator) {
        const char *sep = strchr(p, spec->separator);
        *end = sep ? sep : p + strlen(p);
    } else {
        const char *q = p;
        while (*q == ' ' || *q == '\t') q++;
        while (*q && *q != ' ' && *q != '\t') q++;
        *end = q;
    }
}

static void key_extract(const char *line, const SortKey *key, const SortSpec *spec, StrBuf *out) {
    sb_clear(out);
    if (key->start_field == 0) {
        sb_puts(out, line);
        return;
    }
    const char *start, *end_of_start;
    field_bounds(line, key->start_field, spec, &start, &end_of_start);
    if (key->blanks) start = skip_blanks(start);
    if (key->start_char > 1) {
        const char *limit = end_of_start;
        start += key->start_char - 1;
        if (start > limit) start = limit;
    }

    const char *end;
    if (!key->has_end) end = line + strlen(line);
    else {
        const char *end_start, *end_end;
        field_bounds(line, key->end_field, spec, &end_start, &end_end);
        if (key->end_char == 0) end = end_end;
        else {
            if (key->blanks) end_start = skip_blanks(end_start);
            end = end_start + key->end_char;
            if (end > end_end) end = end_end;
        }
    }
    if (end < start) end = start;
    sb_putn(out, start, (size_t)(end - start));
}

static double parse_numeric(const char *text) {
    const char *p = skip_blanks(text);
    int negative = 0;
    if (*p == '-') {
        negative = 1;
        p++;
    } else if (*p == '+') p++;
    double value = 0;
    int any = 0;
    while (isdigit((unsigned char)*p)) {
        value = value * 10 + (*p - '0');
        p++;
        any = 1;
    }
    if (*p == '.') {
        p++;
        double scale = 0.1;
        while (isdigit((unsigned char)*p)) {
            value += (*p - '0') * scale;
            scale /= 10;
            p++;
            any = 1;
        }
    }
    if (!any) return 0;
    return negative ? -value : value;
}

static double parse_human(const char *text) {
    const char *p = skip_blanks(text);
    double value = parse_numeric(p);
    while (*p && (isdigit((unsigned char)*p) || *p == '.' || *p == '-' || *p == '+')) p++;
    static const char *units = "KMGTPEZY";
    const char *hit = *p ? strchr(units, toupper((unsigned char)*p)) : NULL;
    if (hit) {
        int power = (int)(hit - units) + 1;
        for (int i = 0; i < power; i++) value *= 1024;
    }
    return value;
}

static int compare_versions(const char *a, const char *b) {
    while (*a || *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            while (*a == '0') a++;
            while (*b == '0') b++;
            const char *ea = a, *eb = b;
            while (isdigit((unsigned char)*ea)) ea++;
            while (isdigit((unsigned char)*eb)) eb++;
            if (ea - a != eb - b) return (ea - a) < (eb - b) ? -1 : 1;
            int c = strncmp(a, b, (size_t)(ea - a));
            if (c) return c;
            a = ea;
            b = eb;
            continue;
        }
        if (*a != *b) return (unsigned char)*a < (unsigned char)*b ? -1 : 1;
        if (!*a) return 0;
        a++;
        b++;
    }
    return 0;
}

static int compare_folded(const char *a, const char *b, const SortKey *key) {
    for (;;) {
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (key->dictionary) {
            while (ca && !isalnum(ca) && ca != ' ' && ca != '\t') ca = (unsigned char)*++a;
            while (cb && !isalnum(cb) && cb != ' ' && cb != '\t') cb = (unsigned char)*++b;
        }
        if (key->nonprinting) {
            while (ca && !isprint(ca)) ca = (unsigned char)*++a;
            while (cb && !isprint(cb)) cb = (unsigned char)*++b;
        }
        if (key->fold) {
            ca = (unsigned char)toupper(ca);
            cb = (unsigned char)toupper(cb);
        }
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
        a++;
        b++;
    }
}

static double parse_general(const char *text) {
    char *end;
    double value = strtod(text, &end);
    if (end == text) return -1e308;
    return value;
}

static int compare_with_key(const char *a, const char *b, const SortKey *key) {
    int result;
    if (key->blanks && key->start_field == 0) {
        a = skip_blanks(a);
        b = skip_blanks(b);
    }
    if (key->numeric || key->general || key->human) {
        double x = key->human ? parse_human(a) : key->general ? parse_general(a) : parse_numeric(a);
        double y = key->human ? parse_human(b) : key->general ? parse_general(b) : parse_numeric(b);
        result = x < y ? -1 : x > y ? 1 : 0;
    } else if (key->version) {
        result = compare_versions(a, b);
    } else if (key->fold || key->dictionary || key->nonprinting) {
        result = compare_folded(a, b, key);
    } else {
        result = strcmp(a, b);
    }
    return key->reverse ? -result : result;
}

static const SortSpec *active_spec;
static StrBuf key_a, key_b;

static int compare_keys_only(const char *a, const char *b) {
    const SortSpec *spec = active_spec;
    if (spec->key_count == 0) return compare_with_key(a, b, &spec->global);
    for (int k = 0; k < spec->key_count; k++) {
        key_extract(a, &spec->keys[k], spec, &key_a);
        key_extract(b, &spec->keys[k], spec, &key_b);
        int c = compare_with_key(key_a.data, key_b.data, &spec->keys[k]);
        if (c) return c;
    }
    return 0;
}

static int compare_lines(const void *pa, const void *pb) {
    const char *a = *(const char *const *)pa;
    const char *b = *(const char *const *)pb;
    int c = compare_keys_only(a, b);
    if (c || active_spec->stable || active_spec->unique) return c;
    c = strcmp(a, b);
    return active_spec->global.reverse ? -c : c;
}

static void merge_sort(char **items, char **scratch, size_t count) {
    if (count < 2) return;
    size_t half = count / 2;
    merge_sort(items, scratch, half);
    merge_sort(items + half, scratch, count - half);
    size_t i = 0, j = half, k = 0;
    while (i < half && j < count) {
        if (compare_lines(&items[j], &items[i]) < 0) scratch[k++] = items[j++];
        else scratch[k++] = items[i++];
    }
    while (i < half) scratch[k++] = items[i++];
    while (j < count) scratch[k++] = items[j++];
    memcpy(items, scratch, count * sizeof(char *));
}

static int parse_key_flags(const char *p, SortKey *key) {
    for (; *p; p++) {
        switch (*p) {
        case 'n': key->numeric = 1; break;
        case 'g': key->general = 1; break;
        case 'h': key->human = 1; break;
        case 'V': key->version = 1; break;
        case 'r': key->reverse = 1; break;
        case 'f': key->fold = 1; break;
        case 'b': key->blanks = 1; break;
        case 'd': key->dictionary = 1; break;
        case 'i': key->nonprinting = 1; break;
        default: return 0;
        }
    }
    return 1;
}

static int parse_key(const char *text, SortKey *key, const SortKey *global) {
    memset(key, 0, sizeof(*key));
    char *end;
    key->start_field = (int)strtol(text, &end, 10);
    if (end == text || key->start_field < 1) return 0;
    if (*end == '.') {
        key->start_char = (int)strtol(end + 1, &end, 10);
        if (key->start_char < 1) return 0;
    }
    const char *flags_start = end;
    const char *comma = strchr(end, ',');
    size_t flag_length = comma ? (size_t)(comma - flags_start) : strlen(flags_start);
    char flags[32];
    if (flag_length >= sizeof(flags)) return 0;
    memcpy(flags, flags_start, flag_length);
    flags[flag_length] = '\0';
    int any_flag = flag_length > 0;
    if (!parse_key_flags(flags, key)) return 0;

    if (comma) {
        key->has_end = 1;
        key->end_field = (int)strtol(comma + 1, &end, 10);
        if (end == comma + 1 || key->end_field < 1) return 0;
        if (*end == '.') key->end_char = (int)strtol(end + 1, &end, 10);
        if (*end) {
            if (!parse_key_flags(end, key)) return 0;
            any_flag = 1;
        }
    }
    if (!any_flag) {
        int blanks = key->blanks;
        *key = *global;
        key->start_field = 0;
        key->blanks = blanks || global->blanks;
        char *retry_end;
        key->start_field = (int)strtol(text, &retry_end, 10);
        if (*retry_end == '.') key->start_char = (int)strtol(retry_end + 1, &retry_end, 10);
        if (comma) {
            key->has_end = 1;
            key->end_field = (int)strtol(comma + 1, &retry_end, 10);
            if (*retry_end == '.') key->end_char = (int)strtol(retry_end + 1, &retry_end, 10);
        }
        key->reverse = global->reverse;
    }
    return 1;
}

static int cmd_sort(int argc, char **argv) {
    static const OptSpec specs[] = {
        {'b', "ignore-leading-blanks", 0}, {'c', "check", 2},          {'C', NULL, 0},
        {'d', "dictionary-order", 0},      {'f', "ignore-case", 0},     {'g', "general-numeric-sort", 0},
        {'h', "human-numeric-sort", 0},    {'i', "ignore-nonprinting", 0}, {'k', "key", 1},
        {'m', "merge", 0},                 {'n', "numeric-sort", 0},    {'o', "output", 1},
        {'r', "reverse", 0},               {'s', "stable", 0},          {'t', "field-separator", 1},
        {'u', "unique", 0},                {'V', "version-sort", 0},    {'z', "zero-terminated", 0},
        {'S', "buffer-size", 1},           {'T', "temporary-directory", 1}, {'P', "parallel", 1},
        {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "sort", &args, 2);
    if (early >= 0) return early;

    SortSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.global.numeric = args_has(&args, 'n');
    spec.global.general = args_has(&args, 'g');
    spec.global.human = args_has(&args, 'h');
    spec.global.version = args_has(&args, 'V');
    spec.global.reverse = args_has(&args, 'r');
    spec.global.fold = args_has(&args, 'f');
    spec.global.blanks = args_has(&args, 'b');
    spec.global.dictionary = args_has(&args, 'd');
    spec.global.nonprinting = args_has(&args, 'i');
    spec.stable = args_has(&args, 's');
    spec.unique = args_has(&args, 'u');
    if (args_has(&args, 't')) {
        const char *sep = args_value(&args, 't');
        if (strcmp(sep, "\\0") == 0) spec.separator = '\0';
        else if (strlen(sep) != 1) {
            cmd_error("sort", "multi-character tab '%s'", sep);
            args_free(&args);
            return 2;
        } else spec.separator = sep[0];
        spec.has_separator = 1;
    }
    for (int i = 0; i < args_count(&args, 'k') && spec.key_count < 16; i++) {
        const char *text = args_value_at(&args, 'k', i);
        if (!parse_key(text, &spec.keys[spec.key_count], &spec.global)) {
            cmd_error("sort", "invalid key '%s'", text);
            args_free(&args);
            return 2;
        }
        spec.key_count++;
    }

    StrList lines;
    sl_init(&lines);
    int status = cmd_read_lines("sort", &args, &lines);
    if (status) {
        sl_free(&lines);
        args_free(&args);
        return 2;
    }

    active_spec = &spec;
    sb_init(&key_a);
    sb_init(&key_b);

    if (args_has(&args, 'c') || args_has(&args, 'C')) {
        int quiet = args_has(&args, 'C');
        for (size_t i = 1; i < lines.len; i++) {
            int c = compare_lines(&lines.items[i - 1], &lines.items[i]);
            int disorder = spec.unique ? c >= 0 : c > 0;
            if (disorder) {
                if (!quiet)
                    cmd_error("sort", "%s:%zu: disorder: %s",
                              args.operand_count ? args.operands[0] : "-", i + 1, lines.items[i]);
                status = 1;
                break;
            }
        }
        sb_free(&key_a);
        sb_free(&key_b);
        sl_free(&lines);
        args_free(&args);
        return status;
    }

    int plain = spec.key_count == 0 && !spec.has_separator && !spec.global.general &&
                !spec.global.human && !spec.global.version && !spec.global.fold &&
                !spec.global.blanks && !spec.global.dictionary && !spec.global.nonprinting &&
                !spec.stable;
    if (plain) {
        core_sort_pointers(lines.items, lines.len,
                           spec.global.numeric ? FRESH_SORT_NUMERIC : FRESH_SORT_BYTES);
        if (spec.global.reverse) {
            for (size_t i = 0, j = lines.len; i + 1 < j; i++, j--) {
                char *swap = lines.items[i];
                lines.items[i] = lines.items[j - 1];
                lines.items[j - 1] = swap;
            }
        }
    } else {
        char **scratch = xmalloc((lines.len + 1) * sizeof(char *));
        merge_sort(lines.items, scratch, lines.len);
        free(scratch);
    }

    FILE *out = stdout;
    const char *output = args_value(&args, 'o');
    if (output && strcmp(output, "-") != 0) {
        out = fopen(output, "wb");
        if (!out) {
            cmd_error("sort", "open failed: %s: %s", output, strerror(errno));
            status = 2;
            out = stdout;
        }
    }
    for (size_t i = 0; i < lines.len; i++) {
        if (spec.unique && i > 0 && compare_keys_only(lines.items[i - 1], lines.items[i]) == 0)
            continue;
        fputs(lines.items[i], out);
        fputc('\n', out);
    }
    if (out != stdout) fclose(out);

    sb_free(&key_a);
    sb_free(&key_b);
    sl_free(&lines);
    args_free(&args);
    return status;
}

static const char *uniq_compare_start(const char *line, int fields, int chars) {
    const char *p = line;
    for (int f = 0; f < fields; f++) {
        while (*p == ' ' || *p == '\t') p++;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    for (int c = 0; c < chars && *p; c++) p++;
    return p;
}

static int uniq_equal(const char *a, const char *b, int fields, int chars, int width, int icase) {
    const char *pa = uniq_compare_start(a, fields, chars);
    const char *pb = uniq_compare_start(b, fields, chars);
    if (width > 0) {
        return icase ? _strnicmp(pa, pb, (size_t)width) == 0 : strncmp(pa, pb, (size_t)width) == 0;
    }
    return icase ? _stricmp(pa, pb) == 0 : strcmp(pa, pb) == 0;
}

static int cmd_uniq(int argc, char **argv) {
    static const OptSpec specs[] = {{'c', "count", 0},          {'d', "repeated", 0},
                                    {'D', "all-repeated", 2},   {'f', "skip-fields", 1},
                                    {'i', "ignore-case", 0},    {'s', "skip-chars", 1},
                                    {'u', "unique", 0},         {'w', "check-chars", 1},
                                    {'z', "zero-terminated", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "uniq", &args, 1);
    if (early >= 0) return early;

    int count_mode = args_has(&args, 'c');
    int only_repeated = args_has(&args, 'd');
    int all_repeated = args_has(&args, 'D');
    int only_unique = args_has(&args, 'u');
    int icase = args_has(&args, 'i');
    int fields = args_has(&args, 'f') ? atoi(args_value(&args, 'f')) : 0;
    int chars = args_has(&args, 's') ? atoi(args_value(&args, 's')) : 0;
    int width = args_has(&args, 'w') ? atoi(args_value(&args, 'w')) : 0;

    if (args.operand_count > 2) {
        cmd_error("uniq", "extra operand '%s'", args.operands[2]);
        args_free(&args);
        return 1;
    }
    FILE *in = cmd_open("uniq", args.operand_count > 0 ? args.operands[0] : NULL);
    if (!in) {
        args_free(&args);
        return 1;
    }
    FILE *out = stdout;
    if (args.operand_count > 1 && strcmp(args.operands[1], "-") != 0) {
        out = fopen(args.operands[1], "wb");
        if (!out) {
            cmd_file_error("uniq", args.operands[1]);
            cmd_close(in);
            args_free(&args);
            return 1;
        }
    }

    StrList lines;
    sl_init(&lines);
    StrBuf line;
    sb_init(&line);
    while (cmd_read_line(in, &line) != 0) sl_push_copy(&lines, line.data);
    sb_free(&line);
    cmd_close(in);

    size_t i = 0;
    while (i < lines.len) {
        size_t run = 1;
        while (i + run < lines.len &&
               uniq_equal(lines.items[i], lines.items[i + run], fields, chars, width, icase))
            run++;
        int show = 1;
        if (only_repeated && run < 2) show = 0;
        if (only_unique && run > 1) show = 0;
        if (all_repeated) {
            if (run > 1)
                for (size_t r = 0; r < run; r++) fprintf(out, "%s\n", lines.items[i + r]);
        } else if (show) {
            if (count_mode) fprintf(out, "%7zu %s\n", run, lines.items[i]);
            else fprintf(out, "%s\n", lines.items[i]);
        }
        i += run;
    }
    if (out != stdout) fclose(out);
    sl_free(&lines);
    args_free(&args);
    return 0;
}

typedef struct {
    int from;
    int to;
} Range;

static int parse_ranges(const char *list, Range *ranges, int max, int *count) {
    *count = 0;
    const char *p = list;
    while (*p) {
        if (*count >= max) return 0;
        const char *start = p;
        long from = 0, to = 0;
        int open_end = 0;
        if (*p == '-') {
            from = 1;
            p++;
            if (!isdigit((unsigned char)*p)) return 0;
            to = strtol(p, (char **)&p, 10);
        } else {
            if (!isdigit((unsigned char)*p)) return 0;
            from = strtol(p, (char **)&p, 10);
            if (*p == '-') {
                p++;
                if (isdigit((unsigned char)*p)) to = strtol(p, (char **)&p, 10);
                else open_end = 1;
            } else to = from;
        }
        if (from < 1 || (!open_end && to < from)) return 0;
        (void)start;
        ranges[*count].from = (int)from;
        ranges[*count].to = open_end ? 0 : (int)to;
        (*count)++;
        if (*p == ',') p++;
        else if (*p) return 0;
    }
    return *count > 0;
}

static int in_ranges(const Range *ranges, int count, int position) {
    for (int i = 0; i < count; i++) {
        if (position >= ranges[i].from && (ranges[i].to == 0 || position <= ranges[i].to)) return 1;
    }
    return 0;
}

typedef struct {
    Range ranges[64];
    int range_count;
    char mode;
    char delimiter;
    int only_delimited;
    int complement;
    const char *output_delimiter;
} CutState;

static int cut_line(const char *line, int newline, void *ctx) {
    CutState *cut = ctx;
    (void)newline;
    if (cut->mode == 'f') {
        if (!strchr(line, cut->delimiter)) {
            if (!cut->only_delimited) put_line(line, 1);
            return 1;
        }
        StrBuf out;
        sb_init(&out);
        int field = 1;
        int printed = 0;
        const char *p = line;
        const char *sep = cut->output_delimiter;
        char single[2] = {cut->delimiter, '\0'};
        if (!sep) sep = single;
        for (;;) {
            const char *end = strchr(p, cut->delimiter);
            size_t length = end ? (size_t)(end - p) : strlen(p);
            int selected = in_ranges(cut->ranges, cut->range_count, field);
            if (cut->complement) selected = !selected;
            if (selected) {
                if (printed) sb_puts(&out, sep);
                sb_putn(&out, p, length);
                printed = 1;
            }
            if (!end) break;
            p = end + 1;
            field++;
        }
        put_line(out.data, 1);
        sb_free(&out);
        return 1;
    }

    StrBuf out;
    sb_init(&out);
    int position = 1;
    int printed = 0;
    int last_selected = 0;
    for (const char *p = line; *p; p++, position++) {
        int selected = in_ranges(cut->ranges, cut->range_count, position);
        if (cut->complement) selected = !selected;
        if (!selected) {
            last_selected = 0;
            continue;
        }
        if (cut->output_delimiter && printed && !last_selected) sb_puts(&out, cut->output_delimiter);
        sb_putc(&out, *p);
        printed = 1;
        last_selected = 1;
    }
    put_line(out.data, 1);
    sb_free(&out);
    return 1;
}

static int cmd_cut(int argc, char **argv) {
    static const OptSpec specs[] = {{'b', "bytes", 1},          {'c', "characters", 1},
                                    {'d', "delimiter", 1},      {'f', "fields", 1},
                                    {'n', NULL, 0},             {'s', "only-delimited", 0},
                                    {'C', "complement", 0},     {'O', "output-delimiter", 1},
                                    {'z', "zero-terminated", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "cut", &args, 1);
    if (early >= 0) return early;

    CutState cut;
    memset(&cut, 0, sizeof(cut));
    cut.delimiter = '\t';
    cut.mode = args_last_of(&args, "bcf");
    int modes = args_has(&args, 'b') + args_has(&args, 'c') + args_has(&args, 'f');
    if (modes > 1) {
        cmd_error("cut", "only one type of list may be specified");
        fprintf(stderr, "Try 'cut --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    if (!cut.mode) {
        cmd_error("cut", "you must specify a list of bytes, characters, or fields");
        fprintf(stderr, "Try 'cut --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    if (args_has(&args, 'd') && cut.mode != 'f') {
        cmd_error("cut", "an input delimiter may be specified only when operating on fields");
        fprintf(stderr, "Try 'cut --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    if (args_has(&args, 's') && cut.mode != 'f') {
        cmd_error("cut", "suppressing non-delimited lines makes sense\n\tonly when operating on fields");
        fprintf(stderr, "Try 'cut --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    const char *list = args_value(&args, cut.mode);
    if (!parse_ranges(list, cut.ranges, 64, &cut.range_count)) {
        if (*list == '\0') cmd_error("cut", "%s", cut.mode == 'f' ? "fields are numbered from 1"
                                                                   : "byte/character positions are numbered from 1");
        else cmd_error("cut", "invalid %s value '%s'", cut.mode == 'f' ? "field" : "byte/character position", list);
        fprintf(stderr, "Try 'cut --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    if (args_has(&args, 'd')) {
        const char *d = args_value(&args, 'd');
        if (strlen(d) > 1) {
            cmd_error("cut", "the delimiter must be a single character");
            fprintf(stderr, "Try 'cut --help' for more information.\n");
            args_free(&args);
            return 1;
        }
        cut.delimiter = d[0];
    }
    cut.only_delimited = args_has(&args, 's');
    cut.complement = args_has(&args, 'C');
    cut.output_delimiter = args_value(&args, 'O');

    int status = 0;
    each_operand_line("cut", &args, &status, cut_line, &cut);
    args_free(&args);
    return status;
}

static int class_member(const char *name, int c) {
    if (strcmp(name, "alpha") == 0) return isalpha(c);
    if (strcmp(name, "digit") == 0) return isdigit(c);
    if (strcmp(name, "alnum") == 0) return isalnum(c);
    if (strcmp(name, "upper") == 0) return isupper(c);
    if (strcmp(name, "lower") == 0) return islower(c);
    if (strcmp(name, "space") == 0) return isspace(c);
    if (strcmp(name, "blank") == 0) return c == ' ' || c == '\t';
    if (strcmp(name, "punct") == 0) return ispunct(c);
    if (strcmp(name, "print") == 0) return isprint(c);
    if (strcmp(name, "graph") == 0) return isgraph(c);
    if (strcmp(name, "cntrl") == 0) return iscntrl(c);
    if (strcmp(name, "xdigit") == 0) return isxdigit(c);
    return -1;
}

static int tr_escape(const char **pp) {
    const char *p = *pp;
    int c;
    switch (*p) {
    case 'a': c = 7; break;
    case 'b': c = 8; break;
    case 'f': c = 12; break;
    case 'n': c = '\n'; break;
    case 'r': c = '\r'; break;
    case 't': c = '\t'; break;
    case 'v': c = 11; break;
    case '\\': c = '\\'; break;
    default:
        if (*p >= '0' && *p <= '7') {
            c = 0;
            int digits = 0;
            while (digits < 3 && *p >= '0' && *p <= '7') {
                c = c * 8 + (*p - '0');
                p++;
                digits++;
            }
            *pp = p;
            return c;
        }
        c = *p;
        break;
    }
    *pp = p + 1;
    return c;
}

static int tr_expand(const char *spec, int for_set2, size_t set1_length, StrBuf *out, int *upper_lower) {
    *upper_lower = 0;
    const char *p = spec;
    while (*p) {
        int c;
        if (*p == '[' && p[1] == ':') {
            const char *close = strstr(p + 2, ":]");
            if (close) {
                char name[16];
                size_t length = (size_t)(close - p - 2);
                if (length < sizeof(name)) {
                    memcpy(name, p + 2, length);
                    name[length] = '\0';
                    if (class_member(name, 'a') >= 0) {
                        if (strcmp(name, "upper") == 0 || strcmp(name, "lower") == 0) *upper_lower = name[0];
                        for (int ch = 0; ch < 256; ch++)
                            if (class_member(name, ch)) sb_putc(out, (char)ch);
                        p = close + 2;
                        continue;
                    }
                }
            }
        }
        if (*p == '[' && p[1] == '=' && p[2] && p[3] == '=' && p[4] == ']') {
            sb_putc(out, p[2]);
            p += 5;
            continue;
        }
        if (*p == '[' && p[1] && p[2] == '*' && for_set2) {
            const char *close = strchr(p + 3, ']');
            if (close) {
                char repeated = p[1];
                long times = close == p + 3 ? (long)set1_length - (long)out->len : strtol(p + 3, NULL, p[3] == '0' ? 8 : 10);
                for (long i = 0; i < times; i++) sb_putc(out, repeated);
                p = close + 1;
                continue;
            }
        }
        if (*p == '\\' && p[1]) {
            p++;
            c = tr_escape(&p);
        } else {
            c = (unsigned char)*p++;
        }
        if (*p == '-' && p[1]) {
            const char *q = p + 1;
            int high;
            if (*q == '\\' && q[1]) {
                q++;
                high = tr_escape(&q);
            } else high = (unsigned char)*q++;
            if (high < c) return 0;
            for (int ch = c; ch <= high; ch++) sb_putc(out, (char)ch);
            p = q;
            continue;
        }
        sb_putc(out, (char)c);
    }
    return 1;
}

static int cmd_tr(int argc, char **argv) {
    static const OptSpec specs[] = {{'c', "complement", 0}, {'C', NULL, 0}, {'d', "delete", 0},
                                    {'s', "squeeze-repeats", 0}, {'t', "truncate-set1", 0},
                                    {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "tr", &args, 1);
    if (early >= 0) return early;

    int deleting = args_has(&args, 'd');
    int squeeze = args_has(&args, 's');
    int complement = args_has(&args, 'c') || args_has(&args, 'C');
    int truncate = args_has(&args, 't');

    if (args.operand_count < 1) {
        cmd_error("tr", "missing operand");
        fprintf(stderr, "Try 'tr --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    if (args.operand_count > 2) {
        cmd_error("tr", "extra operand '%s'", args.operands[2]);
        fprintf(stderr, "Try 'tr --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    if (deleting && !squeeze && args.operand_count == 2) {
        cmd_error("tr", "extra operand '%s'", args.operands[1]);
        fprintf(stderr, "Only one string may be given when deleting without squeezing repeats.");
        fprintf(stderr, "\nTry 'tr --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    if (!deleting && args.operand_count == 1 && !squeeze) {
        cmd_error("tr", "missing operand after '%s'", args.operands[0]);
        fprintf(stderr, "Two strings must be given when translating.\n");
        fprintf(stderr, "Try 'tr --help' for more information.\n");
        args_free(&args);
        return 1;
    }

    StrBuf set1, set2;
    sb_init(&set1);
    sb_init(&set2);
    int class1 = 0, class2 = 0;
    if (!tr_expand(args.operands[0], 0, 0, &set1, &class1)) {
        cmd_error("tr", "range-endpoints of '%s' are in reverse collating sequence order", args.operands[0]);
        goto fail;
    }
    if (complement) {
        unsigned char present[256] = {0};
        for (size_t i = 0; i < set1.len; i++) present[(unsigned char)set1.data[i]] = 1;
        sb_clear(&set1);
        for (int c = 0; c < 256; c++)
            if (!present[c]) sb_putc(&set1, (char)c);
    }
    if (args.operand_count == 2) {
        if (!tr_expand(args.operands[1], 1, set1.len, &set2, &class2)) {
            cmd_error("tr", "range-endpoints of '%s' are in reverse collating sequence order", args.operands[1]);
            goto fail;
        }
    }
    int translating = !deleting && args.operand_count == 2;
    if (translating) {
        if (set2.len == 0) {
            cmd_error("tr", "when not truncating set1, string2 must be non-empty");
            goto fail;
        }
        if (truncate && set1.len > set2.len) set1.len = set2.len;
        if (class1 == 'u' && class2 == 'l') {
            sb_clear(&set1);
            sb_clear(&set2);
            for (int c = 'A'; c <= 'Z'; c++) {
                sb_putc(&set1, (char)c);
                sb_putc(&set2, (char)tolower(c));
            }
        } else if (class1 == 'l' && class2 == 'u') {
            sb_clear(&set1);
            sb_clear(&set2);
            for (int c = 'a'; c <= 'z'; c++) {
                sb_putc(&set1, (char)c);
                sb_putc(&set2, (char)toupper(c));
            }
        }
    }

    int map[256];
    unsigned char in_set1[256] = {0};
    unsigned char squeeze_set[256] = {0};
    for (int c = 0; c < 256; c++) map[c] = c;
    for (size_t i = 0; i < set1.len; i++) {
        unsigned char c = (unsigned char)set1.data[i];
        in_set1[c] = 1;
        if (translating) map[c] = (unsigned char)set2.data[i < set2.len ? i : set2.len - 1];
    }
    if (squeeze) {
        const StrBuf *source = translating ? &set2 : &set1;
        for (size_t i = 0; i < source->len; i++) squeeze_set[(unsigned char)source->data[i]] = 1;
    }

    int last = -1;
    int c;
    while ((c = fgetc(stdin)) != EOF) {
        if (deleting && in_set1[c]) continue;
        int out = translating ? map[c] : c;
        if (squeeze && squeeze_set[out] && out == last) continue;
        fputc(out, stdout);
        last = out;
    }
    sb_free(&set1);
    sb_free(&set2);
    args_free(&args);
    return 0;

fail:
    sb_free(&set1);
    sb_free(&set2);
    args_free(&args);
    return 1;
}

typedef struct {
    StrList patterns;
    int fixed;
    int icase;
    int invert;
    int word;
    int whole;
    int count_only;
    int list_matching;
    int list_missing;
    int quiet;
    int line_numbers;
    int only_matching;
    int with_names;
    int no_names;
    int after;
    int before;
    long max_count;
    int suppress_errors;
    int byte_offset;
    int initial_tab;
    int null_after_name;
    int found_any;
    int error;
    int printed_groups;
} GrepState;

static int fixed_find(const char *text, const char *needle, int icase, size_t *where) {
    size_t length = strlen(needle);
    for (const char *p = text; *p || length == 0; p++) {
        if (icase ? _strnicmp(p, needle, length) == 0 : strncmp(p, needle, length) == 0) {
            *where = (size_t)(p - text);
            return 1;
        }
        if (!*p) break;
    }
    return 0;
}

static int grep_find(const GrepState *grep, const char *line, size_t offset, size_t *start, size_t *end) {
    size_t best_start = 0, best_end = 0;
    int found = 0;
    for (size_t i = 0; i < grep->patterns.len; i++) {
        const char *pattern = grep->patterns.items[i];
        size_t s, e;
        if (grep->fixed) {
            size_t where;
            if (!fixed_find(line + offset, pattern, grep->icase, &where)) continue;
            s = offset + where;
            e = s + strlen(pattern);
            if (grep->word) {
                int before = s > 0 && (isalnum((unsigned char)line[s - 1]) || line[s - 1] == '_');
                int after = line[e] && (isalnum((unsigned char)line[e]) || line[e] == '_');
                if (before || after) {
                    size_t retry_start, retry_end;
                    if (grep_find(grep, line, s + 1, &retry_start, &retry_end)) {
                        s = retry_start;
                        e = retry_end;
                    } else continue;
                }
            }
            if (grep->whole && (s != 0 || line[e])) continue;
        } else {
            RegexMatch match;
            if (!regex_search_at(pattern, line, offset, grep->icase ? REGEX_ICASE : 0, &match)) continue;
            s = (size_t)match.start[0];
            e = (size_t)match.end[0];
        }
        if (!found || s < best_start || (s == best_start && e > best_end)) {
            best_start = s;
            best_end = e;
            found = 1;
        }
    }
    if (found) {
        *start = best_start;
        *end = best_end;
    }
    return found;
}

static int grep_matches(const GrepState *grep, const char *line) {
    size_t s, e;
    return grep_find(grep, line, 0, &s, &e);
}

static void grep_prefix(const GrepState *grep, const char *name, long number, char separator) {
    if (grep->with_names && name) printf("%s%c", name, grep->null_after_name ? '\0' : separator);
    if (grep->line_numbers) printf("%ld%c", number, separator);
}

static int grep_file(GrepState *grep, const char *name, const char *shown) {
    FILE *f;
    if (!name || strcmp(name, "-") == 0) f = stdin;
    else {
        f = fopen(name, "rb");
        if (!f) {
            if (!grep->suppress_errors) cmd_error("grep", "%s: %s", name, strerror(errno));
            grep->error = 1;
            return 0;
        }
    }

    StrList lines;
    sl_init(&lines);
    StrBuf line;
    sb_init(&line);
    while (cmd_read_line(f, &line) != 0) sl_push_copy(&lines, line.data);
    sb_free(&line);
    if (f != stdin) fclose(f);

    long matches = 0;
    long last_printed = -1;
    long stop_after = grep->max_count;
    size_t offset_bytes = 0;
    for (size_t i = 0; i < lines.len; i++) {
        const char *text = lines.items[i];
        int matched = grep_matches(grep, text);
        if (grep->invert) matched = !matched;
        if (matched) {
            matches++;
            grep->found_any = 1;
            if (grep->quiet) {
                sl_free(&lines);
                return 1;
            }
            if (grep->count_only || grep->list_matching || grep->list_missing) {
                if (stop_after > 0 && matches >= stop_after) break;
                continue;
            }
            long first = (long)i - grep->before;
            if (first < 0) first = 0;
            if (last_printed >= 0 && first > last_printed + 1 && (grep->before || grep->after))
                puts("--");
            else if (last_printed < 0 && grep->printed_groups && (grep->before || grep->after))
                puts("--");
            for (long j = first; j < (long)i; j++) {
                if (j <= last_printed) continue;
                grep_prefix(grep, shown, j + 1, '-');
                put_line(lines.items[j], 1);
                last_printed = j;
            }
            if (grep->only_matching) {
                size_t offset = 0, s, e;
                while (grep_find(grep, text, offset, &s, &e)) {
                    if (e == s) {
                        offset = s + 1;
                        if (offset > strlen(text)) break;
                        continue;
                    }
                    grep_prefix(grep, shown, (long)i + 1, ':');
                    if (grep->byte_offset) printf("%zu:", offset_bytes + s);
                    printf("%.*s\n", (int)(e - s), text + s);
                    offset = e;
                }
            } else {
                grep_prefix(grep, shown, (long)i + 1, ':');
                if (grep->byte_offset) printf("%zu:", offset_bytes);
                put_line(text, 1);
            }
            last_printed = (long)i;
            grep->printed_groups = 1;
            long after_end = (long)i + grep->after;
            for (long j = (long)i + 1; j <= after_end && j < (long)lines.len; j++) {
                int later = grep_matches(grep, lines.items[j]);
                if (grep->invert) later = !later;
                if (later) break;
                grep_prefix(grep, shown, j + 1, '-');
                put_line(lines.items[j], 1);
                last_printed = j;
            }
            if (stop_after > 0 && matches >= stop_after) break;
        }
        offset_bytes += strlen(text) + 1;
    }
    if (grep->count_only) {
        if (grep->with_names && shown) printf("%s%c", shown, grep->null_after_name ? '\0' : ':');
        printf("%ld\n", matches);
    }
    if (grep->list_matching && matches > 0) printf("%s%c", shown ? shown : "(standard input)", grep->null_after_name ? '\0' : '\n');
    if (grep->list_missing && matches == 0) printf("%s%c", shown ? shown : "(standard input)", grep->null_after_name ? '\0' : '\n');
    sl_free(&lines);
    return matches > 0;
}

static void grep_tree(GrepState *grep, const char *directory, const char *shown_base) {
    char pattern[PATH_BUF];
    snprintf(pattern, sizeof(pattern), "%s" PATH_SEP_STR "*", directory);
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return;
    StrList names;
    sl_init(&names);
    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        sl_push_copy(&names, data.cFileName);
    } while (FindNextFileA(find, &data));
    FindClose(find);
    sl_sort(&names);
    for (size_t i = 0; i < names.len; i++) {
        char *child = path_join(directory, names.items[i]);
        char shown[PATH_BUF];
        if (shown_base && *shown_base) snprintf(shown, sizeof(shown), "%s/%s", shown_base, names.items[i]);
        else snprintf(shown, sizeof(shown), "%s", names.items[i]);
        if (path_is_dir(child)) grep_tree(grep, child, shown);
        else grep_file(grep, child, shown);
        free(child);
    }
    sl_free(&names);
}

static int cmd_grep(int argc, char **argv) {
    static const OptSpec specs[] = {
        {'A', "after-context", 1},   {'B', "before-context", 1}, {'C', "context", 1},
        {'c', "count", 0},           {'E', "extended-regexp", 0}, {'e', "regexp", 1},
        {'F', "fixed-strings", 0},   {'f', "file", 1},            {'G', "basic-regexp", 0},
        {'H', "with-filename", 0},   {'h', "no-filename", 0},     {'i', "ignore-case", 0},
        {'y', NULL, 0},              {'L', "files-without-match", 0}, {'l', "files-with-matches", 0},
        {'m', "max-count", 1},       {'n', "line-number", 0},     {'o', "only-matching", 0},
        {'q', "quiet", 0},           {'q', "silent", 0},          {'r', "recursive", 0},
        {'R', "dereference-recursive", 0}, {'s', "no-messages", 0}, {'v', "invert-match", 0},
        {'w', "word-regexp", 0},     {'x', "line-regexp", 0},     {'b', "byte-offset", 0},
        {'T', "initial-tab", 0},     {'Z', "null", 0},            {'a', "text", 0},
        {'I', NULL, 0},              {'U', "binary", 0},          {'z', "null-data", 0},
        {'P', "perl-regexp", 0},     {'1', "color", 2},           {'1', "colour", 2},
        {'2', "binary-files", 1},    {'3', "include", 1},         {'4', "exclude", 1},
        {'5', "exclude-dir", 1},     {'6', "no-ignore-case", 0},  {'7', "label", 1},
        {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "grep", &args, 2);
    if (early >= 0) return early;

    GrepState grep;
    memset(&grep, 0, sizeof(grep));
    sl_init(&grep.patterns);
    grep.fixed = args_has(&args, 'F');
    grep.icase = (args_has(&args, 'i') || args_has(&args, 'y')) && !args_has(&args, '6');
    grep.invert = args_has(&args, 'v');
    grep.word = args_has(&args, 'w');
    grep.whole = args_has(&args, 'x');
    grep.count_only = args_has(&args, 'c');
    grep.list_matching = args_has(&args, 'l');
    grep.list_missing = args_has(&args, 'L');
    grep.quiet = args_has(&args, 'q');
    grep.line_numbers = args_has(&args, 'n');
    grep.only_matching = args_has(&args, 'o');
    grep.suppress_errors = args_has(&args, 's');
    grep.byte_offset = args_has(&args, 'b');
    grep.null_after_name = args_has(&args, 'Z');
    grep.max_count = args_has(&args, 'm') ? atol(args_value(&args, 'm')) : 0;
    int recursive = args_has(&args, 'r') || args_has(&args, 'R');
    int extended = args_has(&args, 'E') || args_has(&args, 'P');
    if (args_has(&args, 'C')) grep.after = grep.before = atoi(args_value(&args, 'C'));
    if (args_has(&args, 'A')) grep.after = atoi(args_value(&args, 'A'));
    if (args_has(&args, 'B')) grep.before = atoi(args_value(&args, 'B'));

    StrList raw;
    sl_init(&raw);
    for (int i = 0; i < args_count(&args, 'e'); i++) sl_push_copy(&raw, args_value_at(&args, 'e', i));
    for (int i = 0; i < args_count(&args, 'f'); i++) {
        FILE *f = cmd_open("grep", args_value_at(&args, 'f', i));
        if (!f) {
            sl_free(&raw);
            sl_free(&grep.patterns);
            args_free(&args);
            return 2;
        }
        StrBuf line;
        sb_init(&line);
        while (cmd_read_line(f, &line) != 0) sl_push_copy(&raw, line.data);
        sb_free(&line);
        cmd_close(f);
    }
    int first_operand = 0;
    if (raw.len == 0) {
        if (args.operand_count == 0) {
            fprintf(stderr, "Usage: grep [OPTION]... PATTERNS [FILE]...\n");
            fprintf(stderr, "Try 'grep --help' for more information.\n");
            sl_free(&raw);
            sl_free(&grep.patterns);
            args_free(&args);
            return 2;
        }
        char *copy = xstrdup(args.operands[0]);
        char *cursor = copy;
        char *piece;
        while ((piece = str_next_field(&cursor, '\n')) != NULL) sl_push_copy(&raw, piece);
        free(copy);
        first_operand = 1;
    }
    for (size_t i = 0; i < raw.len; i++) {
        StrBuf pattern;
        sb_init(&pattern);
        if (grep.fixed) sb_puts(&pattern, raw.items[i]);
        else {
            StrBuf ere;
            sb_init(&ere);
            if (extended) sb_puts(&ere, raw.items[i]);
            else regex_bre_to_ere(raw.items[i], &ere);
            if (grep.whole) sb_puts(&pattern, "^(?:");
            else if (grep.word) sb_puts(&pattern, "\\b(?:");
            sb_puts(&pattern, ere.data);
            if (grep.whole) sb_puts(&pattern, ")$");
            else if (grep.word) sb_puts(&pattern, ")\\b");
            sb_free(&ere);
            if (!regex_valid(pattern.data)) {
                cmd_error("grep", "Invalid regular expression: %s", raw.items[i]);
                sb_free(&pattern);
                sl_free(&raw);
                sl_free(&grep.patterns);
                args_free(&args);
                return 2;
            }
        }
        sl_push(&grep.patterns, sb_take(&pattern));
    }
    sl_free(&raw);

    int files = args.operand_count - first_operand;
    char **names = args.operands + first_operand;
    grep.with_names = args_has(&args, 'H') || ((files > 1 || recursive) && !args_has(&args, 'h'));

    if (files == 0) {
        if (recursive) grep_tree(&grep, ".", "");
        else grep_file(&grep, NULL, args_value(&args, '7'));
    } else {
        for (int i = 0; i < files; i++) {
            if (strcmp(names[i], "-") == 0) {
                grep_file(&grep, NULL, args_value(&args, '7') ? args_value(&args, '7') : "(standard input)");
                continue;
            }
            if (path_is_dir(names[i])) {
                if (recursive) {
                    char base[PATH_BUF];
                    snprintf(base, sizeof(base), "%s", names[i]);
                    size_t length = strlen(base);
                    while (length > 1 && (base[length - 1] == '/' || base[length - 1] == '\\')) base[--length] = '\0';
                    grep_tree(&grep, base, base);
                } else {
                    if (!grep.suppress_errors) cmd_error("grep", "%s: Is a directory", names[i]);
                    grep.error = 1;
                }
                continue;
            }
            grep_file(&grep, names[i], names[i]);
            if (grep.quiet && grep.found_any) break;
        }
    }

    sl_free(&grep.patterns);
    args_free(&args);
    if (grep.quiet && grep.found_any) return 0;
    if (grep.error) return 2;
    return grep.found_any ? 0 : 1;
}

typedef enum { ADDR_NONE, ADDR_LINE, ADDR_LAST, ADDR_REGEX, ADDR_STEP, ADDR_PLUS, ADDR_MULTIPLE } AddrKind;

typedef struct {
    AddrKind kind;
    long number;
    long step;
    char *regex;
    int icase;
} SedAddr;

typedef struct {
    SedAddr addr1, addr2;
    int negate;
    char name;
    char *text;
    char *regex;
    char *replacement;
    int global;
    int print_flag;
    int icase;
    long nth;
    char *ymap_from;
    char *ymap_to;
    int block_end;
    int jump;
    int active;
    long active_end;
    int exit_code;
    char *write_file;
} SedCmd;

typedef struct {
    SedCmd *items;
    int count;
    int cap;
    int extended;
    int quiet;
    int separate;
    int debug;
    const char *last_regex;
} SedProgram;

static SedCmd *sed_add(SedProgram *program) {
    if (program->count == program->cap) {
        program->cap = program->cap ? program->cap * 2 : 16;
        program->items = xrealloc(program->items, (size_t)program->cap * sizeof(SedCmd));
    }
    SedCmd *cmd = &program->items[program->count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->block_end = -1;
    cmd->jump = -1;
    return cmd;
}

static char *sed_read_delimited(const char **pp, char delimiter, int is_regex) {
    const char *p = *pp;
    StrBuf out;
    sb_init(&out);
    while (*p && *p != delimiter) {
        if (*p == '\\' && p[1]) {
            if (p[1] == delimiter) {
                if (is_regex && strchr(".*[]^$\\+?(){}|/", delimiter) && delimiter != '/') {
                    sb_putc(&out, '\\');
                }
                sb_putc(&out, delimiter);
                p += 2;
                continue;
            }
            if (p[1] == 'n' && is_regex) {
                sb_puts(&out, "\\n");
                p += 2;
                continue;
            }
            sb_putc(&out, p[0]);
            sb_putc(&out, p[1]);
            p += 2;
            continue;
        }
        sb_putc(&out, *p++);
    }
    if (*p != delimiter) {
        sb_free(&out);
        return NULL;
    }
    *pp = p + 1;
    return sb_take(&out);
}

static const char *sed_skip_space(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static int sed_parse_address(const char **pp, SedAddr *addr, SedProgram *program) {
    const char *p = *pp;
    addr->kind = ADDR_NONE;
    if (isdigit((unsigned char)*p)) {
        addr->number = strtol(p, (char **)&p, 10);
        addr->kind = ADDR_LINE;
        if (*p == '~') {
            p++;
            addr->step = strtol(p, (char **)&p, 10);
            addr->kind = ADDR_STEP;
        }
    } else if (*p == '$') {
        addr->kind = ADDR_LAST;
        p++;
    } else if (*p == '/' || *p == '\\') {
        char delimiter = '/';
        if (*p == '\\') {
            p++;
            delimiter = *p;
        }
        p++;
        char *regex = sed_read_delimited(&p, delimiter, 1);
        if (!regex) return 0;
        StrBuf ere;
        sb_init(&ere);
        if (program->extended) sb_puts(&ere, regex);
        else regex_bre_to_ere(regex, &ere);
        free(regex);
        addr->regex = sb_take(&ere);
        addr->kind = ADDR_REGEX;
        while (*p == 'I' || *p == 'M') {
            if (*p == 'I') addr->icase = 1;
            p++;
        }
    } else {
        return 1;
    }
    *pp = p;
    return 1;
}

static int sed_parse_second_address(const char **pp, SedAddr *addr, SedProgram *program) {
    const char *p = *pp;
    if (*p == '+') {
        p++;
        addr->number = strtol(p, (char **)&p, 10);
        addr->kind = ADDR_PLUS;
        *pp = p;
        return 1;
    }
    if (*p == '~') {
        p++;
        addr->number = strtol(p, (char **)&p, 10);
        addr->kind = ADDR_MULTIPLE;
        *pp = p;
        return 1;
    }
    return sed_parse_address(pp, addr, program);
}

static char *sed_read_text(const char **pp) {
    const char *p = *pp;
    p = sed_skip_space(p);
    if (*p == '\\') {
        p++;
        if (*p == '\n') p++;
        else p = sed_skip_space(p);
    }
    StrBuf out;
    sb_init(&out);
    while (*p && *p != '\n') {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == '\n') {
                sb_putc(&out, '\n');
                p++;
                continue;
            }
            sb_putc(&out, *p++);
            continue;
        }
        sb_putc(&out, *p++);
    }
    *pp = p;
    return sb_take(&out);
}

static char *sed_read_label(const char **pp) {
    const char *p = sed_skip_space(*pp);
    const char *start = p;
    while (*p && *p != '\n' && *p != ';' && *p != '}') p++;
    const char *end = p;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
    *pp = p;
    return xstrndup(start, (size_t)(end - start));
}

static int sed_parse(SedProgram *program, const char *script, int *block_stack, int *depth) {
    const char *p = script;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ';') p++;
        if (!*p) return 1;
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }
        SedCmd *cmd = sed_add(program);
        if (!sed_parse_address(&p, &cmd->addr1, program)) return 0;
        p = sed_skip_space(p);
        if (*p == ',' && cmd->addr1.kind != ADDR_NONE) {
            p++;
            p = sed_skip_space(p);
            if (!sed_parse_second_address(&p, &cmd->addr2, program)) return 0;
            p = sed_skip_space(p);
        }
        while (*p == '!') {
            cmd->negate = 1;
            p++;
            p = sed_skip_space(p);
        }
        cmd->name = *p;
        if (!*p) return 0;
        p++;
        switch (cmd->name) {
        case '{':
            if (*depth >= 32) return 0;
            block_stack[(*depth)++] = program->count - 1;
            break;
        case '}': {
            if (*depth == 0) return 0;
            int open = block_stack[--(*depth)];
            program->items[open].block_end = program->count - 1;
            break;
        }
        case 's': {
            char delimiter = *p++;
            if (!delimiter) return 0;
            char *regex = sed_read_delimited(&p, delimiter, 1);
            if (!regex) return 0;
            char *replacement = sed_read_delimited(&p, delimiter, 0);
            if (!replacement) {
                free(regex);
                return 0;
            }
            StrBuf ere;
            sb_init(&ere);
            if (program->extended) sb_puts(&ere, regex);
            else regex_bre_to_ere(regex, &ere);
            free(regex);
            cmd->regex = sb_take(&ere);
            cmd->replacement = replacement;
            while (*p && *p != ';' && *p != '\n' && *p != '}' && *p != ' ') {
                if (*p == 'g') cmd->global = 1;
                else if (*p == 'p') cmd->print_flag = 1;
                else if (*p == 'i' || *p == 'I') cmd->icase = 1;
                else if (*p == 'm' || *p == 'M') {
                } else if (isdigit((unsigned char)*p)) {
                    cmd->nth = strtol(p, (char **)&p, 10);
                    continue;
                } else if (*p == 'w') {
                    p++;
                    cmd->write_file = sed_read_label(&p);
                    break;
                } else if (*p == 'e') {
                } else return 0;
                p++;
            }
            break;
        }
        case 'y': {
            char delimiter = *p++;
            char *from = sed_read_delimited(&p, delimiter, 0);
            if (!from) return 0;
            char *to = sed_read_delimited(&p, delimiter, 0);
            if (!to || strlen(from) != strlen(to)) {
                free(from);
                free(to);
                return 0;
            }
            cmd->ymap_from = from;
            cmd->ymap_to = to;
            break;
        }
        case 'a':
        case 'i':
        case 'c':
            cmd->text = sed_read_text(&p);
            break;
        case ':':
        case 'b':
        case 't':
        case 'T':
        case 'r':
        case 'R':
        case 'w':
        case 'W':
            cmd->text = sed_read_label(&p);
            break;
        case 'q':
        case 'Q':
        case 'l':
        case 'L':
            p = sed_skip_space(p);
            if (isdigit((unsigned char)*p)) cmd->exit_code = (int)strtol(p, (char **)&p, 10);
            break;
        case 'd':
        case 'D':
        case 'p':
        case 'P':
        case 'n':
        case 'N':
        case 'h':
        case 'H':
        case 'g':
        case 'G':
        case 'x':
        case '=':
        case 'z':
        case 'F':
            break;
        default:
            cmd_error("sed", "-e expression #1, char %d: unknown command: `%c'", (int)(p - script), cmd->name);
            return -1;
        }
        p = sed_skip_space(p);
        if (*p == '}' || cmd->name == '{' || cmd->name == '}') continue;
        if (*p && *p != ';' && *p != '\n' && *p != '#') {
            if (cmd->name == 'a' || cmd->name == 'i' || cmd->name == 'c') continue;
            cmd_error("sed", "-e expression #1, char %d: extra characters after command", (int)(p - script));
            return -1;
        }
    }
}

static void sed_resolve_labels(SedProgram *program) {
    for (int i = 0; i < program->count; i++) {
        SedCmd *cmd = &program->items[i];
        if (cmd->name != 'b' && cmd->name != 't' && cmd->name != 'T') continue;
        if (!cmd->text || !*cmd->text) {
            cmd->jump = program->count;
            continue;
        }
        for (int j = 0; j < program->count; j++) {
            if (program->items[j].name == ':' && strcmp(program->items[j].text, cmd->text) == 0) {
                cmd->jump = j;
                break;
            }
        }
        if (cmd->jump < 0) {
            cmd_error("sed", "can't find label for jump to `%s'", cmd->text);
            cmd->jump = program->count;
        }
    }
}

typedef struct {
    StrList lines;
    size_t next;
    long number;
    StrBuf pattern;
    StrBuf hold;
    StrBuf append;
    int quit;
    int exit_code;
    int substituted;
    int deleted;
    FILE *out;
} SedInput;

static int sed_next_line(SedInput *in) {
    if (in->next >= in->lines.len) return 0;
    sb_clear(&in->pattern);
    sb_puts(&in->pattern, in->lines.items[in->next++]);
    in->number++;
    return 1;
}

static int sed_is_last(const SedInput *in) {
    return in->next >= in->lines.len;
}

static int sed_addr_match(const SedAddr *addr, const SedInput *in) {
    switch (addr->kind) {
    case ADDR_LINE: return in->number == addr->number;
    case ADDR_LAST: return sed_is_last(in);
    case ADDR_REGEX:
        return regex_search_at(addr->regex, in->pattern.data, 0, addr->icase ? REGEX_ICASE : 0, NULL);
    case ADDR_STEP:
        if (addr->step <= 0) return in->number == addr->number;
        return in->number >= addr->number && (in->number - addr->number) % addr->step == 0;
    default: return 1;
    }
}

static int sed_selected(SedCmd *cmd, const SedInput *in) {
    int selected;
    if (cmd->addr1.kind == ADDR_NONE) selected = 1;
    else if (cmd->addr2.kind == ADDR_NONE) selected = sed_addr_match(&cmd->addr1, in);
    else if (cmd->active) {
        selected = 1;
        int ends;
        if (cmd->addr2.kind == ADDR_LINE) ends = in->number >= cmd->addr2.number;
        else if (cmd->addr2.kind == ADDR_PLUS || cmd->addr2.kind == ADDR_MULTIPLE) ends = in->number >= cmd->active_end;
        else ends = sed_addr_match(&cmd->addr2, in);
        if (ends) cmd->active = 0;
    } else if (sed_addr_match(&cmd->addr1, in)) {
        selected = 1;
        cmd->active = 1;
        if (cmd->addr2.kind == ADDR_LINE && cmd->addr2.number <= in->number) cmd->active = 0;
        else if (cmd->addr2.kind == ADDR_PLUS) cmd->active_end = in->number + cmd->addr2.number;
        else if (cmd->addr2.kind == ADDR_MULTIPLE) {
            long m = cmd->addr2.number > 0 ? cmd->addr2.number : 1;
            cmd->active_end = ((in->number + m - 1) / m) * m;
            if (cmd->active_end <= in->number) cmd->active = 0;
        }
        if (cmd->addr2.kind == ADDR_PLUS && cmd->addr2.number == 0) cmd->active = 0;
    } else {
        selected = 0;
    }
    return cmd->negate ? !selected : selected;
}

static void sed_substitute(SedCmd *cmd, SedInput *in) {
    StrBuf out;
    sb_init(&out);
    const char *text = in->pattern.data;
    size_t offset = 0;
    long count = 0;
    int replaced = 0;
    int flags = cmd->icase ? REGEX_ICASE : 0;
    while (offset <= strlen(text)) {
        RegexMatch match;
        if (!regex_search_at(cmd->regex, text, offset, flags, &match)) break;
        count++;
        size_t start = (size_t)match.start[0];
        size_t end = (size_t)match.end[0];
        int take = cmd->nth ? (count >= cmd->nth && (cmd->global || count == cmd->nth)) : (cmd->global || count == 1);
        sb_putn(&out, text + offset, start - offset);
        if (take) {
            for (const char *r = cmd->replacement; *r; r++) {
                if (*r == '\\' && r[1]) {
                    r++;
                    if (isdigit((unsigned char)*r)) {
                        int group = *r - '0';
                        if (group < match.count && match.start[group] >= 0)
                            sb_putn(&out, text + match.start[group], (size_t)(match.end[group] - match.start[group]));
                    } else if (*r == 'n') sb_putc(&out, '\n');
                    else if (*r == 't') sb_putc(&out, '\t');
                    else sb_putc(&out, *r);
                    continue;
                }
                if (*r == '&') {
                    sb_putn(&out, text + start, end - start);
                    continue;
                }
                sb_putc(&out, *r);
            }
            replaced = 1;
        } else {
            sb_putn(&out, text + start, end - start);
        }
        if (end == start) {
            if (text[end]) sb_putc(&out, text[end]);
            offset = end + 1;
        } else offset = end;
        if (take && !cmd->global) break;
        if (!text[end] && end == start) break;
    }
    if (offset <= strlen(text)) sb_puts(&out, text + offset);
    if (replaced) {
        sb_clear(&in->pattern);
        sb_puts(&in->pattern, out.data);
        in->substituted = 1;
        if (cmd->print_flag) fprintf(in->out, "%s\n", in->pattern.data);
        if (cmd->write_file) {
            FILE *w = fopen(cmd->write_file, "ab");
            if (w) {
                fprintf(w, "%s\n", in->pattern.data);
                fclose(w);
            }
        }
    }
    sb_free(&out);
}

static void sed_flush_append(SedInput *in) {
    if (in->append.len) {
        fputs(in->append.data, in->out);
        sb_clear(&in->append);
    }
}

static void sed_run(SedProgram *program, SedInput *in) {
    while (!in->quit && sed_next_line(in)) {
    restart:
        in->substituted = 0;
        in->deleted = 0;
        int pc = 0;
        while (pc < program->count) {
            SedCmd *cmd = &program->items[pc];
            if (cmd->name == ':') {
                pc++;
                continue;
            }
            if (cmd->name == '}') {
                pc++;
                continue;
            }
            if (!sed_selected(cmd, in)) {
                pc = cmd->name == '{' ? cmd->block_end + 1 : pc + 1;
                continue;
            }
            switch (cmd->name) {
            case '{': break;
            case 's': sed_substitute(cmd, in); break;
            case 'd': in->deleted = 1; pc = program->count; continue;
            case 'D': {
                char *newline = strchr(in->pattern.data, '\n');
                if (!newline) {
                    in->deleted = 1;
                    pc = program->count;
                    continue;
                }
                size_t rest = strlen(newline + 1);
                memmove(in->pattern.data, newline + 1, rest + 1);
                in->pattern.len = rest;
                sed_flush_append(in);
                goto restart;
            }
            case 'p': fprintf(in->out, "%s\n", in->pattern.data); break;
            case 'P': {
                char *newline = strchr(in->pattern.data, '\n');
                fprintf(in->out, "%.*s\n", newline ? (int)(newline - in->pattern.data) : (int)in->pattern.len, in->pattern.data);
                break;
            }
            case 'n':
                if (!program->quiet) fprintf(in->out, "%s\n", in->pattern.data);
                sed_flush_append(in);
                if (!sed_next_line(in)) {
                    in->quit = 1;
                    in->deleted = 1;
                    pc = program->count;
                    continue;
                }
                break;
            case 'N':
                if (sed_is_last(in)) {
                    in->quit = 1;
                    pc = program->count;
                    continue;
                }
                sb_putc(&in->pattern, '\n');
                sb_puts(&in->pattern, in->lines.items[in->next++]);
                in->number++;
                break;
            case 'h': sb_clear(&in->hold); sb_puts(&in->hold, in->pattern.data); break;
            case 'H': sb_putc(&in->hold, '\n'); sb_puts(&in->hold, in->pattern.data); break;
            case 'g': sb_clear(&in->pattern); sb_puts(&in->pattern, in->hold.data); break;
            case 'G': sb_putc(&in->pattern, '\n'); sb_puts(&in->pattern, in->hold.data); break;
            case 'x': {
                StrBuf swap = in->pattern;
                in->pattern = in->hold;
                in->hold = swap;
                break;
            }
            case 'a': sb_puts(&in->append, cmd->text); sb_putc(&in->append, '\n'); break;
            case 'i': fprintf(in->out, "%s\n", cmd->text); break;
            case 'c':
                if (cmd->addr2.kind == ADDR_NONE || !cmd->active) fprintf(in->out, "%s\n", cmd->text);
                in->deleted = 1;
                pc = program->count;
                continue;
            case 'y':
                for (size_t i = 0; i < in->pattern.len; i++) {
                    const char *hit = strchr(cmd->ymap_from, in->pattern.data[i]);
                    if (hit && *hit) in->pattern.data[i] = cmd->ymap_to[hit - cmd->ymap_from];
                }
                break;
            case '=': fprintf(in->out, "%ld\n", in->number); break;
            case 'l': {
                StrBuf shown;
                sb_init(&shown);
                for (const char *p = in->pattern.data; *p; p++) {
                    unsigned char c = (unsigned char)*p;
                    if (c == '\\') sb_puts(&shown, "\\\\");
                    else if (c == '\n') sb_puts(&shown, "\\n");
                    else if (c == '\t') sb_puts(&shown, "\\t");
                    else if (c < 32 || c >= 127) sb_printf(&shown, "\\%03o", c);
                    else sb_putc(&shown, (char)c);
                }
                fprintf(in->out, "%s$\n", shown.data);
                sb_free(&shown);
                break;
            }
            case 'q':
                in->quit = 1;
                in->exit_code = cmd->exit_code;
                pc = program->count;
                continue;
            case 'Q':
                in->quit = 1;
                in->deleted = 1;
                in->exit_code = cmd->exit_code;
                pc = program->count;
                continue;
            case 'z': sb_clear(&in->pattern); break;
            case 'b': pc = cmd->jump; continue;
            case 't':
                if (in->substituted) {
                    in->substituted = 0;
                    pc = cmd->jump;
                    continue;
                }
                break;
            case 'T':
                if (!in->substituted) {
                    pc = cmd->jump;
                    continue;
                }
                in->substituted = 0;
                break;
            case 'r': {
                FILE *f = fopen(cmd->text, "rb");
                if (f) {
                    char block[4096];
                    size_t n;
                    while ((n = fread(block, 1, sizeof(block), f)) > 0) sb_putn(&in->append, block, n);
                    fclose(f);
                }
                break;
            }
            case 'w': {
                FILE *w = fopen(cmd->text, "ab");
                if (w) {
                    fprintf(w, "%s\n", in->pattern.data);
                    fclose(w);
                }
                break;
            }
            default: break;
            }
            pc++;
        }
        if (!in->deleted && !program->quiet) fprintf(in->out, "%s\n", in->pattern.data);
        sed_flush_append(in);
    }
}

static void sed_program_free(SedProgram *program) {
    for (int i = 0; i < program->count; i++) {
        SedCmd *cmd = &program->items[i];
        free(cmd->addr1.regex);
        free(cmd->addr2.regex);
        free(cmd->text);
        free(cmd->regex);
        free(cmd->replacement);
        free(cmd->ymap_from);
        free(cmd->ymap_to);
        free(cmd->write_file);
    }
    free(program->items);
}

static int cmd_sed(int argc, char **argv) {
    static const OptSpec specs[] = {{'n', "quiet", 0},       {'n', "silent", 0},
                                    {'e', "expression", 1},  {'f', "file", 1},
                                    {'E', "regexp-extended", 0}, {'r', NULL, 0},
                                    {'i', "in-place", 2},    {'s', "separate", 0},
                                    {'z', "null-data", 0},   {'u', "unbuffered", 0},
                                    {'l', "line-length", 1}, {'1', "posix", 0},
                                    {'2', "debug", 0},       {'3', "sandbox", 0},
                                    {'4', "follow-symlinks", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "sed", &args, 1);
    if (early >= 0) return early;

    SedProgram program;
    memset(&program, 0, sizeof(program));
    program.quiet = args_has(&args, 'n');
    program.extended = args_has(&args, 'E') || args_has(&args, 'r');
    int in_place = args_has(&args, 'i');
    program.separate = args_has(&args, 's') || in_place;

    StrBuf script;
    sb_init(&script);
    int scripts = 0;
    for (int i = 0; i < args.count; i++) {
        if (args.letters[i] == 'e') {
            if (script.len) sb_putc(&script, '\n');
            sb_puts(&script, args.values[i]);
            scripts++;
        } else if (args.letters[i] == 'f') {
            FILE *f = cmd_open("sed", args.values[i]);
            if (!f) {
                sb_free(&script);
                args_free(&args);
                return 1;
            }
            StrBuf line;
            sb_init(&line);
            while (cmd_read_line(f, &line) != 0) {
                if (script.len) sb_putc(&script, '\n');
                sb_puts(&script, line.data);
            }
            sb_free(&line);
            cmd_close(f);
            scripts++;
        }
    }
    int first_operand = 0;
    if (!scripts) {
        if (args.operand_count == 0) {
            fprintf(stderr, "Usage: sed [OPTION]... {script-only-if-no-other-script} [input-file]...\n");
            sb_free(&script);
            args_free(&args);
            return 1;
        }
        sb_puts(&script, args.operands[0]);
        first_operand = 1;
    }

    int block_stack[32];
    int depth = 0;
    int parsed = sed_parse(&program, script.data, block_stack, &depth);
    if (parsed == 0) cmd_error("sed", "-e expression #1, char %zu: unterminated command or address", script.len);
    if (parsed <= 0 || depth != 0) {
        if (depth != 0) cmd_error("sed", "-e expression #1, char %zu: unmatched `{'", script.len);
        sed_program_free(&program);
        sb_free(&script);
        args_free(&args);
        return 1;
    }
    sed_resolve_labels(&program);
    sb_free(&script);

    int files = args.operand_count - first_operand;
    char **names = args.operands + first_operand;
    int status = 0;

    SedInput in;
    memset(&in, 0, sizeof(in));
    sl_init(&in.lines);
    sb_init(&in.pattern);
    sb_init(&in.hold);
    sb_init(&in.append);
    in.out = stdout;

    int groups = program.separate ? (files ? files : 1) : 1;
    for (int g = 0; g < groups && !in.quit; g++) {
        sl_clear(&in.lines);
        in.next = 0;
        if (program.separate) in.number = 0;
        for (int i = 0; i < program.count; i++) program.items[i].active = 0;

        int from = program.separate ? g : 0;
        int to = program.separate ? g + 1 : (files ? files : 1);
        for (int i = from; i < to; i++) {
            const char *name = files ? names[i] : NULL;
            FILE *f;
            if (!name || strcmp(name, "-") == 0) f = stdin;
            else {
                f = fopen(name, "rb");
                if (!f) {
                    cmd_error("sed", "can't read %s: %s", name, strerror(errno));
                    status = 2;
                    continue;
                }
            }
            StrBuf line;
            sb_init(&line);
            while (cmd_read_line(f, &line) != 0) sl_push_copy(&in.lines, line.data);
            sb_free(&line);
            if (f != stdin) fclose(f);
        }

        char temp[PATH_BUF] = "";
        if (in_place && files) {
            snprintf(temp, sizeof(temp), "%s.sedtmp", names[g]);
            in.out = fopen(temp, "wb");
            if (!in.out) {
                cmd_error("sed", "couldn't open temporary file %s: %s", temp, strerror(errno));
                in.out = stdout;
                status = 4;
                continue;
            }
        }
        sed_run(&program, &in);
        if (in.out != stdout) {
            fclose(in.out);
            in.out = stdout;
            const char *suffix = args_value(&args, 'i');
            if (suffix && *suffix) {
                char backup[PATH_BUF];
                if (strchr(suffix, '*')) {
                    StrBuf b;
                    sb_init(&b);
                    for (const char *p = suffix; *p; p++) {
                        if (*p == '*') sb_puts(&b, cmd_name(names[g]));
                        else sb_putc(&b, *p);
                    }
                    snprintf(backup, sizeof(backup), "%s", b.data);
                    sb_free(&b);
                } else snprintf(backup, sizeof(backup), "%s%s", names[g], suffix);
                DeleteFileA(backup);
                MoveFileExA(names[g], backup, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
            }
            MoveFileExA(temp, names[g], MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
        }
    }
    if (in.exit_code) status = in.exit_code;

    sl_free(&in.lines);
    sb_free(&in.pattern);
    sb_free(&in.hold);
    sb_free(&in.append);
    sed_program_free(&program);
    args_free(&args);
    return status;
}

static int cmd_nl(int argc, char **argv) {
    static const OptSpec specs[] = {{'b', "body-numbering", 1},   {'d', "section-delimiter", 1},
                                    {'f', "footer-numbering", 1}, {'h', "header-numbering", 1},
                                    {'i', "line-increment", 1},   {'l', "join-blank-lines", 1},
                                    {'n', "number-format", 1},    {'p', "no-renumber", 0},
                                    {'s', "number-separator", 1}, {'v', "starting-line-number", 1},
                                    {'w', "number-width", 1},     {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "nl", &args, 1);
    if (early >= 0) return early;

    const char *body = args_has(&args, 'b') ? args_value(&args, 'b') : "t";
    const char *format = args_has(&args, 'n') ? args_value(&args, 'n') : "rn";
    const char *separator = args_has(&args, 's') ? args_value(&args, 's') : "\t";
    long number = args_has(&args, 'v') ? atol(args_value(&args, 'v')) : 1;
    long increment = args_has(&args, 'i') ? atol(args_value(&args, 'i')) : 1;
    int width = args_has(&args, 'w') ? atoi(args_value(&args, 'w')) : 6;
    long join_blank = args_has(&args, 'l') ? atol(args_value(&args, 'l')) : 1;
    if (width < 1) width = 6;

    StrList lines;
    sl_init(&lines);
    int status = cmd_read_lines("nl", &args, &lines);
    long blank_run = 0;
    for (size_t i = 0; i < lines.len; i++) {
        const char *line = lines.items[i];
        int numbered;
        if (body[0] == 'a') numbered = 1;
        else if (body[0] == 'n') numbered = 0;
        else if (body[0] == 'p') numbered = regex_search(body + 1, line, NULL);
        else numbered = line[0] != '\0';
        if (body[0] == 'a' && line[0] == '\0') {
            blank_run++;
            if (blank_run < join_blank) numbered = 0;
            else blank_run = 0;
        } else blank_run = 0;

        if (numbered) {
            if (strcmp(format, "ln") == 0) printf("%-*ld%s", width, number, separator);
            else if (strcmp(format, "rz") == 0) printf("%0*ld%s", width, number, separator);
            else printf("%*ld%s", width, number, separator);
            number += increment;
        } else {
            printf("%*s", width + (int)strlen(separator), "");
        }
        puts(line);
    }
    sl_free(&lines);
    args_free(&args);
    return status;
}

static int cmd_tac(int argc, char **argv) {
    static const OptSpec specs[] = {{'b', "before", 0}, {'r', "regex", 0}, {'s', "separator", 1}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "tac", &args, 1);
    if (early >= 0) return early;
    StrList lines;
    sl_init(&lines);
    int status = cmd_read_lines("tac", &args, &lines);
    for (size_t i = lines.len; i > 0; i--) puts(lines.items[i - 1]);
    sl_free(&lines);
    args_free(&args);
    return status;
}

static int rev_line(const char *line, int newline, void *ctx) {
    (void)ctx;
    size_t length = strlen(line);
    for (size_t c = length; c > 0; c--) putchar(line[c - 1]);
    if (newline) putchar('\n');
    return 1;
}

static int cmd_rev(int argc, char **argv) {
    Args args;
    int early = parse_or_exit(argc, argv, NO_OPTIONS, "rev", &args, 1);
    if (early >= 0) return early;
    int status = 0;
    each_operand_line("rev", &args, &status, rev_line, NULL);
    args_free(&args);
    return status;
}

static int cmd_yes(int argc, char **argv) {
    Args args;
    int early = parse_or_exit(argc, argv, NO_OPTIONS, "yes", &args, 1);
    if (early >= 0) return early;
    StrBuf text;
    sb_init(&text);
    if (args.operand_count == 0) sb_puts(&text, "y");
    for (int i = 0; i < args.operand_count; i++) {
        if (i > 0) sb_putc(&text, ' ');
        sb_puts(&text, args.operands[i]);
    }
    sb_putc(&text, '\n');
    for (long i = 0; i < 1000000; i++) {
        if (fwrite(text.data, 1, text.len, stdout) != text.len) break;
        if (i % 256 == 0 && (fflush(stdout) != 0 || shell.interrupted)) break;
    }
    sb_free(&text);
    args_free(&args);
    return 0;
}

typedef struct {
    int width;
    int spaces;
    int bytes;
} FoldState;

static int fold_line(const char *line, int newline, void *ctx) {
    FoldState *fold = ctx;
    (void)newline;
    size_t length = strlen(line);
    size_t start = 0;
    while (length - start > (size_t)fold->width) {
        size_t cut = start + (size_t)fold->width;
        if (fold->spaces) {
            size_t back = cut;
            while (back > start && line[back - 1] != ' ' && line[back - 1] != '\t') back--;
            if (back > start) cut = back;
        }
        printf("%.*s\n", (int)(cut - start), line + start);
        start = cut;
    }
    printf("%s\n", line + start);
    return 1;
}

static int cmd_fold(int argc, char **argv) {
    static const OptSpec specs[] = {{'b', "bytes", 0}, {'s', "spaces", 0}, {'w', "width", 1}, {0, NULL, 0}};
    Args args;
    memset(&args, 0, sizeof(args));
    args.number_shorthand = 'w';
    int parsed = args_parse(argc, argv, specs, "fold", &args);
    if (parsed != ARGS_OK) return parsed == ARGS_DONE ? 0 : 1;
    FoldState fold = {80, args_has(&args, 's'), args_has(&args, 'b')};
    if (args_has(&args, 'w')) {
        fold.width = atoi(args_value(&args, 'w'));
        if (fold.width < 1) {
            cmd_error("fold", "invalid number of columns: '%s'", args_value(&args, 'w'));
            args_free(&args);
            return 1;
        }
    }
    int status = 0;
    each_operand_line("fold", &args, &status, fold_line, &fold);
    args_free(&args);
    return status;
}

static int cmd_column(int argc, char **argv) {
    static const OptSpec specs[] = {{'t', "table", 0}, {'s', "separator", 1}, {'c', "output-width", 1},
                                    {'o', "output-separator", 1}, {'x', "fillrows", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "column", &args, 1);
    if (early >= 0) return early;

    StrList lines;
    sl_init(&lines);
    int status = cmd_read_lines("column", &args, &lines);

    if (args_has(&args, 't')) {
        const char *separators = args_has(&args, 's') ? args_value(&args, 's') : " \t";
        const char *output_separator = args_has(&args, 'o') ? args_value(&args, 'o') : "  ";
        size_t widths[64] = {0};
        StrList *rows = xmalloc(lines.len * sizeof(StrList));
        for (size_t i = 0; i < lines.len; i++) {
            sl_init(&rows[i]);
            const char *p = lines.items[i];
            while (*p) {
                while (*p && strchr(separators, *p)) p++;
                if (!*p) break;
                const char *start = p;
                while (*p && !strchr(separators, *p)) p++;
                sl_push(&rows[i], xstrndup(start, (size_t)(p - start)));
            }
            for (size_t c = 0; c < rows[i].len && c < 64; c++) {
                size_t w = strlen(rows[i].items[c]);
                if (w > widths[c]) widths[c] = w;
            }
        }
        for (size_t i = 0; i < lines.len; i++) {
            if (rows[i].len == 0) continue;
            for (size_t c = 0; c < rows[i].len; c++) {
                if (c + 1 == rows[i].len) printf("%s", rows[i].items[c]);
                else printf("%-*s%s", (int)widths[c < 64 ? c : 63], rows[i].items[c], output_separator);
            }
            putchar('\n');
            sl_free(&rows[i]);
        }
        free(rows);
    } else {
        int total_width = args_has(&args, 'c') ? atoi(args_value(&args, 'c')) : 80;
        size_t longest = 0;
        for (size_t i = 0; i < lines.len; i++) {
            size_t length = strlen(lines.items[i]);
            if (length > longest) longest = length;
        }
        int column_width = (int)longest + 2;
        int columns = column_width > 0 ? total_width / column_width : 1;
        if (columns < 1) columns = 1;
        size_t rows = (lines.len + (size_t)columns - 1) / (size_t)columns;
        for (size_t r = 0; r < rows; r++) {
            for (size_t c = 0; c < (size_t)columns; c++) {
                size_t index = args_has(&args, 'x') ? r * (size_t)columns + c : c * rows + r;
                if (index >= lines.len) continue;
                int last = (c + 1 == (size_t)columns) || (args_has(&args, 'x') ? index + 1 >= lines.len : (c + 1) * rows + r >= lines.len);
                if (last) printf("%s", lines.items[index]);
                else printf("%-*s", column_width, lines.items[index]);
            }
            putchar('\n');
        }
    }
    sl_free(&lines);
    args_free(&args);
    return status;
}

static void paste_delimiters(const char *spec, StrBuf *out) {
    for (const char *p = spec; *p; p++) {
        if (*p == '\\' && p[1]) {
            p++;
            if (*p == 'n') sb_putc(out, '\n');
            else if (*p == 't') sb_putc(out, '\t');
            else if (*p == '0') sb_putc(out, '\0');
            else if (*p == '\\') sb_putc(out, '\\');
            else sb_putc(out, *p);
            continue;
        }
        sb_putc(out, *p);
    }
}

static int cmd_paste(int argc, char **argv) {
    static const OptSpec specs[] = {{'d', "delimiters", 1}, {'s', "serial", 0}, {'z', "zero-terminated", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "paste", &args, 1);
    if (early >= 0) return early;

    StrBuf delims;
    sb_init(&delims);
    if (args_has(&args, 'd')) paste_delimiters(args_value(&args, 'd'), &delims);
    else sb_putc(&delims, '\t');
    if (delims.len == 0) sb_putc(&delims, '\0');

    int count = args.operand_count ? args.operand_count : 1;
    FILE **files = xmalloc((size_t)count * sizeof(FILE *));
    int status = 0;
    for (int i = 0; i < count; i++) {
        files[i] = cmd_open("paste", args.operand_count ? args.operands[i] : NULL);
        if (!files[i]) status = 1;
    }

    StrBuf line;
    sb_init(&line);
    if (args_has(&args, 's')) {
        for (int i = 0; i < count; i++) {
            if (!files[i]) continue;
            int first = 1;
            size_t d = 0;
            while (cmd_read_line(files[i], &line) != 0) {
                if (!first) {
                    char delim = delims.data[d % delims.len];
                    if (delim) putchar(delim);
                    d++;
                }
                fputs(line.data, stdout);
                first = 0;
            }
            putchar('\n');
        }
    } else {
        for (;;) {
            int active = 0;
            StrBuf row;
            sb_init(&row);
            size_t d = 0;
            for (int i = 0; i < count; i++) {
                if (i > 0) {
                    char delim = delims.data[d % delims.len];
                    if (delim) sb_putc(&row, delim);
                    d++;
                }
                if (files[i] && cmd_read_line(files[i], &line) != 0) {
                    active = 1;
                    sb_puts(&row, line.data);
                }
            }
            if (!active) {
                sb_free(&row);
                break;
            }
            puts(row.data);
            sb_free(&row);
        }
    }
    sb_free(&line);
    for (int i = 0; i < count; i++) cmd_close(files[i]);
    free(files);
    sb_free(&delims);
    args_free(&args);
    return status;
}

static int cmd_comm(int argc, char **argv) {
    static const OptSpec specs[] = {{'1', NULL, 0}, {'2', NULL, 0}, {'3', NULL, 0},
                                    {'i', NULL, 0}, {'z', "zero-terminated", 0},
                                    {'c', "check-order", 0}, {'n', "nocheck-order", 0},
                                    {'o', "output-delimiter", 1}, {'t', "total", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "comm", &args, 1);
    if (early >= 0) return early;
    if (args.operand_count != 2) {
        if (args.operand_count == 0) cmd_error("comm", "missing operand");
        else if (args.operand_count == 1) cmd_error("comm", "missing operand after '%s'", args.operands[0]);
        else cmd_error("comm", "extra operand '%s'", args.operands[2]);
        fprintf(stderr, "Try 'comm --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    int hide1 = args_has(&args, '1'), hide2 = args_has(&args, '2'), hide3 = args_has(&args, '3');
    const char *delimiter = args_has(&args, 'o') ? args_value(&args, 'o') : "\t";
    int icase = args_has(&args, 'i');

    StrList left, right;
    sl_init(&left);
    sl_init(&right);
    for (int side = 0; side < 2; side++) {
        FILE *f = cmd_open("comm", args.operands[side]);
        if (!f) {
            sl_free(&left);
            sl_free(&right);
            args_free(&args);
            return 1;
        }
        StrBuf line;
        sb_init(&line);
        while (cmd_read_line(f, &line) != 0) sl_push_copy(side == 0 ? &left : &right, line.data);
        sb_free(&line);
        cmd_close(f);
    }

    long totals[3] = {0, 0, 0};
    size_t i = 0, j = 0;
    while (i < left.len || j < right.len) {
        int compared = i >= left.len ? 1 : j >= right.len ? -1
                       : icase ? _stricmp(left.items[i], right.items[j]) : strcmp(left.items[i], right.items[j]);
        if (compared < 0) {
            totals[0]++;
            if (!hide1) puts(left.items[i]);
            i++;
        } else if (compared > 0) {
            totals[1]++;
            if (!hide2) printf("%s%s\n", hide1 ? "" : delimiter, right.items[j]);
            j++;
        } else {
            totals[2]++;
            if (!hide3) printf("%s%s%s\n", hide1 ? "" : delimiter, hide2 ? "" : delimiter, left.items[i]);
            i++;
            j++;
        }
    }
    if (args_has(&args, 't')) printf("%ld%s%ld%s%ld%stotal\n", totals[0], delimiter, totals[1], delimiter, totals[2], delimiter);
    sl_free(&left);
    sl_free(&right);
    args_free(&args);
    return 0;
}

static int diff_equal(const char *a, const char *b, int icase, int ignore_space, int ignore_blank) {
    if (ignore_space || ignore_blank) {
        for (;;) {
            if (ignore_space) {
                while (*a == ' ' || *a == '\t') a++;
                while (*b == ' ' || *b == '\t') b++;
            } else {
                while ((*a == ' ' || *a == '\t') && (*b == ' ' || *b == '\t')) {
                    while (*a == ' ' || *a == '\t') a++;
                    while (*b == ' ' || *b == '\t') b++;
                }
            }
            int ca = icase ? tolower((unsigned char)*a) : (unsigned char)*a;
            int cb = icase ? tolower((unsigned char)*b) : (unsigned char)*b;
            if (ca != cb) return 0;
            if (!*a) return 1;
            a++;
            b++;
        }
    }
    return icase ? _stricmp(a, b) == 0 : strcmp(a, b) == 0;
}

typedef struct {
    int kind;
    size_t a_start, a_end, b_start, b_end;
} Hunk;

static void diff_hunks(const StrList *a, const StrList *b, int icase, int ws, int blank, Hunk **out, size_t *count) {
    size_t n = a->len, m = b->len;
    size_t *lcs = xmalloc((n + 1) * (m + 1) * sizeof(size_t));
    for (size_t i = 0; i <= n; i++) lcs[i * (m + 1) + m] = 0;
    for (size_t j = 0; j <= m; j++) lcs[n * (m + 1) + j] = 0;
    for (size_t i = n; i-- > 0;) {
        for (size_t j = m; j-- > 0;) {
            if (diff_equal(a->items[i], b->items[j], icase, ws, blank)) lcs[i * (m + 1) + j] = lcs[(i + 1) * (m + 1) + j + 1] + 1;
            else {
                size_t down = lcs[(i + 1) * (m + 1) + j], right = lcs[i * (m + 1) + j + 1];
                lcs[i * (m + 1) + j] = down > right ? down : right;
            }
        }
    }
    Hunk *hunks = NULL;
    size_t hunk_count = 0, cap = 0;
    size_t i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && diff_equal(a->items[i], b->items[j], icase, ws, blank)) {
            i++;
            j++;
            continue;
        }
        size_t ai = i, bj = j;
        while (i < n || j < m) {
            if (i < n && j < m && diff_equal(a->items[i], b->items[j], icase, ws, blank)) break;
            if (i < n && (j >= m || lcs[(i + 1) * (m + 1) + j] >= lcs[i * (m + 1) + j + 1])) i++;
            else j++;
        }
        if (hunk_count == cap) {
            cap = cap ? cap * 2 : 8;
            hunks = xrealloc(hunks, cap * sizeof(Hunk));
        }
        hunks[hunk_count].a_start = ai;
        hunks[hunk_count].a_end = i;
        hunks[hunk_count].b_start = bj;
        hunks[hunk_count].b_end = j;
        hunks[hunk_count].kind = 0;
        hunk_count++;
    }
    free(lcs);
    *out = hunks;
    *count = hunk_count;
}

static void diff_line(const char *prefix, const char *line) {
    size_t length = strlen(line);
    int no_newline = length > 0 && line[length - 1] == '\x01';
    printf("%s%.*s\n", prefix, (int)(no_newline ? length - 1 : length), line);
    if (no_newline) printf("\\ No newline at end of file\n");
}

static void diff_range(size_t start, size_t end) {
    if (end - start <= 1) printf("%zu", end > start ? start + 1 : start);
    else printf("%zu,%zu", start + 1, end);
}

static void file_stamp(const char *path, char *out, size_t size) {
    struct stat info;
    if (stat(path, &info) != 0) {
        snprintf(out, size, "1970-01-01 00:00:00.000000000 +0000");
        return;
    }
    struct tm parts;
    time_t when = info.st_mtime;
#ifdef _WIN32
    struct tm *local = localtime(&when);
    parts = *local;
    long offset = 0;
    {
        struct tm *utc = gmtime(&when);
        struct tm copy = *utc;
        time_t as_local = mktime(&copy);
        offset = (long)difftime(when, as_local);
    }
#else
    localtime_r(&when, &parts);
    long offset = parts.tm_gmtoff;
#endif
    char base[64];
    strftime(base, sizeof(base), "%Y-%m-%d %H:%M:%S", &parts);
    snprintf(out, size, "%s.000000000 %c%02ld%02ld", base, offset < 0 ? '-' : '+', labs(offset) / 3600, (labs(offset) % 3600) / 60);
}

static int cmd_diff(int argc, char **argv) {
    static const OptSpec specs[] = {{'q', "brief", 0},          {'s', "report-identical-files", 0},
                                    {'i', "ignore-case", 0},    {'w', "ignore-all-space", 0},
                                    {'b', "ignore-space-change", 0}, {'B', "ignore-blank-lines", 0},
                                    {'u', NULL, 0},             {'U', "unified", 1},
                                    {'r', "recursive", 0},      {'N', "new-file", 0},
                                    {'c', NULL, 0},             {'C', "context", 1},
                                    {'a', "text", 0},           {'t', "expand-tabs", 0},
                                    {'1', "strip-trailing-cr", 0}, {'2', "color", 2},
                                    {'3', "label", 1},          {'4', "no-dereference", 0},
                                    {'5', "normal", 0},         {'6', "minimal", 0},
                                    {'d', NULL, 0},             {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "diff", &args, 2);
    if (early >= 0) return early;
    if (args.operand_count != 2) {
        cmd_error("diff", args.operand_count < 2 ? "missing operand after '%s'" : "extra operand '%s'",
                  args.operand_count == 1 ? args.operands[0] : args.operand_count > 2 ? args.operands[2] : "diff");
        args_free(&args);
        return 2;
    }
    const char *left_name = args.operands[0];
    const char *right_name = args.operands[1];
    char joined[PATH_BUF];
    if (path_is_dir(left_name) && !path_is_dir(right_name)) {
        snprintf(joined, sizeof(joined), "%s/%s", left_name, cmd_name(right_name));
        left_name = joined;
    } else if (path_is_dir(right_name) && !path_is_dir(left_name)) {
        snprintf(joined, sizeof(joined), "%s/%s", right_name, cmd_name(left_name));
        right_name = joined;
    }
    int icase = args_has(&args, 'i');
    int ws = args_has(&args, 'w') || args_has(&args, 'b');
    int blank = args_has(&args, 'B');
    int unified = args_has(&args, 'u') || args_has(&args, 'U');
    int context = args_has(&args, 'U') ? atoi(args_value(&args, 'U')) : 3;

    StrList left, right;
    sl_init(&left);
    sl_init(&right);
    const char *names[2] = {left_name, right_name};
    for (int side = 0; side < 2; side++) {
        FILE *f = cmd_open("diff", names[side]);
        if (!f) {
            sl_free(&left);
            sl_free(&right);
            args_free(&args);
            return 2;
        }
        StrBuf line;
        sb_init(&line);
        int kind;
        while ((kind = cmd_read_line(f, &line)) != 0) {
            if (kind == 2) {
                sb_putc(&line, '\x01');
                sl_push_copy(side == 0 ? &left : &right, line.data);
                break;
            }
            sl_push_copy(side == 0 ? &left : &right, line.data);
        }
        sb_free(&line);
        cmd_close(f);
    }

    Hunk *hunks;
    size_t hunk_count;
    diff_hunks(&left, &right, icase, ws, blank, &hunks, &hunk_count);
    if (blank) {
        size_t kept = 0;
        for (size_t h = 0; h < hunk_count; h++) {
            int all_blank = 1;
            for (size_t i = hunks[h].a_start; i < hunks[h].a_end; i++)
                if (left.items[i][0]) all_blank = 0;
            for (size_t j = hunks[h].b_start; j < hunks[h].b_end; j++)
                if (right.items[j][0]) all_blank = 0;
            if (!all_blank) hunks[kept++] = hunks[h];
        }
        hunk_count = kept;
    }
    int differ = hunk_count > 0;

    if (args_has(&args, 'q')) {
        if (differ) printf("Files %s and %s differ\n", left_name, right_name);
    } else if (!differ) {
        if (args_has(&args, 's')) printf("Files %s and %s are identical\n", left_name, right_name);
    } else if (unified) {
        char stamp_left[80], stamp_right[80];
        file_stamp(left_name, stamp_left, sizeof(stamp_left));
        file_stamp(right_name, stamp_right, sizeof(stamp_right));
        printf("--- %s\t%s\n+++ %s\t%s\n", left_name, stamp_left, right_name, stamp_right);
        size_t h = 0;
        while (h < hunk_count) {
            size_t group_end = h;
            while (group_end + 1 < hunk_count && hunks[group_end + 1].a_start <= hunks[group_end].a_end + (size_t)(2 * context)) group_end++;
            size_t a_from = hunks[h].a_start > (size_t)context ? hunks[h].a_start - (size_t)context : 0;
            size_t a_to = hunks[group_end].a_end + (size_t)context;
            if (a_to > left.len) a_to = left.len;
            size_t b_from = hunks[h].b_start > (size_t)context ? hunks[h].b_start - (size_t)context : 0;
            size_t b_to = hunks[group_end].b_end + (size_t)context;
            if (b_to > right.len) b_to = right.len;
            printf("@@ -%zu", a_to - a_from == 0 ? a_from : a_from + 1);
            if (a_to - a_from != 1) printf(",%zu", a_to - a_from);
            printf(" +%zu", b_to - b_from == 0 ? b_from : b_from + 1);
            if (b_to - b_from != 1) printf(",%zu", b_to - b_from);
            printf(" @@\n");
            size_t ai = a_from, bj = b_from;
            for (size_t k = h; k <= group_end; k++) {
                while (ai < hunks[k].a_start) {
                    printf(" %s\n", left.items[ai++]);
                    bj++;
                }
                for (; ai < hunks[k].a_end; ai++) diff_line("-", left.items[ai]);
                for (; bj < hunks[k].b_end; bj++) diff_line("+", right.items[bj]);
            }
            while (ai < a_to) {
                printf(" %s\n", left.items[ai++]);
                bj++;
            }
            h = group_end + 1;
        }
    } else {
        for (size_t h = 0; h < hunk_count; h++) {
            Hunk *k = &hunks[h];
            int removed = k->a_end > k->a_start, added = k->b_end > k->b_start;
            if (removed && added) {
                diff_range(k->a_start, k->a_end);
                printf("c");
                diff_range(k->b_start, k->b_end);
            } else if (removed) {
                diff_range(k->a_start, k->a_end);
                printf("d%zu", k->b_start);
            } else {
                printf("%zua", k->a_start);
                diff_range(k->b_start, k->b_end);
            }
            printf("\n");
            for (size_t i = k->a_start; i < k->a_end; i++) diff_line("< ", left.items[i]);
            if (removed && added) printf("---\n");
            for (size_t j = k->b_start; j < k->b_end; j++) diff_line("> ", right.items[j]);
        }
    }
    free(hunks);
    sl_free(&left);
    sl_free(&right);
    args_free(&args);
    return differ ? 1 : 0;
}

static int cmd_cmp(int argc, char **argv) {
    static const OptSpec specs[] = {{'b', "print-bytes", 0}, {'l', "verbose", 0}, {'s', "quiet", 0},
                                    {'s', "silent", 0}, {'n', "bytes", 1}, {'i', "ignore-initial", 1}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "cmp", &args, 2);
    if (early >= 0) return early;
    if (args.operand_count < 1 || args.operand_count > 4) {
        cmd_error("cmp", "missing operand");
        args_free(&args);
        return 2;
    }
    const char *name_a = args.operands[0];
    const char *name_b = args.operand_count > 1 ? args.operands[1] : "-";
    FILE *a = cmd_open("cmp", name_a);
    FILE *b = a ? cmd_open("cmp", name_b) : NULL;
    if (!a || !b) {
        cmd_close(a);
        cmd_close(b);
        args_free(&args);
        return 2;
    }
    long long limit = args_has(&args, 'n') ? atoll(args_value(&args, 'n')) : -1;
    int quiet = args_has(&args, 's');
    int list = args_has(&args, 'l');
    int show_bytes = args_has(&args, 'b');
    long long position = 1, line = 1;
    int status = 0;
    while (limit < 0 || position <= limit) {
        int ca = fgetc(a), cb = fgetc(b);
        if (ca == EOF && cb == EOF) break;
        if (ca == EOF || cb == EOF) {
            if (!quiet) {
                if (position == 1) fprintf(stderr, "cmp: EOF on %s which is empty\n", ca == EOF ? name_a : name_b);
                else fprintf(stderr, "cmp: EOF on %s after byte %lld, line %lld\n", ca == EOF ? name_a : name_b, position - 1, line - 1 + (line > 1 ? 1 : 0) - 1 + 1);
            }
            status = 1;
            break;
        }
        if (ca != cb) {
            status = 1;
            if (list) {
                if (show_bytes) printf("%lld %3o %c %3o %c\n", position, ca, ca, cb, cb);
                else printf("%lld %3o %3o\n", position, ca, cb);
            } else {
                if (!quiet) {
                    if (show_bytes) printf("%s %s differ: char %lld, line %lld is %3o %c %3o %c\n", name_a, name_b, position, line, ca, ca, cb, cb);
                    else printf("%s %s differ: char %lld, line %lld\n", name_a, name_b, position, line);
                }
                break;
            }
        }
        if (ca == '\n') line++;
        position++;
    }
    cmd_close(a);
    cmd_close(b);
    args_free(&args);
    return status;
}

static int cmd_shuf(int argc, char **argv) {
    static const OptSpec specs[] = {{'e', "echo", 0}, {'i', "input-range", 1}, {'n', "head-count", 1},
                                    {'o', "output", 1}, {'r', "repeat", 0}, {'z', "zero-terminated", 0},
                                    {'s', "random-source", 1}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "shuf", &args, 1);
    if (early >= 0) return early;
    StrList lines;
    sl_init(&lines);
    int status = 0;
    if (args_has(&args, 'e')) {
        for (int i = 0; i < args.operand_count; i++) sl_push_copy(&lines, args.operands[i]);
    } else if (args_has(&args, 'i')) {
        long lo = 0, hi = -1;
        if (sscanf(args_value(&args, 'i'), "%ld-%ld", &lo, &hi) != 2 || hi < lo - 1) {
            cmd_error("shuf", "invalid input range: '%s'", args_value(&args, 'i'));
            sl_free(&lines);
            args_free(&args);
            return 1;
        }
        for (long v = lo; v <= hi; v++) {
            char text[32];
            snprintf(text, sizeof(text), "%ld", v);
            sl_push_copy(&lines, text);
        }
    } else {
        if (args.operand_count > 1) {
            cmd_error("shuf", "extra operand '%s'", args.operands[1]);
            sl_free(&lines);
            args_free(&args);
            return 1;
        }
        status = cmd_read_lines("shuf", &args, &lines);
    }
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned)GetTickCount() ^ (unsigned)GetCurrentProcessId());
        seeded = 1;
    }
    long wanted = args_has(&args, 'n') ? atol(args_value(&args, 'n')) : (long)lines.len;
    FILE *out = stdout;
    if (args_has(&args, 'o')) {
        out = fopen(args_value(&args, 'o'), "wb");
        if (!out) {
            cmd_file_error("shuf", args_value(&args, 'o'));
            out = stdout;
            status = 1;
        }
    }
    if (args_has(&args, 'r')) {
        if (lines.len > 0)
            for (long i = 0; wanted < 0 || i < wanted; i++) fprintf(out, "%s\n", lines.items[(size_t)rand() % lines.len]);
    } else {
        for (size_t i = lines.len; i > 1; i--) {
            size_t j = (size_t)rand() % i;
            char *swap = lines.items[i - 1];
            lines.items[i - 1] = lines.items[j];
            lines.items[j] = swap;
        }
        for (size_t i = 0; i < lines.len && (long)i < wanted; i++) fprintf(out, "%s\n", lines.items[i]);
    }
    if (out != stdout) fclose(out);
    sl_free(&lines);
    args_free(&args);
    return status;
}

static int cmd_tee(int argc, char **argv) {
    static const OptSpec specs[] = {{'a', "append", 0}, {'i', "ignore-interrupts", 0}, {'p', NULL, 0},
                                    {'o', "output-error", 2}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "tee", &args, 1);
    if (early >= 0) return early;
    int append = args_has(&args, 'a');
    int status = 0;
    FILE **files = xmalloc(((size_t)args.operand_count + 1) * sizeof(FILE *));
    for (int i = 0; i < args.operand_count; i++) {
        files[i] = fopen(args.operands[i], append ? "ab" : "wb");
        if (!files[i]) {
            cmd_file_error("tee", args.operands[i]);
            status = 1;
        }
    }
    char buffer[65536];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
        fwrite(buffer, 1, n, stdout);
        for (int i = 0; i < args.operand_count; i++)
            if (files[i]) fwrite(buffer, 1, n, files[i]);
    }
    for (int i = 0; i < args.operand_count; i++)
        if (files[i]) fclose(files[i]);
    free(files);
    args_free(&args);
    return status;
}

static char escape_char(char name) {
    switch (name) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'a': return 7;
    case 'b': return 8;
    case 'f': return 12;
    case 'v': return 11;
    case 'e': return 27;
    default: return name;
    }
}

static int numeric_escape(const char **p, StrBuf *out, int leading_zero) {
    const char *c = *p;
    if (*c >= '0' && *c <= '7') {
        int value = 0;
        int digits = 0;
        if (leading_zero && *c == '0') c++;
        while (digits < 3 && *c >= '0' && *c <= '7') {
            value = value * 8 + (*c - '0');
            c++;
            digits++;
        }
        if (digits == 0 && !(leading_zero && c[-1] == '0')) return 0;
        sb_putc(out, (char)value);
        *p = c;
        return 1;
    }
    if (*c == 'x' && isxdigit((unsigned char)c[1])) {
        int value = 0;
        int digits = 0;
        c++;
        while (digits < 2 && isxdigit((unsigned char)*c)) {
            value = value * 16 + (isdigit((unsigned char)*c) ? *c - '0' : (tolower((unsigned char)*c) - 'a') + 10);
            c++;
            digits++;
        }
        sb_putc(out, (char)value);
        *p = c;
        return 1;
    }
    return 0;
}

static int printf_escapes(const char *s, StrBuf *out) {
    for (const char *p = s; *p; p++) {
        if (*p != '\\' || !p[1]) {
            sb_putc(out, *p);
            continue;
        }
        p++;
        if (*p == '\\') sb_putc(out, '\\');
        else if (*p == 'c') return 1;
        else if (strchr("ntrabfve", *p)) sb_putc(out, escape_char(*p));
        else if (numeric_escape(&p, out, 1)) p--;
        else {
            sb_putc(out, '\\');
            sb_putc(out, *p);
        }
    }
    return 0;
}

static long long printf_integer(const char *value, int *bad) {
    if ((*value == '\'' || *value == '"') && value[1]) return (unsigned char)value[1];
    char *end;
    errno = 0;
    long long number = strtoll(value, &end, 0);
    while (*end == ' ') end++;
    if (end == value || *end) *bad = 1;
    return number;
}

static int cmd_printf(int argc, char **argv) {
    const char *destination = NULL;
    int start = 1;
    if (argc > 2 && strcmp(argv[1], "-v") == 0) {
        destination = argv[2];
        start = 3;
    }
    if (start >= argc) {
        fprintf(stderr, "printf: usage: printf [-v var] format [arguments]\n");
        return 2;
    }
    if (strcmp(argv[start], "--") == 0) start++;
    if (start >= argc) {
        fprintf(stderr, "printf: usage: printf [-v var] format [arguments]\n");
        return 2;
    }

    const char *format = argv[start];
    int next = start + 1;
    int has_conversion = 0;
    int status = 0;
    int stop = 0;

    StrBuf rendered;
    sb_init(&rendered);

    do {
        for (const char *p = format; *p && !stop; p++) {
            if (*p == '\\' && p[1]) {
                p++;
                if (*p == '\\') sb_putc(&rendered, '\\');
                else if (numeric_escape(&p, &rendered, 0)) p--;
                else if (strchr("ntrabfve", *p)) sb_putc(&rendered, escape_char(*p));
                else {
                    sb_putc(&rendered, '\\');
                    sb_putc(&rendered, *p);
                }
                continue;
            }
            if (*p != '%') {
                sb_putc(&rendered, *p);
                continue;
            }
            if (p[1] == '%') {
                sb_putc(&rendered, '%');
                p++;
                continue;
            }

            char spec[64];
            size_t s = 0;
            spec[s++] = '%';
            p++;
            while (*p && strchr("-+ #0", *p) && s < sizeof(spec) - 4) spec[s++] = *p++;
            if (*p == '*') {
                p++;
                const char *width = next < argc ? argv[next++] : "0";
                s += (size_t)snprintf(spec + s, sizeof(spec) - s - 4, "%ld", strtol(width, NULL, 10));
            } else {
                while (isdigit((unsigned char)*p) && s < sizeof(spec) - 4) spec[s++] = *p++;
            }
            if (*p == '.') {
                spec[s++] = *p++;
                if (*p == '*') {
                    p++;
                    const char *precision = next < argc ? argv[next++] : "0";
                    s += (size_t)snprintf(spec + s, sizeof(spec) - s - 4, "%ld", strtol(precision, NULL, 10));
                } else {
                    while (isdigit((unsigned char)*p) && s < sizeof(spec) - 4) spec[s++] = *p++;
                }
            }
            while (*p == 'l' || *p == 'h' || *p == 'L' || *p == 'j' || *p == 'z' || *p == 't') p++;
            char conv = *p;
            if (!conv) {
                fprintf(stderr, "printf: missing format character\n");
                status = 1;
                break;
            }
            has_conversion = 1;
            const char *value = next < argc ? argv[next++] : "";
            int bad = 0;

            switch (conv) {
            case 'd':
            case 'i': {
                long long number = printf_integer(value, &bad);
                spec[s++] = 'l';
                spec[s++] = 'l';
                spec[s++] = 'd';
                spec[s] = '\0';
                sb_printf(&rendered, spec, number);
                break;
            }
            case 'o':
            case 'u':
            case 'x':
            case 'X': {
                long long number = printf_integer(value, &bad);
                spec[s++] = 'l';
                spec[s++] = 'l';
                spec[s++] = conv;
                spec[s] = '\0';
                sb_printf(&rendered, spec, (unsigned long long)number);
                break;
            }
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
            case 'a':
            case 'A': {
                char *end;
                double number = strtod(value, &end);
                if (end == value || *end) {
                    if (*value) bad = 1;
                    if (end == value) number = 0;
                }
                spec[s++] = conv;
                spec[s] = '\0';
                sb_printf(&rendered, spec, number);
                break;
            }
            case 'c':
                spec[s++] = 'c';
                spec[s] = '\0';
                if (*value) sb_printf(&rendered, spec, value[0]);
                break;
            case 'b': {
                StrBuf expanded;
                sb_init(&expanded);
                int stopped = printf_escapes(value, &expanded);
                spec[s++] = 's';
                spec[s] = '\0';
                sb_printf(&rendered, spec, expanded.data);
                sb_free(&expanded);
                if (stopped) stop = 1;
                break;
            }
            case 'q': {
                spec[s++] = 's';
                spec[s] = '\0';
                StrBuf quoted;
                sb_init(&quoted);
                if (!*value) sb_puts(&quoted, "''");
                else {
                    for (const char *q = value; *q; q++) {
                        if (strchr(" \t'\"\\$`!*?[]{}()<>|&;~#=", *q)) sb_putc(&quoted, '\\');
                        if (*q == '\n') sb_puts(&quoted, "$'\\n'");
                        else sb_putc(&quoted, *q);
                    }
                }
                sb_printf(&rendered, spec, quoted.data);
                sb_free(&quoted);
                break;
            }
            case 's':
                spec[s++] = 's';
                spec[s] = '\0';
                sb_printf(&rendered, spec, value);
                break;
            default:
                fprintf(stderr, "printf: %%%c: invalid conversion specification\n", conv);
                status = 1;
                stop = 1;
                break;
            }
            if (bad) {
                fprintf(stderr, "printf: %s: invalid number\n", value);
                status = 1;
            }
        }
    } while (next < argc && has_conversion && !stop);

    if (destination) var_set(destination, rendered.data);
    else fwrite(rendered.data, 1, rendered.len, stdout);
    sb_free(&rendered);
    return status;
}

static const char BASE64_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int cmd_base64(int argc, char **argv) {
    static const OptSpec specs[] = {{'d', "decode", 0}, {'i', "ignore-garbage", 0}, {'w', "wrap", 1}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "base64", &args, 1);
    if (early >= 0) return early;
    if (args.operand_count > 1) {
        cmd_error("base64", "extra operand '%s'", args.operands[1]);
        args_free(&args);
        return 1;
    }
    StrBuf data;
    sb_init(&data);
    if (cmd_read_all("base64", &args, &data)) {
        sb_free(&data);
        args_free(&args);
        return 1;
    }
    int status = 0;
    if (args_has(&args, 'd')) {
        int ignore = args_has(&args, 'i');
        unsigned int accumulator = 0;
        int bits = 0;
        int pad = 0;
        for (size_t i = 0; i < data.len; i++) {
            unsigned char c = (unsigned char)data.data[i];
            if (c == '\n' || c == '\r') continue;
            if (c == '=') {
                pad++;
                continue;
            }
            const char *hit = strchr(BASE64_ALPHABET, c);
            if (!hit || !c) {
                if (ignore) continue;
                cmd_error("base64", "invalid input");
                status = 1;
                break;
            }
            accumulator = (accumulator << 6) | (unsigned)(hit - BASE64_ALPHABET);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                putchar((int)((accumulator >> bits) & 0xFF));
            }
        }
    } else {
        long wrap = args_has(&args, 'w') ? atol(args_value(&args, 'w')) : 76;
        long column = 0;
        for (size_t i = 0; i < data.len; i += 3) {
            unsigned int chunk = (unsigned char)data.data[i] << 16;
            size_t have = data.len - i;
            if (have > 1) chunk |= (unsigned char)data.data[i + 1] << 8;
            if (have > 2) chunk |= (unsigned char)data.data[i + 2];
            char quad[4];
            quad[0] = BASE64_ALPHABET[(chunk >> 18) & 63];
            quad[1] = BASE64_ALPHABET[(chunk >> 12) & 63];
            quad[2] = have > 1 ? BASE64_ALPHABET[(chunk >> 6) & 63] : '=';
            quad[3] = have > 2 ? BASE64_ALPHABET[chunk & 63] : '=';
            for (int q = 0; q < 4; q++) {
                if (wrap > 0 && column == wrap) {
                    putchar('\n');
                    column = 0;
                }
                putchar(quad[q]);
                column++;
            }
        }
        if (wrap > 0 && column > 0) putchar('\n');
        else if (wrap == 0) {
        }
    }
    sb_free(&data);
    args_free(&args);
    return status;
}

typedef struct {
    int tab;
    int initial_only;
    int unexpand;
    int all;
} TabState;

static int expand_line(const char *line, int newline, void *ctx) {
    TabState *tabs = ctx;
    int column = 0;
    int leading = 1;
    if (tabs->unexpand) {
        StrBuf out;
        sb_init(&out);
        int spaces = 0;
        for (const char *p = line; *p; p++) {
            if (*p == ' ' && (leading || tabs->all)) {
                spaces++;
                column++;
                if (column % tabs->tab == 0) {
                    sb_putc(&out, '\t');
                    spaces = 0;
                }
                continue;
            }
            for (int s = 0; s < spaces; s++) sb_putc(&out, ' ');
            spaces = 0;
            if (*p == '\t') {
                column = (column / tabs->tab + 1) * tabs->tab;
                sb_putc(&out, '\t');
                continue;
            }
            leading = 0;
            column++;
            sb_putc(&out, *p);
        }
        for (int s = 0; s < spaces; s++) sb_putc(&out, ' ');
        put_line(out.data, newline);
        sb_free(&out);
        return 1;
    }
    for (const char *p = line; *p; p++) {
        if (*p == '\t' && (leading || !tabs->initial_only)) {
            int next = (column / tabs->tab + 1) * tabs->tab;
            while (column < next) {
                putchar(' ');
                column++;
            }
            continue;
        }
        if (*p != ' ') leading = 0;
        putchar(*p);
        column++;
    }
    if (newline) putchar('\n');
    return 1;
}

static int expand_common(int argc, char **argv, int unexpand) {
    const char *tool = unexpand ? "unexpand" : "expand";
    static const OptSpec specs[] = {{'t', "tabs", 1}, {'i', "initial", 0}, {'a', "all", 0},
                                    {'1', "first-only", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, tool, &args, 1);
    if (early >= 0) return early;
    TabState tabs = {8, args_has(&args, 'i'), unexpand, args_has(&args, 'a') || (unexpand && args_has(&args, 't'))};
    if (args_has(&args, 't')) tabs.tab = atoi(args_value(&args, 't'));
    if (tabs.tab < 1) tabs.tab = 8;
    int status = 0;
    each_operand_line(tool, &args, &status, expand_line, &tabs);
    args_free(&args);
    return status;
}

static int cmd_expand(int argc, char **argv) {
    return expand_common(argc, argv, 0);
}

static int cmd_unexpand(int argc, char **argv) {
    return expand_common(argc, argv, 1);
}

const Command TEXT_COMMANDS[] = {
    {"awk", awk_main, "[-F fs] [-v var=value] 'program' [file...]"},
    {"base64", cmd_base64, "[-d] [-w COLS] [FILE]"},
    {"cat", cmd_cat, "[-AbeEnstTv] [FILE]..."},
    {"cmp", cmd_cmp, "[-bls] [-n LIMIT] FILE1 [FILE2]"},
    {"column", cmd_column, "[-t] [-s SEP] [-c WIDTH] [FILE]..."},
    {"comm", cmd_comm, "[-123i] FILE1 FILE2"},
    {"cut", cmd_cut, "-b LIST | -c LIST | -f LIST [-d DELIM] [-s] [--complement] [FILE]..."},
    {"diff", cmd_diff, "[-qsiwbBu] FILE1 FILE2"},
    {"expand", cmd_expand, "[-t N] [-i] [FILE]..."},
    {"fold", cmd_fold, "[-bs] [-w WIDTH] [FILE]..."},
    {"grep", cmd_grep, "[OPTION]... PATTERNS [FILE]..."},
    {"head", cmd_head, "[-n [-]NUM] [-c [-]NUM] [-qv] [FILE]..."},
    {"nl", cmd_nl, "[-b STYLE] [-n FORMAT] [-s SEP] [-w N] [-v N] [-i N] [FILE]..."},
    {"paste", cmd_paste, "[-d LIST] [-s] [FILE]..."},
    {"printf", cmd_printf, "[-v var] FORMAT [ARGUMENT]..."},
    {"rev", cmd_rev, "[FILE]..."},
    {"sed", cmd_sed, "[-nEi] [-e script] [-f file] [script] [FILE]..."},
    {"shuf", cmd_shuf, "[-n COUNT] [-e ARG... | -i LO-HI | FILE]"},
    {"sort", cmd_sort, "[-bcdfghinrsuV] [-k KEYDEF] [-t SEP] [-o FILE] [FILE]..."},
    {"tac", cmd_tac, "[FILE]..."},
    {"tail", cmd_tail, "[-n [+]NUM] [-c [+]NUM] [-qv] [FILE]..."},
    {"tee", cmd_tee, "[-a] [FILE]..."},
    {"tr", cmd_tr, "[-cdst] SET1 [SET2]"},
    {"unexpand", cmd_unexpand, "[-t N] [-a] [FILE]..."},
    {"uniq", cmd_uniq, "[-cdDiu] [-f N] [-s N] [-w N] [INPUT [OUTPUT]]"},
    {"wc", cmd_wc, "[-clmwL] [FILE]..."},
    {"yes", cmd_yes, "[STRING]..."},
};

const size_t TEXT_COMMAND_COUNT = END(TEXT_COMMANDS);
