# Installing FreSH

Every way to get FreSH, what each one does to your machine, and how to check
what you downloaded before you run it.

### Scoop

```sh
scoop bucket add fresh https://github.com/S42yt/FreSH
scoop install fresh/fresh              # the full install
scoop install fresh/fresh-portable     # the single executable
```

Scoop verifies the hash itself, so there is no SmartScreen prompt. A Scoop
bucket is just a git repository, which is why this works straight from here.

### winget

Not yet. `winget install S42yt.FreSH` searches Microsoft's community index,
which FreSH has not been submitted to, so it answers *no package found*. The
manifests are written, validated and laid out the way that index expects, in
[manifests](../manifests/), so from a clone this works today:

```powershell
winget install --manifest manifests\S42yt.FreSH\26.10.0
```

This section loses the "not yet" when the pull request to
`microsoft/winget-pkgs` is merged.

### Portable

Download `FreSH.exe`, put it anywhere on your `PATH`, run `FreSH`. No installer,
no registry, no administrator. Settings still live in `~/.freshrc`, and nothing
about the shell depends on having been installed.

### The installer

![Setup](../assets/FreSH_wizard.png)

1. Download `FreSH-Setup.exe` from the releases page
2. Run it and pick **Just for me** (no admin) or **For all users**
3. Say whether FreSH should become your default shell
4. Open a new terminal and type `FreSH`

The installer registers FreSH the way Windows expects a shell to be
registered:

- adds a **Windows Terminal profile**, so FreSH sits next to PowerShell in the
  dropdown
- optionally makes it the **default profile**, backing up `settings.json` first
- registers under **App Paths**, so `FreSH` works from the Run dialog
- adds it to **PATH**
- adds **Open FreSH here** to the Explorer folder context menu
- registers FreSH as a handler for `.frsh` and `.sh` scripts
- creates Start Menu and Desktop shortcuts
- adds an entry to **Apps & Features** with a working uninstaller

Restart Windows Terminal after installing. Fragments are only read at startup.

Silent install:

```
FreSH-Setup.exe /silent /user /default
```

Uninstall from *Settings > Apps > FreSH*, or run `Uninstall-FreSH.exe` from the
install folder.

### Windows on ARM

Every release ships `FreSH-Setup-arm64.exe` and `FreSH-arm64.exe`, native
ARM64 builds of the installer and the portable executable. They register and
behave exactly like the x64 ones. Scoop and the winget manifests pick the
right one for the machine; when downloading by hand, take the `arm64` files on
a Snapdragon or other ARM device and the plain ones everywhere else. The x64
build also runs on ARM under emulation, so nothing breaks if you take the
wrong one, it is only slower to start.

### macOS and Linux

One line, on either:

```sh
sh -c "$(curl -fsSL https://raw.githubusercontent.com/S42yt/FreSH/master/install.sh)"
```

That downloads `fresh-setup` for your system and runs it. It is the same
wizard as the Windows installer, in text:

1. pick **Just for me** (`~/.local/bin`, no sudo) or **For all users**
   (`/usr/local/bin`)
2. say whether FreSH should become your login shell, which lists it in
   `/etc/shells` and runs `chsh`
3. open a new terminal and type `fresh`

Silent, the way the Windows installer takes `/silent /user /default`:

```sh
sh -c "$(curl -fsSL https://raw.githubusercontent.com/S42yt/FreSH/master/install.sh)" -- --silent --user
fresh-setup --silent --system --default
fresh-setup --uninstall
```

Piped into `sh` with no terminal, `install.sh` installs for the current user
silently. `FRESH_VERSION=26.11.1-prerelease-3` pins a release.

Every release also ships the pieces by hand: `fresh-setup-macos-arm64`,
`fresh-setup-macos-x86_64`, `fresh-setup-linux-x86_64` and
`fresh-setup-linux-arm64` are the installers, and `fresh-macos-*` /
`fresh-linux-*` are the bare binaries for people who prefer to place them
themselves:

```sh
curl -fsSL -o fresh https://github.com/S42yt/FreSH/releases/latest/download/fresh-macos-$(uname -m)
chmod +x fresh
xattr -d com.apple.quarantine fresh 2> /dev/null
sudo mv fresh /usr/local/bin/fresh
```

The `xattr` line removes the quarantine flag Gatekeeper puts on a download, the
macOS equivalent of the SmartScreen prompt. The binaries are not notarized, so
without it macOS refuses to start them; the setup binary needs the same. Verify
a file the same way as on Windows, with
`gh attestation verify fresh --repo S42yt/FreSH` or against `SHA256SUMS.txt`.

`fresh update` works on macOS too: it downloads the new binary next to the
running one and swaps it in, so the directory it lives in has to be writable
by you, or the update tells you to fetch the file yourself. Releases before
26.11.1-prerelease-2 have no macOS binary, so the selector marks them
`not for this platform` and refuses to install them there.

The Linux binaries are linked statically, so they run on any distribution
without caring which libc it has. `fresh update` swaps the binary in place on
both systems, and the first release it can install is 26.11.1-prerelease-2 on
macOS and 26.11.1-prerelease-3 on Linux. On Linux the clipboard builtins use
`xclip` or `wl-copy` when one is installed, and `open` goes through
`xdg-open`.

What differs from Windows on both is small and written down in
[commands](commands.md#platforms): the PowerShell and cmd routing does not
exist, `copy` and `paste` use the system clipboard tools, `admin` runs through
`sudo`, and the bundled unix commands step aside for the real ones on `PATH`
exactly as they do on Windows.

### Checking what you downloaded

FreSH is not code signed yet, so Windows shows a SmartScreen prompt the first
time you run a downloaded binary. Rather than tell you to click through it,
every release ships `SHA256SUMS.txt` and a signed build provenance statement:

```
gh attestation verify FreSH.exe --repo S42yt/FreSH
```

That proves the file came out of a GitHub runner, from this repository, from
the commit the release names. More in [verifying a download](verifying.md)
and the [signing policy](code-signing.md).

## Updating

## Running it

```sh
FreSH script.sh arg1 arg2   # run a script
FreSH -c "echo hello"       # run one command
FreSH --norc -c "echo hi"   # skip ~/.freshrc, the theme and the plugins
FreSH C:\some\folder        # start in a folder
FreSH --version
```

