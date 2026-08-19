/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "update.h"

#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include <wininet.h>

#include "config.h"
#include "keys.h"
#include "rustcore.h"
#include "shell.h"
#include "style.h"
#include "term.h"
#include "util.h"
#include "vars.h"

#define RELEASE_API "https://api.github.com/repos/S42yt/FreSH/releases/latest"
#define RELEASE_API_LIST "https://api.github.com/repos/S42yt/FreSH/releases?per_page=100"
#define RELEASE_PAGE "https://github.com/S42yt/FreSH/releases"
#define DOWNLOAD_FORMAT "https://github.com/S42yt/FreSH/releases/download/v%s/FreSH-Setup.exe"
#define USER_AGENT "FreSH-updater"

typedef HINTERNET(WINAPI *OpenFn)(LPCSTR, DWORD, LPCSTR, LPCSTR, DWORD);
typedef HINTERNET(WINAPI *OpenUrlFn)(HINTERNET, LPCSTR, LPCSTR, DWORD, DWORD, DWORD_PTR);
typedef BOOL(WINAPI *QueryFn)(HINTERNET, DWORD, LPVOID, LPDWORD, LPDWORD);
typedef BOOL(WINAPI *ReadFn)(HINTERNET, LPVOID, DWORD, LPDWORD);
typedef BOOL(WINAPI *CloseFn)(HINTERNET);

static OpenFn internet_open;
static OpenUrlFn internet_open_url;
static QueryFn internet_query;
static ReadFn internet_read;
static CloseFn internet_close;

static int wininet_ready(void) {
    static HMODULE library = NULL;
    if (library) return internet_open != NULL;

    library = LoadLibraryA("wininet.dll");
    if (!library) return 0;

    internet_open = (OpenFn)(void *)GetProcAddress(library, "InternetOpenA");
    internet_open_url = (OpenUrlFn)(void *)GetProcAddress(library, "InternetOpenUrlA");
    internet_query = (QueryFn)(void *)GetProcAddress(library, "HttpQueryInfoA");
    internet_read = (ReadFn)(void *)GetProcAddress(library, "InternetReadFile");
    internet_close = (CloseFn)(void *)GetProcAddress(library, "InternetCloseHandle");

    return internet_open && internet_open_url && internet_query && internet_read && internet_close;
}

static int http_fetch(const char *url, StrBuf *body, const char *save_to) {
    if (!wininet_ready()) {
        shell_error("update: this build cannot reach the network");
        return 0;
    }

    HINTERNET session = internet_open(USER_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!session) return 0;

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                  INTERNET_FLAG_NO_UI | INTERNET_FLAG_SECURE;
    HINTERNET request = internet_open_url(session, url, NULL, 0, flags, 0);
    if (!request) {
        internet_close(session);
        return 0;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (internet_query(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status,
                       &status_size, NULL) &&
        status >= 400) {
        internet_close(request);
        internet_close(session);
        return 0;
    }

    FILE *file = NULL;
    if (save_to) {
        file = fopen(save_to, "wb");
        if (!file) {
            internet_close(request);
            internet_close(session);
            return 0;
        }
    }

    char buffer[8192];
    DWORD read = 0;
    while (internet_read(request, buffer, sizeof(buffer), &read) && read > 0) {
        if (file) fwrite(buffer, 1, read, file);
        else sb_putn(body, buffer, read);
    }

    if (file) fclose(file);
    internet_close(request);
    internet_close(session);
    return 1;
}

int http_download(const char *url, const char *path) {
    return http_fetch(url, NULL, path);
}

static int read_latest_version(char *out, size_t out_size) {
    StrBuf body;
    sb_init(&body);
    if (!http_fetch(RELEASE_API, &body, NULL)) {
        sb_free(&body);
        return 0;
    }

    char *key = strstr(body.data, "\"tag_name\"");
    if (!key) {
        sb_free(&body);
        return 0;
    }
    char *open_quote = strchr(key + 10, '"');
    if (!open_quote) {
        sb_free(&body);
        return 0;
    }
    char *close_quote = strchr(open_quote + 1, '"');
    if (!close_quote) {
        sb_free(&body);
        return 0;
    }

    char *tag = open_quote + 1;
    if (*tag == 'v') tag++;
    size_t length = (size_t)(close_quote - tag);
    if (length >= out_size) length = out_size - 1;
    memcpy(out, tag, length);
    out[length] = '\0';

    sb_free(&body);
    return out[0] != '\0';
}

static int split_version(const char *text, int parts[3], int *early) {
    parts[0] = parts[1] = parts[2] = 0;
    sscanf(text, "%d.%d.%d", &parts[0], &parts[1], &parts[2]);

    const char *dash = strchr(text, '-');
    *early = dash != NULL;
    if (!dash) return 0;

    const char *last = strrchr(text, '-');
    return atoi(last + 1);
}

static int version_newer(const char *candidate, const char *current) {
    int a[3];
    int b[3];
    int candidate_early = 0;
    int current_early = 0;
    int candidate_step = split_version(candidate, a, &candidate_early);
    int current_step = split_version(current, b, &current_early);

    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    if (candidate_early != current_early) return current_early;
    if (candidate_early) return candidate_step > current_step;
    return 0;
}

static int install_update(const char *version) {
    char url[512];
    snprintf(url, sizeof(url), DOWNLOAD_FORMAT, version);

    char directory[PATH_BUF];
    if (!GetTempPathA(sizeof(directory), directory)) return 1;

    char setup[PATH_BUF];
    snprintf(setup, sizeof(setup), "%sFreSH-Setup-%s.exe", directory, version);

    printf("  %sdownloading %s%s\n", style(S_DIM), url, style(S_RESET));
    if (!http_fetch(url, NULL, setup)) {
        shell_error("update: could not download %s", url);
        return 1;
    }

    char command[PATH_BUF * 2];
    snprintf(command, sizeof(command),
             "cmd.exe /d /c timeout /t 2 /nobreak > nul & \"%s\" /silent /user", setup);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);

    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si,
                        &pi)) {
        shell_error("update: could not start the installer");
        return 1;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    printf("  %s%s installing %s, this shell closes so the file can be replaced%s\n",
           style(S_ACCENT), "\xe2\x86\x91", version, style(S_RESET));
    shell.running = 0;
    return 0;
}

