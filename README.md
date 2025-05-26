# FreSH - First-Run Experience Shell

A fast bash-like shell written in C for Windows.

**Important:** Global Installation of FreSH is not supported from the official repo rn. Support will come soon.

_Note: Would probably work on UNIX or Linux but its not the focus so no support is planned._

![TUI](./assets/FreSH_tui.png)

## Building

### Prerequisites
- [GCC Compiler](https://gcc.gnu.org/)
- [Astyle](http://astyle.sourceforge.net/) 
- Any IDE with proper C support.

### Developement

There are 2 versions to build it:

- [Powershell File(.ps1)](./build.ps1)
- [Shell File(.sh)](./build.sh)

Both can be used on windows.
(For .sh you may need a bash compiling terminal like WSL or Git Bash)

Format:
```sh
astyle --style=google --indent=spaces=4 --suffix=none *.c *.h
```

Build:
```sh
./build.ps1
#or
./build.sh
```

Start the Shell
```sh
./build/FreSH.exe
```

## Inside the Shell
Inside the Shell there are many native commands but all commands saved in your bin folder are also supported.

### Native Commands

```sh
help            Show the help message
exit/quit       Exit the shell
cd [dir]        Change directory
pwd             Print working directory
ls              List directory contents 
clear           Clear the screen completely
history         Show command history
binlist         Show registered bin commands
which <cmd>     Show path to command
echo <text>     Print text to screen
shinfo          Show shell script support info
gitinfo         Show git repository information
gitconfig       Configure git display 
```

## License

This project is Licensed under a [MIT LICENSE](LICENSE)

## Contributing
_Soon_