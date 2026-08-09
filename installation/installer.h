/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_INSTALLER_H
#define FRESH_INSTALLER_H

#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAX_PATH_LEN 1024
#define FRESH_VERSION "2.0.0"
#define FRESH_TERMINAL_GUID "{4f3e2c10-9a7b-4d55-8c21-2f6b9d0e7a31}"

typedef enum {
    INSTALL_USER,
    INSTALL_SYSTEM
} InstallScope;

typedef struct {
    InstallScope scope;
    char install_dir[MAX_PATH_LEN];
    char exe_path[MAX_PATH_LEN];
    int add_to_path;
    int start_menu_shortcut;
    int desktop_shortcut;
    int terminal_profile;
    int context_menu;
    int script_association;
    int default_shell;
} InstallOptions;

typedef void (*StepLogger)(int ok, const char *message);

void installer_init(void);
int installer_is_admin(void);
int installer_relaunch_elevated(void);
void installer_default_options(InstallOptions *options, InstallScope scope);
int installer_perform(const InstallOptions *options, StepLogger log);
int installer_uninstall(StepLogger log);
int installer_step_count(const InstallOptions *options);

#endif
