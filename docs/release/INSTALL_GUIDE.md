# Installing ANDOR OSP (beta)

ANDOR OSP is a Mac instrument. It runs on its own, or inside your music software as an Audio
Unit (Logic Pro, GarageBand, MainStage) or a VST3 (Ableton Live, Cubase, Studio One, REAPER,
Bitwig and others).

## You need

- A Mac with Apple silicon or Intel, macOS 11 Big Sur or later.
- About 250 MB of free disk space.
- An administrator password: the installer puts OSP where every music app looks for it.

## Install

1. Double-click **ANDOR-OSP-0.9.0-beta.1-macOS-universal.pkg**. It is signed and notarized by
   Apple, so it opens like any other installer. You do not need to change any security
   setting or use Terminal.
2. Click through **Introduction**, then **Continue**.
3. On **Installation Type**, click **Install** for all three formats, or **Customize** to
   choose:
   - **Audio Unit (AU):** for Logic Pro, GarageBand and MainStage.
   - **VST3:** for Ableton Live, REAPER and other VST3 hosts.
   - **Standalone app:** OSP without a DAW.
   - **Move earlier OSP test builds aside:** tick this if you installed an earlier OSP test
     build (from a zip, with `install.command`). Your DAW then finds only this version. The
     old copies are moved, not deleted, to `~/Library/Application Support/ANDOR/Previous Test
     Builds`.
4. Enter your administrator password, then **Close**.

## Where it goes

| Format | Location |
|---|---|
| Audio Unit | `/Library/Audio/Plug-Ins/Components/OSP.component` |
| VST3 | `/Library/Audio/Plug-Ins/VST3/OSP.vst3` |
| Standalone app | `/Applications/OSP.app` |

Your own sounds, presets, packs, patterns and settings are kept in your user folders. The
installer never touches them, so installing a newer beta keeps everything you made.

## First steps

- **Logic Pro:** restart it, then create a Software Instrument track: **AU Instruments › OSP
  › OSP**.
- **Ableton Live:** restart it, or click **Rescan** in Settings › Plug-Ins. OSP is under
  **Plug-Ins › VST3 › OSP**.
- **REAPER:** Options › Preferences › Plug-ins › VST › **Re-scan**, then insert **VST3i: OSP
  (OSP)**.
- **The app:** open **OSP** from Applications. The first time you RECORD or GRAB, macOS asks
  whether OSP may use the microphone. OSP listens only while you record, and nothing leaves
  your computer.

OSP opens on its first Starting State. Drop one tonal recording (a sung note, a held
instrument note) onto a source to make your instrument.

## Uninstall

Drag these to the Trash: `/Library/Audio/Plug-Ins/Components/OSP.component`,
`/Library/Audio/Plug-Ins/VST3/OSP.vst3` and `/Applications/OSP.app`. Your own sounds and
presets stay in your user folders, in case you install again.

## Problems?

See **KNOWN_ISSUES.md**, and report anything else as **BETA_TESTING.md** describes.
