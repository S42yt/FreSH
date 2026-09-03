/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#include "theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"

#include "exec.h"
#include "shell.h"
#include "style.h"
#include "util.h"
#include "vars.h"

typedef struct {
    const char *name;
    const char *body;
} BundledFile;

static const BundledFile THEMES[] = {
    {"fresh",
     "# fresh, the default. Two lines, lambda, git branch.\n"
     "FRESH_PROMPT='%F{green}%n%f %F{cyan}%~%f%g\\n%F{brightwhite}%#%f '\n"
     "FRESH_RPROMPT='%e'\n"},

    {"minimal",
     "# One line, nothing but the path and an arrow.\n"
     "FRESH_PROMPT='%F{blue}%~%f %F{grey}>%f '\n"
     "FRESH_RPROMPT=''\n"},

    {"classic",
     "# The bash look: user@host:path$\n"
     "FRESH_PROMPT='%F{green}%n@%m%f:%F{blue}%d%f$ '\n"
     "FRESH_RPROMPT=''\n"},

    {"powerline",
     "# Block segments. Needs a font with powerline glyphs, such as Cascadia Code PL.\n"
     "FRESH_PROMPT='%K{blue}%F{black} %~ %f%k%F{blue}\\ue0b0%f%g\\n%F{brightwhite}%#%f '\n"
     "FRESH_RPROMPT='%F{grey}%t%f'\n"},

    {"lambda",
     "# Just the lambda, path and branch on the right.\n"
     "FRESH_PROMPT='%F{magenta}%#%f '\n"
     "FRESH_RPROMPT='%F{grey}%~%f%g'\n"},

    {"full",
     "# Everything on show: time, user, host, path, git, exit code.\n"
     "FRESH_PROMPT='%F{grey}%t%f %F{green}%n@%m%f %F{cyan}%~%f%g\\n%F{brightwhite}%#%f '\n"
     "FRESH_RPROMPT='%e'\n"},
};

static const BundledFile PLUGINS[] = {
    {"git",
     "# git shortcuts\n"
     "alias g='git'\n"
     "alias gs='git status'\n"
     "alias ga='git add'\n"
     "alias gc='git commit'\n"
     "alias gco='git checkout'\n"
     "alias gb='git branch'\n"
     "alias gd='git diff'\n"
     "alias gl='git log --oneline --graph --decorate'\n"
     "alias gp='git push'\n"
     "alias gpl='git pull'\n"
     "\n"
     "groot() { cd \"$(git rev-parse --show-toplevel)\"; }\n"
     "gclean() { git branch --merged | grep -v '\\*' ; }\n"
     "\n"
     "describe groot 'go to the top of the repository' 'groot'\n"
     "describe gclean 'list the branches already merged' 'gclean'\n"},

    {"dirs",
     "# directory helpers\n"
     "alias ..='cd ..'\n"
     "alias ...='cd ../..'\n"
     "alias ....='cd ../../..'\n"
     "alias ll='ls -l'\n"
     "alias la='ls -la'\n"
     "alias l='ls'\n"
     "\n"
     "mkcd() { mkdir -p \"$1\"; cd \"$1\"; }\n"
     "up() { cd ..; ls; }\n"
     "\n"
     "describe mkcd 'make a directory and go into it' 'mkcd <directory>'\n"
     "describe up 'go up one directory and list it' 'up'\n"},

    {"sys",
     "# system helpers\n"
     "alias path='echo $PATH'\n"
     "alias reload='source ~/.freshrc'\n"
     "alias h='history'\n"
     "alias c='clear'\n"
     "\n"
     "ports() { netstat -ano | grep LISTENING; }\n"
     "psg() { ps | grep \"$1\"; }\n"
     "\n"
     "describe ports 'show the ports being listened on' 'ports'\n"
     "describe psg 'find a running process by name' 'psg <pattern>'\n"},

    {"edit",
     "# open things in your editor\n"
     "alias e='$EDITOR'\n"
     "alias rc='$EDITOR ~/.freshrc'\n"
     "\n"
     "edit() { open \"$1\"; }\n"
     "\n"
     "describe edit 'open a file in your editor' 'edit <file>'\n"},
};

static char *fresh_home(void) {
    const char *custom = var_get("FRESH_HOME");
    if (custom && *custom) return xstrdup(custom);
    return config_path(".fresh");
}

static char *bundle_directory(const char *kind) {
    char *home = fresh_home();
    char *directory = path_join(home, kind);
    free(home);
    return directory;
}

