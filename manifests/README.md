# winget manifests

Two packages, one directory per version, in the layout
[microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs) expects:

| Package | What it installs |
| --- | --- |
| `S42yt.FreSH` | the installer, which registers FreSH as a shell |
| `S42yt.FreSH.Portable` | the single executable, nothing registered |

`tools/manifests.sh` copies the newest version directory to the new version and
stamps it with the release's SHA-256, which the release workflow runs after the
binaries are built and hashed.

## Installing from here

These manifests live in this repository. `winget` searches Microsoft's
community index, which is a different repository, so until FreSH is accepted
there `winget install S42yt.FreSH` will say the package was not found. From a
clone, this works today:

```powershell
winget install --manifest manifests\S42yt.FreSH\26.10.0
winget install --manifest manifests\S42yt.FreSH.Portable\26.10.0
```

Check a manifest before submitting it:

```powershell
winget validate --manifest manifests\S42yt.FreSH\26.10.0
```

## Getting into the community index

Submitting means opening a pull request against `microsoft/winget-pkgs` with
these files copied to `manifests/s/S42yt/FreSH/<version>/`. A bot validates
the installer, a moderator reviews it, and once it is merged
`winget install S42yt.FreSH` works for everyone. Later versions are the same
pull request with a new directory, which `wingetcreate update` can open.

Until that lands, the ways to install are in the
[README](../README.md#installation): the installer, the portable exe, or the
[Scoop bucket](../bucket/README.md), which does work from this repository
because a Scoop bucket is just a git repository.
