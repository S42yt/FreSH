/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "payload.h"

#ifndef FRESH_VERSION
#define FRESH_VERSION "2.0.0"
#endif

#define PATH_MAX_LEN 1024
#define PATH_MARKER "# added by FreSH setup"

typedef enum { INSTALL_USER, INSTALL_SYSTEM } InstallScope;

typedef struct {
    InstallScope scope;
    char install_dir[PATH_MAX_LEN];
    char binary[PATH_MAX_LEN];
    int default_shell;
} InstallOptions;

static int silent_mode = 0;
static int step_index = 0;
static int step_total = 1;

static const char *home_dir(void) {
    const char *home = getenv("HOME");
    if (home && *home) return home;
    struct passwd *entry = getpwuid(getuid());
    return entry && entry->pw_dir ? entry->pw_dir : "/";
}

static void screen(const char *title) {
    if (silent_mode) return;
    printf("\n  \xce\xbb  FreSH %s  \xe2\x80\x94  %s\n\n", FRESH_VERSION, title);
}

static void text(const char *line) {
    printf("  %s\n", line);
}

static void blank(void) {
    printf("\n");
}

static void wait_key(void) {
    if (silent_mode || !isatty(0)) return;
    printf("\n  Press Enter to continue...");
    fflush(stdout);
    int c;
    while ((c = getchar()) != '\n' && c != EOF) continue;
}

static int menu(const char **options, int count) {
    for (int i = 0; i < count; i++) printf("  %d) %s\n", i + 1, options[i]);
    for (;;) {
        printf("\n  Choice [1-%d]: ", count);
        fflush(stdout);
        char line[64];
        if (!fgets(line, sizeof(line), stdin)) return -1;
        int choice = atoi(line);
        if (choice >= 1 && choice <= count) return choice - 1;
    }
}

static void log_step(int ok, const char *message) {
    step_index++;
    printf("  %s %s\n", ok ? "\xe2\x9c\x93" : "\xe2\x9c\x97", message);
    if (silent_mode) return;
    printf("  [%d/%d]\n\n", step_index, step_total);
}

static int run(const char *command) {
    int status = system(command);
    return status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static void shell_quote(const char *text, char *out, size_t size) {
    size_t used = 0;
    if (used + 1 < size) out[used++] = '\'';
    for (const char *p = text; *p && used + 5 < size; p++) {
        if (*p == '\'') {
            memcpy(out + used, "'\\''", 4);
            used += 4;
        } else {
            out[used++] = *p;
        }
    }
    if (used + 1 < size) out[used++] = '\'';
    out[used] = '\0';
}

static void default_options(InstallOptions *options, InstallScope scope) {
    memset(options, 0, sizeof(*options));
    options->scope = scope;
    if (scope == INSTALL_SYSTEM) snprintf(options->install_dir, sizeof(options->install_dir), "/usr/local/bin");
    else snprintf(options->install_dir, sizeof(options->install_dir), "%s/.local/bin", home_dir());
    snprintf(options->binary, sizeof(options->binary), "%s/fresh", options->install_dir);
}

static int make_directories(const char *path) {
    char copy[PATH_MAX_LEN];
    snprintf(copy, sizeof(copy), "%s", path);
    for (char *p = copy + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(copy, 0755);
        *p = '/';
    }
    mkdir(copy, 0755);
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static int write_payload(const InstallOptions *options) {
    if (!make_directories(options->install_dir)) return 0;

    char staged[PATH_MAX_LEN + 16];
    snprintf(staged, sizeof(staged), "%s.setup", options->binary);
    FILE *f = fopen(staged, "wb");
    if (!f) return 0;
    size_t written = fwrite(FRESH_PAYLOAD, 1, (size_t)FRESH_PAYLOAD_size, f);
    int ok = fclose(f) == 0 && written == (size_t)FRESH_PAYLOAD_size;
    if (ok) ok = chmod(staged, 0755) == 0 && rename(staged, options->binary) == 0;
    if (!ok) unlink(staged);
    return ok;
}

static int file_exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0;
}

static int path_line_present(const char *rc) {
    FILE *f = fopen(rc, "r");
    if (!f) return 0;
    char line[PATH_MAX_LEN * 2];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, PATH_MARKER)) found = 1;
    }
    fclose(f);
    return found;
}

