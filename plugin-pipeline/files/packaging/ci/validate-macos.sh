#!/usr/bin/env bash
# Checks the macOS plug-ins the way hosts will:
#   AU    Apple's auval (what Logic runs before it lists a plug-in). Type, subtype and
#         manufacturer come from the component's own Info.plist.
#   VST3  Tracktion's pluginval, when PLUGINVAL=1 in packaging/pipeline.conf.
# Run after package.sh on a Mac (the CI does). It installs the AU for the current user.
source "$(dirname "$0")/common.sh"

status=0
for comp in "$artefacts"/AU/*.component; do
    [ -e "$comp" ] || continue
    plist="$comp/Contents/Info.plist"
    type="$(/usr/libexec/PlistBuddy -c "Print :AudioComponents:0:type" "$plist")"
    subtype="$(/usr/libexec/PlistBuddy -c "Print :AudioComponents:0:subtype" "$plist")"
    manufacturer="$(/usr/libexec/PlistBuddy -c "Print :AudioComponents:0:manufacturer" "$plist")"
    mkdir -p ~/Library/Audio/Plug-Ins/Components
    rm -rf ~/Library/Audio/Plug-Ins/Components/"$(basename "$comp")"
    cp -R "$comp" ~/Library/Audio/Plug-Ins/Components/
    killall -9 AudioComponentRegistrar 2> /dev/null || true
    echo "auval -v $type $subtype $manufacturer"
    auval -v "$type" "$subtype" "$manufacturer" || status=1
done

if [ "${PLUGINVAL:-0}" = 1 ]; then
    for vst3 in "$artefacts"/VST3/*.vst3; do
        [ -e "$vst3" ] || continue
        tools="$(mktemp -d)"
        curl -fsSL -o "$tools/pluginval.zip" https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip
        ditto -x -k "$tools/pluginval.zip" "$tools"
        "$tools/pluginval.app/Contents/MacOS/pluginval" --strictness-level 5 --skip-gui-tests --validate "$vst3" || status=1
    done
fi

exit "$status"
