/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "commands.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "platform.h"

#ifdef _WIN32
#include <tlhelp32.h>
#include <wincrypt.h>
#else
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/utsname.h>
#endif

#include "exec.h"
#include "expand.h"
#include "regex.h"
#include "shell.h"
#include "style.h"
#include "update.h"
#include "vars.h"

#define END(list) (sizeof(list) / sizeof(list[0]))

static int parse_or_exit(int argc, char **argv, const OptSpec *specs, const char *tool, Args *args,
                         int failure) {
    int parsed = args_parse(argc, argv, specs, tool, args);
    if (parsed == ARGS_OK) return -1;
    return parsed == ARGS_DONE ? 0 : failure;
}

static int cmd_env(int argc, char **argv) {
    static const OptSpec specs[] = {{'i', "ignore-environment", 0}, {'u', "unset", 1}, {'0', "null", 0},
                                    {'C', "chdir", 1}, {'S', "split-string", 1}, {'v', "debug", 0},
                                    {0, NULL, 0}};
    Args args;
    memset(&args, 0, sizeof(args));
    args.stop_at_operand = 1;
    int parsed = args_parse(argc, argv, specs, "env", &args);
    if (parsed != ARGS_OK) return parsed == ARGS_DONE ? 0 : 125;

    int first = 0;
    while (first < args.operand_count && strchr(args.operands[first], '=') &&
           isalpha((unsigned char)args.operands[first][0]))
        first++;

    if (first >= args.operand_count) {
        if (args_has(&args, 'i')) {
            args_free(&args);
            return 0;
        }
        StrList list;
        sl_init(&list);
        vars_list(&list);
        for (size_t i = 0; i < list.len; i++) {
            int hidden = 0;
            for (int u = 0; u < args_count(&args, 'u'); u++) {
                const char *name = args_value_at(&args, 'u', u);
                size_t length = strlen(name);
                if (strncmp(list.items[i], name, length) == 0 && list.items[i][length] == '=') hidden = 1;
            }
            for (int a = 0; a < first; a++) {
                const char *equals = strchr(args.operands[a], '=');
                if (strncmp(list.items[i], args.operands[a], (size_t)(equals - args.operands[a]) + 1) == 0) hidden = 1;
            }
            if (!hidden) printf("%s%c", list.items[i], args_has(&args, '0') ? '\0' : '\n');
        }
        for (int a = 0; a < first; a++) printf("%s%c", args.operands[a], args_has(&args, '0') ? '\0' : '\n');
        sl_free(&list);
        args_free(&args);
        return 0;
    }

    StrBuf command;
    sb_init(&command);
    if (args_has(&args, 'C')) {
        sb_puts(&command, "cd ");
        sb_put_quoted(&command, args_value(&args, 'C'));
        sb_puts(&command, " && ");
    }
    for (int u = 0; u < args_count(&args, 'u'); u++) {
        sb_puts(&command, "unset ");
        sb_puts(&command, args_value_at(&args, 'u', u));
        sb_puts(&command, "; ");
    }
    for (int a = 0; a < first; a++) {
        const char *equals = strchr(args.operands[a], '=');
        sb_putn(&command, args.operands[a], (size_t)(equals - args.operands[a]) + 1);
        sb_put_quoted(&command, equals + 1);
        sb_putc(&command, ' ');
    }
    for (int i = first; i < args.operand_count; i++) {
        sb_put_quoted(&command, args.operands[i]);
        sb_putc(&command, ' ');
    }
    int status = exec_subshell(command.data);
    sb_free(&command);
    args_free(&args);
    return status;
}

static int cmd_printenv(int argc, char **argv) {
    static const OptSpec specs[] = {{'0', "null", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "printenv", &args, 2);
    if (early >= 0) return early;
    char terminator = args_has(&args, '0') ? '\0' : '\n';
    int status = 0;
    if (args.operand_count == 0) {
        StrList list;
        sl_init(&list);
        vars_list(&list);
        for (size_t i = 0; i < list.len; i++) printf("%s%c", list.items[i], terminator);
        sl_free(&list);
    } else {
        for (int i = 0; i < args.operand_count; i++) {
            const char *value = var_get(args.operands[i]);
            if (!value) value = getenv(args.operands[i]);
            if (value) printf("%s%c", value, terminator);
            else status = 1;
        }
    }
    args_free(&args);
    return status;
}

static const char *WEEKDAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *MONTHS[] = {"January", "February", "March", "April", "May", "June", "July",
                               "August", "September", "October", "November", "December"};

static long utc_offset_seconds(time_t when, const struct tm *local) {
#ifdef _WIN32
    struct tm copy = *local;
    time_t as_utc = mktime(&copy);
    struct tm *utc = gmtime(&when);
    struct tm utc_copy = *utc;
    time_t utc_as_local = mktime(&utc_copy);
    (void)as_utc;
    return (long)difftime(when, utc_as_local);
#else
    (void)when;
    return local->tm_gmtoff;
#endif
}

static void format_date(const char *format, const struct tm *parts, time_t when, int utc, long nanoseconds,
                        StrBuf *out) {
    for (const char *p = format; *p; p++) {
        if (*p != '%') {
            sb_putc(out, *p);
            continue;
        }
        p++;
        int no_pad = 0, space_pad = 0, upper = 0, zero_pad = 0;
        while (*p == '-' || *p == '_' || *p == '^' || *p == '0' || *p == '#') {
            if (*p == '-') no_pad = 1;
            else if (*p == '_') space_pad = 1;
            else if (*p == '^') upper = 1;
            else if (*p == '0') zero_pad = 1;
            p++;
        }
        int width = 0;
        while (isdigit((unsigned char)*p)) width = width * 10 + (*p++ - '0');
        if (*p == 'E' || *p == 'O') p++;
        char piece[128] = "";
        char spec = *p;
        if (!spec) break;
        long number = -1;
        int default_width = 2;
        char pad = '0';
        switch (spec) {
        case '%': snprintf(piece, sizeof(piece), "%%"); break;
        case 'a': snprintf(piece, sizeof(piece), "%.3s", WEEKDAYS[parts->tm_wday]); break;
        case 'A': snprintf(piece, sizeof(piece), "%s", WEEKDAYS[parts->tm_wday]); break;
        case 'b': case 'h': snprintf(piece, sizeof(piece), "%.3s", MONTHS[parts->tm_mon]); break;
        case 'B': snprintf(piece, sizeof(piece), "%s", MONTHS[parts->tm_mon]); break;
        case 'c': {
            StrBuf inner;
            sb_init(&inner);
            format_date("%a %b %e %H:%M:%S %Y", parts, when, utc, nanoseconds, &inner);
            snprintf(piece, sizeof(piece), "%s", inner.data);
            sb_free(&inner);
            break;
        }
        case 'C': number = (parts->tm_year + 1900) / 100; break;
        case 'd': number = parts->tm_mday; break;
        case 'D': {
            StrBuf inner;
            sb_init(&inner);
            format_date("%m/%d/%y", parts, when, utc, nanoseconds, &inner);
            snprintf(piece, sizeof(piece), "%s", inner.data);
            sb_free(&inner);
            break;
        }
        case 'e': number = parts->tm_mday; pad = ' '; break;
        case 'F': {
            StrBuf inner;
            sb_init(&inner);
            format_date("%Y-%m-%d", parts, when, utc, nanoseconds, &inner);
            snprintf(piece, sizeof(piece), "%s", inner.data);
            sb_free(&inner);
            break;
        }
        case 'g': case 'G': case 'V': {
            int wday = (parts->tm_wday + 6) % 7;
            int week = (parts->tm_yday - wday + 10) / 7;
            int year = parts->tm_year + 1900;
            if (week < 1) {
                year--;
                week = 52;
                int dec31_wday = (wday - parts->tm_yday - 1 + 7 * 60) % 7;
                if (dec31_wday == 3 || (dec31_wday == 4 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))) week = 53;
            } else if (week == 53) {
                int days_in_year = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 366 : 365;
                if (days_in_year - parts->tm_yday - 1 < 3 - wday) {
                    week = 1;
                    year++;
                }
            }
            if (spec == 'V') number = week;
            else if (spec == 'G') { number = year; default_width = 4; }
            else number = year % 100;
            break;
        }
        case 'H': number = parts->tm_hour; break;
        case 'I': number = parts->tm_hour % 12 ? parts->tm_hour % 12 : 12; break;
        case 'j': number = parts->tm_yday + 1; default_width = 3; break;
        case 'k': number = parts->tm_hour; pad = ' '; break;
        case 'l': number = parts->tm_hour % 12 ? parts->tm_hour % 12 : 12; pad = ' '; break;
        case 'm': number = parts->tm_mon + 1; break;
        case 'M': number = parts->tm_min; break;
        case 'n': snprintf(piece, sizeof(piece), "\n"); break;
        case 'N':
            snprintf(piece, sizeof(piece), "%09ld", nanoseconds);
            if (width > 0 && width < 9) piece[width] = '\0';
            width = 0;
            break;
        case 'p': snprintf(piece, sizeof(piece), "%s", parts->tm_hour < 12 ? "AM" : "PM"); break;
        case 'P': snprintf(piece, sizeof(piece), "%s", parts->tm_hour < 12 ? "am" : "pm"); break;
        case 'r': {
            StrBuf inner;
            sb_init(&inner);
            format_date("%I:%M:%S %p", parts, when, utc, nanoseconds, &inner);
            snprintf(piece, sizeof(piece), "%s", inner.data);
            sb_free(&inner);
            break;
        }
        case 'R': snprintf(piece, sizeof(piece), "%02d:%02d", parts->tm_hour, parts->tm_min); break;
        case 's': snprintf(piece, sizeof(piece), "%lld", (long long)when); break;
        case 'S': number = parts->tm_sec; break;
        case 't': snprintf(piece, sizeof(piece), "\t"); break;
        case 'T': snprintf(piece, sizeof(piece), "%02d:%02d:%02d", parts->tm_hour, parts->tm_min, parts->tm_sec); break;
        case 'u': number = parts->tm_wday ? parts->tm_wday : 7; default_width = 1; break;
        case 'U': number = (parts->tm_yday + 7 - parts->tm_wday) / 7; break;
        case 'w': number = parts->tm_wday; default_width = 1; break;
        case 'W': number = (parts->tm_yday + 7 - (parts->tm_wday + 6) % 7) / 7; break;
        case 'x': {
            StrBuf inner;
            sb_init(&inner);
            format_date("%m/%d/%y", parts, when, utc, nanoseconds, &inner);
            snprintf(piece, sizeof(piece), "%s", inner.data);
            sb_free(&inner);
            break;
        }
        case 'X': snprintf(piece, sizeof(piece), "%02d:%02d:%02d", parts->tm_hour, parts->tm_min, parts->tm_sec); break;
        case 'y': number = parts->tm_year % 100; break;
        case 'Y': number = parts->tm_year + 1900; default_width = 4; break;
        case 'z': {
            long offset = utc ? 0 : utc_offset_seconds(when, parts);
            snprintf(piece, sizeof(piece), "%c%02ld%02ld", offset < 0 ? '-' : '+', labs(offset) / 3600, (labs(offset) % 3600) / 60);
            break;
        }
        case ':': {
            if (p[1] == 'z') {
                p++;
                long offset = utc ? 0 : utc_offset_seconds(when, parts);
                snprintf(piece, sizeof(piece), "%c%02ld:%02ld", offset < 0 ? '-' : '+', labs(offset) / 3600, (labs(offset) % 3600) / 60);
            }
            break;
        }
        case 'Z':
            if (utc) snprintf(piece, sizeof(piece), "UTC");
            else {
                char zone[64] = "";
                strftime(zone, sizeof(zone), "%Z", parts);
                snprintf(piece, sizeof(piece), "%s", zone);
            }
            break;
        default:
            snprintf(piece, sizeof(piece), "%%%c", spec);
            break;
        }
        if (number >= 0) {
            int w = width ? width : default_width;
            if (no_pad) snprintf(piece, sizeof(piece), "%ld", number);
            else if (space_pad || (pad == ' ' && !zero_pad)) snprintf(piece, sizeof(piece), "%*ld", w, number);
            else snprintf(piece, sizeof(piece), "%0*ld", w, number);
        } else if (width && strlen(piece) < (size_t)width) {
            char padded[128];
            snprintf(padded, sizeof(padded), "%*s", width, piece);
            snprintf(piece, sizeof(piece), "%s", padded);
        }
        if (upper) for (char *c = piece; *c; c++) *c = (char)toupper((unsigned char)*c);
        sb_puts(out, piece);
    }
}

