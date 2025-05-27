/*
* Copyright (c) 2025 Musa/S42
 * FreSH - First-Run Experience Shell
 * MIT License - See LICENSE file for details
 */


#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

typedef enum {
    ERROR_NONE_VAL = 0,
    ERROR_COMMAND_NOT_FOUND_VAL,
    ERROR_PERMISSION_DENIED_VAL,
    ERROR_DIRECTORY_NOT_FOUND_VAL,
    ERROR_FILE_NOT_FOUND_VAL,
    ERROR_PIPE_CREATION_FAILED_VAL,
    ERROR_PROCESS_CREATION_FAILED_VAL,
    ERROR_INVALID_ARGUMENT_VAL,
    ERROR_GENERAL_VAL
} ErrorCode;

void error_handler_init();
void show_error(int error_code, const char *additional_info);
void show_error_message(const char *message);
void error_handler_cleanup();

#endif //ERROR_HANDLER_H
