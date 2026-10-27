ANDOR Lab test build for macOS: ANDOR Phrase + ANDOR Grain
(universal: Apple Silicon and Intel; development prototypes, not a release)

Contents: ANDOR Phrase.component / .vst3 / .app and ANDOR Grain.component / .vst3 / .app.
Nothing installs itself. They never replace OSP or ANDOR Drums (different plug-in codes).

1. Unzip this folder and open Terminal in it (right-click the folder > New Terminal at Folder).
2. This build is not signed or notarised, so first remove the download quarantine:

     xattr -cr .

3. Copy the plug-ins (no admin rights needed):

     mkdir -p ~/Library/Audio/Plug-Ins/Components ~/Library/Audio/Plug-Ins/VST3
     cp -R "ANDOR Phrase.component" "ANDOR Grain.component" ~/Library/Audio/Plug-Ins/Components/
     cp -R "ANDOR Phrase.vst3" "ANDOR Grain.vst3" ~/Library/Audio/Plug-Ins/VST3/
     killall -9 AudioComponentRegistrar 2>/dev/null; true

4. Optional check:  auval -v aumu Aphr Ospx   and   auval -v aumu Agrn Ospx
5. Try the standalone apps first (double-click; Options > Audio/MIDI settings), then rescan
   plug-ins in Ableton Live / Logic / Reaper. What to test: docs/andor-lab/test-instructions.md

Uninstall: delete the four bundles from ~/Library/Audio/Plug-Ins/Components and
~/Library/Audio/Plug-Ins/VST3. Presets live in ~/Documents/ANDOR Lab.
