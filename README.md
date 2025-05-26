# FRESH - First-Run Experience Shell

A powerful bash-like shell written in C for Windows with extensive built-in commands and git integration.

## Features

- **Bash-like prompt**: `user@hostname:~/path (git-branch)$ `
- **Comprehensive built-ins**: pwd, echo, ls, cat, grep, find, head, tail, wc, and more
- **Git integration**: Shows current branch in prompt
- **Command history**: Persistent history with arrow key navigation
- **Aliases**: Create custom command shortcuts
- **Environment variables**: Built-in export and env commands
- **Clean error handling**: Terminal-style error messages
- **Tab completion**: File and directory name completion
- **Cross-platform**: Works on Windows with bash-style commands

## Built-in Commands

### Core Commands
- `help` - Display help information
- `exit`, `quit` - Exit the shell
- `cd [dir]` - Change directory
- `pwd` - Print working directory
- `clear`, `cls` - Clear screen

### File Operations
- `ls`, `dir` - List directory contents
- `cat`, `type [file]` - Display file contents
- `cp [src] [dest]` - Copy files
- `mv [src] [dest]` - Move/rename files
- `rm`, `del [file]` - Remove files
- `mkdir [dir]` - Create directory
- `find [path] [pattern]` - Search for files
- `grep [pattern] [file]` - Search text in files

### Text Processing
- `head [file]` - Show first 10 lines
- `tail [file]` - Show last 10 lines
- `wc [file]` - Count lines, words, characters

### Environment
- `env` - Show environment variables
- `export [var=value]` - Set environment variable
- `which [command]` - Locate command
- `alias [name=value]` - Create aliases
- `history` - Show command history

## Building

### Using GCC (recommended)
```bash
./build.sh
```

### Using PowerShell
```powershell
./build.ps1
```

### Using CMake
```bash
mkdir build && cd build
cmake ..
make
```

## Usage

Run the executable:
```bash
./build/FRESH.exe
```

The shell provides a familiar bash-like experience with Windows compatibility.

