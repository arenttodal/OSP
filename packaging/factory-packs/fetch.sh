#!/usr/bin/env bash
# Fetches the factory packs named in packs.lock (beside this script) into this folder, each
# checked against its SHA-256. CI runs it before configuring; run it locally the same way.
#   packs.lock, one pack per line:  <release-tag> <asset-file-name> <sha256>
# The assets live on this repository's GitHub Releases (OSP_PACKS_REPO=owner/name for another).
# Needs the GitHub CLI (gh) with GH_TOKEN, or a login. No packs.lock or no entries: nothing to
# fetch, and the build is exactly as without packs.
set -euo pipefail
dir="$(cd "$(dirname "$0")" && pwd)"
lock="$dir/packs.lock"
if [ ! -f "$lock" ]; then
    echo "factory packs: no packs.lock, nothing to fetch"
    exit 0
fi
sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
count=0
while read -r tag asset sum _; do
    case "${tag:-}" in ""|\#*) continue ;; esac
    out="$dir/$asset"
    if [ -f "$out" ] && [ "$(sha256 "$out")" = "$sum" ]; then
        echo "factory packs: $asset already here"
    else
        gh release download "$tag" --pattern "$asset" --dir "$dir" --clobber ${OSP_PACKS_REPO:+--repo "$OSP_PACKS_REPO"}
        got="$(sha256 "$out")"
        if [ "$got" != "$sum" ]; then
            echo "factory packs: $asset has SHA-256 $got, packs.lock says $sum" >&2
            rm -f "$out"
            exit 1
        fi
        echo "factory packs: $asset from release $tag (verified)"
    fi
    count=$((count + 1))
done < "$lock"
echo "factory packs: $count pack(s) ready"
