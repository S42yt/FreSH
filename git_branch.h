/*
 * Copyright (c) 2025 Musa
 * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#ifndef GIT_BRANCH_H
#define GIT_BRANCH_H

typedef struct {
    int show_user;
    int show_branch;
    int show_repo;
} GitConfig;

char* get_git_branch(void);
char* get_git_user(void);
char* get_git_repo_name(void);
int is_git_repository(void);
void git_cleanup(void);

void git_config_init(void);
void git_config_set_user(int show);
void git_config_set_branch(int show);
void git_config_set_repo(int show);
int git_config_get_user(void);
int git_config_get_branch(void);
int git_config_get_repo(void);
void git_config_save(void);
void git_config_load(void);

#endif // GIT_BRANCH_H