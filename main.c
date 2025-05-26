/*
 * Copyright (c) 2025 Musa
  * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "shell_core.h"
#include "shell_prompt.h"
#include "error_handler.h"
#include "history.h"
#include "bin_register.h"

#define MAX_CMD 1024

void read_command(char *buffer, int buffer_size) {
    if (fgets(buffer, buffer_size, stdin) != NULL) {

        buffer[strcspn(buffer, "\r\n")] = 0;
    } else {
        buffer[0] = '\0';
    }
}

int main() {
    char input[MAX_CMD];

    shell_init();
    prompt_init();


    register_bin_commands();

    history_load();
    printf("FreSH v1.0 - First-Run Experience Shell\n");
    printf("Type 'help' for available commands, 'exit' to quit\n\n");

    while (1) {
        print_shell_prompt();
        read_command(input, sizeof(input));

        if (strlen(input) == 0) continue;

        history_add(input);
        int result = shell_execute(input);

        if (!is_shell_running()) {
            break;
        }
    }

    history_save();

    prompt_cleanup();
    shell_cleanup();

    return 0;
}
