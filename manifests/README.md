# winget manifests

Two packages, one directory per version, in the layout
[microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs) expects:

| Package | What it installs |
| --- | --- |
| `S42yt.FreSH` | the installer, which registers FreSH as a shell |
| `S42yt.FreSH.Portable` | the single executable, nothing registered |

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

Every version needs a pull request against `microsoft/winget-pkgs`, one per
package. `tools/winget-submit.sh <version>` opens both:

```sh
bash tools/winget-submit.sh 26.10.0
```

The release workflow runs it on a stable tag if the `WINGET_TOKEN` secret is
set, a classic GitHub token with the `public_repo` scope.

Until they merge, install with the installer, the portable exe, or the
[Scoop bucket](../bucket/README.md).

## While the first pull requests are open

A package that is not in the community index yet has one "New package" pull
request per manifest waiting on a moderator. Until those merge there is nothing
to update, so `tools/winget-submit.sh` checks for them and skips rather than
opening a second submission for the same package. Once they merge, catch winget
up with the current release by hand:

```sh
bash tools/winget-submit.sh 26.11.0
```