static int month_from_name(const char *word) {
    for (int m = 0; m < 12; m++) {
        if (_strnicmp(word, MONTHS[m], 3) == 0) return m;
    }
    return -1;
}

static int parse_date_string(const char *text, int utc, time_t *out, int *has_time) {
    *has_time = 1;
    time_t now = time(NULL);
    if (*text == '@') {
        *out = (time_t)atoll(text + 1);
        return 1;
    }
    struct tm parts;
    struct tm *base = utc ? gmtime(&now) : localtime(&now);
    parts = *base;
    parts.tm_isdst = -1;

    char copy[256];
    snprintf(copy, sizeof(copy), "%s", text);
    for (char *c = copy; *c; c++) *c = (char)tolower((unsigned char)*c);
    char *cursor = copy;
    char *word;
    int seen_date = 0;
    int relative_days = 0, relative_months = 0, relative_years = 0;
    long relative_seconds = 0;
    int absolute_time = 0;
    long pending = 0;
    int have_pending = 0;
    int sign = 1;

    while ((word = str_next_field(&cursor, ' ')) != NULL) {
        if (!*word) continue;
        int year, month, day, hour, minute, second = 0;
        if (sscanf(word, "%d-%d-%d", &year, &month, &day) == 3 && strchr(word, '-') && isdigit((unsigned char)word[0])) {
            parts.tm_year = year - 1900;
            parts.tm_mon = month - 1;
            parts.tm_mday = day;
            parts.tm_hour = parts.tm_min = parts.tm_sec = 0;
            seen_date = 1;
            char *t = strchr(word, 't');
            if (t && sscanf(t + 1, "%d:%d:%d", &hour, &minute, &second) >= 2) {
                parts.tm_hour = hour;
                parts.tm_min = minute;
                parts.tm_sec = second;
                absolute_time = 1;
            }
            continue;
        }
        if (sscanf(word, "%d/%d/%d", &month, &day, &year) == 3) {
            parts.tm_year = (year < 100 ? year + 2000 : year) - 1900;
            parts.tm_mon = month - 1;
            parts.tm_mday = day;
            parts.tm_hour = parts.tm_min = parts.tm_sec = 0;
            seen_date = 1;
            continue;
        }
        if (sscanf(word, "%d:%d:%d", &hour, &minute, &second) >= 2 && strchr(word, ':')) {
            parts.tm_hour = hour;
            parts.tm_min = minute;
            parts.tm_sec = second;
            absolute_time = 1;
            continue;
        }
        if (strcmp(word, "now") == 0 || strcmp(word, "today") == 0) {
            if (strcmp(word, "today") == 0) parts.tm_hour = parts.tm_min = parts.tm_sec = 0;
            continue;
        }
        if (strcmp(word, "yesterday") == 0) {
            relative_days -= 1;
            parts.tm_hour = parts.tm_min = parts.tm_sec = 0;
            continue;
        }
        if (strcmp(word, "tomorrow") == 0) {
            relative_days += 1;
            parts.tm_hour = parts.tm_min = parts.tm_sec = 0;
            continue;
        }
        if (strcmp(word, "midnight") == 0) {
            parts.tm_hour = parts.tm_min = parts.tm_sec = 0;
            continue;
        }
        if (strcmp(word, "noon") == 0) {
            parts.tm_hour = 12;
            parts.tm_min = parts.tm_sec = 0;
            continue;
        }
        if (strcmp(word, "utc") == 0 || strcmp(word, "gmt") == 0 || strcmp(word, "z") == 0) continue;
        if (strcmp(word, "ago") == 0) {
            relative_days = -relative_days;
            relative_months = -relative_months;
            relative_years = -relative_years;
            relative_seconds = -relative_seconds;
            continue;
        }
        if (strcmp(word, "next") == 0) {
            pending = 1;
            have_pending = 1;
            continue;
        }
        if (strcmp(word, "last") == 0) {
            pending = -1;
            have_pending = 1;
            continue;
        }
        int m = month_from_name(word);
        if (m >= 0 && !isdigit((unsigned char)word[0])) {
            parts.tm_mon = m;
            seen_date = 1;
            continue;
        }
        char *end;
        long value = strtol(word, &end, 10);
        if (end != word && !*end) {
            int explicit_sign = word[0] == '+' || word[0] == '-';
            const char *peek = cursor;
            while (peek && *peek == ' ') peek++;
            int unit_follows = peek && (strncmp(peek, "sec", 3) == 0 || strncmp(peek, "min", 3) == 0 ||
                                        strncmp(peek, "hour", 4) == 0 || strncmp(peek, "day", 3) == 0 ||
                                        strncmp(peek, "week", 4) == 0 || strncmp(peek, "fortnight", 9) == 0 ||
                                        strncmp(peek, "month", 5) == 0 || strncmp(peek, "year", 4) == 0);
            if (seen_date && !explicit_sign && !unit_follows) {
                if (value > 31) parts.tm_year = (int)value - 1900;
                else parts.tm_mday = (int)value;
            } else {
                pending = value;
                have_pending = 1;
            }
            sign = 1;
            continue;
        }
        if (end != word && *end) {
            pending = value;
            have_pending = 1;
            word = end;
        }
        long amount = have_pending ? pending : 1;
        have_pending = 0;
        if (strncmp(word, "sec", 3) == 0) relative_seconds += amount * sign;
        else if (strncmp(word, "min", 3) == 0) relative_seconds += amount * 60 * sign;
        else if (strncmp(word, "hour", 4) == 0) relative_seconds += amount * 3600 * sign;
        else if (strncmp(word, "day", 3) == 0) relative_days += (int)amount * sign;
        else if (strncmp(word, "week", 4) == 0) relative_days += (int)amount * 7 * sign;
        else if (strncmp(word, "fortnight", 9) == 0) relative_days += (int)amount * 14 * sign;
        else if (strncmp(word, "month", 5) == 0) relative_months += (int)amount * sign;
        else if (strncmp(word, "year", 4) == 0) relative_years += (int)amount * sign;
        else return 0;
    }
    if (seen_date && !absolute_time) *has_time = 0;
    parts.tm_year += relative_years;
    parts.tm_mon += relative_months;
    parts.tm_mday += relative_days;
    time_t result;
    if (utc) {
#ifdef _WIN32
        result = _mkgmtime(&parts);
#else
        result = timegm(&parts);
#endif
    } else {
        result = mktime(&parts);
    }
    if (result == (time_t)-1) return 0;
    *out = result + relative_seconds;
    return 1;
}

