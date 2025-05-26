Write-Host "Building FreSH with GCC..." -ForegroundColor Green

# Create build directory if it doesn't exist
if (!(Test-Path -Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
    Write-Host "Created build directory" -ForegroundColor Yellow
}

if (!(Test-Path -Path "build/o")) {
    New-Item -ItemType Directory -Path "build/o" | Out-Null
    Write-Host "Created o directory" -ForegroundColor Yellow
}

# Compile source files
Write-Host "Compiling source files..." -ForegroundColor Cyan
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
Write-Host "Linking..." -ForegroundColor Cyan
gcc build/o/main.o build/o/shell_core.o build/o/shell_prompt.o build/o/command_parser.o build/o/error_handler.o build/o/history.o build/o/builtins.o build/o/git_branch.o build/o/bin_register.o -o build/FreSH.exe

# Check if build was successful
if ($?) {
    Write-Host "Build successful! Executable is in build/FreSH.exe" -ForegroundColor Green
} else {
    Write-Host "Build failed!" -ForegroundColor Red
}