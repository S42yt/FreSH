/*
 * Copyright (c) 2025 Musa/S42
 * FreSH Installation Wizard
 * MIT License - See LICENSE file for details
 */

#ifndef INSTALLER_H
#define INSTALLER_H

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>
#include <direct.h>


#define MAX_PATH_LEN 1024
#define MAX_INPUT_LEN 512
#define FRESH_VERSION "1.0.0"


typedef enum {
    INSTALL_USER,
    INSTALL_SYSTEM
} install_type_t;


typedef struct {
    char install_dir[MAX_PATH_LEN];
    char exe_path[MAX_PATH_LEN];
    char temp_dir[MAX_PATH_LEN];
} install_paths_t;


void installer_init();
void show_welcome_screen();
int show_license_agreement();
install_type_t choose_installation_type();
void choose_installation_path(install_paths_t *paths, install_type_t type);
int confirm_installation(install_paths_t *paths, install_type_t type);
int perform_installation(install_paths_t *paths, install_type_t type);
void show_completion_screen(int success);
void cleanup_installer();


void clear_screen();
void set_console_color(int color);
void draw_box(int x, int y, int width, int height);
void print_centered(const char *text, int width);
void show_progress_bar(int progress, int total);
int get_user_choice(const char *options[], int count);
void wait_for_keypress();


int copy_fresh_executable(install_paths_t *paths);
int register_shell_in_registry(install_paths_t *paths, install_type_t type);
int add_to_path(install_paths_t *paths, install_type_t type);
int create_start_menu_shortcut(install_paths_t *paths);
int create_desktop_shortcut(install_paths_t *paths);


int create_uninstaller(install_paths_t *paths);

#endif
