#!/bin/bash
# Installs every plug-in next to this file for the current user (no admin password).
# Double-click it in Finder. This is an unsigned test build: it also removes the download
# quarantine, so macOS lets the plug-ins load.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
xattr -cr "$here" 2> /dev/null || true

installed=()
tilde='~'
put()
{
    mkdir -p "$2"
    rm -rf "$2/$(basename "$1")"
    cp -R "$1" "$2/"
    installed+=("$(basename "$1")  ->  ${2/#$HOME/$tilde}")
}
shopt -s nullglob
for b in "$here"/*.component; do put "$b" "$HOME/Library/Audio/Plug-Ins/Components"; done
for b in "$here"/*.vst3; do put "$b" "$HOME/Library/Audio/Plug-Ins/VST3"; done
for b in "$here"/*.clap; do put "$b" "$HOME/Library/Audio/Plug-Ins/CLAP"; done
for b in "$here"/*.app; do put "$b" "$HOME/Applications"; done

# Make Logic and other AU hosts see the new version.
killall -9 AudioComponentRegistrar 2> /dev/null || true

echo "Installed:"
for line in "${installed[@]}"; do echo "  $line"; done
echo
echo "Restart your DAW. Logic: the plug-in appears under its manufacturer's name."
