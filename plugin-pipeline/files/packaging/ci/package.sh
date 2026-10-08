#!/usr/bin/env bash
# Packs the built plug-ins into a ready-to-install zip in dist/:
#   macOS    <Product>-macOS-<commit>.zip    AU, VST3, CLAP, app + "Install.command"
#   Windows  <Product>-Windows-<commit>.zip  VST3, CLAP, exe + "Install.bat"
#   Linux    <Product>-Linux-<commit>.zip    VST3, CLAP, standalone + "install.sh"
# Also writes dist/<Product>-<platform>.zip (no commit in the name) for the fixed link, and
# ARTIFACT_NAME / ARCHIVE / LATEST_ARCHIVE to $GITHUB_ENV when run in GitHub Actions.
source "$(dirname "$0")/common.sh"

case "$(uname -s)" in
    Darwin) platform=macOS ;;
    MINGW* | MSYS* | CYGWIN*) platform=Windows ;;
    Linux) platform=Linux ;;
    *) echo "error: unsupported system $(uname -s)" >&2; exit 1 ;;
esac

folder="$safe-$platform-test"
stage="dist/$folder"
rm -rf "$stage"
mkdir -p "$stage"
for b in "${bundles[@]}"; do
    cp -R "$b" "$stage/"
done

# The read-me, with the product's name in it.
render() { sed "s|{{PRODUCT}}|$product|g; s|{{VERSION}}|$version|g" "$1"; }

case "$platform" in
    macOS)
        for b in "$stage"/*.component "$stage"/*.vst3 "$stage"/*.clap "$stage"/*.app; do
            [ -e "$b" ] || continue
            # A complete ad-hoc signature: Apple Silicon only loads signed code, and copying
            # bundles around can leave the linker's signature incomplete. Not a Developer ID
            # signature (Install.command clears the download quarantine instead).
            codesign --force --deep --sign - "$b"
            for exe in "$b"/Contents/MacOS/*; do lipo -info "$exe" || true; done
        done
        cp packaging/macos/install.command "$stage/Install.command"
        chmod +x "$stage/Install.command"
        render packaging/macos/README.txt > "$stage/README.txt"
        archive="$safe-macOS-$version.zip"
        # ditto keeps what Finder needs (permissions, symlinks inside bundles).
        ditto -c -k --keepParent "$stage" "dist/$archive"
        ;;
    Windows)
        # Batch files want CRLF line ends.
        sed 's/$/\r/' packaging/windows/install.bat > "$stage/Install.bat"
        render packaging/windows/README.txt | sed 's/$/\r/' > "$stage/README.txt"
        archive="$safe-Windows-$version.zip"
        (cd dist && 7z a -tzip "$archive" "$folder" > /dev/null)
        ;;
    Linux)
        cp packaging/linux/install.sh "$stage/install.sh"
        chmod +x "$stage/install.sh"
        render packaging/linux/README.txt > "$stage/README.txt"
        archive="$safe-Linux-$version.zip"
        (cd dist && rm -f "$archive" && zip -qry "$archive" "$folder")
        ;;
esac

latest="$safe-$platform.zip"
cp "dist/$archive" "dist/$latest"
echo "Packed dist/$archive:"
for b in "${bundles[@]}"; do echo "  $(basename "$b")"; done

if [ -n "${GITHUB_ENV:-}" ]; then
    {
        echo "ARTIFACT_NAME=$safe-$platform-$version"
        echo "ARCHIVE=dist/$archive"
        echo "LATEST_ARCHIVE=dist/$latest"
    } >> "$GITHUB_ENV"
fi
