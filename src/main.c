/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include <io.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "exec.h"
#include "history.h"
#include "line.h"
#include "parser.h"
#include "shell.h"
#include "style.h"
#include "term.h"
#include "theme.h"
#include "util.h"
#include "vars.h"

ShellState shell;

static const char *DEFAULT_RC =
    "# FreSH configuration\n"
    "# This file is a FreSH script, sourced every time the shell starts.\n"
    "# Anything you can type at the prompt works here.\n"
    "\n"
    "# ---- startup ----------------------------------------------------------\n"
    "\n"
    "# show the banner when an interactive shell starts\n"
    "FRESH_BANNER=1\n"
    "# replace the banner with your own line, leave empty for the default one\n"
    "FRESH_BANNER_TEXT=\"\"\n"
    "# where ~ points and where cd with no argument goes\n"
    "# HOME=\"C:/Users/me\"\n"
    "# uncomment to start every shell in one place\n"
    "# cd ~\n"
    "\n"
    "# ---- theme and plugins ------------------------------------------------\n"
    "\n"
    "# themes live in ~/.fresh/themes, run 'theme' to see them\n"
    "# fresh minimal classic powerline lambda full, or none to keep your own\n"
    "FRESH_THEME=fresh\n"
    "# plugins live in ~/.fresh/plugins, run 'plugin' to see them\n"
    "FRESH_PLUGINS=\"git dirs sys\"\n"
    "\n"
    "# ---- prompt -----------------------------------------------------------\n"
    "\n"
    "# The theme sets FRESH_PROMPT and FRESH_RPROMPT. Override them here to\n"
    "# build your own, or set FRESH_THEME=none and write it from scratch.\n"
    "#   %n user   %m host   %~ short path   %d full path\n"
    "#   %g git segment   %b branch   %t time   %D date\n"
    "#   %? exit code   %e exit code only when it failed   %# prompt character\n"
    "#   %F{green} colour on   %f colour off   %K{blue} background   %k off\n"
    "#   %S bold on   %s bold off   \\n new line\n"
    "# FRESH_PROMPT='%F{cyan}%~%f%g\\n%# '\n"
    "# FRESH_RPROMPT='%t'\n"
    "\n"
    "# character on the second prompt line\n"
    "FRESH_PROMPT_CHAR=\"\xce\xbb\"\n"
    "# how many trailing path components to show\n"
    "FRESH_PATH_DEPTH=3\n"
    "# user name on the first prompt line\n"
    "FRESH_SHOW_USER=1\n"
    "# git branch, and the ! marker when the tree is dirty\n"
    "FRESH_SHOW_GIT=1\n"
    "FRESH_SHOW_GIT_DIRTY=1\n"
    "# exit code of the last command, right aligned\n"
    "FRESH_SHOW_RPROMPT=1\n"
    "# put the current directory in the window title\n"
    "FRESH_TITLE=1\n"
    "\n"
    "# ---- editing ----------------------------------------------------------\n"
    "\n"
    "# colour output and syntax highlighting\n"
    "FRESH_COLOR=1\n"
    "FRESH_HIGHLIGHT=1\n"
    "# grey inline suggestion from history, accepted with Right or End\n"
    "FRESH_SUGGEST=1\n"
    "# hand PowerShell cmdlets and cmd builtins to the shell that understands\n"
    "# them, so Get-Process and dir work without leaving FreSH\n"
    "FRESH_FOREIGN=1\n"
    "\n"
    "# ---- history ----------------------------------------------------------\n"
    "\n"
    "HISTSIZE=5000\n"
    "# HISTFILE=\"C:/Users/me/.fresh_history\"\n"
    "\n"
    "# ---- aliases ----------------------------------------------------------\n"
    "\n"
    "alias ll='ls -l'\n"
    "alias la='ls -la'\n"
    "alias ..='cd ..'\n"
    "alias ...='cd ../..'\n"
    "alias g='git'\n"
    "alias gs='git status'\n"
    "alias gp='git pull'\n"
    "alias gc='git commit'\n"
    "alias gd='git diff'\n"
    "\n"
    "# ---- your own ---------------------------------------------------------\n"
    "\n"
    "# export EDITOR=nvim\n"
    "# export PATH=\"$HOME/bin;$PATH\"\n"
    "#\n"
    "# functions work too:\n"
    "# mkcd() { mkdir -p \"$1\"; cd \"$1\"; }\n";

void shell_error(const char *fmt, ...) {
    int colored = _isatty(_fileno(stderr));
    fflush(stdout);
    fputs(colored ? "\x1b[31mFreSH: " : "FreSH: ", stderr);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputs(colored ? "\x1b[0m\n" : "\n", stderr);
    fflush(stderr);
}

void shell_handle_signal(int which) {
    if (which == SIGNAL_INT) return;

    if (which == SIGNAL_QUIT) {
        if (shell.trap_quit) run_trap(shell.trap_quit);
        return;
    }

    const char *handler = shell.trap_hup ? shell.trap_hup : shell.trap_term;
    if (handler) run_trap(handler);
    if (shell.trap_exit) {
        char *exit_handler = shell.trap_exit;
        shell.trap_exit = NULL;
        run_trap(exit_handler);
        free(exit_handler);
    }
    history_save();
}

static void load_rc(void) {
    char *rc = config_path(".freshrc");
    if (!path_is_file(rc)) {
        FILE *f = fopen(rc, "wb");
        if (f) {
            fputs(DEFAULT_RC, f);
            fclose(f);
        }
    }
    if (path_is_file(rc)) exec_script_file(rc, NULL);
    free(rc);

    fresh_home_init();
    theme_load_configured();
    plugins_load_configured();
}

