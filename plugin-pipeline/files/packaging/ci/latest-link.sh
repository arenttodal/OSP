#!/usr/bin/env bash
# Keeps a fixed download link per branch: a GitHub pre-release "test-<branch>" whose
# <Product>-<platform>.zip is replaced by every build, so one bookmark always gets the
# newest one:
#   https://github.com/<owner>/<repo>/releases/download/test-<branch>/<Product>-macOS.zip
# Runs in GitHub Actions after package.sh (needs GH_TOKEN and contents: write).
source "$(dirname "$0")/common.sh"
[ "${LATEST_LINK:-1}" = 1 ] || { echo "LATEST_LINK is off."; exit 0; }
: "${GITHUB_REF_NAME:?run in GitHub Actions}" "${LATEST_ARCHIVE:?run package.sh first}"

tag="test-$(printf '%s' "$GITHUB_REF_NAME" | sed 's/[^A-Za-z0-9._-]/-/g')"
notes="Newest test build of the $GITHUB_REF_NAME branch (commit ${GITHUB_SHA:0:7}, $(date -u '+%Y-%m-%d %H:%M UTC')). Replaced by every push; unzip and run the installer inside."

# macOS and Windows jobs both get here; whichever is first creates the release.
if ! gh release view "$tag" > /dev/null 2>&1; then
    gh release create "$tag" --prerelease --target "$GITHUB_SHA" \
        --title "Test build: $GITHUB_REF_NAME" --notes "$notes" || true
fi
gh release upload "$tag" "$LATEST_ARCHIVE" --clobber
gh release edit "$tag" --notes "$notes" > /dev/null
# The tag follows the branch, so the release page names the commit it was built from.
git push --force origin "$GITHUB_SHA:refs/tags/$tag" 2> /dev/null || true

url="https://github.com/$GITHUB_REPOSITORY/releases/download/$tag/$(basename "$LATEST_ARCHIVE")"
echo "Fixed link: $url"
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    echo "Fixed link (always the newest build of $GITHUB_REF_NAME): $url" >> "$GITHUB_STEP_SUMMARY"
fi
