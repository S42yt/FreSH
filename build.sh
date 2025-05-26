#!/bin/bash

echo -e "\e[32mBuilding FreSH with GCC...\e[0m"

# Create build directory if it doesn't exist
if [ ! -d "build" ]; then
    mkdir -p build
    echo -e "\e[33mCreated build directory\e[0m"
fi

# Create o directory if it doesn't exist
if [ ! -d "build/o" ]; then
    mkdir -p build/o
    echo -e "\e[33mCreated o directory\e[0m"
fi

# Compile source files
echo -e "\e[36mCompiling source files...\e[0m"
gcc -c main.c -o build/o/main.o
gcc -c shell_core.c -o build/o/shell_core.o
gcc -c shell_prompt.c -o build/o/shell_prompt.o
gcc -c command_parser.c -o build/o/command_parser.o
gcc -c error_handler.c -o build/o/error_handler.o
gcc -c history.c -o build/o/history.o
gcc -c builtins.c -o build/o/builtins.o
gcc -c git_branch.c -o build/o/git_branch.o
gcc -c bin_register.c -o build/o/bin_register.o

# Link object files
echo -e "\e[36mLinking...\e[0m"
gcc build/o/main.o build/o/shell_core.o build/o/shell_prompt.o build/o/command_parser.o build/o/error_handler.o build/o/history.o build/o/builtins.o build/o/git_branch.o build/o/bin_register.o -o build/FreSH.exe

# Check if build was successful
if [ $? -eq 0 ]; then
    echo -e "\e[32mBuild successful! Executable is in build/FreSH.exe\e[0m"
    # Make the executable file executable
    chmod +x build/FreSH.exe
else
    echo -e "\e[31mBuild failed!\e[0m"
fi