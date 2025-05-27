#!/bin/bash
# filepath: c:\Users\musa.bostanci\CLionProjects\untitled3\installation\build.sh
# FreSH Installation Wizard Build Script
# Copyright (c) 2025 Musa/S42

set -e  # Exit on any error

echo -e "\033[32m====================================\033[0m"
echo -e "\033[32m  Building FreSH Installation Wizard\033[0m"
echo -e "\033[32m====================================\033[0m"
echo

# Check if FreSH.exe exists in root build directory
if [ ! -f "../build/FreSH.exe" ]; then
    echo -e "\033[31mError:\033[0m FreSH.exe not found in ../build/"
    echo -e "\033[33mHint:\033[0m Please build FreSH first using the main build script"
    exit 1
fi

# Create build directory if it doesn't exist
if [ ! -d "build" ]; then
    mkdir -p build
    echo -e "\033[36mInfo:\033[0m Created build directory"
fi

echo -e "\033[36mInfo:\033[0m Copying FreSH.exe to installer build directory..."
# Copy FreSH.exe to build directory
cp "../build/FreSH.exe" "build/FreSH.exe"
echo -e "\033[32mSuccess:\033[0m FreSH.exe copied successfully"

echo
echo -e "\033[36mInfo:\033[0m Compiling installer with GCC..."

# Compile the installer with ANSI API versions (removed Unicode defines)
gcc -Wall -Wextra -Wno-format-truncation -std=c99 -O2 \
    -D_WIN32_WINNT=0x0601 \
    main.c installer.c tui.c \
    -o build/FreSH-Setup.exe \
    -lole32 -luuid -lshell32 -ladvapi32 -luser32

if [ $? -eq 0 ]; then
    echo
    echo -e "\033[32m====================================\033[0m"
    echo -e "\033[32m  Build Completed Successfully! 🎉\033[0m"
    echo -e "\033[32m====================================\033[0m"
    echo
    echo -e "\033[36mOutput:\033[0m build/FreSH-Setup.exe"
    echo -e "\033[36mSize:\033[0m $(du -h build/FreSH-Setup.exe | cut -f1)"
    echo
    echo -e "\033[33mInstaller Features:\033[0m"
    echo "  ✓ FreSH.exe bundled inside"
    echo "  ✓ Interactive TUI installation wizard"
    echo "  ✓ Windows shell registration"
    echo "  ✓ PATH environment setup"
    echo "  ✓ Start Menu and Desktop shortcuts"
    echo "  ✓ Uninstaller creation"
    echo
    echo -e "\033[33mTo test the installer:\033[0m"
    echo "  cd build && ./FreSH-Setup.exe"
    echo
else
    echo
    echo -e "\033[31mError:\033[0m Build failed! Check compilation errors above."
    exit 1
fi