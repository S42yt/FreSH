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

