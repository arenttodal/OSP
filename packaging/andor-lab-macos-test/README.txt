ANDOR Lab test build for macOS: ANDOR Phrase + ANDOR Grain
(universal: Apple Silicon and Intel; development prototypes, not a release)

1. Unzip this folder.
2. Double-click install.command. If macOS refuses to open it, right-click it, choose
   Open, then Open again. Or run in Terminal:  bash install.command
3. Restart your DAW.
   - Logic: Instrument slot > AU Instruments > OSP > ANDOR Phrase / ANDOR Grain
   - Ableton Live / Reaper: rescan plug-ins, then use the VST3 or AU "ANDOR Phrase" / "ANDOR Grain"
   - Or open ~/Applications/ANDOR Phrase.app and ANDOR Grain.app (standalone; choose your
     audio device in Options)

This build is not signed or notarised (that needs a Developer ID). install.command removes
the download quarantine so macOS lets it load. It never replaces OSP or ANDOR Drums
(different plug-in codes). What to test: docs/andor-lab/test-instructions.md

Uninstall: delete ANDOR Phrase.component and ANDOR Grain.component from
~/Library/Audio/Plug-Ins/Components, the two .vst3 bundles from ~/Library/Audio/Plug-Ins/VST3
and the two apps from ~/Applications. Presets live in ~/Documents/ANDOR Lab.
