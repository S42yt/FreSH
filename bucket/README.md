# Scoop bucket

This directory is a Scoop bucket, so the repository can be added directly:

```
scoop bucket add fresh https://github.com/S42yt/FreSH
scoop install fresh            # the installer, registers FreSH as a shell
scoop install fresh-portable   # the single executable, nothing registered
```

Both manifests are stamped with the version and the SHA-256 of the release by
`tools/manifests.sh`, which the release workflow runs after it has built and
hashed the binaries. `checkver` and `autoupdate` point at the release tags and
at `SHA256SUMS.txt`, so a future release is picked up without editing anything
by hand.

Installing through Scoop also sidesteps the SmartScreen prompt that an
unsigned download shows, because Scoop verifies the hash itself. See
[verifying a download](../docs/verifying.md).
