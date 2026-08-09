/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "installer.h"

#include <direct.h>

#include "payload.h"

#define UNINSTALL_KEY "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\FreSH"
#define APP_PATHS_KEY "Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\FreSH.exe"
#define PROGID_KEY "Software\\Classes\\FreSH.Script"
#define CONTEXT_KEY "Software\\Classes\\Directory\\Background\\shell\\FreSH"
#define CONTEXT_DIR_KEY "Software\\Classes\\Directory\\shell\\FreSH"

static char self_path[MAX_PATH_LEN];

void installer_init(void) {
    GetModuleFileNameA(NULL, self_path, MAX_PATH_LEN - 1);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

int installer_is_admin(void) {
    BOOL is_admin = FALSE;
    PSID group = NULL;
    SID_IDENTIFIER_AUTHORITY authority = {SECURITY_NT_AUTHORITY};

    if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &group)) {
        CheckTokenMembership(NULL, group, &is_admin);
        FreeSid(group);
    }
    return is_admin ? 1 : 0;
}

int installer_relaunch_elevated(void) {
    SHELLEXECUTEINFOA info;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.lpVerb = "runas";
    info.lpFile = self_path;
    info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExA(&info) ? 1 : 0;
}

static void ensure_directory(const char *path) {
    char buffer[MAX_PATH_LEN];
    snprintf(buffer, sizeof(buffer), "%s", path);
    for (char *p = buffer + 1; *p; p++) {
        if (*p == '\\') {
            *p = '\0';
            _mkdir(buffer);
            *p = '\\';
        }
    }
    _mkdir(buffer);
}

void installer_default_options(InstallOptions *options, InstallScope scope) {
    memset(options, 0, sizeof(*options));
    options->scope = scope;
    options->add_to_path = 1;
    options->start_menu_shortcut = 1;
    options->desktop_shortcut = 1;
    options->terminal_profile = 1;
    options->context_menu = 1;
    options->script_association = 1;

    char base[MAX_PATH_LEN];
    int folder = scope == INSTALL_USER ? CSIDL_LOCAL_APPDATA : CSIDL_PROGRAM_FILES;
    if (SHGetFolderPathA(NULL, folder, NULL, 0, base) != S_OK)
        snprintf(base, sizeof(base), "%s", scope == INSTALL_USER ? getenv("LOCALAPPDATA")
                                                                 : "C:\\Program Files");

    snprintf(options->install_dir, MAX_PATH_LEN, "%s\\FreSH", base);
    snprintf(options->exe_path, MAX_PATH_LEN, "%s\\FreSH.exe", options->install_dir);
}

int installer_step_count(const InstallOptions *options) {
    int steps = 4;
    if (options->add_to_path) steps++;
    if (options->start_menu_shortcut) steps++;
    if (options->desktop_shortcut) steps++;
    if (options->terminal_profile) steps++;
    if (options->context_menu) steps++;
    if (options->script_association) steps++;
    return steps;
}

static HKEY scope_root(const InstallOptions *options) {
    return options->scope == INSTALL_USER ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
}

