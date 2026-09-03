/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "foreign.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"

#include "exec.h"
#include "shell.h"
#include "style.h"
#include "table.h"
#include "util.h"
#include "vars.h"

#ifdef _WIN32

static const char *POWERSHELL_VERBS[] = {
    "Add",       "Approve",  "Clear",     "Compare", "Complete", "Compress", "Connect",
    "Convert",   "ConvertFrom", "ConvertTo", "Copy", "Debug",    "Disable",  "Disconnect",
    "Dismount",  "Enable",   "Enter",     "Exit",    "Expand",   "Export",   "Find",
    "Format",    "Get",      "Grant",     "Group",   "Import",   "Install",  "Invoke",
    "Join",      "Limit",    "Lock",      "Measure", "Merge",    "Mount",    "Move",
    "New",       "Open",     "Optimize",  "Out",     "Ping",     "Pop",      "Publish",
    "Push",      "Read",     "Receive",   "Register", "Remove",  "Rename",   "Repair",
    "Request",   "Reset",    "Resolve",   "Restart", "Restore",  "Resume",   "Revoke",
    "Save",      "Search",   "Select",    "Send",    "Set",      "Show",     "Sort",
    "Split",     "Start",    "Step",      "Stop",    "Submit",   "Suspend",  "Sync",
    "Test",      "Trace",    "Unblock",   "Uninstall", "Unlock", "Unpublish", "Unregister",
    "Update",    "Use",      "Wait",      "Watch",   "Where",    "Write",    NULL,
};

static const char *POWERSHELL_ALIASES[] = {
    "gci",  "gc",   "gcm",  "gm",   "gp",   "gps",  "gsv",  "sls",  "iwr",  "irm",
    "ise",  "ni",   "ri",   "si",   "sp",   "spps", "sasv", "epcsv", "ipcsv", "gu",
    "measure", "select", "sort", "where", "foreach", "ogv", "oh",  "fl",   "ft",   "fw",
    NULL,
};

static const char *CMD_BUILTINS[] = {
    "assoc", "cls",  "dir",   "del",   "erase", "ftype", "md",    "rd",    "ren",
    "rename", "start", "title", "tree", "ver",   "vol",   "pause", "chcp",  "color",
    "copy",  "move", "call",  "prompt", "setx", "subst", "attrib", "doskey", "mklink",
    NULL,
};

static StrList cmdlet_cache;
static Table cmdlet_table;
static int cmdlet_cache_loaded = 0;

static int list_contains(const char **list, const char *name, int case_sensitive) {
    for (int i = 0; list[i]; i++) {
        if (case_sensitive ? strcmp(list[i], name) == 0 : str_ieq(list[i], name)) return 1;
    }
    return 0;
}

static char *first_word(const char *line, size_t *length_out) {
    while (*line == ' ' || *line == '\t') line++;
    const char *end = line;
    while (*end && !isspace((unsigned char)*end)) end++;
    *length_out = (size_t)(end - line);
    return (char *)line;
}

static int looks_like_cmdlet(const char *word) {
    const char *dash = strchr(word, '-');
    if (!dash || dash == word) return 0;
    if (strchr(dash + 1, '-')) return 0;

    char verb[64];
    size_t length = (size_t)(dash - word);
    if (length == 0 || length >= sizeof(verb)) return 0;
    memcpy(verb, word, length);
    verb[length] = '\0';

    if (!isupper((unsigned char)verb[0])) return 0;
    if (!isupper((unsigned char)dash[1])) return 0;
    return list_contains(POWERSHELL_VERBS, verb, 1);
}

