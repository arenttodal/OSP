#!/bin/bash
# Installs the OSP test build for the current user (no admin rights needed).
# Double-click in Finder, or run from Terminal. Unsigned test build: this also removes
# the download quarantine so macOS lets the plug-ins load.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
xattr -cr "$here" 2>/dev/null || true
mkdir -p ~/Library/Audio/Plug-Ins/Components ~/Library/Audio/Plug-Ins/VST3
rm -rf ~/Library/Audio/Plug-Ins/Components/OSP.component ~/Library/Audio/Plug-Ins/VST3/OSP.vst3
cp -R "$here/OSP.component" ~/Library/Audio/Plug-Ins/Components/
cp -R "$here/OSP.vst3" ~/Library/Audio/Plug-Ins/VST3/
if [ -d "$here/OSP.app" ]; then
    rm -rf ~/Applications/OSP.app
    mkdir -p ~/Applications
    cp -R "$here/OSP.app" ~/Applications/
fi
# Make Logic and other AU hosts rescan.
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Installed: AU and VST3 in ~/Library/Audio/Plug-Ins, the standalone app in ~/Applications."
echo "Restart your DAW. In Logic, the plug-in is under Instrument > AU Instruments > OSP."
