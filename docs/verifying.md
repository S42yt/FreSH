# Verifying a download

FreSH is not signed with a code signing certificate, so Windows shows a
SmartScreen prompt the first time you run `FreSH-Setup.exe` or `FreSH.exe`.
That prompt means "this file has no certificate and has not been downloaded
often yet". It does not mean anything is wrong, and it is not something the
project can wave away by telling you to click through.

What the project can do is give you a way to check, which is stronger than a
certificate: a certificate proves who compiled a file, while build provenance
proves **what source it was compiled from, on whose machine**.

## Build provenance

Every release is built by GitHub Actions from a tagged commit, and the workflow
attaches a signed provenance statement to each binary. To check it:

```
gh attestation verify FreSH.exe --repo S42yt/FreSH
```

A pass means that exact file came out of a GitHub runner, from this repository,
from the commit the release names. Nobody, including the maintainer, can
produce that statement for a file built anywhere else.

## Checksums

Every release also ships `SHA256SUMS.txt`:

```powershell
Get-FileHash FreSH.exe -Algorithm SHA256
```

Compare it with the line in `SHA256SUMS.txt`. This catches a corrupted or
swapped download, though on its own it does not prove where the file came from,
since whoever replaced the file could replace the list too. Prefer
`gh attestation verify` when you can.

## Package managers

```
scoop bucket add fresh https://github.com/S42yt/FreSH
scoop install fresh
```

Scoop checks the hash before installing and shows no SmartScreen prompt, so it
is the least friction if you already have it. A winget package is prepared but
not yet in Microsoft's index, see [manifests](../manifests/README.md).

## Signing

The project is applying to the [SignPath Foundation](https://signpath.org),
which provides free code signing to open source projects with the private key
held in their hardware security module rather than by the project. If that is
granted, released binaries will be signed and the publisher shown in the
SmartScreen dialog will be SignPath Foundation, since the certificate is issued
to them. Until then the two checks above are the honest answer, and this page
will say so.

See [code signing](code-signing.md) for the policy.
