#!/usr/bin/env bash
set -e

version=$1

if [ -z "$version" ]; then
    echo "usage: tools/winget-submit.sh <version>" >&2
    exit 2
fi

if [ "${version#*-}" != "$version" ]; then
    echo "$version is a prerelease, which does not go to winget" >&2
    exit 0
fi

owner=$(gh api user --jq .login)
gh repo fork microsoft/winget-pkgs --clone=false > /dev/null 2>&1 || true
sha=$(gh api repos/microsoft/winget-pkgs/git/ref/heads/master --jq .object.sha)

submit() {
    local package=$1
    local target=$2
    local branch="$package-$version"
    local source="manifests/$package/$version"

    if [ ! -d "$source" ]; then
        echo "$source is missing, so $package is not submitted" >&2
        return 0
    fi

    if gh api "repos/$owner/winget-pkgs/git/ref/heads/$branch" > /dev/null 2>&1; then
        echo "$branch already exists on the fork, so $package is not submitted again"
        return 0
    fi

    gh api "repos/$owner/winget-pkgs/git/refs" -f ref="refs/heads/$branch" -f sha="$sha" > /dev/null

    for file in "$source"/*.yaml; do
        local name
        name=$(basename "$file")
        gh api "repos/$owner/winget-pkgs/contents/$target/$version/$name" --method PUT \
            -f message="New version: $package version $version" \
            -f content="$(base64 -w0 < "$file")" \
            -f branch="$branch" > /dev/null
        echo "  added $target/$version/$name"
    done

    local body
    body=$(mktemp)
    {
        printf 'FreSH %s.\n\n' "$version"
        printf -- '- Homepage: https://github.com/S42yt/FreSH\n'
        printf -- '- Release: https://github.com/S42yt/FreSH/releases/tag/v%s\n' "$version"
        printf -- '- License: GPL-3.0-only\n\n'
        printf '### Checks\n\n'
        printf -- '- [x] The manifests validate against the schema\n'
        printf -- '- [x] The `InstallerSha256` is taken from the `SHA256SUMS.txt` published with the release\n'
        printf -- '- [x] The build, the test suite and a differential test against bash all passed before the release was published\n\n'
        printf '### Provenance\n\n'
        printf 'Built by GitHub Actions from the tagged commit, with a signed provenance statement:\n\n'
        printf '```\ngh attestation verify FreSH.exe --repo S42yt/FreSH\n```\n'
    } > "$body"

    gh pr create --repo microsoft/winget-pkgs --base master --head "$owner:$branch" \
        --title "New version: $package version $version" --body-file "$body"
    rm -f "$body"
}

submit S42yt.FreSH manifests/s/S42yt/FreSH
submit S42yt.FreSH.Portable manifests/s/S42yt/FreSH/Portable