void shell_init(int interactive) {
    memset(&shell, 0, sizeof(shell));
    shell.running = 1;
    shell.interactive = interactive;
    shell.started = GetTickCount();
    sl_init(&shell.params);

    char exe[PATH_BUF];
    if (GetModuleFileNameA(NULL, exe, sizeof(exe))) sl_push_copy(&shell.params, exe);
    else sl_push_copy(&shell.params, "FreSH");

    vars_init();
    exec_init();
    path_reload_environment();
}

void shell_cleanup(void) {
    if (shell.trap_exit) {
        char *handler = shell.trap_exit;
        shell.trap_exit = NULL;
        shell.running = 1;
        run_trap(handler);
        free(handler);
    }
    jobs_cleanup();
    exec_cleanup();
    vars_cleanup();
    sl_free(&shell.params);
    free(shell.trap_int);
    free(shell.trap_err);
}

static int needs_more_input(const char *text) {
    int incomplete = 0;
    char *error = NULL;
    Node *node = parse_string(text, &incomplete, &error);
    node_free(node);
    free(error);
    return incomplete;
}

static void show_banner(void) {
    if (!option_enabled("FRESH_BANNER", 1)) return;

    const char *custom = var_get("FRESH_BANNER_TEXT");
    if (custom && *custom) {
        printf("%s\n\n", custom);
        return;
    }
    printf("\n  %s%s%s  %sFreSH%s %s%s%s\n", style(S_ACCENT), S_LAMBDA, style(S_RESET),
           style(S_HEADING), style(S_RESET), style(S_DIM), FRESH_VERSION, style(S_RESET));
    printf("  %stype help for the basics, edit ~/.freshrc to make it yours%s\n\n", style(S_DIM),
           style(S_RESET));
}

static void interactive_loop(void) {
    show_banner();

    while (shell.running) {
        char *line = line_read(0);
        if (!line) break;

        StrBuf command;
        sb_init(&command);
        sb_puts(&command, line);
        free(line);

        while (command.len > 0 && needs_more_input(command.data)) {
            char *more = line_read(1);
            if (!more) break;
            if (shell.interrupted) {
                sb_clear(&command);
                sb_puts(&command, more);
                free(more);
                break;
            }
            sb_putc(&command, '\n');
            sb_puts(&command, more);
            free(more);
        }

        char *trimmed = str_trim(command.data);
        if (*trimmed) {
            history_add(trimmed);
            history_save();
            exec_text(trimmed);
        }
        sb_free(&command);
    }
}

static const char *exception_name(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "segmentation fault, a bad or null memory access";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow, most likely runaway recursion";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_INT_OVERFLOW: return "integer overflow";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "floating point divide by zero";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array index out of bounds";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "a misaligned memory access";
    case EXCEPTION_IN_PAGE_ERROR: return "a page fault, the file backing memory was unreachable";
    default: return NULL;
    }
}

static LONG WINAPI crash_report(EXCEPTION_POINTERS *info) {
    static char message[512];
    DWORD code = info->ExceptionRecord->ExceptionCode;
    const char *name = exception_name(code);
    void *at = info->ExceptionRecord->ExceptionAddress;
    int colored = _isatty(_fileno(stderr));

    const char *red = colored ? "\x1b[31m" : "";
    const char *dim = colored ? "\x1b[90m" : "";
    const char *reset = colored ? "\x1b[0m" : "";
    const char *logo = "      __\n"
                       "     /\\ \\\n"
                       "    /  \\ \\    \xce\xbb  FreSH\n"
                       "   / /\\ \\ \\\n"
                       "  /_/  \\_\\_\\\n";
    const char *reason = name ? name : "an unexpected fault";

    int length = snprintf(message, sizeof(message),
                          "\n%s%s%s\n"
                          "%s  crashed: %s%s\n"
                          "%s  fault 0x%08lx at %p, FreSH %s%s\n"
                          "%s  if this is a bug, report it at "
                          "https://github.com/S42yt/FreSH/issues%s\n",
                          red, logo, reset, red, reason, reset, dim, (unsigned long)code, at,
                          FRESH_VERSION, reset, dim, reset);

    if (length > 0) {
        DWORD written;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, (DWORD)length, &written, NULL);
    }
    ExitProcess((UINT)code);
    return EXCEPTION_EXECUTE_HANDLER;
}

int main(int argc, char *argv[]) {
    ULONG stack_reserve = 65536;
    SetThreadStackGuarantee(&stack_reserve);
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    SetUnhandledExceptionFilter(crash_report);

    int interactive = 1;
    const char *command = NULL;
    const char *script = NULL;
    int script_arg = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            command = argv[++i];
            interactive = 0;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("FreSH %s\n", FRESH_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("usage: FreSH [-c command] [script [args...]]\n");
            return 0;
        } else {
            script = argv[i];
            script_arg = i + 1;
            interactive = 0;
            break;
        }
    }

    term_init();
    shell_init(interactive);

    if (script && path_is_dir(script)) {
        SetCurrentDirectoryA(script);
        script = NULL;
        shell.interactive = 1;
    }

    int status = 0;
    if (command) {
        load_rc();
        status = exec_text(command);
    } else if (script) {
        load_rc();
        StrList args;
        sl_init(&args);
        for (int i = script_arg; i < argc; i++) sl_push_copy(&args, argv[i]);
        status = exec_script_file(script, &args);
        sl_free(&args);
    } else {
        load_rc();
        history_load();
        interactive_loop();
        history_save();
        status = shell.last_status;
    }

    shell_cleanup();
    term_cleanup();
    return status;
}
