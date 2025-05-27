/*
 * Copyright (c) 2025 Musa/S42
 * FreSH Installation Wizard - Core Installation Logic
 * MIT License - See LICENSE file for details
 */

#include "installer.h"
#include "tui.h"
#include <io.h>
#include <fcntl.h>
#include <errno.h>

static char source_exe_path[MAX_PATH_LEN];

void installer_init() {

    char installer_path[MAX_PATH_LEN];
    GetModuleFileNameA(NULL, installer_path, MAX_PATH_LEN - 1);
    installer_path[MAX_PATH_LEN - 1] = '\0';


    char *last_slash = strrchr(installer_path, '\\');
    if (last_slash) {
        *last_slash = '\0';
    }


    int ret = snprintf(source_exe_path, MAX_PATH_LEN, "%s\\FreSH.exe", installer_path);
    if (ret >= MAX_PATH_LEN) {

        strcpy(source_exe_path, "FreSH.exe");
    }


    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

void choose_installation_path(install_paths_t *paths, install_type_t type) {
    if (type == INSTALL_USER) {

        char appdata[MAX_PATH_LEN];
        if (SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appdata) == S_OK) {
            int ret = snprintf(paths->install_dir, MAX_PATH_LEN, "%s\\FreSH", appdata);
            if (ret >= MAX_PATH_LEN) {

                char *userprofile = getenv("USERPROFILE");
                snprintf(paths->install_dir, MAX_PATH_LEN, "%s\\FreSH", userprofile ? userprofile : "C:\\Users\\Default");
            }
        } else {

            char *userprofile = getenv("USERPROFILE");
            if (userprofile) {
                snprintf(paths->install_dir, MAX_PATH_LEN, "%s\\AppData\\Local\\FreSH", userprofile);
            } else {
                strcpy(paths->install_dir, "C:\\Users\\Default\\AppData\\Local\\FreSH");
            }
        }
    } else {

        char progfiles[MAX_PATH_LEN];
        if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, 0, progfiles) == S_OK) {
            int ret = snprintf(paths->install_dir, MAX_PATH_LEN, "%s\\FreSH", progfiles);
            if (ret >= MAX_PATH_LEN) {
                strcpy(paths->install_dir, "C:\\Program Files\\FreSH");
            }
        } else {
            strcpy(paths->install_dir, "C:\\Program Files\\FreSH");
        }
    }

    snprintf(paths->exe_path, MAX_PATH_LEN, "%s\\FreSH.exe", paths->install_dir);


    char temp[MAX_PATH_LEN];
    GetTempPathA(MAX_PATH_LEN, temp);
    snprintf(paths->temp_dir, MAX_PATH_LEN, "%s\\FreSH_Install", temp);
}

int confirm_installation(install_paths_t *paths, install_type_t type) {
    clear_screen();

    draw_box(2, 1, 76, 22);

    gotoxy(5, 3);
    set_console_color(11);
    printf("    CONFIRM INSTALLATION\n");
    set_console_color(7);
    printf("    ════════════════════════════════════════════════════════════════\n\n");

    printf("    Installation Details:\n\n");
    printf("    Installation Type: %s\n",
           type == INSTALL_USER ? "Current User" : "System-Wide");
    printf("    Installation Path: %s\n", paths->install_dir);
    printf("    Executable Path:   %s\n\n", paths->exe_path);

    printf("    The installer will:\n");
    printf("    • Copy FreSH.exe to the installation directory\n");
    printf("    • Register FreSH as a Windows shell\n");
    printf("    • Add FreSH to the system PATH\n");
    printf("    • Create Start Menu and Desktop shortcuts\n");
    printf("    • Create an uninstaller\n\n");

    const char *options[] = {
        "Proceed with installation",
        "Go back and change settings",
        "Cancel installation"
    };

    int choice = get_user_choice(options, 3);
    return choice;
}

int copy_fresh_executable(install_paths_t *paths) {

    if (_mkdir(paths->install_dir) != 0 && errno != EEXIST) {
        printf("    Error: Could not create installation directory\n");
        return 0;
    }


    if (_access(source_exe_path, 0) != 0) {
        printf("    Error: FreSH.exe not found in installer package\n");
        printf("    Expected path: %s\n", source_exe_path);
        return 0;
    }


    if (!CopyFileA(source_exe_path, paths->exe_path, FALSE)) {
        printf("    Error: Could not copy FreSH.exe to installation directory\n");
        printf("    Error code: %lu\n", GetLastError());
        return 0;
    }

    printf("    ✓ FreSH.exe copied successfully\n");
    return 1;
}

