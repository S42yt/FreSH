cmake_minimum_required(VERSION 3.30)
project(FreeShell C)

set(CMAKE_C_STANDARD 11)

dd_executable(FreeShell
    main.c
    shell_core.c
    shell_prompt.c
    command_parser.c
    error_handler.c
    history.c
    builtins.c
)

# Windows API libraries
target_link_libraries(FreeShell)
