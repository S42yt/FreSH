/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include <wininet.h>

#include "config.h"
#include "shell.h"
#include "style.h"
#include "util.h"
#include "vars.h"

#define RELEASE_API "https://api.github.com/repos/S42yt/FreSH/releases/latest"
#define RELEASE_PAGE "https://github.com/S42yt/FreSH/releases"
#define DOWNLOAD_FORMAT "https://github.com/S42yt/FreSH/releases/download/v%s/FreSH-Setup.exe"
#define USER_AGENT "FreSH-updater"

static int http_fetch(const char *url, StrBuf *body, const char *save_to) {
    HINTERNET session = InternetOpenA(USER_AGENT, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!session) return 0;

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE |
                  INTERNET_FLAG_NO_UI | INTERNET_FLAG_SECURE;
    HINTERNET request = InternetOpenUrlA(session, url, NULL, 0, flags, 0);
    if (!request) {
        InternetCloseHandle(session);
        return 0;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (HttpQueryInfoA(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status,
                       &status_size, NULL) &&
        status >= 400) {
        InternetCloseHandle(request);
        InternetCloseHandle(session);
        return 0;
    }

    FILE *file = NULL;
    if (save_to) {
        file = fopen(save_to, "wb");
        if (!file) {
            InternetCloseHandle(request);
            InternetCloseHandle(session);
            return 0;
        }
    }

    char buffer[8192];
    DWORD read = 0;
    while (InternetReadFile(request, buffer, sizeof(buffer), &read) && read > 0) {
        if (file) fwrite(buffer, 1, read, file);
        else sb_putn(body, buffer, read);
    }

    if (file) fclose(file);
    InternetCloseHandle(request);
    InternetCloseHandle(session);
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

static void split_version(const char *text, int parts[3]) {
    parts[0] = parts[1] = parts[2] = 0;
    sscanf(text, "%d.%d.%d", &parts[0], &parts[1], &parts[2]);
}

static int version_newer(const char *candidate, const char *current) {
    int a[3];
    int b[3];
    split_version(candidate, a);
    split_version(current, b);

    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
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

static int command_update(int argc, char **argv) {
    int check_only = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--check") == 0 || strcmp(argv[i], "-c") == 0) check_only = 1;
    }

    printf("  %schecking for updates%s\n", style(S_DIM), style(S_RESET));

    char latest[64];
    if (!read_latest_version(latest, sizeof(latest))) {
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
        shell_error("fresh: usage: fresh [version | doctor | update [--check]]");
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
    snprintf(info[0], sizeof(info[0]), "%sFreSH%s %s%s%s", style(S_HEADING), style(S_RESET),
             style(S_DIM), FRESH_VERSION, style(S_RESET));
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
    printf("  %sfresh doctor%s checks your configuration\n", style(S_DIM), style(S_RESET));
    printf("\n");

    free(rc);
    return 0;
}
