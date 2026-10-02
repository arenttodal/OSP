OSP test build for macOS (universal: Apple Silicon and Intel)

1. Unzip this folder.
2. Double-click install.command. If macOS refuses to open it, right-click it, choose
   Open, then Open again. Or run in Terminal:  bash install.command
3. Restart your DAW.
   - Logic: Instrument slot > AU Instruments > OSP > OSP
   - Ableton Live / Reaper: rescan plug-ins, then use the VST3 or AU "OSP"
   - Or open ~/Applications/OSP.app (standalone, choose your audio device in Options)

This build is not signed or notarised (that needs a Developer ID). install.command
removes the download quarantine so macOS lets it load.

Uninstall: delete OSP.component from ~/Library/Audio/Plug-Ins/Components, OSP.vst3
from ~/Library/Audio/Plug-Ins/VST3 and OSP.app from ~/Applications.