int register_shell_in_registry(install_paths_t *paths, install_type_t type) {
    HKEY hkey;
    HKEY root = type == INSTALL_USER ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;


    const char *shell_key = "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon\\AlternateShells\\FreSH";

    LONG result = RegCreateKeyExA(root, shell_key, 0, NULL, REG_OPTION_NON_VOLATILE,
                                  KEY_WRITE, NULL, &hkey, NULL);

    if (result == ERROR_SUCCESS) {
        RegSetValueExA(hkey, NULL, 0, REG_SZ, (BYTE*)paths->exe_path, strlen(paths->exe_path) + 1);
        RegCloseKey(hkey);
        printf("    ✓ FreSH registered as Windows shell\n");
        return 1;
    } else {
        printf("    Warning: Could not register FreSH as Windows shell\n");
        return 0;
    }
}

int add_to_path(install_paths_t *paths, install_type_t type) {
    HKEY hkey;
    HKEY root = type == INSTALL_USER ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    const char *env_key = type == INSTALL_USER ?
                          "Environment" : "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";

    LONG result = RegOpenKeyExA(root, env_key, 0, KEY_READ | KEY_WRITE, &hkey);

    if (result == ERROR_SUCCESS) {
        char current_path[8192] = {0};
        DWORD path_size = sizeof(current_path) - 1;
        DWORD path_type;


        result = RegQueryValueExA(hkey, "PATH", NULL, &path_type, (BYTE*)current_path, &path_size);


        if (strstr(current_path, paths->install_dir) == NULL) {

            size_t new_size = strlen(current_path) + strlen(paths->install_dir) + 2;

            if (new_size < sizeof(current_path)) {

                if (strlen(current_path) > 0 && current_path[strlen(current_path) - 1] != ';') {
                    strcat(current_path, ";");
                }
                strcat(current_path, paths->install_dir);

                result = RegSetValueExA(hkey, "PATH", 0, REG_EXPAND_SZ,
                                        (BYTE*)current_path, strlen(current_path) + 1);

                if (result == ERROR_SUCCESS) {
                    printf("    ✓ Added FreSH to system PATH\n");


                    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                                        (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
                } else {
                    printf("    Warning: Could not add FreSH to PATH\n");
                }
            } else {
                printf("    Warning: PATH too long to add FreSH directory\n");
            }
        } else {
            printf("    ✓ FreSH already in PATH\n");
        }

        RegCloseKey(hkey);
        return 1;
    } else {
        printf("    Warning: Could not access PATH environment variable\n");
        return 0;
    }
}

int create_start_menu_shortcut(install_paths_t *paths) {
    char shortcut_path[MAX_PATH_LEN];
    char start_menu[MAX_PATH_LEN];


    if (SHGetFolderPathA(NULL, CSIDL_PROGRAMS, NULL, 0, start_menu) != S_OK) {
        printf("    Warning: Could not create Start Menu shortcut\n");
        return 0;
    }

    int ret = snprintf(shortcut_path, MAX_PATH_LEN, "%s\\FreSH.lnk", start_menu);
    if (ret >= MAX_PATH_LEN) {
        printf("    Warning: Start Menu path too long\n");
        return 0;
    }


    IShellLinkA *shell_link;
    IPersistFile *persist_file;

    CoInitialize(NULL);

    HRESULT hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IShellLinkA, (void**)&shell_link);

    if (SUCCEEDED(hr)) {
        shell_link->lpVtbl->SetPath(shell_link, paths->exe_path);
        shell_link->lpVtbl->SetDescription(shell_link, "FreSH - First-Run Experience Shell");
        shell_link->lpVtbl->SetWorkingDirectory(shell_link, paths->install_dir);

        hr = shell_link->lpVtbl->QueryInterface(shell_link, &IID_IPersistFile, (void**)&persist_file);

        if (SUCCEEDED(hr)) {
            WCHAR shortcut_path_w[MAX_PATH_LEN];
            MultiByteToWideChar(CP_ACP, 0, shortcut_path, -1, shortcut_path_w, MAX_PATH_LEN);

            hr = persist_file->lpVtbl->Save(persist_file, shortcut_path_w, TRUE);
            persist_file->lpVtbl->Release(persist_file);

            if (SUCCEEDED(hr)) {
                printf("    ✓ Start Menu shortcut created\n");
            }
        }

        shell_link->lpVtbl->Release(shell_link);
    }

    CoUninitialize();
    return SUCCEEDED(hr) ? 1 : 0;
}