typedef struct {
    char tag[64];
    char base[32];
    char kind[24];
    int step;
} Early;

static void early_split(const char *tag, Early *out) {
    snprintf(out->tag, sizeof(out->tag), "%s", tag);
    out->kind[0] = '\0';
    out->step = 0;

    const char *dash = strchr(tag, '-');
    size_t length = dash ? (size_t)(dash - tag) : strlen(tag);
    if (length >= sizeof(out->base)) length = sizeof(out->base) - 1;
    memcpy(out->base, tag, length);
    out->base[length] = '\0';
    if (!dash) return;

    const char *word = dash + 1;
    const char *end = strchr(word, '-');
    size_t kind_length = end ? (size_t)(end - word) : strlen(word);
    if (kind_length >= sizeof(out->kind)) kind_length = sizeof(out->kind) - 1;
    memcpy(out->kind, word, kind_length);
    out->kind[kind_length] = '\0';
    if (end) out->step = atoi(end + 1);
}

static int is_experiment(const Early *release) {
    return release->kind[0] && strcmp(release->kind, "prerelease") != 0;
}

static int quoted_field(const char *key, char *out, size_t out_size) {
    const char *open_quote = strchr(key + strlen("\"tag_name\""), '"');
    if (!open_quote) return 0;
    const char *close_quote = strchr(open_quote + 1, '"');
    if (!close_quote) return 0;

    const char *text = open_quote + 1;
    size_t length = (size_t)(close_quote - text);
    if (length >= out_size) length = out_size - 1;
    memcpy(out, text, length);
    out[length] = '\0';
    return (int)(close_quote - key) + 1;
}

static int marked_prerelease(const char *object, const char *limit) {
    const char *flag = strstr(object, "\"prerelease\"");
    if (!flag || (limit && flag > limit)) return 0;

    const char *value = flag + strlen("\"prerelease\"");
    while (*value == ':' || *value == ' ') value++;
    return strncmp(value, "true", 4) == 0;
}

static int read_releases(Early *out, int max, int keep_stable, int one_per_version) {
    StrBuf body;
    sb_init(&body);
    const char *fixture = var_get("FRESH_RELEASES_JSON");
    if (fixture && *fixture) {
        FILE *file = fopen(fixture, "rb");
        if (!file) {
            sb_free(&body);
            return -1;
        }
        char chunk[4096];
        size_t read;
        while ((read = fread(chunk, 1, sizeof(chunk), file)) > 0) sb_putn(&body, chunk, read);
        fclose(file);
    } else if (!http_fetch(RELEASE_API_LIST, &body, NULL)) {
        sb_free(&body);
        return -1;
    }

    if (!body.data) {
        sb_free(&body);
        return 0;
    }

    int count = 0;
    const char *scan = body.data;
    while (count < max) {
        const char *key = strstr(scan, "\"tag_name\"");
        if (!key) break;

        char tag[64];
        int used = quoted_field(key, tag, sizeof(tag));
        if (!used) break;
        scan = key + used;

        const char *next = strstr(scan, "\"tag_name\"");
        if (!marked_prerelease(scan, next) && !keep_stable) continue;

        Early found;
        const char *name = tag[0] == 'v' ? tag + 1 : tag;
        early_split(name, &found);

        int seen = 0;
        if (one_per_version) {
            for (int i = 0; i < count; i++) {
                if (strcmp(out[i].base, found.base) != 0) continue;
                if (strcmp(out[i].kind, found.kind) != 0) continue;
                seen = 1;
                if (found.step > out[i].step) out[i] = found;
                break;
            }
        }
        if (!seen) out[count++] = found;
    }

    sb_free(&body);
    return count;
}