static int write_string_value(HKEY root, const char *subkey, const char *name, const char *value) {
    HKEY key;
    if (RegCreateKeyExA(root, subkey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key,
                        NULL) != ERROR_SUCCESS)
        return 0;
    LONG result = RegSetValueExA(key, name, 0, REG_SZ, (const BYTE *)value,
                                 (DWORD)strlen(value) + 1);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

static int write_dword_value(HKEY root, const char *subkey, const char *name, DWORD value) {
    HKEY key;
    if (RegCreateKeyExA(root, subkey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &key,
                        NULL) != ERROR_SUCCESS)
        return 0;
    LONG result = RegSetValueExA(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

static void delete_key_tree(HKEY root, const char *subkey) {
    RegDeleteTreeA(root, subkey);
}

static int write_payload(const InstallOptions *options) {
    ensure_directory(options->install_dir);

    FILE *f = fopen(options->exe_path, "wb");
    if (!f) return 0;
    size_t written = fwrite(FRESH_PAYLOAD, 1, (size_t)FRESH_PAYLOAD_size, f);
    fclose(f);
    return written == (size_t)FRESH_PAYLOAD_size;
}

static int write_uninstaller(const InstallOptions *options) {
    char legacy[MAX_PATH_LEN];
    snprintf(legacy, sizeof(legacy), "%s\\Uninstall.exe", options->install_dir);
    DeleteFileA(legacy);

    char target[MAX_PATH_LEN];
    snprintf(target, sizeof(target), "%s\\Uninstall-FreSH.exe", options->install_dir);
    return CopyFileA(self_path, target, FALSE) ? 1 : 0;
}

static int register_uninstall_entry(const InstallOptions *options) {
    HKEY root = scope_root(options);
    char uninstall_command[MAX_PATH_LEN];
    snprintf(uninstall_command, sizeof(uninstall_command), "\"%s\\Uninstall-FreSH.exe\" /uninstall",
             options->install_dir);

    int ok = write_string_value(root, UNINSTALL_KEY, "DisplayName", "FreSH - First-Run Experience Shell");
    ok &= write_string_value(root, UNINSTALL_KEY, "DisplayVersion", FRESH_VERSION);
    ok &= write_string_value(root, UNINSTALL_KEY, "Publisher", "Musa Bostanci");
    ok &= write_string_value(root, UNINSTALL_KEY, "InstallLocation", options->install_dir);
    ok &= write_string_value(root, UNINSTALL_KEY, "DisplayIcon", options->exe_path);
    ok &= write_string_value(root, UNINSTALL_KEY, "UninstallString", uninstall_command);
    ok &= write_dword_value(root, UNINSTALL_KEY, "NoModify", 1);
    ok &= write_dword_value(root, UNINSTALL_KEY, "NoRepair", 1);
    ok &= write_dword_value(root, UNINSTALL_KEY, "EstimatedSize", FRESH_PAYLOAD_size / 1024);
    return ok;
}

static int register_app_path(const InstallOptions *options) {
    HKEY root = scope_root(options);
    int ok = write_string_value(root, APP_PATHS_KEY, NULL, options->exe_path);
    ok &= write_string_value(root, APP_PATHS_KEY, "Path", options->install_dir);
    return ok;
}

static int register_context_menu(const InstallOptions *options) {
    HKEY root = scope_root(options);
    char command[MAX_PATH_LEN];
    char command_key[MAX_PATH_LEN];

    snprintf(command, sizeof(command), "\"%s\"", options->exe_path);
    snprintf(command_key, sizeof(command_key), "%s\\command", CONTEXT_KEY);
    int ok = write_string_value(root, CONTEXT_KEY, "MUIVerb", "Open FreSH here");
    ok &= write_string_value(root, CONTEXT_KEY, "Icon", options->exe_path);
    ok &= write_string_value(root, command_key, NULL, command);

    snprintf(command, sizeof(command), "\"%s\" \"%%V\"", options->exe_path);
    snprintf(command_key, sizeof(command_key), "%s\\command", CONTEXT_DIR_KEY);
    ok &= write_string_value(root, CONTEXT_DIR_KEY, "MUIVerb", "Open FreSH here");
    ok &= write_string_value(root, CONTEXT_DIR_KEY, "Icon", options->exe_path);
    ok &= write_string_value(root, command_key, NULL, command);
    return ok;
}

static int register_script_association(const InstallOptions *options) {
    HKEY root = scope_root(options);
    char command[MAX_PATH_LEN];
    char subkey[MAX_PATH_LEN];

    int ok = write_string_value(root, PROGID_KEY, NULL, "FreSH Shell Script");

    snprintf(subkey, sizeof(subkey), "%s\\DefaultIcon", PROGID_KEY);
    ok &= write_string_value(root, subkey, NULL, options->exe_path);

    snprintf(command, sizeof(command), "\"%s\" \"%%1\" %%*", options->exe_path);
    snprintf(subkey, sizeof(subkey), "%s\\shell\\open\\command", PROGID_KEY);
    ok &= write_string_value(root, subkey, NULL, command);

    ok &= write_string_value(root, "Software\\Classes\\.sh\\OpenWithProgids", "FreSH.Script", "");
    ok &= write_string_value(root, "Software\\Classes\\.fresh", NULL, "FreSH.Script");
    return ok;
}

static int terminal_fragment_path(const InstallOptions *options, char *out, size_t out_size) {
    char base[MAX_PATH_LEN];
    int folder = options->scope == INSTALL_USER ? CSIDL_LOCAL_APPDATA : CSIDL_PROGRAM_FILES;
    if (SHGetFolderPathA(NULL, folder, NULL, 0, base) != S_OK) return 0;
    snprintf(out, out_size, "%s\\Microsoft\\Windows Terminal\\Fragments\\FreSH", base);
    if (options->scope == INSTALL_SYSTEM)
        snprintf(out, out_size, "%s\\Windows Terminal\\Fragments\\FreSH", base);
    return 1;
}

static void json_escape_path(const char *path, char *out, size_t out_size) {
    size_t index = 0;
    for (const char *p = path; *p && index + 2 < out_size; p++) {
        if (*p == '\\') out[index++] = '\\';
        out[index++] = *p;
    }
    out[index] = '\0';
}

static int register_terminal_profile(const InstallOptions *options) {
    char directory[MAX_PATH_LEN];
    if (!terminal_fragment_path(options, directory, sizeof(directory))) return 0;
    ensure_directory(directory);

    char file[MAX_PATH_LEN];
    snprintf(file, sizeof(file), "%s\\fresh.json", directory);

    char escaped[MAX_PATH_LEN * 2];
    json_escape_path(options->exe_path, escaped, sizeof(escaped));

    FILE *f = fopen(file, "w");
    if (!f) return 0;
    fprintf(f,
            "{\n"
            "    \"profiles\": [\n"
            "        {\n"
            "            \"guid\": \"%s\",\n"
            "            \"name\": \"FreSH\",\n"
            "            \"commandline\": \"%s\",\n"
            "            \"icon\": \"%s\",\n"
            "            \"startingDirectory\": \"%%USERPROFILE%%\",\n"
            "            \"colorScheme\": \"Campbell\",\n"
            "            \"hidden\": false\n"
            "        }\n"
            "    ]\n"
            "}\n",
            FRESH_TERMINAL_GUID, escaped, escaped);
    fclose(f);
    return 1;
}

static int path_contains(const char *path, const char *entry) {
    size_t entry_length = strlen(entry);
    const char *cursor = path;
    while (*cursor) {
        const char *end = strchr(cursor, ';');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        if (length == entry_length && _strnicmp(cursor, entry, entry_length) == 0) return 1;
        if (!end) break;
        cursor = end + 1;
    }
    return 0;
}

static const char *environment_key(const InstallOptions *options) {
    return options->scope == INSTALL_USER
               ? "Environment"
               : "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";
}

static int add_to_path(const InstallOptions *options) {
    HKEY key;
    if (RegCreateKeyExA(scope_root(options), environment_key(options), 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, NULL, &key,
                        NULL) != ERROR_SUCCESS)
        return 0;

    char current[32768] = {0};
    DWORD size = sizeof(current) - 1;
    DWORD type = REG_EXPAND_SZ;
    RegQueryValueExA(key, "PATH", NULL, &type, (BYTE *)current, &size);

    if (path_contains(current, options->install_dir)) {
        RegCloseKey(key);
        return 1;
    }

    size_t length = strlen(current);
    if (length + strlen(options->install_dir) + 2 >= sizeof(current)) {
        RegCloseKey(key);
        return 0;
    }
    if (length > 0 && current[length - 1] != ';') strcat(current, ";");
    strcat(current, options->install_dir);

    LONG result = RegSetValueExA(key, "PATH", 0, REG_EXPAND_SZ, (BYTE *)current,
                                 (DWORD)strlen(current) + 1);
    RegCloseKey(key);

    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM) "Environment",
                        SMTO_ABORTIFHUNG, 5000, NULL);
    return result == ERROR_SUCCESS;
}

static int remove_from_path(InstallScope scope, const char *install_dir) {
    InstallOptions options;
    memset(&options, 0, sizeof(options));
    options.scope = scope;

    HKEY key;
    if (RegOpenKeyExA(scope_root(&options), environment_key(&options), 0, KEY_READ | KEY_WRITE,
                      &key) != ERROR_SUCCESS)
        return 0;

    char current[32768] = {0};
    DWORD size = sizeof(current) - 1;
    if (RegQueryValueExA(key, "PATH", NULL, NULL, (BYTE *)current, &size) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return 0;
    }

    char rebuilt[32768] = {0};
    char *token = strtok(current, ";");
    while (token) {
        if (_stricmp(token, install_dir) != 0) {
            if (rebuilt[0]) strcat(rebuilt, ";");
            strcat(rebuilt, token);
        }
        token = strtok(NULL, ";");
    }

    RegSetValueExA(key, "PATH", 0, REG_EXPAND_SZ, (BYTE *)rebuilt, (DWORD)strlen(rebuilt) + 1);
    RegCloseKey(key);
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM) "Environment",
                        SMTO_ABORTIFHUNG, 5000, NULL);
    return 1;
}

