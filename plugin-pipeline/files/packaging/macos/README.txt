{{PRODUCT}} test build for macOS ({{VERSION}}; Apple Silicon and Intel)

1. Unzip this folder (Safari does it for you).
2. Double-click Install.command.
   If macOS says it cannot be opened: right-click it, choose Open, then Open again.
   Or in Terminal:  bash Install.command
3. Restart your DAW.
   - Logic: the instrument or effect menu, under the manufacturer's name
   - Ableton Live, Reaper, Bitwig: rescan plug-ins (VST3 or AU)
   - The standalone app (if included) is in ~/Applications

Installed per user, no admin password:
  AU    ~/Library/Audio/Plug-Ins/Components
  VST3  ~/Library/Audio/Plug-Ins/VST3
  CLAP  ~/Library/Audio/Plug-Ins/CLAP
  App   ~/Applications

Not signed or notarised (that needs an Apple Developer ID); Install.command removes the
download quarantine so macOS lets it load. To uninstall, delete the files above.
