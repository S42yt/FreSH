# Code signing policy

## What is signed

Released binaries, `FreSH.exe` and `FreSH-Setup.exe`, are the only artifacts
that would ever be signed. Nothing built from a branch, a pull request or a
local build is signed.

## Where the key lives

The project holds no signing key. If the application to the
[SignPath Foundation](https://signpath.org) is granted, the certificate is
issued to SignPath Foundation and the private key never leaves their hardware
security module. Signing happens inside the release pipeline, on a build
GitHub Actions produced from a tagged commit, and the maintainer cannot sign a
file by hand.

This has a consequence worth stating plainly: the publisher Windows shows would
be SignPath Foundation, not Musa Bostanci, and SignPath reserve the right to
revoke, including for conduct reasons.

## Who does what

FreSH has one maintainer, Musa Bostanci, who writes the code, reviews it and
approves releases. There is no second reviewer to appeal to, and pretending
otherwise would be theatre. What stands in for it:

- every change lands on `master` through CI, which builds the shell, runs the
  test suite, diffs the behaviour against real bash and runs the fuzz corpus
  under the address and undefined sanitizers
- releases are cut from tags, built only by GitHub Actions, and carry build
  provenance anyone can verify
- the source is GPL-3.0 with no proprietary components, so any claim here can
  be checked against the repository

Contributions from other people go through a pull request that the maintainer
reviews before it is merged.

## Reporting a problem

Anything that looks like a tampered binary, or a signature that does not match,
should go to <https://github.com/S42yt/FreSH/issues>. See
[verifying a download](verifying.md) for how to check a file yourself.