static int create_shortcut(const char *shortcut_path, const char *target, const char *working_dir) {
    IShellLinkA *link = NULL;
    IPersistFile *file = NULL;
    HRESULT hr;

    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA,
                          (void **)&link);
    if (SUCCEEDED(hr)) {
        link->lpVtbl->SetPath(link, target);
        link->lpVtbl->SetDescription(link, "FreSH - First-Run Experience Shell");
        link->lpVtbl->SetWorkingDirectory(link, working_dir);
        link->lpVtbl->SetIconLocation(link, target, 0);

        hr = link->lpVtbl->QueryInterface(link, &IID_IPersistFile, (void **)&file);
        if (SUCCEEDED(hr)) {
            WCHAR wide[MAX_PATH_LEN];
            MultiByteToWideChar(CP_ACP, 0, shortcut_path, -1, wide, MAX_PATH_LEN);
            hr = file->lpVtbl->Save(file, wide, TRUE);
            file->lpVtbl->Release(file);
        }
        link->lpVtbl->Release(link);
    }
    CoUninitialize();
    return SUCCEEDED(hr) ? 1 : 0;
}

static int shortcut_in_folder(const InstallOptions *options, int folder) {
    char base[MAX_PATH_LEN];
    if (SHGetFolderPathA(NULL, folder, NULL, 0, base) != S_OK) return 0;
    char shortcut[MAX_PATH_LEN];
    snprintf(shortcut, sizeof(shortcut), "%s\\FreSH.lnk", base);
    return create_shortcut(shortcut, options->exe_path, options->install_dir);
}