static int run_through(const char *program, const char *arguments) {
    StrBuf command_line;
    sb_init(&command_line);
    sb_printf(&command_line, "%s %s", program, arguments);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    fflush(stdout);
    fflush(stderr);

    if (!CreateProcessA(NULL, command_line.data, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        shell_error("cannot start %s", program);
        sb_free(&command_line);
        return 127;
    }
    sb_free(&command_line);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
}

int foreign_run_powershell(const char *command) {
    char directory[PATH_BUF];
    char script[PATH_BUF];
    if (!GetTempPathA(sizeof(directory), directory)) return 1;
    if (!GetTempFileNameA(directory, "frps", 0, script)) return 1;

    char final[PATH_BUF];
    snprintf(final, sizeof(final), "%s.ps1", script);
    DeleteFileA(script);

    FILE *f = fopen(final, "wb");
    if (!f) return 1;
    fputs(command, f);
    fputs("\r\n", f);
    fclose(f);

    StrBuf arguments;
    sb_init(&arguments);
    sb_printf(&arguments, "-NoLogo -NoProfile -ExecutionPolicy Bypass -File \"%s\"", final);
    int status = run_through("powershell.exe", arguments.data);
    sb_free(&arguments);

    DeleteFileA(final);
    return status;
}

int foreign_run_cmd(const char *command) {
    StrBuf arguments;
    sb_init(&arguments);
    sb_printf(&arguments, "/d /c %s", command);
    int status = run_through("cmd.exe", arguments.data);
    sb_free(&arguments);
    return status;
}

int foreign_route(const char *line, int *status) {
    if (!option_enabled("FRESH_FOREIGN", 1)) return 0;
    if (strchr(line, '\n')) return 0;

    size_t length = 0;
    char *start = first_word(line, &length);
    if (length == 0 || length >= 128) return 0;

    char word[128];
    memcpy(word, start, length);
    word[length] = '\0';

    int cmdlet = looks_like_cmdlet(word) || list_contains(POWERSHELL_ALIASES, word, 0);
    int builtin = !cmdlet && list_contains(CMD_BUILTINS, word, 0);
    if (!cmdlet && !builtin) return 0;

    if (builtin_lookup(word) || function_defined(word) || alias_get(word)) return 0;

    char resolved[PATH_BUF];
    if (resolve_command(word, resolved, sizeof(resolved))) return 0;

    *status = cmdlet ? foreign_run_powershell(line) : foreign_run_cmd(line);
    return 1;
}

static char *cmdlet_cache_path(void) {
    const char *home = var_get("FRESH_HOME");
    if (home && *home) return path_join(home, "cmdlets.cache");
    char *base = config_path(".fresh");
    char *file = path_join(base, "cmdlets.cache");
    free(base);
    return file;
}

static void load_cmdlet_cache(void) {
    if (cmdlet_cache_loaded) return;
    cmdlet_cache_loaded = 1;
    sl_init(&cmdlet_cache);
    table_init(&cmdlet_table, 1024, 1, NULL);

    char *path = cmdlet_cache_path();
    FILE *f = fopen(path, "r");

    if (!f) {
        StrBuf out;
        sb_init(&out);
        capture_command(
            "powershell.exe -NoLogo -NoProfile -Command "
            "\"Get-Command -CommandType Cmdlet,Function,Alias | "
            "Select-Object -ExpandProperty Name\" 2> nul",
            &out);

        if (out.len > 0) {
            FILE *write = fopen(path, "wb");
            if (write) {
                fputs(out.data, write);
                fclose(write);
            }
        }
        sb_free(&out);
        f = fopen(path, "r");
    }

    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = '\0';
            if (*line) {
                sl_push_copy(&cmdlet_cache, line);
                table_put(&cmdlet_table, line, (void *)1);
            }
        }
        fclose(f);
    }
    free(path);
}

void foreign_forget(void) {
    if (!cmdlet_cache_loaded) return;
    sl_free(&cmdlet_cache);
    table_free(&cmdlet_table);
    cmdlet_cache_loaded = 0;

    char *path = cmdlet_cache_path();
    DeleteFileA(path);
    free(path);
}

