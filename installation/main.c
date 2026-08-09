/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include <conio.h>

#include "installer.h"
#include "tui.h"

static int step_index = 0;
static int step_total = 1;

static int silent_mode = 0;

static void log_step(int ok, const char *message) {
    step_index++;
    tui_step(ok, message);
    if (silent_mode) return;
    tui_progress(step_index, step_total);
    printf("\n");
}

static void show_welcome(void) {
    tui_screen("Setup");
    tui_text("FreSH is a fast, zsh-flavoured shell for Windows.");
    tui_blank();
    tui_text("This installer will:");
    tui_text("  - install FreSH.exe and an uninstaller");
    tui_text("  - register FreSH as a Windows application and add it to PATH");
    tui_text("  - add a Windows Terminal profile, like PowerShell has");
    tui_text("  - add 'Open FreSH here' to the Explorer context menu");
    tui_text("  - register FreSH as a handler for .sh scripts");
    tui_wait_key();
}

static int show_license(void) {
    tui_screen("License");
    tui_text("FreSH is free software under the GNU General Public License v3.");
    tui_blank();
    tui_text("You may use, study, share and modify it. Derivative works must");
    tui_text("stay under the same license and ship their source code.");
    tui_text("There is no warranty, to the extent permitted by law.");
    tui_blank();
    tui_text("Full text: https://www.gnu.org/licenses/gpl-3.0.html");
    tui_blank();

    const char *options[] = {"Accept and continue", "Cancel"};
    return tui_menu(options, 2) == 0;
}

static int choose_scope(InstallScope *scope) {
    tui_screen("Installation type");
    tui_text("Where should FreSH be installed?");
    tui_blank();

    const char *options[] = {"Just for me  (no administrator rights needed)",
                             "For all users  (requires administrator rights)"};
    int choice = tui_menu(options, 2);
    if (choice < 0) return 0;

    *scope = choice == 0 ? INSTALL_USER : INSTALL_SYSTEM;
    if (*scope == INSTALL_SYSTEM && !installer_is_admin()) {
        tui_blank();
        tui_text("A system-wide install needs administrator rights.");
        tui_blank();
        const char *elevate[] = {"Restart the installer as administrator",
                                 "Install just for me instead", "Cancel"};
        int answer = tui_menu(elevate, 3);
        if (answer == 0) {
            if (installer_relaunch_elevated()) return -1;
            tui_blank();
            tui_text("Could not restart with administrator rights.");
            tui_wait_key();
            return 0;
        }
        if (answer == 1) *scope = INSTALL_USER;
        else return 0;
    }
    return 1;
}

static void ask_default_shell(InstallOptions *options) {
    tui_screen("Default shell");
    tui_text("Windows Terminal opens a profile when you start it or press Ctrl+Shift+T.");
    tui_text("FreSH can take that spot, the way PowerShell has it now.");
    tui_blank();
    tui_text("Your current settings.json is backed up first, and uninstalling");
    tui_text("puts the old default back.");
    tui_blank();

    const char *options_text[] = {"Make FreSH my default shell",
                                  "Keep my current default, just add FreSH to the list"};
    options->default_shell = tui_menu(options_text, 2) == 0;
}

static int confirm(const InstallOptions *options) {
    tui_screen("Confirm");
    tui_text(options->scope == INSTALL_USER ? "Installing for the current user"
                                            : "Installing for all users");
    printf("  Location: %s\n", options->install_dir);
    printf("  Default shell: %s\n", options->default_shell ? "yes" : "no");
    tui_blank();

    const char *choices[] = {"Install now", "Cancel"};
    return tui_menu(choices, 2) == 0;
}

static void show_done(const InstallOptions *options) {
    tui_screen("Done");
    tui_text("FreSH is installed.");
    tui_blank();
    tui_text("Start it from Windows Terminal, the Start Menu, or by running");
    tui_text("  FreSH");
    tui_blank();
    tui_text("Open terminals keep the old PATH, so open a new one first.");
    printf("  Config file: %%USERPROFILE%%\\.freshrc\n");
    printf("  Installed to: %s\n", options->install_dir);
    tui_wait_key();
}

static int run_uninstall(void) {
    tui_screen("Uninstall");
    tui_text("Remove FreSH from this computer?");
    tui_blank();

    const char *options[] = {"Remove FreSH", "Keep it"};
    if (tui_menu(options, 2) != 0) return 0;

    tui_blank();
    step_total = 7;
    int removed = installer_uninstall(log_step);
    tui_blank();
    tui_text(removed ? "FreSH has been removed." : "Nothing to remove.");
    tui_wait_key();
    return removed ? 0 : 1;
}

static int run_silent(InstallScope scope, int default_shell) {
    InstallOptions options;
    installer_default_options(&options, scope);
    options.default_shell = default_shell;
    silent_mode = 1;
    step_index = 0;
    step_total = installer_step_count(&options);
    printf("Installing FreSH %s to %s\n", FRESH_VERSION, options.install_dir);
    if (!installer_perform(&options, log_step)) return 1;
    printf("Done. Open a new terminal and run FreSH.\n");
    return 0;
}

int main(int argc, char *argv[]) {
    installer_init();
    tui_init();

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "/uninstall") == 0 || strcmp(argv[i], "--uninstall") == 0)
            return run_uninstall();
        if (strcmp(argv[i], "/silent") == 0 || strcmp(argv[i], "--silent") == 0) {
            InstallScope scope = installer_is_admin() ? INSTALL_SYSTEM : INSTALL_USER;
            int default_shell = 0;
            for (int j = 1; j < argc; j++) {
                if (strcmp(argv[j], "/user") == 0) scope = INSTALL_USER;
                if (strcmp(argv[j], "/system") == 0) scope = INSTALL_SYSTEM;
                if (strcmp(argv[j], "/default") == 0) default_shell = 1;
            }
            return run_silent(scope, default_shell);
        }
    }

    show_welcome();
    if (!show_license()) {
        tui_screen("Cancelled");
        tui_text("Installation cancelled.");
        tui_wait_key();
        return 1;
    }

    InstallScope scope = INSTALL_USER;
    int decision = choose_scope(&scope);
    if (decision <= 0) {
        tui_screen("Cancelled");
        tui_text(decision == -1 ? "Continuing in the elevated installer."
                                : "Installation cancelled.");
        tui_wait_key();
        return decision == -1 ? 0 : 1;
    }

    InstallOptions options;
    installer_default_options(&options, scope);
    ask_default_shell(&options);

    if (!confirm(&options)) {
        tui_screen("Cancelled");
        tui_text("Installation cancelled.");
        tui_wait_key();
        return 1;
    }

    tui_screen("Installing");
    step_index = 0;
    step_total = installer_step_count(&options);

    if (!installer_perform(&options, log_step)) {
        tui_blank();
        tui_text("Installation failed. Try running the installer as administrator.");
        tui_wait_key();
        return 1;
    }

    show_done(&options);
    return 0;
}