static void write_bundle(const char *directory, const BundledFile *files, size_t count,
                         const char *extension, int overwrite) {
    for (size_t i = 0; i < count; i++) {
        char name[PATH_BUF];
        snprintf(name, sizeof(name), "%s%s", files[i].name, extension);
        char *target = path_join(directory, name);
        if (overwrite || !path_is_file(target)) {
            FILE *f = fopen(target, "wb");
            if (f) {
                fputs(files[i].body, f);
                fclose(f);
            }
        }
        free(target);
    }
}

static void install_bundles(int overwrite) {
    char *themes = bundle_directory("themes");
    char *plugins = bundle_directory("plugins");

    path_mkdirs(themes);
    path_mkdirs(plugins);
    write_bundle(themes, THEMES, sizeof(THEMES) / sizeof(THEMES[0]), ".theme", overwrite);
    write_bundle(plugins, PLUGINS, sizeof(PLUGINS) / sizeof(PLUGINS[0]), ".plugin", overwrite);

    free(themes);
    free(plugins);
}

void fresh_home_init(void) {
    install_bundles(0);
}

static int source_bundle(const char *kind, const char *name, const char *extension) {
    char *directory = bundle_directory(kind);
    char file[PATH_BUF];
    snprintf(file, sizeof(file), "%s%s", name, extension);
    char *target = path_join(directory, file);
    free(directory);

    int found = path_is_file(target);
    if (found) exec_script_file(target, NULL);
    free(target);
    return found;
}

void theme_load_configured(void) {
    const char *name = var_get("FRESH_THEME");
    if (!name || !*name) name = "fresh";
    if (str_ieq(name, "none")) return;
    if (!source_bundle("themes", name, ".theme"))
        shell_error("theme %s not found in ~/.fresh/themes", name);
}

void plugins_load_configured(void) {
    const char *list = var_get("FRESH_PLUGINS");
    if (!list || !*list) return;

    char *copy = xstrdup(list);
    char *cursor = copy;
    char *name;
    while ((name = str_next_field(&cursor, ' ')) != NULL) {
        char *trimmed = str_trim(name);
        if (!*trimmed) continue;
        if (!source_bundle("plugins", trimmed, ".plugin"))
            shell_error("plugin %s not found in ~/.fresh/plugins", trimmed);
    }
    free(copy);
}

static void list_bundle(const char *kind, const char *extension, const char *active) {
    char *directory = bundle_directory(kind);
    char pattern[PATH_BUF];
    snprintf(pattern, sizeof(pattern), "%s\\*%s", directory, extension);

    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        printf("  %snothing in %s%s\n", style(S_DIM), directory, style(S_RESET));
        free(directory);
        return;
    }

    do {
        char name[PATH_BUF];
        snprintf(name, sizeof(name), "%s", data.cFileName);
        char *dot = strrchr(name, '.');
        if (dot) *dot = '\0';

        int is_active = active && strstr(active, name) != NULL;
        printf("  %s%s%s%s\n", is_active ? style(S_ACCENT) : "", is_active ? "* " : "  ", name,
               is_active ? style(S_RESET) : "");
    } while (FindNextFileA(find, &data));
    FindClose(find);

    printf("  %s%s%s\n", style(S_DIM), directory, style(S_RESET));
    free(directory);
}

int builtin_theme(int argc, char **argv) {
    const char *current = var_get("FRESH_THEME");
    if (argc < 2) {
        printf("  %sthemes%s\n", style(S_LABEL), style(S_RESET));
        list_bundle("themes", ".theme", current ? current : "fresh");
        printf("  %sset FRESH_THEME in ~/.freshrc to keep one%s\n", style(S_DIM), style(S_RESET));
        return 0;
    }

    if (strcmp(argv[1], "reset") == 0) {
        install_bundles(1);
        printf("  %sbundled themes and plugins rewritten%s\n", style(S_DIM), style(S_RESET));
        return 0;
    }

    if (!source_bundle("themes", argv[1], ".theme")) {
        shell_error("theme: %s: not found", argv[1]);
        return 1;
    }
    var_set("FRESH_THEME", argv[1]);
    return 0;
}

int builtin_plugin(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "list") == 0) {
        printf("  %splugins%s\n", style(S_LABEL), style(S_RESET));
        list_bundle("plugins", ".plugin", var_get("FRESH_PLUGINS"));
        printf("  %sset FRESH_PLUGINS=\"git dirs\" in ~/.freshrc to load them%s\n", style(S_DIM),
               style(S_RESET));
        return 0;
    }

    if (strcmp(argv[1], "load") == 0 && argc > 2) {
        int status = 0;
        for (int i = 2; i < argc; i++) {
            if (source_bundle("plugins", argv[i], ".plugin")) continue;
            shell_error("plugin: %s: not found", argv[i]);
            status = 1;
        }
        return status;
    }

    shell_error("plugin: usage: plugin [list] | plugin load <name>...");
    return 2;
}
