#!/bin/bash
# Installs the ANDOR Drums test build for the current user (no admin rights needed).
# Double-click in Finder, or run from Terminal. Unsigned test build: this also removes
# the download quarantine so macOS lets the plug-ins load. OSP is not touched.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
xattr -cr "$here" 2>/dev/null || true
mkdir -p ~/Library/Audio/Plug-Ins/Components ~/Library/Audio/Plug-Ins/VST3
rm -rf "$HOME/Library/Audio/Plug-Ins/Components/ANDOR Drums.component" "$HOME/Library/Audio/Plug-Ins/VST3/ANDOR Drums.vst3"
cp -R "$here/ANDOR Drums.component" ~/Library/Audio/Plug-Ins/Components/
cp -R "$here/ANDOR Drums.vst3" ~/Library/Audio/Plug-Ins/VST3/
if [ -d "$here/ANDOR Drums.app" ]; then
    rm -rf "$HOME/Applications/ANDOR Drums.app"
    mkdir -p ~/Applications
    cp -R "$here/ANDOR Drums.app" ~/Applications/
fi
# Make Logic and other AU hosts rescan.
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Installed: AU and VST3 in ~/Library/Audio/Plug-Ins, the standalone app in ~/Applications."
echo "Restart your DAW. In Logic: Instrument > AU Instruments > OSP > ANDOR Drums."