static const char *rc_candidates[] = {".zshrc", ".bashrc", ".bash_profile", ".profile"};

static int add_to_path(const InstallOptions *options) {
    if (options->scope == INSTALL_SYSTEM) return 1;

    int touched = 0;
    for (size_t i = 0; i < sizeof(rc_candidates) / sizeof(rc_candidates[0]); i++) {
        char rc[PATH_MAX_LEN];
        snprintf(rc, sizeof(rc), "%s/%s", home_dir(), rc_candidates[i]);
        int required = strcmp(rc_candidates[i], ".profile") == 0 && touched == 0;
        if (!file_exists(rc) && !required) continue;
        if (path_line_present(rc)) {
            touched++;
            continue;
        }
        FILE *f = fopen(rc, "a");
        if (!f) continue;
        fprintf(f, "\nexport PATH=\"$HOME/.local/bin:$PATH\" %s\n", PATH_MARKER);
        fclose(f);
        touched++;
    }
    return touched > 0;
}

static int remove_from_path(void) {
    int changed = 0;
    for (size_t i = 0; i < sizeof(rc_candidates) / sizeof(rc_candidates[0]); i++) {
        char rc[PATH_MAX_LEN];
        snprintf(rc, sizeof(rc), "%s/%s", home_dir(), rc_candidates[i]);
        if (!path_line_present(rc)) continue;

        FILE *in = fopen(rc, "r");
        if (!in) continue;
        char staged[PATH_MAX_LEN + 8];
        snprintf(staged, sizeof(staged), "%s.fresh", rc);
        FILE *out = fopen(staged, "w");
        if (!out) {
            fclose(in);
            continue;
        }
        char line[PATH_MAX_LEN * 2];
        while (fgets(line, sizeof(line), in)) {
            if (!strstr(line, PATH_MARKER)) fputs(line, out);
        }
        fclose(in);
        fclose(out);
        if (rename(staged, rc) == 0) changed++;
    }
    return changed;
}

static int shells_lists(const char *binary) {
    FILE *f = fopen("/etc/shells", "r");
    if (!f) return 0;
    char line[PATH_MAX_LEN];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, binary) == 0) found = 1;
    }
    fclose(f);
    return found;
}

static int register_shell(const InstallOptions *options) {
    if (shells_lists(options->binary)) return 1;
    char quoted[PATH_MAX_LEN * 2];
    shell_quote(options->binary, quoted, sizeof(quoted));
    char command[PATH_MAX_LEN * 3];
    snprintf(command, sizeof(command), "%s sh -c 'echo %s >> /etc/shells'",
             geteuid() == 0 ? "" : "sudo", quoted);
    return run(command) && shells_lists(options->binary);
}

static int unregister_shell(const char *binary) {
    if (!shells_lists(binary)) return 0;
    char quoted[PATH_MAX_LEN * 2];
    shell_quote(binary, quoted, sizeof(quoted));
    char command[PATH_MAX_LEN * 3];
    snprintf(command, sizeof(command),
             "%s sh -c 'grep -vxF -- %s /etc/shells > /etc/shells.fresh && mv /etc/shells.fresh /etc/shells'",
             geteuid() == 0 ? "" : "sudo", quoted);
    return run(command);
}

static const char *login_shell(void) {
    struct passwd *entry = getpwuid(getuid());
    return entry && entry->pw_shell ? entry->pw_shell : "";
}

static int change_login_shell(const char *binary) {
    char quoted[PATH_MAX_LEN * 2];
    shell_quote(binary, quoted, sizeof(quoted));
    char command[PATH_MAX_LEN * 3];
    snprintf(command, sizeof(command), "chsh -s %s", quoted);
    return run(command);
}

static int step_count(const InstallOptions *options) {
    return 2 + (options->default_shell ? 2 : 0);
}