static void delete_shortcut(int folder) {
    char base[MAX_PATH_LEN];
    if (SHGetFolderPathA(NULL, folder, NULL, 0, base) != S_OK) return;
    char shortcut[MAX_PATH_LEN];
    snprintf(shortcut, sizeof(shortcut), "%s\\FreSH.lnk", base);
    DeleteFileA(shortcut);
}

int installer_perform(const InstallOptions *options, StepLogger log) {
    if (!write_payload(options)) {
        log(0, "Could not write FreSH.exe to the installation folder");
        return 0;
    }
    log(1, "FreSH.exe installed");

    log(write_uninstaller(options), "Uninstaller created");
    log(register_uninstall_entry(options), "Registered in Apps & Features");
    log(register_app_path(options), "Registered as a Windows application");

    if (options->add_to_path) log(add_to_path(options), "Added to PATH");
    if (options->terminal_profile)
        log(register_terminal_profile(options), "Windows Terminal profile created");
    if (options->context_menu) log(register_context_menu(options), "Explorer context menu added");
    if (options->script_association)
        log(register_script_association(options), "Registered as handler for .sh scripts");
    if (options->start_menu_shortcut)
        log(shortcut_in_folder(options, CSIDL_PROGRAMS), "Start Menu shortcut created");
    if (options->desktop_shortcut)
        log(shortcut_in_folder(options, CSIDL_DESKTOP), "Desktop shortcut created");

    return 1;
}

static int read_install_location(HKEY root, char *out, size_t out_size) {
    HKEY key;
    if (RegOpenKeyExA(root, UNINSTALL_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS) return 0;
    DWORD size = (DWORD)out_size;
    LONG result = RegQueryValueExA(key, "InstallLocation", NULL, NULL, (BYTE *)out, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

int installer_uninstall(StepLogger log) {
    HKEY roots[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    InstallScope scopes[] = {INSTALL_USER, INSTALL_SYSTEM};
    int removed_any = 0;

    for (int i = 0; i < 2; i++) {
        char install_dir[MAX_PATH_LEN] = "";
        if (!read_install_location(roots[i], install_dir, sizeof(install_dir))) continue;
        removed_any = 1;

        log(remove_from_path(scopes[i], install_dir), "Removed from PATH");

        delete_key_tree(roots[i], UNINSTALL_KEY);
        delete_key_tree(roots[i], APP_PATHS_KEY);
        delete_key_tree(roots[i], CONTEXT_KEY);
        delete_key_tree(roots[i], CONTEXT_DIR_KEY);
        delete_key_tree(roots[i], PROGID_KEY);
        RegDeleteKeyA(roots[i], "Software\\Classes\\.fresh");
        log(1, "Registry entries removed");

        InstallOptions options;
        memset(&options, 0, sizeof(options));
        options.scope = scopes[i];
        char fragment[MAX_PATH_LEN];
        if (terminal_fragment_path(&options, fragment, sizeof(fragment))) {
            char file[MAX_PATH_LEN];
            snprintf(file, sizeof(file), "%s\\fresh.json", fragment);
            DeleteFileA(file);
            RemoveDirectoryA(fragment);
            log(1, "Windows Terminal profile removed");
        }

        delete_shortcut(CSIDL_PROGRAMS);
        delete_shortcut(CSIDL_DESKTOP);
        log(1, "Shortcuts removed");

        char exe[MAX_PATH_LEN];
        snprintf(exe, sizeof(exe), "%s\\FreSH.exe", install_dir);
        log(DeleteFileA(exe) ? 1 : 0, "FreSH.exe removed");

        char command[MAX_PATH_LEN * 2];
        snprintf(command, sizeof(command),
                 "cmd.exe /c timeout /t 2 /nobreak > nul & rd /s /q \"%s\"", install_dir);
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        if (CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si,
                           &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        log(1, "Installation folder scheduled for removal");
    }

    if (!removed_any) log(0, "No FreSH installation found");
    return removed_any;
}
