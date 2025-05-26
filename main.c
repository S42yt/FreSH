/*
 * Copyright (c) 2025 Musa
  * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <conio.h>
#include "shell_core.h"
#include "shell_prompt.h"
#include "error_handler.h"
#include "history.h"
#include "bin_register.h"
#include "git_branch.h"

#define MAX_CMD 1024
#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_LEFT 75
#define KEY_RIGHT 77
#define KEY_ENTER 13
#define KEY_BACKSPACE 8
#define KEY_DELETE 83
#define KEY_HOME 71
#define KEY_END 79
#define KEY_ESC 27

static int history_index = -1;
static char current_input[MAX_CMD] = {0};

void clear_current_line(int cursor_pos) {
    
    printf("\r$ ");
    
    for (int i = 0; i < cursor_pos + 10; i++) {
        printf(" ");
    }
    printf("\r$ ");
}

void read_command_with_history(char *buffer, int buffer_size) {
    int cursor_pos = 0;
    int input_len = 0;
    char input[MAX_CMD] = {0};
    int ch;
    
    
    history_index = -1;
    strcpy(current_input, "");
    
    while (1) {
        ch = _getch();
        
        if (ch == 0 || ch == 224) { 
            ch = _getch();
            
            switch (ch) {
                case KEY_UP: {
                    if (history_index == -1) {
                        
                        strcpy(current_input, input);
                        history_index = history_count() - 1;
                    } else if (history_index > 0) {
                        history_index--;
                    }
                    
                    if (history_index >= 0 && history_index < history_count()) {
                        const char *hist_cmd = history_get(history_index);
                        if (hist_cmd) {
                            clear_current_line(input_len);
                            strcpy(input, hist_cmd);
                            input_len = strlen(input);
                            cursor_pos = input_len;
                            printf("%s", input);
                        }
                    }
                    break;
                }
                
                case KEY_DOWN: {
                    if (history_index != -1) {
                        if (history_index < history_count() - 1) {
                            history_index++;
                            const char *hist_cmd = history_get(history_index);
                            if (hist_cmd) {
                                clear_current_line(input_len);
                                strcpy(input, hist_cmd);
                                input_len = strlen(input);
                                cursor_pos = input_len;
                                printf("%s", input);
                            }
                        } else {
                            
                            history_index = -1;
                            clear_current_line(input_len);
                            strcpy(input, current_input);
                            input_len = strlen(input);
                            cursor_pos = input_len;
                            printf("%s", input);
                        }
                    }
                    break;
                }
                
                case KEY_LEFT: {
                    if (cursor_pos > 0) {
                        cursor_pos--;
                        printf("\b");
                    }
                    break;
                }
                
                case KEY_RIGHT: {
                    if (cursor_pos < input_len) {
                        printf("%c", input[cursor_pos]);
                        cursor_pos++;
                    }
                    break;
                }
                
                case KEY_HOME: {
                    while (cursor_pos > 0) {
                        printf("\b");
                        cursor_pos--;
                    }
                    break;
                }
                
                case KEY_END: {
                    while (cursor_pos < input_len) {
                        printf("%c", input[cursor_pos]);
                        cursor_pos++;
                    }
                    break;
                }
                
                case KEY_DELETE: {
                    if (cursor_pos < input_len) {
                        
                        for (int i = cursor_pos; i < input_len - 1; i++) {
                            input[i] = input[i + 1];
                        }
                        input_len--;
                        input[input_len] = '\0';
                        
                        
                        printf("%s ", input + cursor_pos);
                        
                        for (int i = cursor_pos; i <= input_len; i++) {
                            printf("\b");
                        }
                    }
                    break;
                }
            }
        } else {
            
            switch (ch) {
                case KEY_ENTER: {
                    printf("\n");
                    strncpy(buffer, input, buffer_size - 1);
                    buffer[buffer_size - 1] = '\0';
                    return;
                }
                
                case KEY_BACKSPACE: {
                    if (cursor_pos > 0) {
                        
                        for (int i = cursor_pos - 1; i < input_len - 1; i++) {
                            input[i] = input[i + 1];
                        }
                        cursor_pos--;
                        input_len--;
                        input[input_len] = '\0';
                        
                        
                        printf("\b%s ", input + cursor_pos);
                        
                        for (int i = cursor_pos; i <= input_len; i++) {
                            printf("\b");
                        }
                    }
                    break;
                }
                
                case KEY_ESC: {
                    
                    clear_current_line(input_len);
                    input[0] = '\0';
                    input_len = 0;
                    cursor_pos = 0;
                    history_index = -1;
                    break;
                }
                
                default: {
                    
                    if (ch >= 32 && ch <= 126 && input_len < buffer_size - 1) {
                        
                        for (int i = input_len; i > cursor_pos; i--) {
                            input[i] = input[i - 1];
                        }
                        input[cursor_pos] = ch;
                        input_len++;
                        input[input_len] = '\0';
                        
                        
                        printf("%s", input + cursor_pos);
                        cursor_pos++;
                        
                        
                        for (int i = cursor_pos; i < input_len; i++) {
                            printf("\b");
                        }
                    }
                    break;
                }
            }
        }
        
        
        if (history_index == -1) {
            strcpy(current_input, input);
        }
    }
}

int main() {
    char input[MAX_CMD];

    shell_init();
    prompt_init();
    git_config_init();

    register_bin_commands();

    history_load();
    printf("FreSH v1.0 - First-Run Experience Shell\n");
    printf("Type 'help' for available commands, 'exit' to quit\n");
    printf("Use Up/Down arrows for command history, Ctrl+C to exit\n\n");

    while (1) {
        print_shell_prompt();
        read_command_with_history(input, sizeof(input));

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
