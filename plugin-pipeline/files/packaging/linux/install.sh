#!/usr/bin/env bash
# Installs every plug-in next to this file for the current user.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
shopt -s nullglob
for b in "$here"/*.vst3; do mkdir -p ~/.vst3 && rm -rf ~/.vst3/"$(basename "$b")" && cp -R "$b" ~/.vst3/ && echo "Installed $(basename "$b") -> ~/.vst3"; done
for b in "$here"/*.clap; do mkdir -p ~/.clap && cp -R "$b" ~/.clap/ && echo "Installed $(basename "$b") -> ~/.clap"; done
echo "Restart your DAW and rescan plug-ins."