static int cmd_date(int argc, char **argv) {
    static const OptSpec specs[] = {{'d', "date", 1},     {'f', "file", 1},   {'I', "iso-8601", 2},
                                    {'r', "reference", 1}, {'R', "rfc-email", 0}, {'R', "rfc-2822", 0},
                                    {'1', "rfc-3339", 1}, {'s', "set", 1},    {'u', "utc", 0},
                                    {'u', "universal", 0}, {'2', "debug", 0}, {'3', "resolution", 0},
                                    {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "date", &args, 1);
    if (early >= 0) return early;
    int utc = args_has(&args, 'u');
    time_t when = time(NULL);
    long nanoseconds = 0;
    int has_time = 1;
    if (args_has(&args, 's')) {
        cmd_error("date", "cannot set date: Operation not permitted");
        args_free(&args);
        return 1;
    }
    if (args_has(&args, 'r')) {
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (!GetFileAttributesExA(args_value(&args, 'r'), GetFileExInfoStandard, &info)) {
            cmd_error("date", "%s: No such file or directory", args_value(&args, 'r'));
            args_free(&args);
            return 1;
        }
        unsigned long long ticks = ((unsigned long long)info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime;
        when = (time_t)(ticks / 10000000ULL - 11644473600ULL);
        nanoseconds = (long)((ticks % 10000000ULL) * 100);
    }
    if (args_has(&args, 'd')) {
        if (!parse_date_string(args_value(&args, 'd'), utc, &when, &has_time)) {
            cmd_error("date", "invalid date '%s'", args_value(&args, 'd'));
            args_free(&args);
            return 1;
        }
        nanoseconds = 0;
    }
    const char *format = "%a %b %e %H:%M:%S %Z %Y";
    int operand = 0;
    if (args.operand_count > 0 && args.operands[0][0] == '+') {
        format = args.operands[0] + 1;
        operand = 1;
    }
    if (args.operand_count > operand) {
        cmd_error("date", "invalid date '%s'", args.operands[operand]);
        args_free(&args);
        return 1;
    }
    if (args_has(&args, 'R')) {
        format = "%a, %d %b %Y %H:%M:%S %z";
    } else if (args_has(&args, 'I')) {
        const char *kind = args_value(&args, 'I');
        if (!kind || strcmp(kind, "date") == 0) format = "%Y-%m-%d";
        else if (strcmp(kind, "hours") == 0) format = "%Y-%m-%dT%H%:z";
        else if (strcmp(kind, "minutes") == 0) format = "%Y-%m-%dT%H:%M%:z";
        else if (strcmp(kind, "seconds") == 0) format = "%Y-%m-%dT%H:%M:%S%:z";
        else if (strcmp(kind, "ns") == 0) format = "%Y-%m-%dT%H:%M:%S,%N%:z";
        else {
            cmd_error("date", "invalid argument '%s' for '--iso-8601'", kind);
            args_free(&args);
            return 1;
        }
    } else if (args_has(&args, '1')) {
        const char *kind = args_value(&args, '1');
        if (strcmp(kind, "date") == 0) format = "%Y-%m-%d";
        else if (strcmp(kind, "seconds") == 0) format = "%Y-%m-%d %H:%M:%S%:z";
        else if (strcmp(kind, "ns") == 0) format = "%Y-%m-%d %H:%M:%S.%N%:z";
        else {
            cmd_error("date", "invalid argument '%s' for '--rfc-3339'", kind);
            args_free(&args);
            return 1;
        }
    }
    struct tm *parts = utc ? gmtime(&when) : localtime(&when);
    StrBuf out;
    sb_init(&out);
    format_date(format, parts, when, utc, nanoseconds, &out);
    puts(out.data);
    sb_free(&out);
    args_free(&args);
    return 0;
}

static int cmd_sleep(int argc, char **argv) {
    Args args;
    static const OptSpec specs[] = {{0, NULL, 0}};
    int early = parse_or_exit(argc, argv, specs, "sleep", &args, 1);
    if (early >= 0) return early;
    if (args.operand_count == 0) {
        cmd_error("sleep", "missing operand");
        fprintf(stderr, "Try 'sleep --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    double total = 0;
    for (int i = 0; i < args.operand_count; i++) {
        char *end;
        double value = strtod(args.operands[i], &end);
        if (end == args.operands[i] || value < 0) {
            cmd_error("sleep", "invalid time interval '%s'", args.operands[i]);
            fprintf(stderr, "Try 'sleep --help' for more information.\n");
            args_free(&args);
            return 1;
        }
        if (*end == 's') end++;
        else if (*end == 'm') { value *= 60; end++; }
        else if (*end == 'h') { value *= 3600; end++; }
        else if (*end == 'd') { value *= 86400; end++; }
        if (*end) {
            cmd_error("sleep", "invalid time interval '%s'", args.operands[i]);
            args_free(&args);
            return 1;
        }
        total += value;
    }
    args_free(&args);
    while (total > 0) {
        double chunk = total > 0.2 ? 0.2 : total;
        Sleep((DWORD)(chunk * 1000));
        total -= chunk;
        if (shell.interrupted) return 130;
    }
    return 0;
}

static int cmd_whoami(int argc, char **argv) {
    Args args;
    static const OptSpec specs[] = {{0, NULL, 0}};
    int early = parse_or_exit(argc, argv, specs, "whoami", &args, 1);
    if (early >= 0) return early;
    args_free(&args);
    char user[256];
    DWORD size = sizeof(user);
    if (!win_user_name(user, &size)) {
        cmd_error("whoami", "cannot find name for user ID");
        return 1;
    }
    printf("%s\n", user);
    return 0;
}

static int cmd_hostname(int argc, char **argv) {
    static const OptSpec specs[] = {{'s', "short", 0}, {'f', "fqdn", 0}, {'f', "long", 0}, {'d', "domain", 0},
                                    {'i', "ip-address", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "hostname", &args, 1);
    if (early >= 0) return early;
    char host[256];
    DWORD size = sizeof(host);
    if (!GetComputerNameA(host, &size)) {
        args_free(&args);
        return 1;
    }
    if (args_has(&args, 's')) {
        char *dot = strchr(host, '.');
        if (dot) *dot = '\0';
    } else if (args_has(&args, 'd')) {
        char *dot = strchr(host, '.');
        printf("%s\n", dot ? dot + 1 : "");
        args_free(&args);
        return 0;
    }
    printf("%s\n", host);
    args_free(&args);
    return 0;
}

static void machine_name(char *out, size_t size) {
#ifdef _WIN32
    SYSTEM_INFO system;
    GetNativeSystemInfo(&system);
    const char *architecture = system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x86_64"
                               : system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "aarch64"
                                                                                               : "i686";
    snprintf(out, size, "%s", architecture);
#else
    struct utsname info;
    if (uname(&info) == 0) snprintf(out, size, "%s", info.machine);
    else snprintf(out, size, "unknown");
#endif
}

static int cmd_uname(int argc, char **argv) {
    static const OptSpec specs[] = {{'a', "all", 0},       {'s', "kernel-name", 0},  {'n', "nodename", 0},
                                    {'r', "kernel-release", 0}, {'v', "kernel-version", 0}, {'m', "machine", 0},
                                    {'p', "processor", 0}, {'i', "hardware-platform", 0}, {'o', "operating-system", 0},
                                    {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "uname", &args, 1);
    if (early >= 0) return early;
    if (args.operand_count) {
        cmd_error("uname", "extra operand '%s'", args.operands[0]);
        args_free(&args);
        return 1;
    }
    char sysname[64], nodename[256], release[128], version[256], machine[64], os[64];
    machine_name(machine, sizeof(machine));
#ifdef _WIN32
    snprintf(sysname, sizeof(sysname), "Windows");
    DWORD size = sizeof(nodename);
    if (!GetComputerNameA(nodename, &size)) snprintf(nodename, sizeof(nodename), "unknown");
    snprintf(release, sizeof(release), "%s", "10.0");
    snprintf(version, sizeof(version), "%s", "Windows NT");
    snprintf(os, sizeof(os), "Windows");
#else
    struct utsname info;
    if (uname(&info) != 0) {
        args_free(&args);
        return 1;
    }
    snprintf(sysname, sizeof(sysname), "%s", info.sysname);
    snprintf(nodename, sizeof(nodename), "%s", info.nodename);
    snprintf(release, sizeof(release), "%s", info.release);
    snprintf(version, sizeof(version), "%s", info.version);
#ifdef __APPLE__
    snprintf(os, sizeof(os), "Darwin");
#else
    snprintf(os, sizeof(os), "GNU/Linux");
#endif
#endif
    int all = args_has(&args, 'a');
    int any = all || args_has(&args, 's') || args_has(&args, 'n') || args_has(&args, 'r') || args_has(&args, 'v') ||
              args_has(&args, 'm') || args_has(&args, 'p') || args_has(&args, 'i') || args_has(&args, 'o');
    StrBuf out;
    sb_init(&out);
    if (!any || all || args_has(&args, 's')) sb_printf(&out, "%s ", sysname);
    if (all || args_has(&args, 'n')) sb_printf(&out, "%s ", nodename);
    if (all || args_has(&args, 'r')) sb_printf(&out, "%s ", release);
    if (all || args_has(&args, 'v')) sb_printf(&out, "%s ", version);
    if (all || args_has(&args, 'm')) sb_printf(&out, "%s ", machine);
    if (args_has(&args, 'p')) sb_printf(&out, "%s ", all ? machine : machine);
    if (args_has(&args, 'i')) sb_printf(&out, "%s ", machine);
    if (all || args_has(&args, 'o')) sb_printf(&out, "%s ", os);
    if (out.len && out.data[out.len - 1] == ' ') out.data[--out.len] = '\0';
    puts(out.data);
    sb_free(&out);
    args_free(&args);
    return 0;
}

static int cmd_arch(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char machine[64];
    machine_name(machine, sizeof(machine));
    puts(machine);
    return 0;
}

static int cmd_nproc(int argc, char **argv) {
    static const OptSpec specs[] = {{'a', "all", 0}, {'i', "ignore", 1}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "nproc", &args, 1);
    if (early >= 0) return early;
    long count;
#ifdef _WIN32
    SYSTEM_INFO system;
    GetNativeSystemInfo(&system);
    count = (long)system.dwNumberOfProcessors;
#else
    count = sysconf(_SC_NPROCESSORS_ONLN);
    if (count < 1) count = 1;
#endif
    if (args_has(&args, 'i')) count -= atol(args_value(&args, 'i'));
    if (count < 1) count = 1;
    printf("%ld\n", count);
    args_free(&args);
    return 0;
}

static int cmd_id(int argc, char **argv) {
    static const OptSpec specs[] = {{'u', "user", 0}, {'g', "group", 0}, {'G', "groups", 0}, {'n', "name", 0},
                                    {'r', "real", 0}, {'z', "zero", 0}, {'a', NULL, 0}, {'Z', "context", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "id", &args, 1);
    if (early >= 0) return early;
    char user[256] = "user";
    DWORD size = sizeof(user);
    win_user_name(user, &size);
    unsigned long uid = 1000, gid = 1000;
    StrList group_names;
    sl_init(&group_names);
    unsigned long group_ids[64];
    size_t group_count = 0;
#ifdef _WIN32
    snprintf(user, sizeof(user), "%s", user);
    sl_push_copy(&group_names, user);
    group_ids[group_count++] = 1000;
    if (running_elevated()) {
        sl_push_copy(&group_names, "administrators");
        group_ids[group_count++] = 544;
    }
    const char *group_name = user;
#else
    uid = (unsigned long)getuid();
    gid = (unsigned long)getgid();
    struct passwd *pw = getpwuid(getuid());
    struct group *primary = getgrgid(getgid());
    const char *group_name = primary ? primary->gr_name : "?";
    gid_t groups[64];
    int ngroups = getgroups(64, groups);
    for (int i = 0; i < ngroups && group_count < 64; i++) {
        struct group *gr = getgrgid(groups[i]);
        sl_push_copy(&group_names, gr ? gr->gr_name : "?");
        group_ids[group_count++] = (unsigned long)groups[i];
    }
    if (pw) snprintf(user, sizeof(user), "%s", pw->pw_name);
#endif
    int names = args_has(&args, 'n');
    char terminator = args_has(&args, 'z') ? '\0' : '\n';
    if (args_has(&args, 'u')) {
        if (names) printf("%s%c", user, terminator);
        else printf("%lu%c", uid, terminator);
    } else if (args_has(&args, 'g')) {
        if (names) printf("%s%c", group_name, terminator);
        else printf("%lu%c", gid, terminator);
    } else if (args_has(&args, 'G')) {
        for (size_t i = 0; i < group_count; i++) {
            if (i) putchar(' ');
            if (names) printf("%s", group_names.items[i]);
            else printf("%lu", group_ids[i]);
        }
        putchar(terminator);
    } else {
        printf("uid=%lu(%s) gid=%lu(%s) groups=", uid, user, gid, group_name);
        for (size_t i = 0; i < group_count; i++) printf("%s%lu(%s)", i ? "," : "", group_ids[i], group_names.items[i]);
        putchar('\n');
    }
    sl_free(&group_names);
    args_free(&args);
    return 0;
}

static int cmd_groups(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char *fake_argv[] = {"id", "-Gn", NULL};
    return cmd_id(2, fake_argv);
}

typedef struct {
    const char *name;
    int number;
} Signal;

static const Signal SIGNALS[] = {
    {"HUP", 1}, {"INT", 2}, {"QUIT", 3}, {"ILL", 4}, {"TRAP", 5}, {"ABRT", 6}, {"BUS", 7}, {"FPE", 8},
    {"KILL", 9}, {"USR1", 10}, {"SEGV", 11}, {"USR2", 12}, {"PIPE", 13}, {"ALRM", 14}, {"TERM", 15},
    {"CHLD", 17}, {"CONT", 18}, {"STOP", 19}, {"TSTP", 20}, {"TTIN", 21}, {"TTOU", 22}, {"WINCH", 28},
};

static int signal_number(const char *text) {
    if (isdigit((unsigned char)*text)) return atoi(text);
    if (_strnicmp(text, "SIG", 3) == 0) text += 3;
    for (size_t i = 0; i < END(SIGNALS); i++) {
        if (_stricmp(SIGNALS[i].name, text) == 0) return SIGNALS[i].number;
    }
    return -1;
}

static const char *signal_name(int number) {
    for (size_t i = 0; i < END(SIGNALS); i++) {
        if (SIGNALS[i].number == number) return SIGNALS[i].name;
    }
    return NULL;
}

static int send_signal(unsigned long pid, int number) {
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_TERMINATE | 0x0400, FALSE, (DWORD)pid);
    if (!process) return 0;
    int ok = 1;
    if (number == 0) ok = 1;
    else if (number == 19 || number == 20) ok = jobs_suspend_pid(pid);
    else if (number == 18) ok = jobs_resume_pid(pid);
    else ok = TerminateProcess(process, (UINT)(128 + number)) != 0;
    CloseHandle(process);
    return ok;
#else
    return kill((pid_t)pid, number) == 0;
#endif
}

static int cmd_kill(int argc, char **argv) {
    int number = 15;
    int list = 0;
    int index = 1;
    while (index < argc && argv[index][0] == '-' && argv[index][1]) {
        const char *arg = argv[index];
        if (strcmp(arg, "--") == 0) {
            index++;
            break;
        }
        if (strcmp(arg, "-l") == 0 || strcmp(arg, "-L") == 0 || strcmp(arg, "--list") == 0 || strcmp(arg, "--table") == 0) {
            list = 1;
            index++;
            continue;
        }
        if (strcmp(arg, "-s") == 0 || strcmp(arg, "--signal") == 0) {
            if (index + 1 >= argc) {
                cmd_error("kill", "option requires an argument -- 's'");
                return 1;
            }
            number = signal_number(argv[index + 1]);
            if (number < 0) {
                cmd_error("kill", "%s: invalid signal specification", argv[index + 1]);
                return 1;
            }
            index += 2;
            continue;
        }
        if (strncmp(arg, "-n", 2) == 0 && index + 1 < argc) {
            number = atoi(argv[index + 1]);
            index += 2;
            continue;
        }
        if (isdigit((unsigned char)arg[1]) || isalpha((unsigned char)arg[1])) {
            int candidate = signal_number(arg + 1);
            if (candidate < 0) {
                cmd_error("kill", "%s: invalid signal specification", arg + 1);
                return 1;
            }
            number = candidate;
            index++;
            continue;
        }
        cmd_error("kill", "invalid option -- '%c'", arg[1]);
        return 1;
    }
    if (list) {
        if (index < argc) {
            for (int i = index; i < argc; i++) {
                if (isdigit((unsigned char)argv[i][0])) {
                    int n = atoi(argv[i]);
                    if (n > 128) n -= 128;
                    const char *name = signal_name(n);
                    if (name) puts(name);
                    else {
                        cmd_error("kill", "%s: invalid signal specification", argv[i]);
                        return 1;
                    }
                } else {
                    int n = signal_number(argv[i]);
                    if (n < 0) {
                        cmd_error("kill", "%s: invalid signal specification", argv[i]);
                        return 1;
                    }
                    printf("%d\n", n);
                }
            }
            return 0;
        }
        for (size_t i = 0; i < END(SIGNALS); i++) printf("%s%s", SIGNALS[i].name, i + 1 == END(SIGNALS) ? "\n" : " ");
        return 0;
    }
    if (index >= argc) {
        fprintf(stderr, "kill: usage: kill [-s sigspec | -n signum | -sigspec] pid | jobspec ... or kill -l [sigspec]\n");
        return 2;
    }
    int status = 0;
    for (int i = index; i < argc; i++) {
        unsigned long pid;
        if (argv[i][0] == '%') {
            long job_pid = jobs_pid(atoi(argv[i] + 1));
            if (job_pid <= 0) {
                cmd_error("kill", "%s: no such job", argv[i]);
                status = 1;
                continue;
            }
            pid = (unsigned long)job_pid;
        } else {
            char *end;
            pid = strtoul(argv[i], &end, 10);
            if (end == argv[i] || *end) {
                cmd_error("kill", "%s: arguments must be process or job IDs", argv[i]);
                status = 1;
                continue;
            }
        }
        if (!send_signal(pid, number)) {
            cmd_error("kill", "(%lu) - No such process", pid);
            status = 1;
        }
    }
    return status;
}

static void xargs_split(const StrBuf *input, char delimiter, int null_mode, StrList *out) {
    if (null_mode || delimiter) {
        char sep = null_mode ? '\0' : delimiter;
        size_t start = 0;
        for (size_t i = 0; i <= input->len; i++) {
            if (i == input->len || input->data[i] == sep) {
                if (i > start || (i < input->len && !null_mode)) sl_push(out, xstrndup(input->data + start, i - start));
                start = i + 1;
            }
        }
        if (out->len && null_mode && out->items[out->len - 1][0] == '\0') {
            free(out->items[--out->len]);
        }
        return;
    }
    const char *p = input->data;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        StrBuf word;
        sb_init(&word);
        while (*p && !isspace((unsigned char)*p)) {
            if (*p == '"' || *p == '\'') {
                char quote = *p++;
                while (*p && *p != quote) sb_putc(&word, *p++);
                if (*p == quote) p++;
                continue;
            }
            if (*p == '\\' && p[1]) {
                p++;
                sb_putc(&word, *p++);
                continue;
            }
            sb_putc(&word, *p++);
        }
        sl_push(out, sb_take(&word));
    }
}

static int xargs_run(const StrList *command, const StrList *items, const char *replace, int trace) {
    StrBuf text;
    sb_init(&text);
    int inserted = 0;
    for (size_t i = 0; i < command->len; i++) {
        if (i) sb_putc(&text, ' ');
        const char *arg = command->items[i];
        if (replace && strstr(arg, replace)) {
            StrBuf expanded;
            sb_init(&expanded);
            size_t length = strlen(replace);
            for (const char *p = arg; *p;) {
                if (strncmp(p, replace, length) == 0) {
                    sb_puts(&expanded, items->len ? items->items[0] : "");
                    p += length;
                } else sb_putc(&expanded, *p++);
            }
            sb_put_quoted(&text, expanded.data);
            sb_free(&expanded);
            inserted = 1;
        } else {
            sb_put_quoted(&text, arg);
        }
    }
    if (!inserted) {
        for (size_t i = 0; i < items->len; i++) {
            sb_putc(&text, ' ');
            sb_put_quoted(&text, items->items[i]);
        }
    }
    if (trace) {
        StrBuf shown;
        sb_init(&shown);
        for (size_t i = 0; i < command->len; i++) {
            if (i) sb_putc(&shown, ' ');
            sb_puts(&shown, command->items[i]);
        }
        if (!inserted)
            for (size_t i = 0; i < items->len; i++) {
                sb_putc(&shown, ' ');
                sb_puts(&shown, items->items[i]);
            }
        fprintf(stderr, "%s\n", shown.data);
        fflush(stderr);
        sb_free(&shown);
    }
    int status = exec_text(text.data);
    sb_free(&text);
    return status;
}

static int cmd_xargs(int argc, char **argv) {
    static const OptSpec specs[] = {{'0', "null", 0},        {'a', "arg-file", 1},   {'d', "delimiter", 1},
                                    {'E', NULL, 1},           {'I', "replace", 2},     {'i', NULL, 2},
                                    {'L', "max-lines", 1},    {'l', NULL, 2},          {'n', "max-args", 1},
                                    {'P', "max-procs", 1},    {'p', "interactive", 0}, {'r', "no-run-if-empty", 0},
                                    {'s', "max-chars", 1},    {'t', "verbose", 0},     {'x', "exit", 0},
                                    {'1', "process-slot-var", 1}, {'2', "show-limits", 0}, {'3', "open-tty", 0},
                                    {0, NULL, 0}};
    Args args;
    memset(&args, 0, sizeof(args));
    args.stop_at_operand = 1;
    int parsed = args_parse(argc, argv, specs, "xargs", &args);
    if (parsed != ARGS_OK) return parsed == ARGS_DONE ? 0 : 1;

    StrBuf input;
    sb_init(&input);
    if (args_has(&args, 'a')) {
        FILE *f = cmd_open("xargs", args_value(&args, 'a'));
        if (!f) {
            sb_free(&input);
            args_free(&args);
            return 1;
        }
        char block[65536];
        size_t n;
        while ((n = fread(block, 1, sizeof(block), f)) > 0) sb_putn(&input, block, n);
        cmd_close(f);
    } else {
        char block[65536];
        size_t n;
        while ((n = fread(block, 1, sizeof(block), stdin)) > 0) sb_putn(&input, block, n);
    }

    char delimiter = 0;
    if (args_has(&args, 'd')) {
        const char *d = args_value(&args, 'd');
        if (d[0] == '\\' && d[1]) {
            if (d[1] == 'n') delimiter = '\n';
            else if (d[1] == 't') delimiter = '\t';
            else if (d[1] == '0') delimiter = '\0';
            else delimiter = d[1];
        } else delimiter = d[0];
    }
    const char *replace = NULL;
    if (args_has(&args, 'I')) replace = args_value(&args, 'I') ? args_value(&args, 'I') : "{}";
    else if (args_has(&args, 'i')) replace = args_value(&args, 'i') ? args_value(&args, 'i') : "{}";
    long per_command = args_has(&args, 'n') ? atol(args_value(&args, 'n')) : 0;
    long lines_per_command = args_has(&args, 'L') ? atol(args_value(&args, 'L')) : (args_has(&args, 'l') ? (args_value(&args, 'l') ? atol(args_value(&args, 'l')) : 1) : 0);
    if (replace) lines_per_command = 1;
    const char *eof_marker = args_value(&args, 'E');

    StrList items;
    sl_init(&items);
    if (replace || lines_per_command) {
        size_t start = 0;
        for (size_t i = 0; i <= input.len; i++) {
            if (i == input.len || input.data[i] == '\n') {
                if (i > start) {
                    char *line = xstrndup(input.data + start, i - start);
                    if (replace) sl_push(&items, line);
                    else {
                        StrBuf one;
                        sb_init(&one);
                        sb_puts(&one, line);
                        StrList words;
                        sl_init(&words);
                        xargs_split(&one, 0, 0, &words);
                        StrBuf joined;
                        sb_init(&joined);
                        for (size_t w = 0; w < words.len; w++) {
                            if (w) sb_putc(&joined, '\x1f');
                            sb_puts(&joined, words.items[w]);
                        }
                        sl_push(&items, sb_take(&joined));
                        sl_free(&words);
                        sb_free(&one);
                        free(line);
                    }
                }
                start = i + 1;
            }
        }
    } else {
        xargs_split(&input, delimiter, args_has(&args, '0'), &items);
    }
    if (eof_marker) {
        for (size_t i = 0; i < items.len; i++) {
            if (strcmp(items.items[i], eof_marker) == 0) {
                items.len = i;
                break;
            }
        }
    }

    StrList command;
    sl_init(&command);
    if (args.operand_count == 0) sl_push_copy(&command, "echo");
    for (int i = 0; i < args.operand_count; i++) sl_push_copy(&command, args.operands[i]);

    int status = 0;
    int trace = args_has(&args, 't');
    if (items.len == 0) {
        if (!args_has(&args, 'r') && !replace) status = xargs_run(&command, &items, NULL, trace);
    } else if (replace) {
        for (size_t i = 0; i < items.len; i++) {
            StrList one;
            sl_init(&one);
            sl_push_copy(&one, items.items[i]);
            int result = xargs_run(&command, &one, replace, trace);
            sl_free(&one);
            if (result) status = result == 255 ? 124 : 123;
            if (result == 255) break;
        }
    } else if (lines_per_command) {
        for (size_t i = 0; i < items.len; i += (size_t)lines_per_command) {
            StrList chunk;
            sl_init(&chunk);
            for (size_t j = i; j < i + (size_t)lines_per_command && j < items.len; j++) {
                char *copy = xstrdup(items.items[j]);
                char *cursor = copy;
                char *piece;
                while ((piece = str_next_field(&cursor, '\x1f')) != NULL) sl_push_copy(&chunk, piece);
                free(copy);
            }
            int result = xargs_run(&command, &chunk, NULL, trace);
            sl_free(&chunk);
            if (result) status = result == 255 ? 124 : 123;
            if (result == 255) break;
        }
    } else if (per_command > 0) {
        for (size_t i = 0; i < items.len; i += (size_t)per_command) {
            StrList chunk;
            sl_init(&chunk);
            for (size_t j = i; j < i + (size_t)per_command && j < items.len; j++) sl_push_copy(&chunk, items.items[j]);
            int result = xargs_run(&command, &chunk, NULL, trace);
            sl_free(&chunk);
            if (result) status = result == 255 ? 124 : 123;
            if (result == 255) break;
        }
    } else {
        int result = xargs_run(&command, &items, NULL, trace);
        if (result) status = result == 255 ? 124 : 123;
    }
    sl_free(&command);
    sl_free(&items);
    sb_free(&input);
    args_free(&args);
    return status;
}

static int decimals_of(const char *text) {
    const char *dot = strchr(text, '.');
    if (!dot) return 0;
    int count = 0;
    for (const char *p = dot + 1; isdigit((unsigned char)*p); p++) count++;
    return count;
}

static int valid_number(const char *text) {
    char *end;
    strtod(text, &end);
    return end != text && !*end;
}

static int cmd_seq(int argc, char **argv) {
    static const OptSpec specs[] = {{'f', "format", 1}, {'s', "separator", 1}, {'w', "equal-width", 0}, {0, NULL, 0}};
    Args args;
    memset(&args, 0, sizeof(args));
    args.negative_operands = 1;
    int parsed = args_parse(argc, argv, specs, "seq", &args);
    if (parsed != ARGS_OK) return parsed == ARGS_DONE ? 0 : 1;
    if (args.operand_count == 0) {
        cmd_error("seq", "missing operand");
        fprintf(stderr, "Try 'seq --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    if (args.operand_count > 3) {
        cmd_error("seq", "extra operand '%s'", args.operands[3]);
        fprintf(stderr, "Try 'seq --help' for more information.\n");
        args_free(&args);
        return 1;
    }
    for (int i = 0; i < args.operand_count; i++) {
        if (!valid_number(args.operands[i])) {
            cmd_error("seq", "invalid floating point argument: '%s'", args.operands[i]);
            fprintf(stderr, "Try 'seq --help' for more information.\n");
            args_free(&args);
            return 1;
        }
    }
    double first = 1, increment = 1, last;
    int decimals = 0;
    if (args.operand_count == 1) {
        last = atof(args.operands[0]);
        decimals = decimals_of(args.operands[0]);
    } else if (args.operand_count == 2) {
        first = atof(args.operands[0]);
        last = atof(args.operands[1]);
        decimals = decimals_of(args.operands[0]);
    } else {
        first = atof(args.operands[0]);
        increment = atof(args.operands[1]);
        last = atof(args.operands[2]);
        int a = decimals_of(args.operands[0]), b = decimals_of(args.operands[1]);
        decimals = a > b ? a : b;
    }
    if (increment == 0) {
        cmd_error("seq", "invalid Zero increment value: '%s'", args.operands[1]);
        args_free(&args);
        return 1;
    }
    const char *separator = args_has(&args, 's') ? args_value(&args, 's') : "\n";
    const char *format = args_value(&args, 'f');
    int width = 0;
    if (args_has(&args, 'w')) {
        char a[64], b[64];
        snprintf(a, sizeof(a), "%.*f", decimals, first);
        snprintf(b, sizeof(b), "%.*f", decimals, last);
        width = (int)(strlen(a) > strlen(b) ? strlen(a) : strlen(b));
    }
    long long steps = (long long)floor((last - first) / increment + 1e-9);
    if (steps < 0) {
        args_free(&args);
        return 0;
    }
    for (long long i = 0; i <= steps; i++) {
        double value = first + increment * (double)i;
        if (i > 0) fputs(separator, stdout);
        if (format) printf(format, value);
        else if (width) printf("%0*.*f", width, decimals, value);
        else printf("%.*f", decimals, value);
    }
    if (strcmp(separator, "\n") == 0 || steps >= 0) putchar('\n');
    args_free(&args);
    return 0;
}

typedef struct {
    char **tokens;
    int count;
    int position;
    int error;
    char *error_text;
} Expr;

typedef struct {
    long long number;
    char *text;
    int is_number;
} ExprValue;

static ExprValue expr_or(Expr *e);

static ExprValue value_number(long long number) {
    ExprValue v = {number, NULL, 1};
    return v;
}

static ExprValue value_text(const char *text) {
    ExprValue v = {0, xstrdup(text), 0};
    char *end;
    long long number = strtoll(text, &end, 10);
    if (end != text && !*end && *text) {
        v.is_number = 1;
        v.number = number;
    }
    return v;
}

static const char *value_string(const ExprValue *v, char *scratch, size_t size) {
    if (v->text) return v->text;
    snprintf(scratch, size, "%lld", v->number);
    return scratch;
}

static int value_true(const ExprValue *v) {
    if (v->text) return v->text[0] != '\0' && !(v->is_number && v->number == 0);
    return v->number != 0;
}

static void value_free(ExprValue *v) {
    free(v->text);
    v->text = NULL;
}

static const char *expr_peek(const Expr *e) {
    return e->position < e->count ? e->tokens[e->position] : NULL;
}

static ExprValue expr_primary(Expr *e) {
    const char *token = expr_peek(e);
    if (!token) {
        e->error = 2;
        return value_number(0);
    }
    e->position++;
    if (strcmp(token, "(") == 0) {
        ExprValue inner = expr_or(e);
        const char *close = expr_peek(e);
        if (!close || strcmp(close, ")") != 0) e->error = 2;
        else e->position++;
        return inner;
    }
    if (strcmp(token, "+") == 0) {
        const char *next = expr_peek(e);
        if (!next) {
            e->error = 2;
            return value_number(0);
        }
        e->position++;
        return value_text(next);
    }
    if (strcmp(token, "length") == 0) {
        ExprValue arg = expr_primary(e);
        char scratch[32];
        long long length = (long long)strlen(value_string(&arg, scratch, sizeof(scratch)));
        value_free(&arg);
        return value_number(length);
    }
    if (strcmp(token, "substr") == 0) {
        ExprValue s = expr_primary(e), pos = expr_primary(e), len = expr_primary(e);
        char scratch[32];
        const char *text = value_string(&s, scratch, sizeof(scratch));
        ExprValue result;
        if (!pos.is_number || !len.is_number || pos.number < 1 || len.number < 0 || (size_t)pos.number > strlen(text)) result = value_text("");
        else {
            size_t available = strlen(text) - (size_t)(pos.number - 1);
            size_t take = (size_t)len.number < available ? (size_t)len.number : available;
            char *piece = xstrndup(text + pos.number - 1, take);
            result = value_text(piece);
            free(piece);
        }
        value_free(&s);
        value_free(&pos);
        value_free(&len);
        return result;
    }
    if (strcmp(token, "index") == 0) {
        ExprValue s = expr_primary(e), chars = expr_primary(e);
        char a[32], b[32];
        const char *text = value_string(&s, a, sizeof(a));
        const char *set = value_string(&chars, b, sizeof(b));
        long long found = 0;
        for (size_t i = 0; text[i]; i++) {
            if (strchr(set, text[i])) {
                found = (long long)i + 1;
                break;
            }
        }
        value_free(&s);
        value_free(&chars);
        return value_number(found);
    }
    if (strcmp(token, "match") == 0) {
        ExprValue s = expr_primary(e), pattern = expr_primary(e);
        char a[32], b[32];
        const char *text = value_string(&s, a, sizeof(a));
        StrBuf ere;
        sb_init(&ere);
        sb_putc(&ere, '^');
        regex_bre_to_ere(value_string(&pattern, b, sizeof(b)), &ere);
        RegexMatch m;
        ExprValue result;
        if (regex_search(ere.data, text, &m)) {
            if (m.count > 1 && m.start[1] >= 0) {
                char *piece = xstrndup(text + m.start[1], (size_t)(m.end[1] - m.start[1]));
                result = value_text(piece);
                free(piece);
            } else result = value_number(m.end[0] - m.start[0]);
        } else result = m.count > 1 ? value_text("") : value_number(0);
        sb_free(&ere);
        value_free(&s);
        value_free(&pattern);
        return result;
    }
    return value_text(token);
}

static ExprValue expr_match_op(Expr *e) {
    ExprValue left = expr_primary(e);
    while (!e->error) {
        const char *op = expr_peek(e);
        if (!op || strcmp(op, ":") != 0) break;
        e->position++;
        ExprValue right = expr_primary(e);
        char a[32], b[32];
        const char *text = value_string(&left, a, sizeof(a));
        StrBuf ere;
        sb_init(&ere);
        sb_putc(&ere, '^');
        regex_bre_to_ere(value_string(&right, b, sizeof(b)), &ere);
        int has_group = strstr(value_string(&right, b, sizeof(b)), "\\(") != NULL;
        RegexMatch m;
        ExprValue result;
        if (regex_search(ere.data, text, &m)) {
            if (has_group && m.start[1] >= 0) {
                char *piece = xstrndup(text + m.start[1], (size_t)(m.end[1] - m.start[1]));
                result = value_text(piece);
                free(piece);
            } else if (has_group) result = value_text("");
            else result = value_number(m.end[0] - m.start[0]);
        } else result = has_group ? value_text("") : value_number(0);
        sb_free(&ere);
        value_free(&left);
        value_free(&right);
        left = result;
    }
    return left;
}

static ExprValue expr_mul(Expr *e) {
    ExprValue left = expr_match_op(e);
    while (!e->error) {
        const char *op = expr_peek(e);
        if (!op || (strcmp(op, "*") != 0 && strcmp(op, "/") != 0 && strcmp(op, "%") != 0)) break;
        e->position++;
        ExprValue right = expr_match_op(e);
        if (!left.is_number || !right.is_number) {
            e->error = 2;
            e->error_text = "non-integer argument";
            value_free(&right);
            return left;
        }
        if ((op[0] == '/' || op[0] == '%') && right.number == 0) {
            e->error = 2;
            e->error_text = "division by zero";
            value_free(&right);
            return left;
        }
        long long result = op[0] == '*' ? left.number * right.number : op[0] == '/' ? left.number / right.number : left.number % right.number;
        value_free(&left);
        value_free(&right);
        left = value_number(result);
    }
    return left;
}

static ExprValue expr_add(Expr *e) {
    ExprValue left = expr_mul(e);
    while (!e->error) {
        const char *op = expr_peek(e);
        if (!op || (strcmp(op, "+") != 0 && strcmp(op, "-") != 0)) break;
        e->position++;
        ExprValue right = expr_mul(e);
        if (!left.is_number || !right.is_number) {
            e->error = 2;
            e->error_text = "non-integer argument";
            value_free(&right);
            return left;
        }
        long long result = op[0] == '+' ? left.number + right.number : left.number - right.number;
        value_free(&left);
        value_free(&right);
        left = value_number(result);
    }
    return left;
}

static ExprValue expr_compare(Expr *e) {
    ExprValue left = expr_add(e);
    while (!e->error) {
        const char *op = expr_peek(e);
        if (!op || !(strcmp(op, "=") == 0 || strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 || strcmp(op, "<") == 0 ||
                     strcmp(op, "<=") == 0 || strcmp(op, ">") == 0 || strcmp(op, ">=") == 0))
            break;
        e->position++;
        ExprValue right = expr_add(e);
        int cmp;
        if (left.is_number && right.is_number) cmp = left.number < right.number ? -1 : left.number > right.number ? 1 : 0;
        else {
            char a[32], b[32];
            cmp = strcmp(value_string(&left, a, sizeof(a)), value_string(&right, b, sizeof(b)));
            cmp = cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
        }
        int result;
        if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) result = cmp == 0;
        else if (strcmp(op, "!=") == 0) result = cmp != 0;
        else if (strcmp(op, "<") == 0) result = cmp < 0;
        else if (strcmp(op, "<=") == 0) result = cmp <= 0;
        else if (strcmp(op, ">") == 0) result = cmp > 0;
        else result = cmp >= 0;
        value_free(&left);
        value_free(&right);
        left = value_number(result);
    }
    return left;
}

static ExprValue expr_and(Expr *e) {
    ExprValue left = expr_compare(e);
    while (!e->error) {
        const char *op = expr_peek(e);
        if (!op || strcmp(op, "&") != 0) break;
        e->position++;
        ExprValue right = expr_compare(e);
        if (!value_true(&left) || !value_true(&right)) {
            value_free(&left);
            value_free(&right);
            left = value_number(0);
        } else value_free(&right);
    }
    return left;
}

static ExprValue expr_or(Expr *e) {
    ExprValue left = expr_and(e);
    while (!e->error) {
        const char *op = expr_peek(e);
        if (!op || strcmp(op, "|") != 0) break;
        e->position++;
        ExprValue right = expr_and(e);
        if (value_true(&left)) value_free(&right);
        else {
            value_free(&left);
            if (value_true(&right)) left = right;
            else {
                value_free(&right);
                left = value_number(0);
            }
        }
    }
    return left;
}

static int cmd_expr(int argc, char **argv) {
    if (argc < 2) {
        cmd_error("expr", "missing operand");
        fprintf(stderr, "Try 'expr --help' for more information.\n");
        return 2;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        printf("Usage: expr EXPRESSION\n");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("expr (FreSH) %s\n", FRESH_VERSION);
        return 0;
    }
    int start = 1;
    if (strcmp(argv[1], "--") == 0) start = 2;
    Expr e = {argv + start, argc - start, 0, 0, NULL};
    ExprValue result = expr_or(&e);
    if (!e.error && e.position < e.count) e.error = 2;
    if (e.error) {
        if (e.error_text) cmd_error("expr", "%s", e.error_text);
        else cmd_error("expr", "syntax error: %s", e.position < e.count ? "unexpected argument" : "missing argument after last operand");
        value_free(&result);
        return 2;
    }
    char scratch[32];
    printf("%s\n", value_string(&result, scratch, sizeof(scratch)));
    int status = value_true(&result) ? 0 : 1;
    value_free(&result);
    return status;
}

#ifdef _WIN32

static int cmd_ps(int argc, char **argv) {
    (void)argc;
    (void)argv;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 1;
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);
    printf("    PID TTY          TIME CMD\n");
    if (Process32First(snapshot, &entry)) {
        do {
            printf("%7lu ?        00:00:00 %s\n", entry.th32ProcessID, entry.szExeFile);
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return 0;
}

static int cmd_pkill(int argc, char **argv) {
    static const OptSpec specs[] = {{'f', "full", 0}, {'x', "exact", 0}, {'e', "echo", 0}, {'c', "count", 0},
                                    {'i', "ignore-case", 0}, {'9', NULL, 0}, {'1', "signal", 1}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "pkill", &args, 2);
    if (early >= 0) return early;
    if (args.operand_count == 0) {
        fprintf(stderr, "pkill: no matching criteria specified\n");
        args_free(&args);
        return 2;
    }
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        args_free(&args);
        return 1;
    }
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);
    int killed = 0;
    if (Process32First(snapshot, &entry)) {
        do {
            char name[MAX_PATH];
            snprintf(name, sizeof(name), "%s", entry.szExeFile);
            char *dot = strrchr(name, '.');
            if (dot && str_ieq(dot, ".exe")) *dot = '\0';
            int matched = args_has(&args, 'x') ? _stricmp(args.operands[0], name) == 0
                                               : regex_search_at(args.operands[0], name, 0, args_has(&args, 'i') ? REGEX_ICASE : 0, NULL) ||
                                                     _stricmp(args.operands[0], entry.szExeFile) == 0;
            if (!matched) continue;
            HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
            if (!process) continue;
            if (TerminateProcess(process, 1)) {
                killed++;
                if (args_has(&args, 'e')) printf("%s killed (pid %lu)\n", name, entry.th32ProcessID);
            }
            CloseHandle(process);
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (args_has(&args, 'c')) printf("%d\n", killed);
    args_free(&args);
    return killed > 0 ? 0 : 1;
}

static int cmd_open_target(int argc, char **argv) {
    const char *target = argc > 1 ? argv[1] : ".";
    typedef HINSTANCE(WINAPI * OpenFn)(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT);
    HMODULE library = LoadLibraryA("shell32.dll");
    OpenFn shell_open = library ? (OpenFn)(void *)GetProcAddress(library, "ShellExecuteA") : NULL;
    if (!shell_open) {
        cmd_error("open", "%s: cannot open", target);
        return 1;
    }
    HINSTANCE result = shell_open(NULL, "open", target, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        cmd_error("open", "%s: cannot open", target);
        return 1;
    }
    return 0;
}

typedef BOOL(WINAPI *AcquireFn)(HCRYPTPROV *, LPCSTR, LPCSTR, DWORD, DWORD);
typedef BOOL(WINAPI *CreateHashFn)(HCRYPTPROV, ALG_ID, HCRYPTKEY, DWORD, HCRYPTHASH *);
typedef BOOL(WINAPI *HashDataFn)(HCRYPTHASH, const BYTE *, DWORD, DWORD);
typedef BOOL(WINAPI *HashParamFn)(HCRYPTHASH, DWORD, BYTE *, DWORD *, DWORD);
typedef BOOL(WINAPI *DestroyHashFn)(HCRYPTHASH);
typedef BOOL(WINAPI *ReleaseFn)(HCRYPTPROV, DWORD);

static AcquireFn crypt_acquire;
static CreateHashFn crypt_create_hash;
static HashDataFn crypt_hash_data;
static HashParamFn crypt_hash_param;
static DestroyHashFn crypt_destroy_hash;
static ReleaseFn crypt_release;

static int crypt_ready(void) {
    static HMODULE library = NULL;
    if (library) return crypt_acquire != NULL;
    library = LoadLibraryA("advapi32.dll");
    if (!library) return 0;
    crypt_acquire = (AcquireFn)(void *)GetProcAddress(library, "CryptAcquireContextA");
    crypt_create_hash = (CreateHashFn)(void *)GetProcAddress(library, "CryptCreateHash");
    crypt_hash_data = (HashDataFn)(void *)GetProcAddress(library, "CryptHashData");
    crypt_hash_param = (HashParamFn)(void *)GetProcAddress(library, "CryptGetHashParam");
    crypt_destroy_hash = (DestroyHashFn)(void *)GetProcAddress(library, "CryptDestroyHash");
    crypt_release = (ReleaseFn)(void *)GetProcAddress(library, "CryptReleaseContext");
    return crypt_acquire && crypt_create_hash && crypt_hash_data && crypt_hash_param && crypt_destroy_hash && crypt_release;
}

static int hash_stream(FILE *f, int algorithm, char *out, size_t out_size) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    if (!crypt_ready()) return 0;
    ALG_ID id = algorithm == 1 ? CALG_MD5 : algorithm == 2 ? CALG_SHA1 : algorithm == 3 ? CALG_SHA_256 : CALG_SHA_512;
    if (!crypt_acquire(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return 0;
    if (!crypt_create_hash(provider, id, 0, 0, &hash)) {
        crypt_release(provider, 0);
        return 0;
    }
    unsigned char buffer[65536];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) crypt_hash_data(hash, buffer, (DWORD)n, 0);
    unsigned char digest[64];
    DWORD digest_size = sizeof(digest);
    int ok = crypt_hash_param(hash, HP_HASHVAL, digest, &digest_size, 0) != 0;
    if (ok) {
        size_t offset = 0;
        for (DWORD i = 0; i < digest_size && offset + 2 < out_size; i++)
            offset += (size_t)snprintf(out + offset, out_size - offset, "%02x", digest[i]);
    }
    crypt_destroy_hash(hash);
    crypt_release(provider, 0);
    return ok;
}

#else

static int cmd_ps(int argc, char **argv) {
    (void)argc;
    (void)argv;
    cmd_error("ps", "not bundled on this platform, the system ps is used when it is on PATH");
    return 1;
}

static int cmd_pkill(int argc, char **argv) {
    (void)argc;
    (void)argv;
    cmd_error("pkill", "not bundled on this platform, the system pkill is used when it is on PATH");
    return 1;
}

static int cmd_open_target(int argc, char **argv) {
    const char *target = argc > 1 ? argv[1] : ".";
    StrBuf command;
    sb_init(&command);
#ifdef __APPLE__
    sb_puts(&command, "/usr/bin/open ");
#else
    sb_puts(&command, "xdg-open ");
#endif
    sb_put_quoted(&command, target);
    int status = exec_text(command.data);
    sb_free(&command);
    return status;
}

static int hash_stream(FILE *f, int algorithm, char *out, size_t out_size) {
    char temp[PATH_BUF];
    char directory[PATH_BUF];
    if (!GetTempPathA(sizeof(directory), directory) || !GetTempFileNameA(directory, "frhash", 0, temp)) return 0;
    FILE *copy = fopen(temp, "wb");
    if (!copy) return 0;
    char buffer[65536];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) fwrite(buffer, 1, n, copy);
    fclose(copy);
#ifdef __APPLE__
    const char *tool = algorithm == 1 ? "md5 -q" : algorithm == 2 ? "shasum -a 1" : algorithm == 3 ? "shasum -a 256" : "shasum -a 512";
#else
    const char *tool = algorithm == 1 ? "md5sum" : algorithm == 2 ? "sha1sum" : algorithm == 3 ? "sha256sum" : "sha512sum";
#endif
    StrBuf command;
    sb_init(&command);
    sb_puts(&command, tool);
    sb_puts(&command, " -- ");
    sb_put_quoted(&command, temp);
    sb_puts(&command, " 2>/dev/null");
    FILE *pipe = popen(command.data, "r");
    sb_free(&command);
    int ok = 0;
    if (pipe) {
        char line[512] = "";
        char *read = fgets(line, sizeof(line), pipe);
        ok = pclose(pipe) == 0 && read != NULL;
        line[strcspn(line, " \t\r\n")] = '\0';
        if (ok) snprintf(out, out_size, "%s", line);
    }
    unlink(temp);
    return ok && out[0] != '\0';
}

#endif

static int hash_command(int argc, char **argv, int algorithm) {
    const char *tool = algorithm == 1 ? "md5sum" : algorithm == 2 ? "sha1sum" : algorithm == 3 ? "sha256sum" : "sha512sum";
    static const OptSpec specs[] = {{'b', "binary", 0}, {'c', "check", 0}, {'t', "text", 0}, {'z', "zero", 0},
                                    {'1', "tag", 0}, {'2', "quiet", 0}, {'3', "status", 0}, {'4', "strict", 0},
                                    {'5', "warn", 0}, {'6', "ignore-missing", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, tool, &args, 1);
    if (early >= 0) return early;
    int status = 0;
    if (args_has(&args, 'c')) {
        int failures = 0, total = 0;
        int quiet = args_has(&args, '2'), silent = args_has(&args, '3');
        int count = args.operand_count ? args.operand_count : 1;
        for (int i = 0; i < count; i++) {
            FILE *list = cmd_open(tool, args.operand_count ? args.operands[i] : NULL);
            if (!list) {
                status = 1;
                continue;
            }
            StrBuf line;
            sb_init(&line);
            while (cmd_read_line(list, &line) != 0) {
                char expected[160], name[PATH_BUF];
                if (sscanf(line.data, "%159s %1023s", expected, name) != 2) continue;
                const char *shown = name[0] == '*' ? name + 1 : name;
                total++;
                FILE *f = fopen(shown, "rb");
                char actual[160] = "";
                int ok = f && hash_stream(f, algorithm, actual, sizeof(actual)) && _stricmp(actual, expected) == 0;
                if (f) fclose(f);
                if (!f) {
                    if (args_has(&args, '6')) {
                        total--;
                        continue;
                    }
                    cmd_error(tool, "%s: No such file or directory", shown);
                    if (!silent) printf("%s: FAILED open or read\n", shown);
                    failures++;
                    continue;
                }
                if (!silent && (!quiet || !ok)) printf("%s: %s\n", shown, ok ? "OK" : "FAILED");
                if (!ok) failures++;
            }
            sb_free(&line);
            cmd_close(list);
        }
        if (failures) {
            if (!silent) cmd_error(tool, "WARNING: %d computed checksum%s did NOT match", failures, failures == 1 ? "" : "s");
            status = 1;
        }
        args_free(&args);
        return status;
    }
    int count = args.operand_count ? args.operand_count : 1;
    for (int i = 0; i < count; i++) {
        const char *name = args.operand_count ? args.operands[i] : NULL;
        FILE *f = cmd_open(tool, name);
        if (!f) {
            status = 1;
            continue;
        }
        char digest[160] = "";
        int ok = hash_stream(f, algorithm, digest, sizeof(digest));
        cmd_close(f);
        if (!ok) {
            cmd_error(tool, "%s: cannot compute the checksum", name ? name : "-");
            status = 1;
            continue;
        }
        if (args_has(&args, '1')) {
            const char *label = algorithm == 1 ? "MD5" : algorithm == 2 ? "SHA1" : algorithm == 3 ? "SHA256" : "SHA512";
            printf("%s (%s) = %s\n", label, name ? name : "-", digest);
        } else {
            printf("%s %c%s%c", digest, args_has(&args, 'b') ? '*' : ' ', name ? name : "-", args_has(&args, 'z') ? '\0' : '\n');
        }
    }
    args_free(&args);
    return status;
}

static int cmd_md5sum(int argc, char **argv) {
    return hash_command(argc, argv, 1);
}

static int cmd_sha1sum(int argc, char **argv) {
    return hash_command(argc, argv, 2);
}

static int cmd_sha256sum(int argc, char **argv) {
    return hash_command(argc, argv, 3);
}

static int cmd_sha512sum(int argc, char **argv) {
    return hash_command(argc, argv, 4);
}

static int cmd_wget(int argc, char **argv) {
    static const OptSpec specs[] = {{'O', "output-document", 1}, {'q', "quiet", 0}, {'o', "output-file", 1},
                                    {'c', "continue", 0}, {'P', "directory-prefix", 1}, {'N', "timestamping", 0},
                                    {'1', "no-check-certificate", 0}, {'2', "no-verbose", 0}, {0, NULL, 0}};
    Args args;
    int early = parse_or_exit(argc, argv, specs, "wget", &args, 1);
    if (early >= 0) return early;
    if (args.operand_count == 0) {
        fprintf(stderr, "wget: missing URL\nUsage: wget [OPTION]... [URL]...\n");
        args_free(&args);
        return 1;
    }
    int status = 0;
    for (int i = 0; i < args.operand_count; i++) {
        const char *url = args.operands[i];
        char derived[PATH_BUF];
        const char *output = args_value(&args, 'O');
        if (!output) {
            const char *slash = strrchr(url, '/');
            snprintf(derived, sizeof(derived), "%s%s%s", args_has(&args, 'P') ? args_value(&args, 'P') : "",
                     args_has(&args, 'P') ? "/" : "", slash && slash[1] ? slash + 1 : "index.html");
            output = derived;
        }
        if (!args_has(&args, 'q')) fprintf(stderr, "%s -> %s\n", url, output);
        if (!http_download(url, output)) {
            cmd_error("wget", "could not fetch %s", url);
            status = 1;
        }
    }
    args_free(&args);
    return status;
}

const Command SYSTEM_COMMANDS[] = {
    {"arch", cmd_arch, ""},
    {"date", cmd_date, "[-u] [-d STRING] [-r FILE] [-R] [-I[FMT]] [+FORMAT]"},
    {"env", cmd_env, "[-i] [-u NAME] [-C DIR] [NAME=VALUE]... [COMMAND [ARG]...]"},
    {"expr", cmd_expr, "EXPRESSION"},
    {"groups", cmd_groups, ""},
    {"hostname", cmd_hostname, "[-sfd]"},
    {"id", cmd_id, "[-ugGnrz]"},
    {"kill", cmd_kill, "[-s SIGNAL | -SIGNAL] PID... | -l [SIGNAL]"},
    {"md5sum", cmd_md5sum, "[-bcz] [--tag] [FILE]..."},
    {"nproc", cmd_nproc, "[--all] [--ignore=N]"},
    {"open", cmd_open_target, "[TARGET]"},
    {"pkill", cmd_pkill, "[-fxeci] PATTERN"},
    {"printenv", cmd_printenv, "[-0] [VARIABLE]..."},
    {"ps", cmd_ps, ""},
    {"seq", cmd_seq, "[-w] [-s SEP] [-f FORMAT] [FIRST [INCREMENT]] LAST"},
    {"sha1sum", cmd_sha1sum, "[-bcz] [--tag] [FILE]..."},
    {"sha256sum", cmd_sha256sum, "[-bcz] [--tag] [FILE]..."},
    {"sha512sum", cmd_sha512sum, "[-bcz] [--tag] [FILE]..."},
    {"sleep", cmd_sleep, "NUMBER[smhd]..."},
    {"uname", cmd_uname, "[-asnrvmpio]"},
    {"wget", cmd_wget, "[-q] [-O FILE] [-P DIR] URL..."},
    {"whoami", cmd_whoami, ""},
    {"xargs", cmd_xargs, "[-0rt] [-d DELIM] [-n N] [-L N] [-I REPLACE] [-a FILE] [COMMAND [ARG]...]"},
};

const size_t SYSTEM_COMMAND_COUNT = END(SYSTEM_COMMANDS);