int foreign_known(const char *name) {
    if (list_contains(CMD_BUILTINS, name, 0)) return 1;
    if (looks_like_cmdlet(name)) return 1;
    if (!cmdlet_cache_loaded) return 0;
    return table_get(&cmdlet_table, name) != NULL;
}

void foreign_names(StrList *out) {
    for (int i = 0; CMD_BUILTINS[i]; i++) sl_push_copy(out, CMD_BUILTINS[i]);

    load_cmdlet_cache();
    for (size_t i = 0; i < cmdlet_cache.len; i++) sl_push_copy(out, cmdlet_cache.items[i]);
}

void foreign_complete(const char *prefix, size_t length, StrList *out) {
    for (int i = 0; CMD_BUILTINS[i]; i++) {
        if (_strnicmp(CMD_BUILTINS[i], prefix, length) == 0) sl_push_copy(out, CMD_BUILTINS[i]);
    }
    load_cmdlet_cache();
    table_collect_prefix(&cmdlet_table, prefix, length, out);
}

int foreign_name_prefix(const char *prefix, size_t length) {
    for (int i = 0; CMD_BUILTINS[i]; i++) {
        if (_strnicmp(CMD_BUILTINS[i], prefix, length) == 0) return 1;
    }
    if (!cmdlet_cache_loaded) return 0;
    for (size_t i = 0; i < cmdlet_cache.len; i++) {
        if (_strnicmp(cmdlet_cache.items[i], prefix, length) == 0) return 1;
    }
    return 0;
}

int builtin_ps1(int argc, char **argv) {
    if (argc < 2) return foreign_run_powershell("");

    StrBuf command;
    sb_init(&command);
    for (int i = 1; i < argc; i++) {
        if (i > 1) sb_putc(&command, ' ');
        sb_puts(&command, argv[i]);
    }
    int status = foreign_run_powershell(command.data);
    sb_free(&command);
    return status;
}

int builtin_cmd(int argc, char **argv) {
    if (argc < 2) return run_through("cmd.exe", "/d /k");

    int index = 1;
    int keep_open = 0;
    if (str_ieq(argv[1], "/c") || str_ieq(argv[1], "-c")) index = 2;
    else if (str_ieq(argv[1], "/k")) {
        keep_open = 1;
        index = 2;
    }
    if (index >= argc) return run_through("cmd.exe", keep_open ? "/d /k" : "/d /c");

    StrBuf command;
    sb_init(&command);
    for (int i = index; i < argc; i++) {
        if (i > index) sb_putc(&command, ' ');
        sb_puts(&command, argv[i]);
    }

    int status;
    if (keep_open) {
        StrBuf arguments;
        sb_init(&arguments);
        sb_printf(&arguments, "/d /k %s", command.data);
        status = run_through("cmd.exe", arguments.data);
        sb_free(&arguments);
    } else {
        status = foreign_run_cmd(command.data);
    }
    sb_free(&command);
    return status;
}

#else

static int not_here(const char *what) {
    shell_error("%s is not available on this platform", what);
    return 127;
}

int foreign_run_powershell(const char *command) {
    (void)command;
    return not_here("PowerShell");
}

int foreign_run_cmd(const char *command) {
    (void)command;
    return not_here("cmd.exe");
}

int foreign_route(const char *line, int *status) {
    (void)line;
    (void)status;
    return 0;
}

void foreign_forget(void) {
}

int foreign_known(const char *name) {
    (void)name;
    return 0;
}

void foreign_names(StrList *out) {
    (void)out;
}

void foreign_complete(const char *prefix, size_t length, StrList *out) {
    (void)prefix;
    (void)length;
    (void)out;
}

int foreign_name_prefix(const char *prefix, size_t length) {
    (void)prefix;
    (void)length;
    return 0;
}

int builtin_ps1(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return not_here("PowerShell");
}

int builtin_cmd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return not_here("cmd.exe");
}

#endif