static int perform(const InstallOptions *options) {
    int ok = write_payload(options);
    log_step(ok, "Install the fresh binary");
    if (!ok) return 0;

    log_step(add_to_path(options), "Put it on PATH");

    if (options->default_shell) {
        int listed = register_shell(options);
        log_step(listed, "List it in /etc/shells");
        if (listed) log_step(change_login_shell(options->binary), "Make it the login shell");
        else log_step(0, "Make it the login shell");
    }
    return 1;
}

static int uninstall(void) {
    const char *dirs[] = {"/usr/local/bin", NULL};
    char user_dir[PATH_MAX_LEN];
    snprintf(user_dir, sizeof(user_dir), "%s/.local/bin", home_dir());
    dirs[1] = user_dir;

    int removed = 0;
    for (int i = 0; i < 2; i++) {
        char binary[PATH_MAX_LEN + 8];
        snprintf(binary, sizeof(binary), "%s/fresh", dirs[i]);
        if (!file_exists(binary)) continue;

        if (strcmp(login_shell(), binary) == 0) {
            const char *fallback = file_exists("/bin/zsh") ? "/bin/zsh" : "/bin/bash";
            log_step(change_login_shell(fallback), "Restore the previous login shell");
        }
        if (shells_lists(binary)) log_step(unregister_shell(binary), "Remove it from /etc/shells");

        int ok = unlink(binary) == 0;
        if (!ok && errno == EACCES) {
            char command[PATH_MAX_LEN * 2];
            char quoted[PATH_MAX_LEN * 2];
            shell_quote(binary, quoted, sizeof(quoted));
            snprintf(command, sizeof(command), "sudo rm -f %s", quoted);
            ok = run(command);
        }
        log_step(ok, "Remove the fresh binary");
        removed += ok;
    }
    if (remove_from_path()) log_step(1, "Take it off PATH");
    return removed;
}

static void show_welcome(void) {
    screen("Setup");
    text("FreSH is a fast, zsh-flavoured shell that runs bash scripts unchanged.");
    blank();
    text("This installer will:");
    text("  - install the fresh binary");
    text("  - put it on PATH");
    text("  - optionally list it in /etc/shells and make it your login shell");
    wait_key();
}

static int show_license(void) {
    screen("License");
    text("FreSH is free software under the GNU General Public License v3.");
    blank();
    text("You may use, study, share and modify it. Derivative works must");
    text("stay under the same license and ship their source code.");
    text("There is no warranty, to the extent permitted by law.");
    blank();
    text("Full text: https://www.gnu.org/licenses/gpl-3.0.html");
    blank();
    const char *options[] = {"Accept and continue", "Cancel"};
    return menu(options, 2) == 0;
}

static int choose_scope(InstallScope *scope) {
    screen("Installation type");
    text("Where should FreSH be installed?");
    blank();
    const char *options[] = {"Just for me  (~/.local/bin, no sudo needed)",
                             "For all users  (/usr/local/bin, needs sudo)"};
    int choice = menu(options, 2);
    if (choice < 0) return 0;
    *scope = choice == 0 ? INSTALL_USER : INSTALL_SYSTEM;
    return 1;
}

static void ask_default_shell(InstallOptions *options) {
    screen("Login shell");
    text("Your terminal starts your login shell. FreSH can take that spot.");
    blank();
    text("That lists it in /etc/shells, which asks for your password, and runs");
    text("chsh. Uninstalling puts zsh or bash back.");
    blank();
    const char *options_text[] = {"Make FreSH my login shell", "Keep my current shell, just install FreSH"};
    options->default_shell = menu(options_text, 2) == 0;
}

static int confirm(const InstallOptions *options) {
    screen("Confirm");
    text(options->scope == INSTALL_USER ? "Installing for the current user" : "Installing for all users");
    printf("  Location: %s\n", options->binary);
    printf("  Login shell: %s\n", options->default_shell ? "yes" : "no");
    blank();
    const char *choices[] = {"Install now", "Cancel"};
    return menu(choices, 2) == 0;
}