#define SELECTOR_MAX 100
#define SELECTOR_PAGE 10

static void selector_line(const Early *release, int marked) {
    const char *note = "";
    if (strcmp(release->tag, FRESH_VERSION) == 0) note = "  installed";
    else if (!version_newer(release->tag, FRESH_VERSION)) note = "  older than yours";

    const char *marker = marked ? "\xe2\x9d\xaf" : " ";
    const char *paint = marked ? style(S_ACCENT) : "";
    const char *fade = marked ? style(S_RESET) : "";

    if (release->kind[0]) {
        printf("  %s%s %s %s %d%s%s%s%s\n", paint, marker, release->base, release->kind,
               release->step, fade, style(S_DIM), note, style(S_RESET));
    } else {
        printf("  %s%s %s release%s%s%s%s\n", paint, marker, release->base, fade, style(S_DIM),
               note, style(S_RESET));
    }
}

static int selector_interactive(const Early *releases, int count) {
    int cursor = 0;
    int pages = (count + SELECTOR_PAGE - 1) / SELECTOR_PAGE;
    int drawn = 0;

    for (;;) {
        int page = cursor / SELECTOR_PAGE;
        int start = page * SELECTOR_PAGE;
        int end = start + SELECTOR_PAGE;
        if (end > count) end = count;

        if (drawn) printf("\x1b[%dA", drawn);
        drawn = 0;

        printf("\x1b[K  %s%d releases, page %d of %d%s\n", style(S_DIM), count, page + 1, pages,
               style(S_RESET));
        drawn++;
        for (int i = start; i < end; i++) {
            printf("\x1b[K");
            selector_line(&releases[i], i == cursor);
            drawn++;
        }
        printf("\x1b[K  %s\xe2\x86\x91\xe2\x86\x93 move   \xe2\x86\x90\xe2\x86\x92 page   "
               "enter install   esc leave%s\n",
               style(S_DIM), style(S_RESET));
        drawn++;
        fflush(stdout);

        int key = term_read_key();
        if (key == KEY_UP && cursor > 0) cursor--;
        else if (key == KEY_DOWN && cursor + 1 < count) cursor++;
        else if ((key == KEY_LEFT || key == KEY_PAGE_UP) && page > 0)
            cursor = (page - 1) * SELECTOR_PAGE;
        else if ((key == KEY_RIGHT || key == KEY_PAGE_DOWN) && page + 1 < pages)
            cursor = (page + 1) * SELECTOR_PAGE;
        else if (key == KEY_ENTER) return cursor;
        else if (key == KEY_ESC || key == 'q' || key == 3) return -1;
    }
}

static int command_selector(int check_only) {
    Early releases[SELECTOR_MAX];
    printf("  %sasking github for the releases%s\n", style(S_DIM), style(S_RESET));

    int count = read_releases(releases, SELECTOR_MAX, 1, 0);
    if (count < 0) {
        shell_error("update: could not reach github, see %s", RELEASE_PAGE);
        return 1;
    }
    if (count == 0) {
        printf("  %s\xe2\x9c\x93%s there is nothing to offer right now\n", style(S_ACCENT),
               style(S_RESET));
        return 0;
    }

    if (!check_only && shell.interactive && _isatty(_fileno(stdin)) && _isatty(_fileno(stdout))) {
        printf("\n");
        int picked = selector_interactive(releases, count);
        printf("\n");
        if (picked < 0) return 0;
        return install_update(releases[picked].tag);
    }

    printf("\n");
    for (int i = 0; i < count; i++) {
        printf("  %s%2d%s", style(S_ACCENT), i + 1, style(S_RESET));
        selector_line(&releases[i], 0);
    }
    printf("\n");

    if (check_only) {
        printf("  %srun fresh update --selector to install one%s\n", style(S_DIM), style(S_RESET));
        return 0;
    }

    printf("  %swhich one? 1-%d, or enter to cancel:%s ", style(S_LABEL), count, style(S_RESET));
    fflush(stdout);

    StrBuf answer;
    sb_init(&answer);
    if (!read_line_fd(0, &answer)) {
        sb_free(&answer);
        printf("\n");
        return 0;
    }

    char picked[64];
    const char *text = answer.data;
    while (*text == ' ' || *text == '\t') text++;
    snprintf(picked, sizeof(picked), "%s", text);
    sb_free(&answer);

    int choice = atoi(picked);
    if (choice < 1 || choice > count) {
        if (!picked[0]) return 0;
        shell_error("update: %s is not one of the choices", picked);
        return 1;
    }
    return install_update(releases[choice - 1].tag);
}

