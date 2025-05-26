cmake_minimum_required(VERSION 3.30)
project(FreSH C)

set(CMAKE_C_STANDARD 11)

add_executable(FreSH
    main.c
    shell_core.c
    shell_prompt.c
    command_parser.c
    error_handler.c
    history.c
    builtins.c
    git_branch.c
    bin_register.c
)

# Windows API libraries
target_link_libraries(FreSH)
