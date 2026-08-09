/*
 * Copyright (c) 2025-2026 Musa Bostanci
 * FreSH - First-Run Experience Shell
 * GNU General Public License v3.0 - See LICENSE file for details
 */

#ifndef FRESH_GITINFO_H
#define FRESH_GITINFO_H

#include <stddef.h>

int git_repo_root(char *out, size_t out_size);
const char *git_branch(void);
const char *git_repo_name(void);
const char *git_user(void);
int git_dirty(void);
void git_invalidate(void);

#endif