static void show_done(const InstallOptions *options) {
    screen("Done");
    text("FreSH is installed.");
    blank();
    text("Start it by running");
    text("  fresh");
    blank();
    text("Open terminals keep the old PATH, so open a new one first.");
    printf("  Config file: %s/.freshrc\n", home_dir());
    printf("  Installed to: %s\n", options->binary);
    wait_key();
}

static int relaunch_with_sudo(int argc, char **argv) {
    char *args[argc + 3];
    args[0] = "sudo";
    args[1] = "-E";
    for (int i = 0; i < argc; i++) args[i + 2] = argv[i];
    args[argc + 2] = NULL;
    execvp("sudo", args);
    return 0;
}

static int run_silent(InstallScope scope, int default_shell) {
    InstallOptions options;
    default_options(&options, scope);
    options.default_shell = default_shell;
    silent_mode = 1;
    step_total = step_count(&options);
    printf("Installing FreSH %s to %s\n", FRESH_VERSION, options.binary);
    if (!perform(&options)) return 1;
    printf("Done. Open a new terminal and run fresh.\n");
    return 0;
}

static int cancelled(void) {
    screen("Cancelled");
    text("Installation cancelled.");
    return 1;
}

int main(int argc, char *argv[]) {
    int want_uninstall = 0;
    int want_silent = 0;
    int want_default = 0;
    InstallScope scope = geteuid() == 0 ? INSTALL_SYSTEM : INSTALL_USER;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (*arg == '/') arg++;
        else if (strncmp(arg, "--", 2) == 0) arg += 2;
        if (strcmp(arg, "uninstall") == 0) want_uninstall = 1;
        else if (strcmp(arg, "silent") == 0) want_silent = 1;
        else if (strcmp(arg, "user") == 0) scope = INSTALL_USER;
        else if (strcmp(arg, "system") == 0) scope = INSTALL_SYSTEM;
        else if (strcmp(arg, "default") == 0) want_default = 1;
        else if (strcmp(arg, "version") == 0) {
            printf("FreSH setup %s\n", FRESH_VERSION);
            return 0;
        } else {
            fprintf(stderr, "usage: fresh-setup [--silent [--user|--system] [--default]] [--uninstall]\n");
            return 2;
        }
    }

    if (want_uninstall) {
        if (!want_silent) {
            screen("Uninstall");
            text("Remove FreSH from this computer?");
            blank();
            const char *options[] = {"Remove FreSH", "Keep it"};
            if (menu(options, 2) != 0) return 0;
            blank();
        }
        silent_mode = want_silent;
        step_total = 4;
        int removed = uninstall();
        blank();
        text(removed ? "FreSH has been removed." : "Nothing to remove.");
        return removed ? 0 : 1;
    }

    if (scope == INSTALL_SYSTEM && geteuid() != 0) {
        if (want_silent) return relaunch_with_sudo(argc, argv);
    }
    if (want_silent) return run_silent(scope, want_default);

    if (!isatty(0)) return run_silent(INSTALL_USER, 0);

    show_welcome();
    if (!show_license()) return cancelled();
    if (!choose_scope(&scope)) return cancelled();
    if (scope == INSTALL_SYSTEM && geteuid() != 0) {
        blank();
        text("A system-wide install needs sudo.");
        blank();
        const char *elevate[] = {"Restart the installer with sudo", "Install just for me instead", "Cancel"};
        int answer = menu(elevate, 3);
        if (answer == 0) {
            execlp("sudo", "sudo", "-E", argv[0], (char *)NULL);
            text("Could not restart with sudo.");
            return 1;
        }
        if (answer == 1) scope = INSTALL_USER;
        else return cancelled();
    }

    InstallOptions options;
    default_options(&options, scope);
    ask_default_shell(&options);
    if (!confirm(&options)) return cancelled();

    screen("Installing");
    step_total = step_count(&options);
    if (!perform(&options)) {
        blank();
        text("Installation failed. Try running the installer with sudo.");
        return 1;
    }
    show_done(&options);
    return 0;
}
