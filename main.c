/*
 * Copyright (c) 2025 Musa
 * MIT License - See LICENSE file for details
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <windows.h>
#include "shell_core.h"
#include "shell_prompt.h"
#include "error_handler.h"
#include "history.h"

#define MAX_CMD 1024
#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_TAB 9
#define KEY_ENTER 13
#define KEY_BACKSPACE 8
#define KEY_CTRL_C 3
#define KEY_ESC 27

void read_command(char *buffer, int buffer_size) {
    int position = 0;
    int ch;
    int current_history_index = history_count();
    buffer[0] = '\0';

    while (1) {
        ch = _getch();

        if (ch == 0 || ch == 224) {
            ch = _getch();

            if (ch == KEY_UP) {
                if (current_history_index > 0) {
                    current_history_index--;
                    const char *cmd = history_get(current_history_index);
                    if (cmd) {
                        strcpy(buffer, cmd);
                        position = strlen(buffer);
                        printf("\r                                                                               \r");
                        print_shell_prompt();
                        printf("%s", buffer);
                    }
                }
            } else if (ch == KEY_DOWN) {
                if (current_history_index < history_count() - 1) {
                    current_history_index++;
                    const char *cmd = history_get(current_history_index);
                    if (cmd) {
                        strcpy(buffer, cmd);
                        position = strlen(buffer);
                    }
                } else {
                    current_history_index = history_count();
                    buffer[0] = '\0';
                    position = 0;
                }

                printf("\r                                                                               \r");
                print_shell_prompt();
                printf("%s", buffer);
            }
        } else if (ch == KEY_ENTER) {
            printf("\n");
            break;
        } else if (ch == KEY_BACKSPACE) {
            if (position > 0) {
                position--;
                buffer[position] = '\0';
                printf("\b \b");
            }
        } else if (ch == KEY_CTRL_C) {
            printf("^C\n");
            buffer[0] = '\0';
            print_shell_prompt();
            position = 0;
        } else if (ch == KEY_TAB) {
        } else if (ch >= 32 && ch <= 126) {
            if (position < buffer_size - 1) {
                buffer[position++] = ch;
                buffer[position] = '\0';
                printf("%c", ch);
            }
        }
    }

    if (buffer[0] != '\0') {
        history_add(buffer);
    }
}

int main() {
    char input[MAX_CMD];

    shell_init();
    prompt_init();

    history_load();

    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║ FreeShell 2.0 - Enhanced and Optimized Shell             ║\n");
    printf("║ Type 'help' for commands, 'exit' to quit                 ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");

    while (1) {
        print_shell_prompt();
        read_command(input, sizeof(input));

        if (strlen(input) == 0) continue;

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
