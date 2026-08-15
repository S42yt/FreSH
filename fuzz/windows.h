/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_FUZZ_WINDOWS_H
#define FRESH_FUZZ_WINDOWS_H

#include <stdio.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

#define _lock_file(f) flockfile(f)
#define _unlock_file(f) funlockfile(f)
#define _fgetc_nolock(f) getc_unlocked(f)

typedef unsigned long DWORD;

#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x10u

static inline DWORD GetFileAttributesA(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) return INVALID_FILE_ATTRIBUTES;
    return S_ISDIR(info.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : 0;
}

#define _stricmp strcasecmp
#define _strnicmp strncasecmp

#endif
