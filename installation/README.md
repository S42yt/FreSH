# FreSH Installation Wizard

A comprehensive TUI-based installation wizard for FreSH (First-Run Experience Shell).

## Features

- **Beautiful TUI Interface**: ASCII art logo and intuitive navigation
- **Two Installation Types**:
  - Current User Installation (recommended, no admin rights required)
  - System-Wide Installation (requires admin privileges)
- **Complete Shell Integration**:
  - Registers FreSH as a Windows shell
  - Adds FreSH to system PATH
  - Creates Start Menu and Desktop shortcuts
- **Bundled Executable**: FreSH.exe is embedded in the installer
- **License Agreement**: Shows MIT license before installation
- **Progress Indication**: Visual progress bar during installation
- **Uninstaller**: Creates uninstaller for easy removal

## Building the Installer

### Prerequisites
- GCC compiler (MinGW for Windows)
- FreSH.exe built in the parent `build/` directory

### Windows (Command Prompt)
```cmd
build.bat
```

### Windows (PowerShell) / Unix
```bash
chmod +x build.sh
./build.sh
```

### Using Make
```bash
make all
make bundle  # Create distributable package
```

## File Structure

```
installation/
├── main.c           # Main entry point
├── installer.h      # Core installer definitions
├── installer.c      # Installation logic
├── tui.h           # TUI interface definitions  
├── tui.c           # TUI implementation
├── build.bat       # Windows build script
├── build.sh        # Unix build script
├── Makefile        # Make build configuration
└── build/          # Build output directory
    ├── FreSH.exe       # Bundled FreSH executable
    └── FreSH-Setup.exe # Final installer
```

## Installation Process

1. **Welcome Screen**: Shows FreSH logo and features
2. **License Agreement**: MIT license acceptance
3. **Installation Type**: Choose user or system-wide installation
4. **Confirmation**: Review settings before proceeding
5. **Installation**: Progress bar shows current step
6. **Completion**: Success message with next steps

## What the Installer Does

1. **Copies FreSH.exe** to the chosen installation directory
2. **Registers FreSH** as a Windows shell in the registry
3. **Updates PATH** environment variable to include FreSH
4. **Creates shortcuts** in Start Menu and Desktop
5. **Creates uninstaller** for easy removal
6. **Notifies system** of environment changes

## Usage After Installation

Once installed, you can:
- Open any terminal and type `FreSH` to start
- Use the Desktop or Start Menu shortcuts
- FreSH is registered as a Windows shell option

## Technical Details

- **Registry Integration**: Registers in `SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon\AlternateShells`
- **Environment Variables**: Updates PATH in user or system environment
- **COM Interface**: Uses Windows Shell API for shortcut creation
- **Unicode Support**: Handles Unicode paths and console output
- **Error Handling**: Graceful fallbacks for non-critical operations

## Uninstallation

Run the uninstaller created in the installation directory:
```cmd
"C:\Path\To\FreSH\Uninstall.exe"
```

## License

MIT License - See LICENSE file for details.
Copyright (c) 2025 Musa/S42
