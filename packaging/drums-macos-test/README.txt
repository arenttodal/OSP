ANDOR Drums test build for macOS (universal: Apple Silicon and Intel)

1. Unzip this folder.
2. Double-click install.command. If macOS refuses to open it, right-click it, choose
   Open, then Open again. Or run in Terminal:  bash install.command
3. Restart your DAW.
   - Logic: Instrument slot > AU Instruments > OSP > ANDOR Drums
   - Ableton Live / Reaper: rescan plug-ins, then use the VST3 or AU "ANDOR Drums"
   - Or open ~/Applications/ANDOR Drums.app (standalone, choose your audio device in Options)

ANDOR Drums installs beside OSP (its own plug-in code, Adrm); OSP and OSP projects are
not affected.

This build is not signed or notarised (that needs a Developer ID). install.command
removes the download quarantine so macOS lets it load.

Uninstall: delete "ANDOR Drums.component" from ~/Library/Audio/Plug-Ins/Components,
"ANDOR Drums.vst3" from ~/Library/Audio/Plug-Ins/VST3 and "ANDOR Drums.app" from
~/Applications.
