/*
 * Copyright (c) 2025 Musa/S42
 * FreSH Installation Wizard - Main Entry Point
 * MIT License - See LICENSE file for details
 */

#include "installer.h"
#include "tui.h"
#include <conio.h>


int perform_uninstall();
int remove_from_registry();
int remove_from_path();
int remove_shortcuts();

int main(int argc, char *argv[]) {
    installer_init();


    if (argc > 1 && strcmp(argv[1], "/uninstall") == 0) {
        clear_screen();

        printf("FreSH Uninstaller\n");
        printf("================\n\n");

        printf("This will completely remove FreSH from your system.\n");
        printf("Are you sure you want to continue? (y/n): ");

        char response;
        scanf(" %c", &response);

        if (response == 'y' || response == 'Y') {
            printf("\nUninstalling FreSH...\n\n");

            if (perform_uninstall()) {
                printf("\n✓ FreSH has been successfully uninstalled!\n");
                printf("Thank you for using FreSH.\n");
            } else {
                printf("\n❌ Uninstall completed with some warnings.\n");
                printf("You may need to manually remove some files.\n");
            }
        } else {
            printf("Uninstall cancelled.\n");
        }

        printf("\nPress any key to exit...");
        _getch();
        return 0;
    }


    show_welcome_screen();

    if (!show_license_agreement()) {
        clear_screen();
        printf("Installation cancelled by user.\n");
        return 0;
    }

    install_type_t install_type = choose_installation_type();

    install_paths_t paths;
    choose_installation_path(&paths, install_type);

    int confirm = confirm_installation(&paths, install_type);
    if (confirm == 1) {
        clear_screen();
        printf("Going back to previous step not implemented in this version.\n");
        printf("Please restart the installer.\n");
        wait_for_keypress();
        return 0;
    } else if (confirm == 2) {
        clear_screen();
        printf("Installation cancelled by user.\n");
        return 0;
    }

    int success = perform_installation(&paths, install_type);
    show_completion_screen(success);
    cleanup_installer();

    return success ? 0 : 1;
}

int perform_uninstall() {
    char install_dir[MAX_PATH_LEN];
    char exe_path[MAX_PATH_LEN];


    GetModuleFileNameA(NULL, exe_path, MAX_PATH_LEN);
    strcpy(install_dir, exe_path);

    char *last_slash = strrchr(install_dir, '\\');
    if (last_slash) {
        *last_slash = '\0';
    }

    printf("Removing FreSH from PATH...\n");
    remove_from_path();

    printf("Removing registry entries...\n");
    remove_from_registry();

    printf("Removing shortcuts...\n");
    remove_shortcuts();

    printf("Removing installation files...\n");


    char fresh_path[MAX_PATH_LEN];
    snprintf(fresh_path, MAX_PATH_LEN, "%s\\FreSH.exe", install_dir);
    DeleteFileA(fresh_path);


    printf("Note: Installation directory will be removed after uninstaller exits.\n");

    return 1;
}

int remove_from_registry() {

    HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    const char *shell_key = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon\\AlternateShells";

    for (int i = 0; i < 2; i++) {
        HKEY hkey;
        if (RegOpenKeyExA(roots[i], shell_key, 0, KEY_WRITE, &hkey) == ERROR_SUCCESS) {
            RegDeleteKeyA(hkey, "FreSH");
            RegCloseKey(hkey);
        }
    }
    return 1;
}

int remove_from_path() {

    HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    const char *env_keys[] = {
        "Environment",
        "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"
    };

    for (int i = 0; i < 2; i++) {
        HKEY hkey;
        if (RegOpenKeyExA(roots[i], env_keys[i], 0, KEY_READ | KEY_WRITE, &hkey) == ERROR_SUCCESS) {
            char path[8192];
            DWORD path_size = sizeof(path);

            if (RegQueryValueExA(hkey, "PATH", NULL, NULL, (BYTE*)path, &path_size) == ERROR_SUCCESS) {

                char new_path[8192] = {0};
                char *token = strtok(path, ";");

                while (token != NULL) {
                    if (strstr(token, "FreSH") == NULL) {
                        if (strlen(new_path) > 0) strcat(new_path, ";");
                        strcat(new_path, token);
                    }
                    token = strtok(NULL, ";");
                }

                RegSetValueExA(hkey, "PATH", 0, REG_EXPAND_SZ,
                               (BYTE*)new_path, strlen(new_path) + 1);
            }
            RegCloseKey(hkey);
        }
    }
    return 1;
}

int remove_shortcuts() {
    char shortcut_path[MAX_PATH_LEN];


    char start_menu[MAX_PATH_LEN];
    if (SHGetFolderPathA(NULL, CSIDL_PROGRAMS, NULL, 0, start_menu) == S_OK) {
        snprintf(shortcut_path, MAX_PATH_LEN, "%s\\FreSH.lnk", start_menu);
        DeleteFileA(shortcut_path);
    }


    char desktop[MAX_PATH_LEN];
    if (SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, desktop) == S_OK) {
        snprintf(shortcut_path, MAX_PATH_LEN, "%s\\FreSH.lnk", desktop);
        DeleteFileA(shortcut_path);
    }

    return 1;
}