int create_desktop_shortcut(install_paths_t *paths) {
    char shortcut_path[MAX_PATH_LEN];
    char desktop[MAX_PATH_LEN];


    if (SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, desktop) != S_OK) {
        printf("    Warning: Could not create Desktop shortcut\n");
        return 0;
    }

    int ret = snprintf(shortcut_path, MAX_PATH_LEN, "%s\\FreSH.lnk", desktop);
    if (ret >= MAX_PATH_LEN) {
        printf("    Warning: Desktop path too long\n");
        return 0;
    }


    IShellLinkA *shell_link;
    IPersistFile *persist_file;

    CoInitialize(NULL);

    HRESULT hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IShellLinkA, (void**)&shell_link);

    if (SUCCEEDED(hr)) {
        shell_link->lpVtbl->SetPath(shell_link, paths->exe_path);
        shell_link->lpVtbl->SetDescription(shell_link, "FreSH - First-Run Experience Shell");
        shell_link->lpVtbl->SetWorkingDirectory(shell_link, paths->install_dir);

        hr = shell_link->lpVtbl->QueryInterface(shell_link, &IID_IPersistFile, (void**)&persist_file);

        if (SUCCEEDED(hr)) {
            WCHAR shortcut_path_w[MAX_PATH_LEN];
            MultiByteToWideChar(CP_ACP, 0, shortcut_path, -1, shortcut_path_w, MAX_PATH_LEN);

            hr = persist_file->lpVtbl->Save(persist_file, shortcut_path_w, TRUE);
            persist_file->lpVtbl->Release(persist_file);

            if (SUCCEEDED(hr)) {
                printf("    ✓ Desktop shortcut created\n");
            }
        }

        shell_link->lpVtbl->Release(shell_link);
    }

    CoUninitialize();
    return SUCCEEDED(hr) ? 1 : 0;
}

int create_uninstaller(install_paths_t *paths) {
    char uninstaller_path[MAX_PATH_LEN];
    int ret = snprintf(uninstaller_path, MAX_PATH_LEN, "%s\\Uninstall.exe", paths->install_dir);

    if (ret >= MAX_PATH_LEN) {
        printf("    Warning: Uninstaller path too long\n");
        return 0;
    }


    char installer_path[MAX_PATH_LEN];
    GetModuleFileNameA(NULL, installer_path, MAX_PATH_LEN);

    if (CopyFileA(installer_path, uninstaller_path, FALSE)) {
        printf("    ✓ Uninstaller created\n");
        return 1;
    } else {
        printf("    Warning: Could not create uninstaller\n");
        return 0;
    }
}

int perform_installation(install_paths_t *paths, install_type_t type) {
    show_installation_progress();

    int steps_completed = 0;
    const int total_steps = 6;


    gotoxy(5, 8);
    printf("    Copying FreSH executable...\n");
    show_progress_bar(++steps_completed, total_steps);
    if (!copy_fresh_executable(paths)) {
        return 0;
    }
    Sleep(500);


    gotoxy(5, 11);
    printf("    Registering FreSH as Windows shell...\n");
    show_progress_bar(++steps_completed, total_steps);
    register_shell_in_registry(paths, type);
    Sleep(500);


    gotoxy(5, 14);
    printf("    Adding FreSH to system PATH...\n");
    show_progress_bar(++steps_completed, total_steps);
    add_to_path(paths, type);
    Sleep(500);


    gotoxy(5, 17);
    printf("    Creating Start Menu shortcut...\n");
    show_progress_bar(++steps_completed, total_steps);
    create_start_menu_shortcut(paths);
    Sleep(500);


    gotoxy(5, 20);
    printf("    Creating Desktop shortcut...\n");
    show_progress_bar(++steps_completed, total_steps);
    create_desktop_shortcut(paths);
    Sleep(500);


    gotoxy(5, 23);
    printf("    Creating uninstaller...\n");
    show_progress_bar(++steps_completed, total_steps);
    create_uninstaller(paths);
    Sleep(500);

    return 1;
}

void cleanup_installer() {

}