static int read_offered_version(char *out, size_t out_size) {
    Early releases[32];
    int count = read_releases(releases, 32, 1, 1);
    if (count < 0) return 0;

    for (int i = 0; i < count; i++) {
        if (is_experiment(&releases[i])) continue;
        snprintf(out, out_size, "%s", releases[i].tag);
        return 1;
    }
    return 0;
}

static int command_update(int argc, char **argv) {
    int check_only = 0;
    int allow_early = 0;
    int selector = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--check") == 0 || strcmp(argv[i], "-c") == 0) check_only = 1;
        if (strcmp(argv[i], "--pre") == 0 || strcmp(argv[i], "--prerelease") == 0) allow_early = 1;
        if (strcmp(argv[i], "--selector") == 0 || strcmp(argv[i], "--pre-selector") == 0)
            selector = 1;
    }

    if (selector) return command_selector(check_only);

    printf("  %schecking for %s%s\n", style(S_DIM), allow_early ? "any release, including prereleases"
                                                                : "updates",
           style(S_RESET));

    char latest[64];
    int found = allow_early ? read_offered_version(latest, sizeof(latest))
                            : read_latest_version(latest, sizeof(latest));
    if (!found) {
        shell_error("update: could not reach github, see %s", RELEASE_PAGE);
        return 1;
    }

    if (!version_newer(latest, FRESH_VERSION)) {
        printf("  %s\xe2\x9c\x93%s %s is the newest release\n", style(S_ACCENT), style(S_RESET),
               FRESH_VERSION);
        return 0;
    }

    printf("  %s\xe2\x86\x91%s %s is available, you have %s\n", style(S_WARN), style(S_RESET),
           latest, FRESH_VERSION);
    if (check_only) {
        printf("  %srun fresh update to install it%s\n", style(S_DIM), style(S_RESET));
        return 0;
    }
    return install_update(latest);
}

int builtin_fresh(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "update") == 0) return command_update(argc, argv);

    if (argc > 1 && (strcmp(argv[1], "doctor") == 0 || strcmp(argv[1], "check") == 0)) {
        StrList problems;
        sl_init(&problems);
        int found = config_check(&problems);
        config_report(found, &problems);
        sl_free(&problems);
        return found ? 1 : 0;
    }

    if (argc > 1 && strcmp(argv[1], "version") == 0) {
        printf("%s\n", FRESH_VERSION);
        return 0;
    }

    if (argc > 1) {
        shell_error(
            "fresh: usage: fresh [version | doctor | update [--check] [--pre] [--selector]]");
        return 2;
    }

    char exe[PATH_BUF] = "";
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    path_to_slashes(exe);

    char *rc = config_path(".freshrc");
    path_to_slashes(rc);

    const char *art[5] = {
        "     __",
        "    /\\ \\",
        "   /  \\ \\",
        "  / /\\ \\ \\",
        " /_/  \\_\\_\\",
    };

    char info[5][PATH_BUF + 64];
    snprintf(info[0], sizeof(info[0]), "%sFreSH%s %s%s, %s core%s", style(S_HEADING), style(S_RESET),
             style(S_DIM), FRESH_VERSION, FRESH_CORE, style(S_RESET));
    snprintf(info[1], sizeof(info[1]), "%s%-8s%s %s", style(S_LABEL), "binary", style(S_RESET), exe);
    snprintf(info[2], sizeof(info[2]), "%s%-8s%s %s", style(S_LABEL), "config", style(S_RESET), rc);
    snprintf(info[3], sizeof(info[3]), "%s%-8s%s %s", style(S_LABEL), "theme", style(S_RESET),
             var_get("FRESH_THEME") ? var_get("FRESH_THEME") : "fresh");
    snprintf(info[4], sizeof(info[4]), "%s%-8s%s %s", style(S_LABEL), "plugins", style(S_RESET),
             var_get("FRESH_PLUGINS") ? var_get("FRESH_PLUGINS") : "none");

    printf("\n");
    for (int i = 0; i < 5; i++)
        printf("  %s%-12s%s   %s\n", style(S_ACCENT), art[i], style(S_RESET), info[i]);
    printf("\n  %sfresh update%s checks github and installs the newest release\n", style(S_DIM),
           style(S_RESET));
    printf("  %sfresh update --pre%s takes prereleases too\n", style(S_DIM), style(S_RESET));
    printf("  %sfresh update --selector%s browses every release, arrows move, enter installs\n",
           style(S_DIM), style(S_RESET));
    printf("  %sfresh doctor%s checks your configuration\n", style(S_DIM), style(S_RESET));
    printf("\n");

    free(rc);
    return 0;
}
